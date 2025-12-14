#pragma once

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "renderer/rendererhandler.h"

#include "assettypes/texture.h"

#include "assethandler/assethandler.h"
#include "renderable.h"

#include "renderpass.h"

struct TaskBase {
    virtual ~TaskBase() = default;
    virtual void operator()() = 0;
};

template<typename F>
struct TaskImpl : TaskBase {
    F f;
    TaskImpl(F&& func) : f(std::move(func)) {}
    void operator()() override { f(); }
};

class ThreadPool {
public:
    using Task = std::unique_ptr<TaskBase>;

    ThreadPool(size_t num_threads) : tasks_running(0) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    Task task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { return !tasks.empty() || stop; });
                        if (stop && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    tasks_running++;
                    (*task)();
                    tasks_running--;
                }
            });
        }
    }

    ~ThreadPool() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
        lock.unlock();
        condition.notify_all();
        for (auto& worker : workers) worker.join();
    }

    template<typename F>
    void enqueue(F&& task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(std::make_unique<TaskImpl<F>>(std::forward<F>(task)));
        }
        condition.notify_one();
    }

    void wait_all() {
        while (tasks_running > 0 || !tasks.empty()) std::this_thread::yield();
    }

    int get_thread_count() {
        return workers.size();
    }

    std::vector<std::thread> workers;
private:
    std::queue<Task> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<size_t> tasks_running;
    bool stop = false;
};

class SpriteRenderPass : public RenderPass {

    ThreadPool thread_pool = ThreadPool(std::max(1u, std::thread::hardware_concurrency()));

    SDL_GPUTexture* m_msaa_color_texture = nullptr;
    SDL_GPUTexture* m_msaa_depth_texture = nullptr;

    TextureAsset            m_depth_texture;
    SDL_GPUGraphicsPipeline *m_pipeline{nullptr};

    std::string passname;

    SDL_GPUShader *vertex_shader   = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;

    SDL_GPUTransferBuffer* SpriteDataTransferBuffer;
    SDL_GPUBuffer* SpriteDataBuffer;
    
    // Index buffer for instanced quad rendering
    SDL_GPUBuffer* IndexBuffer;
    SDL_GPUTransferBuffer* IndexTransferBuffer;

    // Original full-precision struct (64 bytes)
    struct SpriteInstance {
        float x, y, z;
        float rotation;
        float tex_u, tex_v, tex_w, tex_h;
        float r, g, b, a;
        float w, h;
        float pivot_x, pivot_y;
    };

    // Compact half-precision struct (32 bytes)
    // Packed to match shader layout where each uint contains 2× uint16_t values
    struct CompactSpriteInstance {
        uint32_t pos_xy;      // x in low 16 bits, y in high 16 bits
        uint32_t pos_z_rot;   // z in low 16 bits, rotation in high 16 bits
        uint32_t tex_uv;      // tex_u in low 16 bits, tex_v in high 16 bits
        uint32_t tex_wh;      // tex_w in low 16 bits, tex_h in high 16 bits
        uint32_t color_rg;    // r in low 16 bits, g in high 16 bits
        uint32_t color_ba;    // b in low 16 bits, a in high 16 bits
        uint32_t size_wh;     // w in low 16 bits, h in high 16 bits
        uint32_t pivot_xy;    // pivot_x in low 16 bits, pivot_y in high 16 bits
    };

    // Fast inline clamp - compiles to conditional moves (branchless)
    static inline float fast_clamp(float v, float min, float max) {
        return (v < min) ? min : (v > max ? max : v);
    }

    // Fast inline max
    static inline float fast_max(float v, float min) {
        return (v < min) ? min : v;
    }

    // Pack two half-floats into a uint32
    static inline uint32_t pack_half2(float a, float b) {
        // Clamp to prevent NaN/Inf from propagating
        if (!std::isfinite(a)) a = 0.0f;
        if (!std::isfinite(b)) b = 0.0f;
        
        // Clamp to half-float range: -65504 to +65504
        a = fast_clamp(a, -65504.0f, 65504.0f);
        b = fast_clamp(b, -65504.0f, 65504.0f);
        
        uint16_t ha = float_to_half(a);
        uint16_t hb = float_to_half(b);
        return static_cast<uint32_t>(ha) | (static_cast<uint32_t>(hb) << 16);
    }

    // Float32 to Float16 conversion (scalar version)
    static inline uint16_t float_to_half(float f) {
        union { float f; uint32_t i; } u = {f};
        uint32_t bits = u.i;
        
        uint32_t sign = (bits >> 16) & 0x8000;
        int32_t exponent = ((bits >> 23) & 0xFF) - 127 + 15;
        uint32_t mantissa = (bits >> 13) & 0x3FF;
        
        // Handle denormalized numbers and underflow
        if (exponent <= 0) {
            if (exponent < -10) {
                // Too small, flush to zero
                return static_cast<uint16_t>(sign);
            }
            // Denormalized number - shift mantissa
            mantissa = (mantissa | 0x400) >> (1 - exponent);
            return static_cast<uint16_t>(sign | mantissa);
        }
        
        // Handle overflow to infinity
        if (exponent >= 31) {
            return static_cast<uint16_t>(sign | 0x7C00);
        }
        
        // Normal number
        return static_cast<uint16_t>(sign | (exponent << 10) | mantissa);
    }

    struct Batch {
        SDL_GPUTexture* texture = nullptr;
        SDL_GPUSampler* sampler = nullptr;

        size_t offset = 0; // Offset in sprite_buffer (in instances)
        size_t count = 0;  // Number of sprites
    };

    static constexpr Uint32 MAX_SPRITES = 4'000'000;
public:

    std::vector<Renderable> renderQueue;
    size_t renderQueueCount = 0;

public:
    SpriteRenderPass(const SpriteRenderPass &) = delete;

    SpriteRenderPass &operator=(const SpriteRenderPass &) = delete;

    SpriteRenderPass(SpriteRenderPass &&) = delete;

    SpriteRenderPass &operator=(SpriteRenderPass &&) = delete;

    explicit SpriteRenderPass(SDL_GPUDevice *m_gpu_device) : RenderPass(m_gpu_device) {
    }

    [[nodiscard]] bool init(
        SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width,
        uint32_t surface_height, std::string name, bool logInit = true
    ) override;

    void release(bool logRelease = true) override;

    void render(
        SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
    ) override;

    void addToRenderQueue(const Renderable &renderable) override {
        renderQueue[renderQueueCount] = renderable;
        renderQueueCount++;
    }

    void resetRenderQueue() override {
        // Just reset counter - keep allocated memory
        renderQueueCount = 0;
    }

    UniformBuffer &getUniformBuffer() override {

        return uniformBuffer;
    }

    void UpdateRenderPassBlendState(SDL_GPUColorTargetBlendState newstate) {
        renderPassBlendState = newstate;
    }

    UniformBuffer uniformBuffer;

    SDL_GPUColorTargetBlendState renderPassBlendState = GPUstructs::defaultBlendState;

    static const uint8_t sprite_frag_bin[];
    static const size_t  sprite_frag_bin_len = 1000;

    static const uint8_t sprite_vert_bin[];
    static const size_t  sprite_vert_bin_len = 3212;

    static const uint8_t sprite_batch_frag_bin[];
    static const size_t  sprite_batch_frag_bin_len = 1020;

    static const uint8_t sprite_batch_vert_bin[];
    static const size_t sprite_batch_vert_bin_len = 3204;

    // Instanced rendering shaders
    static const uint8_t sprite_instanced_vert_bin[];
    static const size_t sprite_instanced_vert_bin_len = 21564;

    static const uint8_t sprite_instanced_frag_bin[];
    static const size_t sprite_instanced_frag_bin_len = 1000;

    void createShaders();
};
