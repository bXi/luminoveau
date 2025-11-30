#pragma once

#include <algorithm>
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

    struct SpriteInstance {
        float x, y, z;
        float rotation;
        float tex_u, tex_v, tex_w, tex_h;
        float r, g, b, a;
        float w, h;
        float pivot_x, pivot_y;
    };

    struct Batch {
        SDL_GPUTexture* texture = nullptr;
        SDL_GPUSampler* sampler = nullptr;

        size_t offset = 0; // Offset in sprite_buffer (in instances)
        size_t count = 0;  // Number of sprites
    };

    std::vector<SpriteInstance> sprite_data;

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
        uint32_t surface_height, std::string name
    ) override;

    void release() override;

    void render(
        SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
    ) override;

    void addToRenderQueue(const Renderable &renderable) override {
        renderQueue[renderQueueCount] = renderable;
        renderQueueCount++;
    }

    void resetRenderQueue() override {

        //std::vector<Renderable>().swap(renderQueue);
        renderQueue.reserve(MAX_SPRITES);
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


    void createShaders();
};
