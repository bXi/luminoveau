#pragma once

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "renderer/renderer.h"

#include "assets/texture/texture.h"
#include "assets/effect/effect.h"

#include "assets/assethandler.h"
#include "gpu/renderable.h"
#include "gpu/presets.h"

#include "gpu/renderpass.h"
#include "gpu/buffer/buffermanager.h"

#ifndef LUMINOVEAU_WEBGPU_BACKEND
#include <SDL3/SDL_gpu.h>
#include "gpu/backends/sdl/sdlgpu.h"
#endif

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

#ifndef LUMINOVEAU_WEBGPU_BACKEND
    ThreadPool thread_pool = ThreadPool(std::max(1u, std::thread::hardware_concurrency()));
#else
    ThreadPool thread_pool = ThreadPool(0);
#endif

#ifndef LUMINOVEAU_WEBGPU_BACKEND
    SDL_GPUDevice*  m_gpu_device         = nullptr;
    SDL_GPUTexture* m_msaa_color_texture = nullptr;
    SDL_GPUTexture* m_msaa_depth_texture = nullptr;

    TextureAsset            m_depth_texture;
    SDL_GPUGraphicsPipeline *m_pipeline{nullptr};

    SDL_GPUShader *vertex_shader   = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;

    SDL_GPUTransferBuffer* SpriteDataTransferBuffer = nullptr;
    SDL_GPUBuffer* SpriteDataBuffer = nullptr;

    std::unordered_map<uint32_t, SDL_GPUTexture*> m_additionalEffectTextures;

    bool m_noMSAA = false;
    SDL_GPUTextureFormat m_swapchain_format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
#else
    // WebGPU path
    GpuGraphicsPipelineHandle m_wgpu_pipeline    = 0;
    GpuBufferHandle           m_sprite_gpu_buf   = 0;
    GpuTransferBufferHandle   m_sprite_xfer_buf  = 0;
    // Unit quad geometry for sprite rendering
    GpuBufferHandle           m_quad_vertex_buf  = 0;
    GpuBufferHandle           m_quad_index_buf   = 0;
    GpuTransferBufferHandle   m_quad_xfer_vert   = 0;
    GpuTransferBufferHandle   m_quad_xfer_idx    = 0;
    // Effect rendering resources
    GpuTextureFormat          m_swapchain_fmt    = GpuTextureFormat::B8G8R8A8_Unorm;
    GpuTextureHandle          m_effect_tex_a     = 0;
    GpuTextureHandle          m_effect_tex_b     = 0;
    GpuSamplerHandle          m_effect_sampler   = 0;
    GpuBufferHandle           m_effect_vbuf      = 0;  // fullscreen quad for effects
    GpuBufferHandle           m_effect_ibuf      = 0;
    std::unordered_map<GpuShaderHandle, GpuGraphicsPipelineHandle> m_effect_pipelines;
#endif

    std::string passname;

    // Surface dimensions (desktop size) for effect textures
    uint32_t m_surface_width = 0;
    uint32_t m_surface_height = 0;

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
        uint32_t pivot_xy;    // pivot_x in low 16 bits, pivot_y in high 15 bits, isSDF flag in highest bit
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
#ifndef LUMINOVEAU_WEBGPU_BACKEND
        Geometry2D* geometry = nullptr;  // Which geometry this batch uses
        SDL_GPUTexture* texture = nullptr;
        SDL_GPUSampler* sampler = nullptr;
#else
        GpuBufferHandle vertexBuffer = 0;
        GpuBufferHandle indexBuffer  = 0;
        uint32_t        indexCount   = 0;
        GpuTextureHandle texture = 0;
        GpuSamplerHandle sampler = 0;
#endif
        size_t offset = 0; // Offset in sprite_buffer (in instances)
        size_t count = 0;  // Number of sprites
    };

public:

    Buffer<Renderable>* renderQueue = nullptr;

public:
    SpriteRenderPass(const SpriteRenderPass &) = delete;

    SpriteRenderPass &operator=(const SpriteRenderPass &) = delete;

    SpriteRenderPass(SpriteRenderPass &&) = delete;

    SpriteRenderPass &operator=(SpriteRenderPass &&) = delete;

#ifndef LUMINOVEAU_WEBGPU_BACKEND
    // Default ctor pulls the active SDL GPU device from the renderer so callers don't need
    // a backend-specific code path. Explicit ctor still available for tests that need to
    // pass a separate device handle.
    SpriteRenderPass() : RenderPass() {
        m_gpu_device = static_cast<SDL_GPUDevice*>(Renderer::GetDevice());
    }
    explicit SpriteRenderPass(SDL_GPUDevice *gpu_device) : RenderPass() {
        m_gpu_device = gpu_device;
    }
#else
    SpriteRenderPass() : RenderPass() {}
#endif

    [[nodiscard]] bool init(
        GpuTextureFormat swapchain_texture_format, uint32_t surface_width,
        uint32_t surface_height, std::string name, bool logInit = true,
        size_t capacity = 0, bool forceNoMSAA = false
    ) override;

    void release(bool logRelease = true) override;

    void render(
        GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4 &camera
    ) override;

    void addToRenderQueue(const Renderable &renderable) override {
        renderQueue->Add(renderable);
    }

    void resetRenderQueue() override {
        renderQueue->Reset();
    }

    UniformBuffer uniformBuffer;

    UniformBuffer &getUniformBuffer() override {
        return uniformBuffer;
    }

#ifdef LUMINOVEAU_WEBGPU_BACKEND
    GpuGraphicsPipelineHandle _getOrCreateEffectPipeline(const ShaderAsset& vertShader, const ShaderAsset& fragShader);
    void _applyEffectsWGPU(GpuCmdBufferHandle cmdBuffer, const std::vector<EffectAsset>& effects,
                            GpuTextureHandle sourceTexture, GpuTextureHandle targetTexture,
                            const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>>& extraTextures,
                            GpuLoadOp targetLoadOp = GpuLoadOp::Load,
                            float clearR = 0, float clearG = 0, float clearB = 0, float clearA = 0);

    // Blend state applied at pipeline creation time. Defaults to standard alpha blend.
    // Custom sprite render targets override via UpdateRenderPassBlendState.
    GpuColorTargetBlendState renderPassBlendStateGpu = {
        .blendEnabled   = true,
        .srcColorFactor = GpuBlendFactor::SrcAlpha,
        .dstColorFactor = GpuBlendFactor::OneMinusSrcAlpha,
        .colorOp        = GpuBlendOp::Add,
        .srcAlphaFactor = GpuBlendFactor::One,
        .dstAlphaFactor = GpuBlendFactor::OneMinusSrcAlpha,
        .alphaOp        = GpuBlendOp::Add,
    };

    void UpdateRenderPassBlendState(GpuColorTargetBlendState newstate) {
        renderPassBlendStateGpu = newstate;
    }
#endif

#ifndef LUMINOVEAU_WEBGPU_BACKEND
    void UpdateRenderPassBlendState(SDL_GPUColorTargetBlendState newstate) {
        renderPassBlendState = newstate;
    }

    // Backend-neutral overload: callers can pass a GpuPresets blend state
    // without needing an ifdef around toSDL(). Mirrors the WebGPU overload above.
    void UpdateRenderPassBlendState(GpuColorTargetBlendState newstate) {
        renderPassBlendState = toSDL(newstate);
    }

    void SetAdditionalTexture(uint32_t binding, SDL_GPUTexture* texture) {
        m_additionalEffectTextures[binding] = texture;
    }

    void ClearAdditionalTextures() {
        m_additionalEffectTextures.clear();
    }

    SDL_GPUColorTargetBlendState renderPassBlendState = toSDL(GpuPresets::AlphaBlendKeepDstAlpha);

    void createShaders();

    // Effect rendering support
    TextureAsset effectTempA;  // Ping-pong texture A
    TextureAsset effectTempB;  // Ping-pong texture B
    SDL_GPUGraphicsPipeline* effectPipeline = nullptr;
    SDL_GPUGraphicsPipeline* effectSpritePipeline = nullptr;  // Pipeline for rendering sprites to temp (no blending)
    SDL_GPUShader* effectVertShader = nullptr;

    void createEffectResources();
    void releaseEffectResources();
    void applyEffects(SDL_GPUCommandBuffer* cmd_buffer, const std::vector<EffectAsset>& effects,
                     SDL_GPUTexture* sourceTexture, SDL_GPUTexture* targetTexture, const glm::mat4& camera,
                     SDL_GPUTextureFormat targetFormat, bool isFirstBatch,
                     const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>>& effectTextures);
#endif
};
