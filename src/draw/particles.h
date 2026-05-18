#pragma once

#include <vector>
#include <string>

#include "SDL3/SDL_gpu.h"

#include <glm/glm.hpp>

#include "draw/particlesystem.h"
#include "assets/compute/computepipeline.h"
#include "gpu/renderpass.h"
#include "gpu/buffer/uniformobject.h"

// ─────────────────────────────────────────────────────────────────────────────
// ParticleRenderPass
//
// A RenderPass subclass that renders all queued particle systems each frame
// using instanced billboard quads.  Register it in a framebuffer with
// Renderer::AttachRenderPassToFrameBuffer().
// ─────────────────────────────────────────────────────────────────────────────

class ParticleRenderPass : public RenderPass {
public:
    struct DrawCmd {
        uint32_t         particleOffset;
        uint32_t         maxParticles;
        GpuTextureHandle texture = 0;  // 0 → bind white pixel fallback
        GpuSamplerHandle sampler = 0;  // 0 → bind internal linear sampler
        bool             pixelMode;    // true → standard alpha blend; false → additive
    };

    explicit ParticleRenderPass();

    bool init(GpuTextureFormat swapchainFormat, uint32_t width, uint32_t height,
              std::string name, bool logInit = true,
              size_t capacity = 0, bool forceNoMSAA = false) override;
    void release(bool logRelease = true) override;

    void render(GpuCmdBufferHandle cmdBuf,
                GpuTextureHandle   target,
                const glm::mat4&   camera) override;

    void addToRenderQueue(const Renderable&) override {}
    void resetRenderQueue() override { m_drawQueue.clear(); }
    UniformBuffer& getUniformBuffer() override { return m_dummyUniforms; }

    void addDraw(const DrawCmd& cmd) { m_drawQueue.push_back(cmd); }

    // Enable or disable persistence-of-vision trails. decay in (0,1): higher = longer trails.
    void SetPOV(bool enabled, float decay);
    bool  getPOVEnabled() const { return m_povEnabled; }
    float getPOVDecay()   const { return m_povDecay;   }

private:
    SDL_GPUDevice*            m_gpu_device    = nullptr;
    GpuGraphicsPipelineHandle m_pipeline      = 0;  // additive blend
    GpuGraphicsPipelineHandle m_pixelPipeline = 0;  // standard alpha blend (pixel mode)
    GpuShaderHandle           m_vertShader    = 0;
    GpuShaderHandle           m_fragShader    = 0;

    std::vector<DrawCmd> m_drawQueue;
    UniformBuffer        m_dummyUniforms;

    GpuTextureFormat m_format        = GpuTextureFormat::B8G8R8A8_Unorm;
    uint32_t         m_surfaceWidth  = 0;
    uint32_t         m_surfaceHeight = 0;

    // ── Persistence-of-vision ─────────────────────────────────────────────────
    bool             m_povEnabled    = false;
    float            m_povDecay      = 0.92f;
    bool             m_povNeedsClear = true;
    uint32_t         m_povIndex      = 0;          // ping-pong index (0 or 1)

    GpuTextureHandle          m_povTex[2]          = {};
    GpuSamplerHandle          m_povSampler         = 0;
    GpuGraphicsPipelineHandle m_povDecayPipeline   = 0;  // no-blend, overwrites POV tex
    GpuGraphicsPipelineHandle m_povCompositePipeline = 0; // additive onto swapchain
    GpuShaderHandle           m_povVertShader      = 0;
    GpuShaderHandle           m_povFragShader      = 0;
};


// ─────────────────────────────────────────────────────────────────────────────
// Particles namespace — global manager
//
// Typical usage:
//
//   // Once, at startup:
//   Particles::Init();
//   auto fire = Particles::CreateSystem(config);
//   Particles::Start(fire);
//
//   // Attach the render pass to your framebuffer:
//   Renderer::AttachRenderPassToFrameBuffer(
//       Particles::GetRenderPass(), "particles", "myFramebuffer");
//
//   // Every frame in Update():
//   Particles::Update(dt);
//
//   // Every frame in Render():
//   Draw::Particles(fire);          // or Particles::QueueDraw(fire)
//
//   // On shutdown:
//   Particles::Quit();
// ─────────────────────────────────────────────────────────────────────────────

namespace Particles {

    static constexpr uint32_t MAX_PARTICLES      = 50'000'000;
    static constexpr uint32_t MAX_SYSTEMS        = 64;
    static constexpr uint32_t MAX_CUSTOM_COMPUTES = 32;

    // --- Lifecycle ---

    /// Allocates GPU buffers, compiles the compute pipeline.
    /// Must be called after Renderer::InitRendering().
    void Init();

    /// Releases all GPU resources. Call before Renderer::Close().
    void Quit();

    // --- System management ---

    /// Allocate a particle system and return a handle.
    ParticleSystemHandle CreateSystem(const ParticleSystemConfig& config);

    /// Allocate a particle system from a base64-encoded preset string (from the particle editor).
    /// maxParticles controls GPU slot allocation and cannot be changed after creation.
    /// spawnPosition defaults to the world origin — call SetPosition() to move it later.
    /// Returns an invalid handle if the preset string is malformed.
    ParticleSystemHandle CreateSystemFromPreset(const char* encoded, uint32_t maxParticles,
                                                glm::vec3 spawnPosition = {0.0f, 0.0f, 0.0f});

    /// Free a system and reclaim its particle slots.
    void DestroySystem(ParticleSystemHandle& handle);

    /// Begin / end emission.
    void Start(const ParticleSystemHandle& handle);
    void Stop (const ParticleSystemHandle& handle);

    /// Reposition the spawn origin (e.g., to follow an entity).
    void SetPosition(const ParticleSystemHandle& handle, glm::vec3 worldPos);

    /// Assign a texture to the system at runtime (overrides shape with Textured).
    /// Pass 0 for texture to revert to the config's shape.
    void SetTexture(const ParticleSystemHandle& handle,
                    GpuTextureHandle texture, GpuSamplerHandle sampler = 0);

    // --- Custom compute ---

    /// Compile a compute shader from shaderPath and return a reusable handle.
    /// The shader replaces the built-in update for any system it is assigned to.
    ///
    /// Required HLSL binding layout:
    ///   StructuredBuffer<GPUParticleSystem>  systems   : register(t0, space0);
    ///   RWStructuredBuffer<GPUParticle>      particles : register(u0, space1);
    ///   cbuffer Uniforms : register(b0, space2) {
    ///       uint  particleOffset;  // first particle index in the global buffer
    ///       uint  particleCount;   // particles owned by this system
    ///       float deltaTime;
    ///       float time;
    ///   };
    ///
    /// In main(): use (particleOffset + dispatchID.x) as the global particle index.
    ParticleComputeHandle CreateCustomCompute(const std::string& shaderPath);

    /// Release the GPU pipeline created by CreateCustomCompute().
    /// Automatically clears the pipeline from any system it was assigned to.
    void DestroyCustomCompute(ParticleComputeHandle& handle);

    /// Replace the built-in update for a system with a custom compute pipeline.
    void SetCustomCompute(const ParticleSystemHandle& system,
                          const ParticleComputeHandle& compute);

    /// Revert a system to the built-in update logic.
    void ClearCustomCompute(const ParticleSystemHandle& system);

    /// Update system parameters in real-time without recreating the system.
    /// maxParticles cannot be changed. Start/Stop state is preserved.
    void UpdateConfig(const ParticleSystemHandle& handle, const ParticleSystemConfig& cfg);

    /// Read back the current configuration of a system into a ParticleSystemConfig.
    /// Returns a default-constructed config if the handle is invalid.
    ParticleSystemConfig GetConfig(const ParticleSystemHandle& handle);

    // --- Per-frame ---

    /// Queue the GPU compute dispatch that advances all live particles.
    /// Call this in your Update() method.
    void Update(float deltaTime);

    /// Add a system to the current frame's particle draw queue.
    void QueueDraw(const ParticleSystemHandle& handle);

    // --- Accessors used by ParticleRenderPass ---

    GpuBufferHandle     GetParticleBuffer();
    GpuBufferHandle     GetSystemBuffer();
    ParticleRenderPass* GetRenderPass();
    GpuTextureHandle    GetWhiteTexture();   // 1×1 white fallback for non-textured draws
    GpuSamplerHandle    GetLinearSampler();  // default sampler for textured draws

    // --- Persistence of Vision ---

    /// Enable or disable POV trails for all particle systems.
    /// decay in (0,1): higher value = longer / brighter trails (0.92 is a good default).
    void  SetPOV(bool enabled, float decay = 0.92f);
    bool  GetPOVEnabled();
    float GetPOVDecay();

    // --- Internal: called by Renderer::_endFrame() before compute ---
    void _PrepareFrame(GpuCmdBufferHandle cmdBuf);

} // namespace Particles
