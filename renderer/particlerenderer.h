#pragma once

#include <vector>
#include <string>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include "assettypes/particlesystem.h"
#include "assettypes/computepipeline.h"
#include "renderpass.h"
#include "utils/uniformobject.h"

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
        uint32_t particleOffset;
        uint32_t maxParticles;
        // systemIndex is not needed here; particle data carries systemID
    };

    explicit ParticleRenderPass(SDL_GPUDevice* device);

    bool init(SDL_GPUTextureFormat swapchainFormat, uint32_t width, uint32_t height,
              std::string name, bool logInit = true,
              size_t capacity = 0, bool forceNoMSAA = false) override;
    void release(bool logRelease = true) override;

    void render(SDL_GPUCommandBuffer* cmdBuf,
                SDL_GPUTexture*       target,
                const glm::mat4&      camera) override;

    void addToRenderQueue(const Renderable&) override {}
    void resetRenderQueue() override { m_drawQueue.clear(); }
    UniformBuffer& getUniformBuffer() override { return m_dummyUniforms; }

    void addDraw(const DrawCmd& cmd) { m_drawQueue.push_back(cmd); }

private:
    SDL_GPUGraphicsPipeline* m_pipeline   = nullptr;
    SDL_GPUShader*           m_vertShader = nullptr;
    SDL_GPUShader*           m_fragShader = nullptr;

    std::vector<DrawCmd> m_drawQueue;
    UniformBuffer        m_dummyUniforms;

    SDL_GPUTextureFormat m_format        = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    uint32_t             m_surfaceWidth  = 0;
    uint32_t             m_surfaceHeight = 0;
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

    static constexpr uint32_t MAX_PARTICLES = 50'000'000;
    static constexpr uint32_t MAX_SYSTEMS   = 64;

    // --- Lifecycle ---

    /// Allocates GPU buffers, compiles the compute pipeline.
    /// Must be called after Renderer::InitRendering().
    void Init();

    /// Releases all GPU resources. Call before Renderer::Close().
    void Quit();

    // --- System management ---

    /// Allocate a particle system and return a handle.
    ParticleSystemHandle CreateSystem(const ParticleSystemConfig& config);

    /// Free a system and reclaim its particle slots.
    void DestroySystem(ParticleSystemHandle& handle);

    /// Begin / end emission.
    void Start(const ParticleSystemHandle& handle);
    void Stop (const ParticleSystemHandle& handle);

    /// Reposition the spawn origin (e.g., to follow an entity).
    void SetPosition(const ParticleSystemHandle& handle, glm::vec3 worldPos);

    /// Update system parameters in real-time without recreating the system.
    /// maxParticles cannot be changed. Start/Stop state is preserved.
    void UpdateConfig(const ParticleSystemHandle& handle, const ParticleSystemConfig& cfg);

    // --- Per-frame ---

    /// Queue the GPU compute dispatch that advances all live particles.
    /// Call this in your Update() method.
    void Update(float deltaTime);

    /// Add a system to the current frame's particle draw queue.
    void QueueDraw(const ParticleSystemHandle& handle);

    // --- Accessors used by ParticleRenderPass ---

    SDL_GPUBuffer*      GetParticleBuffer();
    SDL_GPUBuffer*      GetSystemBuffer();
    ParticleRenderPass* GetRenderPass();

    // --- Internal: called by Renderer::_endFrame() before compute ---
    void _PrepareFrame(SDL_GPUCommandBuffer* cmdBuf);

} // namespace Particles
