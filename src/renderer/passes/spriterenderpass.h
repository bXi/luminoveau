#pragma once

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <tuple>
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
                        // Count under the lock so wait_all() never observes an empty queue
                        // while a popped-but-not-yet-counted task is still in flight.
                        tasks_running++;
                    }
                    (*task)();
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        if (--tasks_running == 0 && tasks.empty())
                            done_condition.notify_all();
                    }
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
        // No workers (e.g. Emscripten without -pthread): run inline so callers that
        // enqueue + wait_all() still make progress instead of deadlocking.
        if (workers.empty()) {
            task();
            return;
        }
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(std::make_unique<TaskImpl<F>>(std::forward<F>(task)));
        }
        condition.notify_one();
    }

    void wait_all() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        done_condition.wait(lock, [this] { return tasks.empty() && tasks_running == 0; });
    }

    int get_thread_count() {
        return workers.size();
    }

    std::vector<std::thread> workers;
private:
    std::queue<Task> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;       // signals workers: work available / stop
    std::condition_variable done_condition;  // signals wait_all(): queue drained
    size_t tasks_running;                    // guarded by queue_mutex
    bool stop = false;
};

class SpriteRenderPass : public RenderPass {

    // Emscripten without -pthread can't spawn std::thread; default to single-threaded
    // sprite packing on that platform. All other targets use one worker per core.
#ifdef __EMSCRIPTEN__
    ThreadPool thread_pool = ThreadPool(0);
#else
    ThreadPool thread_pool = ThreadPool(std::max(1u, std::thread::hardware_concurrency()));
#endif

    // ── Shared pipeline + sprite-data buffers ─────────────────────────────────
    GpuGraphicsPipelineHandle m_pipeline               = 0;
    GpuGraphicsPipelineHandle m_effect_sprite_pipeline = 0;  // WebGPU only — replace-blend variant for effect ping-pong draws
    GpuShaderHandle           vertex_shader            = 0;
    GpuShaderHandle           fragment_shader          = 0;
    GpuTransferBufferHandle   SpriteDataTransferBuffer = 0;
    GpuBufferHandle           SpriteDataBuffer         = 0;
    GpuTextureFormat          m_swapchain_format       = GpuTextureFormat::B8G8R8A8_Unorm;
    bool                      m_noMSAA                 = false;

    // SDL-only state — unused on WebGPU but cheap to carry unconditionally so the header
    // stays backend-neutral. MSAA + depth textures and per-binding extra-texture map.
    GpuTextureHandle          m_msaa_color_texture     = 0;
    GpuTextureHandle          m_msaa_depth_texture     = 0;
    TextureAsset              m_depth_texture;
    std::unordered_map<uint32_t, GpuTextureHandle> m_additionalEffectTextures;

    // WebGPU-only state — unused on SDL. Unit quad geometry + effect ping-pong resources
    // + per-shader effect pipeline cache.
    GpuBufferHandle           m_quad_vertex_buf  = 0;
    GpuBufferHandle           m_quad_index_buf   = 0;
    GpuTransferBufferHandle   m_quad_xfer_vert   = 0;
    GpuTransferBufferHandle   m_quad_xfer_idx    = 0;
    GpuTextureHandle          m_effect_tex_a     = 0;
    GpuTextureHandle          m_effect_tex_b     = 0;
    uint32_t                  m_effect_tex_w     = 0;  // physical-window dims at last create
    uint32_t                  m_effect_tex_h     = 0;  // (WebGPU only)
    GpuSamplerHandle          m_effect_sampler   = 0;
    GpuBufferHandle           m_effect_vbuf      = 0;
    GpuBufferHandle           m_effect_ibuf      = 0;
    std::unordered_map<GpuShaderHandle, GpuGraphicsPipelineHandle> m_effect_pipelines;

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
        GpuBufferHandle  vertexBuffer = 0;
        GpuBufferHandle  indexBuffer  = 0;
        uint32_t         indexCount   = 0;
        GpuTextureHandle texture      = 0;
        GpuSamplerHandle sampler      = 0;
        size_t           offset       = 0;  // offset in sprite buffer (instances)
        size_t           count        = 0;  // number of sprites
    };

public:

    Buffer<Renderable>* renderQueue = nullptr;

public:
    SpriteRenderPass(const SpriteRenderPass &) = delete;

    SpriteRenderPass &operator=(const SpriteRenderPass &) = delete;

    SpriteRenderPass(SpriteRenderPass &&) = delete;

    SpriteRenderPass &operator=(SpriteRenderPass &&) = delete;

    SpriteRenderPass() : RenderPass() {}

    [[nodiscard]] bool init(
        GpuTextureFormat swapchain_texture_format, uint32_t surface_width,
        uint32_t surface_height, std::string name, bool logInit = true,
        size_t capacity = 0, bool forceNoMSAA = false
    ) override;

    void release(bool logRelease = true) override;

    void onResize(uint32_t surfaceWidth, uint32_t surfaceHeight) override;

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

    // Blend state applied at pipeline creation time. Defaults to KeepDstAlpha for
    // framebuffer compositing; sprite passes drawing into intermediate targets typically
    // override this via UpdateRenderPassBlendState before init().
    GpuColorTargetBlendState renderPassBlendState = GpuPresets::AlphaBlendKeepDstAlpha;

    void UpdateRenderPassBlendState(GpuColorTargetBlendState newstate) {
        renderPassBlendState = newstate;
    }

    // WebGPU effect-pipeline helpers — defined only in the WebGPU backend .cpp.
    // Declared unconditionally so the header is backend-neutral; SDL builds never reference these symbols.
    GpuGraphicsPipelineHandle _getOrCreateEffectPipeline(const ShaderAsset& vertShader, const ShaderAsset& fragShader);
    void _applyEffectsWGPU(GpuCmdBufferHandle cmdBuffer, const std::vector<EffectAsset>& effects,
                            GpuTextureHandle sourceTexture, GpuTextureHandle targetTexture,
                            const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>>& extraTextures,
                            GpuLoadOp targetLoadOp = GpuLoadOp::Load,
                            float clearR = 0, float clearG = 0, float clearB = 0, float clearA = 0);

    // Shared: each backend supplies its own implementation that writes vertex_shader + fragment_shader.
    void createShaders();

    // SDL-only extra-texture API — no-ops on WebGPU (which routes extras through draw-time arg).
    // Defined unconditionally so user code is backend-neutral.
    void SetAdditionalTexture(uint32_t binding, GpuTextureHandle texture) {
        m_additionalEffectTextures[binding] = texture;
    }
    void ClearAdditionalTextures() {
        m_additionalEffectTextures.clear();
    }

    // SDL-only effect ping-pong resources + impls. WebGPU uses _applyEffectsWGPU instead.
    TextureAsset                effectTempA;
    TextureAsset                effectTempB;
    GpuGraphicsPipelineHandle   effectPipeline       = 0;
    GpuGraphicsPipelineHandle   effectSpritePipeline = 0;
    GpuShaderHandle             effectVertShader     = 0;

    // Cache of per-effect pipelines, keyed by the state that affects pipeline creation:
    // (vert shader, frag shader, isLast, target format, sample count). m_noMSAA is fixed at
    // init so blend state is fully determined by isLast. Avoids recreating pipelines every frame.
    using EffectPipelineKey = std::tuple<GpuShaderHandle, GpuShaderHandle, bool,
                                         GpuTextureFormat, GpuSampleCount>;
    std::map<EffectPipelineKey, GpuGraphicsPipelineHandle> m_effectPipelineCache;

    // Static fullscreen-quad geometry for the effect blit. Created once and reused across
    // frames (index data never changes); vertices re-uploaded only when the UV scale
    // (physical/surface ratio) changes on resize. Avoids 4 GPU buffer alloc+free per effect frame.
    GpuBufferHandle m_effectQuadVbuf     = 0;
    GpuBufferHandle m_effectQuadIbuf     = 0;
    float           m_effectQuadUvScaleX = -1.0f;
    float           m_effectQuadUvScaleY = -1.0f;

    void createEffectResources();
    void releaseEffectResources();
    void applyEffects(GpuCmdBufferHandle cmd_buffer, const std::vector<EffectAsset>& effects,
                     GpuTextureHandle sourceTexture, GpuTextureHandle targetTexture, const glm::mat4& camera,
                     GpuTextureFormat targetFormat, bool isFirstBatch,
                     const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>>& effectTextures);
};
