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

    std::vector<std::thread> workers;
private:
    std::queue<Task> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<size_t> tasks_running;
    bool stop = false;
};

class SpriteRenderPass : public RenderPass {

    struct PreppedSprite {
        Uniforms uniforms;
        SDL_GPUTexture *texture;
        SDL_GPUSampler *sampler;
    };

    struct Uniforms {
        glm::mat4 camera;
        glm::mat4 model;
        glm::vec2 flipped;

        //TODO: fix this ugly mess
        glm::vec2 uv0;
        glm::vec2 uv1;
        glm::vec2 uv2;
        glm::vec2 uv3;
        glm::vec2 uv4;
        glm::vec2 uv5;

        float tintColorR = 1.0f;
        float tintColorG = 1.0f;
        float tintColorB = 1.0f;
        float tintColorA = 1.0f;
    };

    ThreadPool thread_pool = ThreadPool(std::max(1u, std::thread::hardware_concurrency()));

    TextureAsset            m_depth_texture;
    SDL_GPUGraphicsPipeline *m_pipeline{nullptr};

    std::string passname;

    SDL_GPUShader *vertex_shader   = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;

public:

    std::vector<Renderable> renderQueue;

public:
    SpriteRenderPass(const SpriteRenderPass &) = delete;

    SpriteRenderPass &operator=(const SpriteRenderPass &) = delete;

    SpriteRenderPass(SpriteRenderPass &&) = delete;

    SpriteRenderPass &operator=(SpriteRenderPass &&) = delete;

    explicit SpriteRenderPass(SDL_GPUDevice *m_gpu_device) : RenderPass(m_gpu_device) {
    }

    static void PrepSprites(const std::vector<Renderable> &_renderQueue, size_t start, size_t end,
                            std::vector<PreppedSprite> &prepped, float w, float h, const glm::mat4 &camera);

    [[nodiscard]] bool init(
        SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width,
        uint32_t surface_height, std::string name
    ) override;

    void release() override;

    void render(
        SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
    ) override;

    void addToRenderQueue(const Renderable &renderable) override {
        renderQueue.push_back(renderable);
    }

    void resetRenderQueue() override {

        std::vector<Renderable>().swap(renderQueue);
        renderQueue.reserve(150000);
        //renderQueue.clear();
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

    void createShaders();
};
