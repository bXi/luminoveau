#pragma once

#include <vector>
#include <string>

#include <glm/glm.hpp>

#include "config.h"
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

/**
 * @brief Global GPU-accelerated particle manager.
 *
 * Owns the shared particle/system/collider buffers, the built-in physics compute
 * pipeline and the render pass. Singleton — all methods are static.
 */
class Particles {
public:
    // Backend-tunable; defined in config.h.
    static constexpr uint32_t MAX_PARTICLES       = LUMINOVEAU_MAX_PARTICLES;  ///< Total particle slots across all systems.
    static constexpr uint32_t MAX_SYSTEMS         = 64;   ///< Maximum concurrent particle systems.
    static constexpr uint32_t MAX_CUSTOM_COMPUTES = 32;   ///< Maximum custom compute pipelines.
    static constexpr uint32_t MAX_COLLIDERS       = 32;   ///< Maximum active colliders.

    // --- Lifecycle ---

    /// Allocates GPU buffers, compiles the compute pipeline.
    /// Must be called after Renderer::InitRendering().
    static void Init() { get()._init(); }

    /// Releases all GPU resources. Call before Renderer::Close().
    static void Quit() { get()._quit(); }

    // --- System management ---

    /// Allocate a particle system and return a handle.
    static ParticleSystemHandle CreateSystem(const ParticleSystemConfig& config) { return get()._createSystem(config); }

    /// Allocate a particle system from a base64-encoded preset string (from the particle editor).
    /// maxParticles controls GPU slot allocation and cannot be changed after creation.
    /// spawnPosition defaults to the world origin — call SetPosition() to move it later.
    /// Returns an invalid handle if the preset string is malformed.
    static ParticleSystemHandle CreateSystemFromPreset(const char* encoded, uint32_t maxParticles,
                                                glm::vec3 spawnPosition = {0.0f, 0.0f, 0.0f}) {
        return get()._createSystemFromPreset(encoded, maxParticles, spawnPosition);
    }

    /// Free a system and reclaim its particle slots.
    static void DestroySystem(ParticleSystemHandle& handle) { get()._destroySystem(handle); }

    /// Begin emission for a system.
    static void Start(const ParticleSystemHandle& handle) { get()._start(handle); }
    /// Stop emission for a system (existing particles finish their life).
    static void Stop (const ParticleSystemHandle& handle) { get()._stop(handle); }

    /// Reposition the spawn origin (e.g., to follow an entity).
    static void SetPosition(const ParticleSystemHandle& handle, glm::vec3 worldPos) { get()._setPosition(handle, worldPos); }

    /// Assign a texture to the system at runtime (overrides shape with Textured).
    /// Pass 0 for texture to revert to the config's shape.
    static void SetTexture(const ParticleSystemHandle& handle,
                    GpuTextureHandle texture, GpuSamplerHandle sampler = 0) {
        get()._setTexture(handle, texture, sampler);
    }

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
    static ParticleComputeHandle CreateCustomCompute(const std::string& shaderPath) { return get()._createCustomCompute(shaderPath); }

    /// Release the GPU pipeline created by CreateCustomCompute().
    /// Automatically clears the pipeline from any system it was assigned to.
    static void DestroyCustomCompute(ParticleComputeHandle& handle) { get()._destroyCustomCompute(handle); }

    /// Replace the built-in update for a system with a custom compute pipeline.
    static void SetCustomCompute(const ParticleSystemHandle& system,
                          const ParticleComputeHandle& compute) {
        get()._setCustomCompute(system, compute);
    }

    /// Revert a system to the built-in update logic.
    static void ClearCustomCompute(const ParticleSystemHandle& system) { get()._clearCustomCompute(system); }

    /// Update system parameters in real-time without recreating the system.
    /// maxParticles cannot be changed. Start/Stop state is preserved.
    static void UpdateConfig(const ParticleSystemHandle& handle, const ParticleSystemConfig& cfg) { get()._updateConfig(handle, cfg); }

    /// Read back the current configuration of a system into a ParticleSystemConfig.
    /// Returns a default-constructed config if the handle is invalid.
    static ParticleSystemConfig GetConfig(const ParticleSystemHandle& handle) { return get()._getConfig(handle); }

    // --- Per-frame ---

    /// Queue the GPU compute dispatch that advances all live particles.
    /// Call this in your Update() method.
    static void Update(float deltaTime) { get()._update(deltaTime); }

    /// Add a system to the current frame's particle draw queue.
    static void QueueDraw(const ParticleSystemHandle& handle) { get()._queueDraw(handle); }

    // --- Accessors used by ParticleRenderPass ---

    /// @brief Returns the shared GPU buffer holding all particles.
    static GpuBufferHandle     GetParticleBuffer() { return get()._getParticleBuffer(); }
    /// @brief Returns the GPU buffer holding per-system parameters.
    static GpuBufferHandle     GetSystemBuffer()   { return get()._getSystemBuffer(); }
    /// @brief Returns the render pass that draws all queued particle systems.
    static ParticleRenderPass* GetRenderPass()     { return get()._getRenderPass(); }
    /// @brief Returns a 1×1 white texture used as the fallback for non-textured draws.
    static GpuTextureHandle    GetWhiteTexture()   { return get()._getWhiteTexture(); }
    /// @brief Returns the default linear sampler used for textured draws.
    static GpuSamplerHandle    GetLinearSampler()  { return get()._getLinearSampler(); }

    // --- Physics pass (for systems with a custom compute) ---

    /// Run a standard physics step (gravity, drag, collision) after a custom compute.
    /// gravity is in pixels/s². Useful for Mandelbrot and other custom shaders that
    /// set positions directly and don't do their own physics.
    static void EnablePhysicsPass(const ParticleSystemHandle& handle,
                           const ParticleComputeHandle& physicsCompute,
                           glm::vec2 gravity = {0.f, 200.f},
                           float drag = 0.5f) {
        get()._enablePhysicsPass(handle, physicsCompute, gravity, drag);
    }
    /// Disable the physics pass previously enabled with EnablePhysicsPass().
    static void DisablePhysicsPass(const ParticleSystemHandle& handle) { get()._disablePhysicsPass(handle); }
    /// Update the physics pass gravity/drag without re-enabling it.
    static void SetPhysicsPassParams(const ParticleSystemHandle& handle,
                              glm::vec2 gravity, float drag) {
        get()._setPhysicsPassParams(handle, gravity, drag);
    }

    // --- Spring pass (elastic deformation for custom computes) ---

    /// Run a spring/elastic pass after a custom compute.
    /// springCompute: handle created via CreateCustomCompute() from the user's spring shader.
    /// springK: stiffness (e.g. 80), damping: velocity damping (e.g. 8).
    static void EnableSpringPass(const ParticleSystemHandle& handle,
                          const ParticleComputeHandle& springCompute,
                          float springK = 80.f, float damping = 8.f) {
        get()._enableSpringPass(handle, springCompute, springK, damping);
    }
    /// Disable the spring pass previously enabled with EnableSpringPass().
    static void DisableSpringPass(const ParticleSystemHandle& handle) { get()._disableSpringPass(handle); }
    /// Update spring stiffness/damping without re-enabling the pass.
    static void SetSpringParams(const ParticleSystemHandle& handle,
                         float springK, float damping) {
        get()._setSpringParams(handle, springK, damping);
    }
    /// Set cursor interaction for this frame. Call every frame while interacting.
    /// r = 0 or f = 0 disables interaction. f > 0 pulls toward cursor, f < 0 pushes.
    static void SetSpringInteraction(const ParticleSystemHandle& handle,
                              float x, float y, float r, float f) {
        get()._setSpringInteraction(handle, x, y, r, f);
    }

    // --- Colliders ---

    /// Add a static collider that particles bounce off.
    /// restitution: bounce factor [0,1] (1 = elastic). friction: tangential damping [0,1].
    static ColliderHandle AddCollider(ColliderType type, glm::vec4 params,
                               float restitution = 0.5f, float friction = 0.1f) {
        return get()._addCollider(type, params, restitution, friction);
    }

    /// Disable and free a collider slot.
    static void RemoveCollider(ColliderHandle& handle) { get()._removeCollider(handle); }

    /// Disable all active colliders.
    static void ClearColliders() { get()._clearColliders(); }

    /// Update params/restitution/friction of an existing collider without recreating it.
    static void UpdateCollider(const ColliderHandle& handle, glm::vec4 params,
                        float restitution, float friction) {
        get()._updateCollider(handle, params, restitution, friction);
    }

    // --- Persistence of Vision ---

    /// Enable or disable POV trails for all particle systems.
    /// decay in (0,1): higher value = longer / brighter trails (0.92 is a good default).
    static void  SetPOV(bool enabled, float decay = 0.92f) { get()._setPOV(enabled, decay); }
    /// @brief Returns whether persistence-of-vision trails are currently enabled.
    static bool  GetPOVEnabled() { return get()._getPOVEnabled(); }
    /// @brief Returns the current persistence-of-vision decay factor.
    static float GetPOVDecay() { return get()._getPOVDecay(); }

    /// @cond INTERNAL
    // Internal: called by Renderer::_endFrame() before compute.
    static void _PrepareFrame(GpuCmdBufferHandle cmdBuf) { get()._prepareFrame(cmdBuf); }

private:
    Particles(const Particles&) = delete;
    static Particles& get() { static Particles instance; return instance; }
    Particles() = default;

    static constexpr uint32_t INVALID_CUSTOM_COMPUTE = UINT32_MAX;

    // --- Implementation (state lives on the singleton instance) ---
    void _init();
    void _quit();
    ParticleSystemHandle _createSystem(const ParticleSystemConfig& cfg);
    ParticleSystemHandle _createSystemFromPreset(const char* encoded, uint32_t maxParticles, glm::vec3 spawnPosition);
    void _destroySystem(ParticleSystemHandle& handle);
    void _start(const ParticleSystemHandle& handle);
    void _stop(const ParticleSystemHandle& handle);
    void _setPosition(const ParticleSystemHandle& handle, glm::vec3 worldPos);
    void _setTexture(const ParticleSystemHandle& handle, GpuTextureHandle texture, GpuSamplerHandle sampler);
    ParticleComputeHandle _createCustomCompute(const std::string& shaderPath);
    void _destroyCustomCompute(ParticleComputeHandle& handle);
    void _setCustomCompute(const ParticleSystemHandle& system, const ParticleComputeHandle& compute);
    void _clearCustomCompute(const ParticleSystemHandle& system);
    void _updateConfig(const ParticleSystemHandle& handle, const ParticleSystemConfig& cfg);
    ParticleSystemConfig _getConfig(const ParticleSystemHandle& handle);
    void _update(float deltaTime);
    void _queueDraw(const ParticleSystemHandle& handle);
    GpuBufferHandle     _getParticleBuffer();
    GpuBufferHandle     _getSystemBuffer();
    ParticleRenderPass* _getRenderPass();
    GpuTextureHandle    _getWhiteTexture();
    GpuSamplerHandle    _getLinearSampler();
    void _enablePhysicsPass(const ParticleSystemHandle& handle, const ParticleComputeHandle& physicsCompute, glm::vec2 gravity, float drag);
    void _disablePhysicsPass(const ParticleSystemHandle& handle);
    void _setPhysicsPassParams(const ParticleSystemHandle& handle, glm::vec2 gravity, float drag);
    void _enableSpringPass(const ParticleSystemHandle& handle, const ParticleComputeHandle& springCompute, float springK, float damping);
    void _disableSpringPass(const ParticleSystemHandle& handle);
    void _setSpringParams(const ParticleSystemHandle& handle, float springK, float damping);
    void _setSpringInteraction(const ParticleSystemHandle& handle, float x, float y, float r, float f);
    ColliderHandle _addCollider(ColliderType type, glm::vec4 params, float restitution, float friction);
    void _removeCollider(ColliderHandle& handle);
    void _clearColliders();
    void _updateCollider(const ColliderHandle& handle, glm::vec4 params, float restitution, float friction);
    void _setPOV(bool enabled, float decay);
    bool _getPOVEnabled();
    float _getPOVDecay();
    void _prepareFrame(GpuCmdBufferHandle cmdBuf);

    // Internal helpers.
    uint32_t _allocateSystemSlot();
    void     _buildDispatches();
    void     _attachToFramebuffer(const std::string& fbName);

    // --- GPU resources ---
    GpuBufferHandle         s_particleBuf     = 0;  // RW: compute + vertex read
    GpuBufferHandle         s_systemBuf       = 0;  // RO: compute + vertex read
    GpuTransferBufferHandle s_systemUploadBuf = 0;  // CPU→GPU transfer

    // --- Compute pipelines ---
    ComputePipelineAsset s_computePipeline;

    // --- CPU-side system data ---
    GPUParticleSystem s_systemData[MAX_SYSTEMS]     = {};
    bool              s_systemUsed[MAX_SYSTEMS]     = {};
    GpuTextureHandle  s_systemTextures[MAX_SYSTEMS] = {};
    GpuSamplerHandle  s_systemSamplers[MAX_SYSTEMS] = {};
    bool              s_systemDirty = false;

    // --- Per-system render flags ---
    bool s_systemPixelMode[MAX_SYSTEMS] = {};

    // --- Physics pass per-system state ---
    uint32_t  s_systemPhysicsCompute[MAX_SYSTEMS] = {};  // pool index, INVALID = none
    glm::vec2 s_systemPhysicsGravity[MAX_SYSTEMS] = {};
    float     s_systemPhysicsDrag[MAX_SYSTEMS]    = {};

    // --- Spring pass per-system state ---
    uint32_t  s_systemSpringCompute[MAX_SYSTEMS]   = {};
    float     s_systemSpringK[MAX_SYSTEMS]         = {};
    float     s_systemSpringDamp[MAX_SYSTEMS]      = {};
    glm::vec4 s_systemSpringInteract[MAX_SYSTEMS]  = {};

    // --- Custom compute pool ---
    ComputePipelineAsset s_customComputePool[MAX_CUSTOM_COMPUTES] = {};
    bool                 s_customComputeUsed[MAX_CUSTOM_COMPUTES] = {};
    uint32_t             s_systemCustomCompute[MAX_SYSTEMS]       = {};

    // --- Collider resources ---
    GpuBufferHandle         s_colliderBuf       = 0;
    GpuTransferBufferHandle s_colliderUploadBuf = 0;
    GPUCollider             s_colliderData[MAX_COLLIDERS] = {};
    bool                    s_colliderUsed[MAX_COLLIDERS] = {};
    uint32_t                s_colliderHighWater = 0;   // one past last used slot
    bool                    s_colliderDirty     = false;

    // --- Fallback resources for non-textured draws ---
    GpuTextureHandle s_whiteTexture  = 0;
    GpuSamplerHandle s_linearSampler = 0;

    // --- Particle slot allocator ---
    uint32_t s_nextParticleSlot = 0;
    uint32_t s_slotParticleOffset[MAX_SYSTEMS] = {};
    uint32_t s_slotParticleCount [MAX_SYSTEMS] = {};

    // --- Per-frame state ---
    float    s_accumTime    = 0.0f;
    float    s_pendingDt    = 0.0f;
    bool     s_updateQueued = false;

    // --- Render pass ---
    ParticleRenderPass* s_renderPass = nullptr;
    /// @endcond
};
