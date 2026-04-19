#include "particlerenderer.h"

#include <cstring>
#include <algorithm>

#include "rendererhandler.h"
#include "computehandler.h"
#include "shaderhandler.h"
#include "../window/windowhandler.h"
#include "sdl_gpu_structs.h"
#include "../log/loghandler.h"
#include "../assettypes/computepipeline.h"
#include "../utils/uniformobject.h"
#include "../assethandler/shaders_generated.h"

#include <glm/gtc/type_ptr.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Particles namespace — implementation
// ─────────────────────────────────────────────────────────────────────────────

namespace Particles {

// --- GPU resources ---
static SDL_GPUBuffer* s_particleBuf = nullptr;  // RW: compute + vertex read
static SDL_GPUBuffer* s_systemBuf   = nullptr;  // RO: compute + vertex read
static SDL_GPUTransferBuffer* s_systemUploadBuf = nullptr; // CPU→GPU transfer

// --- Compute pipeline ---
static ComputePipelineAsset s_computePipeline;

// --- CPU-side system data ---
static GPUParticleSystem s_systemData[MAX_SYSTEMS]    = {};
static bool              s_systemUsed[MAX_SYSTEMS]    = {};
static SDL_GPUTexture*   s_systemTextures[MAX_SYSTEMS] = {};
static SDL_GPUSampler*   s_systemSamplers[MAX_SYSTEMS] = {};
static bool              s_systemDirty = false;

// --- Per-system render flags ---
static bool s_systemPixelMode[MAX_SYSTEMS] = {};

// --- Custom compute pool ---
static constexpr uint32_t INVALID_CUSTOM_COMPUTE = UINT32_MAX;
static ComputePipelineAsset s_customComputePool[MAX_CUSTOM_COMPUTES] = {};
static bool                 s_customComputeUsed[MAX_CUSTOM_COMPUTES] = {};
// Per-system index into the pool (INVALID_CUSTOM_COMPUTE = none)
static uint32_t             s_systemCustomCompute[MAX_SYSTEMS]       = {};

// --- Fallback resources for non-textured draws ---
static SDL_GPUTexture* s_whiteTexture  = nullptr;
static SDL_GPUSampler* s_linearSampler = nullptr;

// --- Particle slot allocator ---
static uint32_t s_nextParticleSlot = 0;
// Per-slot particle ranges so DestroySystem can reclaim them
static uint32_t s_slotParticleOffset[MAX_SYSTEMS] = {};
static uint32_t s_slotParticleCount [MAX_SYSTEMS] = {};

// --- Per-frame state ---
static float    s_accumTime  = 0.0f;
static float    s_pendingDt  = 0.0f;
static bool     s_updateQueued = false;

// --- Render pass ---
static ParticleRenderPass* s_renderPass = nullptr;

// ─────────────────────────────────────────────────────────────────────────────

void AttachToFramebuffer(const std::string& fbName); // defined below

void Init() {
    SDL_GPUDevice* device = Renderer::GetDevice();

    // Particle buffer: compute RW + vertex read
    SDL_GPUBufferCreateInfo particleBufInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ  |
                 SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                 SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size  = static_cast<uint32_t>(MAX_PARTICLES * sizeof(GPUParticle)),
        .props = 0
    };
    s_particleBuf = SDL_CreateGPUBuffer(device, &particleBufInfo);
    if (!s_particleBuf) {
        LOG_CRITICAL("Particles: failed to create particle buffer: {}", SDL_GetError());
    }

    // System data buffer: compute RO + vertex read
    SDL_GPUBufferCreateInfo systemBufInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                 SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size  = static_cast<uint32_t>(MAX_SYSTEMS * sizeof(GPUParticleSystem)),
        .props = 0
    };
    s_systemBuf = SDL_CreateGPUBuffer(device, &systemBufInfo);
    if (!s_systemBuf) {
        LOG_CRITICAL("Particles: failed to create system buffer: {}", SDL_GetError());
    }

    // Transfer buffer for system data uploads
    SDL_GPUTransferBufferCreateInfo tbInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = static_cast<uint32_t>(MAX_SYSTEMS * sizeof(GPUParticleSystem)),
        .props = 0
    };
    s_systemUploadBuf = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    if (!s_systemUploadBuf) {
        LOG_CRITICAL("Particles: failed to create system upload buffer: {}", SDL_GetError());
    }

    // Zero-init both GPU buffers via an immediate copy pass
    {
        // Zero particle buffer
        SDL_GPUTransferBufferCreateInfo zeroBufInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = static_cast<uint32_t>(MAX_PARTICLES * sizeof(GPUParticle)),
            .props = 0
        };
        SDL_GPUTransferBuffer* zeroTB = SDL_CreateGPUTransferBuffer(device, &zeroBufInfo);
        void* mapped = SDL_MapGPUTransferBuffer(device, zeroTB, false);
        SDL_memset(mapped, 0, MAX_PARTICLES * sizeof(GPUParticle));
        SDL_UnmapGPUTransferBuffer(device, zeroTB);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTransferBufferLocation src  = {.transfer_buffer = zeroTB, .offset = 0};
        SDL_GPUBufferRegion           dst  = {.buffer = s_particleBuf,
                                               .offset = 0,
                                               .size = static_cast<uint32_t>(MAX_PARTICLES * sizeof(GPUParticle))};
        SDL_UploadToGPUBuffer(cp, &src, &dst, false);

        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_WaitForGPUIdle(device);
        SDL_ReleaseGPUTransferBuffer(device, zeroTB);
    }

    // Create 1×1 white fallback texture for non-textured draws
    {
        SDL_GPUTextureCreateInfo texInfo = {
            .type              = SDL_GPU_TEXTURETYPE_2D,
            .format            = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            .usage             = SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width             = 1,
            .height            = 1,
            .layer_count_or_depth = 1,
            .num_levels        = 1,
            .props             = 0,
        };
        s_whiteTexture = SDL_CreateGPUTexture(device, &texInfo);

        uint32_t whitePixel = 0xFFFFFFFFu;
        SDL_GPUTransferBufferCreateInfo tbInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = sizeof(whitePixel),
            .props = 0,
        };
        SDL_GPUTransferBuffer* wtb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
        void* wm = SDL_MapGPUTransferBuffer(device, wtb, false);
        SDL_memcpy(wm, &whitePixel, sizeof(whitePixel));
        SDL_UnmapGPUTransferBuffer(device, wtb);

        SDL_GPUCommandBuffer* wcmd = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUCopyPass* wcp = SDL_BeginGPUCopyPass(wcmd);
        SDL_GPUTextureTransferInfo wsrc = {
            .transfer_buffer = wtb, .offset = 0, .pixels_per_row = 0, .rows_per_layer = 0
        };
        SDL_GPUTextureRegion wdst = {
            .texture = s_whiteTexture, .w = 1, .h = 1, .d = 1
        };
        SDL_UploadToGPUTexture(wcp, &wsrc, &wdst, false);
        SDL_EndGPUCopyPass(wcp);
        SDL_SubmitGPUCommandBuffer(wcmd);
        SDL_WaitForGPUIdle(device);
        SDL_ReleaseGPUTransferBuffer(device, wtb);
    }

    // Linear sampler for textured particles
    s_linearSampler = SDL_CreateGPUSampler(device, &GPUstructs::linearSamplerCreateInfo);

    // Load compute pipeline from embedded engine SPIRV
    s_computePipeline = Shaders::CreateComputePipelineFromBytes(
        device,
        Luminoveau::Shaders::Particles_Comp,
        Luminoveau::Shaders::Particles_Comp_Size);

    // Create and attach the render pass to the primary framebuffer
    s_renderPass = new ParticleRenderPass(device);
    AttachToFramebuffer("primaryFramebuffer");

    std::fill(std::begin(s_systemCustomCompute), std::end(s_systemCustomCompute),
              INVALID_CUSTOM_COMPUTE);

    LOG_INFO("Particles: initialized (max particles: {}, max systems: {})",
             MAX_PARTICLES, MAX_SYSTEMS);
}

void Quit() {
    SDL_GPUDevice* device = Renderer::GetDevice();

    // Discard any compute dispatches queued this frame that reference our buffers.
    // They haven't been submitted yet (that happens in _endFrame), so dropping them
    // is safe and prevents use-after-free inside Compute::_ExecuteQueued.
    Compute::_Reset();

    // Wait for the GPU to finish all previously submitted work before releasing
    // any GPU resources (buffers, pipelines, shaders).
    SDL_WaitForGPUIdle(device);

    if (s_renderPass) {
        // RemoveShaderPass removes the pass from all framebuffers, calls release(),
        // and deletes the object — so we must not touch s_renderPass after this.
        Renderer::RemoveShaderPass("particles");
        s_renderPass = nullptr;
    }

    if (s_computePipeline.pipeline) {
        SDL_ReleaseGPUComputePipeline(device, s_computePipeline.pipeline);
        s_computePipeline = {};
    }

    for (uint32_t i = 0; i < MAX_CUSTOM_COMPUTES; ++i) {
        if (s_customComputeUsed[i] && s_customComputePool[i].pipeline) {
            SDL_ReleaseGPUComputePipeline(device, s_customComputePool[i].pipeline);
            s_customComputePool[i] = {};
            s_customComputeUsed[i] = false;
        }
    }

    if (s_particleBuf)     { SDL_ReleaseGPUBuffer(device, s_particleBuf);     s_particleBuf     = nullptr; }
    if (s_systemBuf)       { SDL_ReleaseGPUBuffer(device, s_systemBuf);       s_systemBuf       = nullptr; }
    if (s_systemUploadBuf) { SDL_ReleaseGPUTransferBuffer(device, s_systemUploadBuf); s_systemUploadBuf = nullptr; }
    if (s_whiteTexture)    { SDL_ReleaseGPUTexture(device, s_whiteTexture);   s_whiteTexture    = nullptr; }
    if (s_linearSampler)   { SDL_ReleaseGPUSampler(device, s_linearSampler);  s_linearSampler   = nullptr; }

    // Reset CPU-side state so Init() can be called again cleanly.
    s_nextParticleSlot = 0;
    s_systemDirty      = false;
    s_accumTime        = 0.0f;
    s_pendingDt        = 0.0f;
    s_updateQueued     = false;
    std::fill(std::begin(s_systemUsed),          std::end(s_systemUsed),          false);
    std::fill(std::begin(s_systemData),          std::end(s_systemData),          GPUParticleSystem{});
    std::fill(std::begin(s_systemTextures),      std::end(s_systemTextures),      nullptr);
    std::fill(std::begin(s_systemSamplers),      std::end(s_systemSamplers),      nullptr);
    std::fill(std::begin(s_systemPixelMode),     std::end(s_systemPixelMode),     false);
    std::fill(std::begin(s_slotParticleOffset),  std::end(s_slotParticleOffset),  0u);
    std::fill(std::begin(s_slotParticleCount),   std::end(s_slotParticleCount),   0u);
    std::fill(std::begin(s_systemCustomCompute), std::end(s_systemCustomCompute), INVALID_CUSTOM_COMPUTE);
    std::fill(std::begin(s_customComputeUsed),   std::end(s_customComputeUsed),   false);
    std::fill(std::begin(s_customComputePool),   std::end(s_customComputePool),   ComputePipelineAsset{});

    LOG_INFO("Particles: shut down");
}

// ─────────────────────────────────────────────────────────────────────────────

static uint32_t AllocateSystemSlot() {
    for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
        if (!s_systemUsed[i]) {
            s_systemUsed[i] = true;
            return i;
        }
    }
    LOG_CRITICAL("Particles: no free system slots (MAX_SYSTEMS = {})", MAX_SYSTEMS);
    return 0;
}

ParticleSystemHandle CreateSystem(const ParticleSystemConfig& cfg) {
    if (s_nextParticleSlot + cfg.maxParticles > MAX_PARTICLES) {
        LOG_ERROR("Particles: not enough particle slots for new system (wanted {}, have {})",
                  cfg.maxParticles, MAX_PARTICLES - s_nextParticleSlot);
        return {};
    }

    ParticleSystemHandle handle;
    handle.systemIndex    = AllocateSystemSlot();
    handle.particleOffset = s_nextParticleSlot;
    handle.maxParticles   = cfg.maxParticles;
    handle.valid          = true;

    s_slotParticleOffset[handle.systemIndex] = handle.particleOffset;
    s_slotParticleCount [handle.systemIndex] = handle.maxParticles;
    s_nextParticleSlot += cfg.maxParticles;

    // Fill GPU system struct
    GPUParticleSystem& sys  = s_systemData[handle.systemIndex];
    sys.spawnPos         = glm::vec4(cfg.spawnPosition, cfg.spawnRadius);
    sys.spawnVel         = glm::vec4(cfg.spawnVelocity, cfg.velocitySpread);
    sys.gravityAndDrag   = glm::vec4(cfg.gravity,       cfg.drag);
    sys.colors[0]        = cfg.colors[0];
    sys.colors[1]        = cfg.colors[1];
    sys.colors[2]        = cfg.colors[2];
    sys.colors[3]        = cfg.colors[3];
    sys.colorPositions   = cfg.colorPositions;
    sys.sizeStartMin     = cfg.sizeStartMin;
    sys.sizeStartMax     = cfg.sizeStartMax;
    sys.sizeEndMin       = cfg.sizeEndMin;
    sys.sizeEndMax       = cfg.sizeEndMax;
    sys.sizeStartBias    = cfg.sizeStartBias;
    sys.sizeEndBias      = cfg.sizeEndBias;
    sys.lifetimeMin      = cfg.lifetimeMin;
    sys.lifetimeMax      = cfg.lifetimeMax;
    sys.lifetimeBias     = cfg.lifetimeBias;
    // Store emitRate scaled by particle count so the per-particle timer
    // interval (1/storedRate = N/emitRate) gives emitRate spawns/second system-wide.
    sys.emitRate         = (cfg.maxParticles > 0)
                               ? (cfg.emitRate / static_cast<float>(cfg.maxParticles))
                               : cfg.emitRate;
    sys.flags            = 0; // not emitting yet
    sys.shapeType        = cfg.texture
                               ? static_cast<uint32_t>(ParticleShape::Textured)
                               : static_cast<uint32_t>(cfg.shape);
    s_systemTextures[handle.systemIndex]  = cfg.texture;
    s_systemSamplers[handle.systemIndex]  = cfg.sampler;
    s_systemPixelMode[handle.systemIndex] = cfg.pixelMode;
    s_systemDirty        = true;

    // Initialise particle slots: staggered respawn timers so emission is smooth
    // from frame 1. All particles start dead with a timer = index / emitRate.
    {
        const uint32_t n = cfg.maxParticles;
        std::vector<GPUParticle> init(n);
        for (uint32_t i = 0; i < n; ++i) {
            init[i].posAndLife    = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // dead
            init[i].velAndMaxLife = glm::vec4(0.0f, 0.0f, 0.0f, cfg.lifetimeMax);
            init[i].systemID      = handle.systemIndex;
            // Stagger as a normalised fraction [0, 1) so particles are uniformly
            // spread across one respawn period from the very first frame.
            init[i].respawnTimer  = (n > 1) ? (static_cast<float>(i) / static_cast<float>(n)) : 0.0f;
            init[i].startSize     = cfg.sizeStartMin;
            init[i].endSize       = cfg.sizeEndMin;
        }

        SDL_GPUDevice* device = Renderer::GetDevice();

        // Upload initial particle data via a dedicated transfer buffer
        SDL_GPUTransferBufferCreateInfo tbInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = static_cast<uint32_t>(n * sizeof(GPUParticle)),
            .props = 0
        };
        SDL_GPUTransferBuffer* ptb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
        void* pmapped = SDL_MapGPUTransferBuffer(device, ptb, false);
        SDL_memcpy(pmapped, init.data(), n * sizeof(GPUParticle));
        SDL_UnmapGPUTransferBuffer(device, ptb);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTransferBufferLocation src = {.transfer_buffer = ptb, .offset = 0};
        SDL_GPUBufferRegion dst = {
            .buffer = s_particleBuf,
            .offset = static_cast<uint32_t>(handle.particleOffset * sizeof(GPUParticle)),
            .size   = static_cast<uint32_t>(n * sizeof(GPUParticle))
        };
        SDL_UploadToGPUBuffer(cp, &src, &dst, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_WaitForGPUIdle(device);
        SDL_ReleaseGPUTransferBuffer(device, ptb);
    }

    LOG_INFO("Particles: created system {} ({} particles, offset {})",
             handle.systemIndex, handle.maxParticles, handle.particleOffset);
    return handle;
}

void DestroySystem(ParticleSystemHandle& handle) {
    if (!handle.valid) return;

    uint32_t idx = handle.systemIndex;
    s_systemUsed[idx]          = false;
    s_systemData[idx]          = {};
    s_systemTextures[idx]      = nullptr;
    s_systemSamplers[idx]      = nullptr;
    s_systemPixelMode[idx]     = false;
    s_systemCustomCompute[idx] = INVALID_CUSTOM_COMPUTE;
    s_systemDirty              = true;

    // Reclaim particle slots: walk the bump pointer back as far as possible.
    // This fully handles the common cases (destroy last, destroy all).
    // For out-of-order destruction the freed slots remain unused until
    // all systems above them are also destroyed.
    if (s_slotParticleOffset[idx] + s_slotParticleCount[idx] == s_nextParticleSlot) {
        s_nextParticleSlot = s_slotParticleOffset[idx];
        // Continue walking back over any other freed slots that are now at the top
        for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
            if (!s_systemUsed[i] && s_slotParticleOffset[i] + s_slotParticleCount[i] == s_nextParticleSlot) {
                s_nextParticleSlot = s_slotParticleOffset[i];
            }
        }
    }
    s_slotParticleOffset[idx] = 0;
    s_slotParticleCount [idx] = 0;

    handle = {};
}

void Start(const ParticleSystemHandle& handle) {
    if (!handle.valid) return;
    s_systemData[handle.systemIndex].flags |= 1u;
    s_systemDirty = true;
}

void Stop(const ParticleSystemHandle& handle) {
    if (!handle.valid) return;
    s_systemData[handle.systemIndex].flags &= ~1u;
    s_systemDirty = true;
}

void SetPosition(const ParticleSystemHandle& handle, glm::vec3 worldPos) {
    if (!handle.valid) return;
    s_systemData[handle.systemIndex].spawnPos.x = worldPos.x;
    s_systemData[handle.systemIndex].spawnPos.y = worldPos.y;
    s_systemData[handle.systemIndex].spawnPos.z = worldPos.z;
    s_systemDirty = true;
}

void UpdateConfig(const ParticleSystemHandle& handle, const ParticleSystemConfig& cfg) {
    if (!handle.valid) return;

    GPUParticleSystem& sys   = s_systemData[handle.systemIndex];
    uint32_t preservedFlags  = sys.flags;           // keep Start/Stop state
    glm::vec3 preservedPos   = glm::vec3(sys.spawnPos); // keep SetPosition value

    sys.spawnPos         = glm::vec4(preservedPos,        cfg.spawnRadius);
    sys.spawnVel         = glm::vec4(cfg.spawnVelocity,   cfg.velocitySpread);
    sys.gravityAndDrag   = glm::vec4(cfg.gravity,         cfg.drag);
    sys.colors[0]        = cfg.colors[0];
    sys.colors[1]        = cfg.colors[1];
    sys.colors[2]        = cfg.colors[2];
    sys.colors[3]        = cfg.colors[3];
    sys.colorPositions   = cfg.colorPositions;
    sys.sizeStartMin     = cfg.sizeStartMin;
    sys.sizeStartMax     = cfg.sizeStartMax;
    sys.sizeEndMin       = cfg.sizeEndMin;
    sys.sizeEndMax       = cfg.sizeEndMax;
    sys.sizeStartBias    = cfg.sizeStartBias;
    sys.sizeEndBias      = cfg.sizeEndBias;
    sys.lifetimeMin      = cfg.lifetimeMin;
    sys.lifetimeMax      = cfg.lifetimeMax;
    sys.lifetimeBias     = cfg.lifetimeBias;
    sys.emitRate         = (handle.maxParticles > 0)
                               ? (cfg.emitRate / static_cast<float>(handle.maxParticles))
                               : cfg.emitRate;
    sys.flags            = preservedFlags;
    sys.shapeType        = cfg.texture
                               ? static_cast<uint32_t>(ParticleShape::Textured)
                               : static_cast<uint32_t>(cfg.shape);
    s_systemTextures[handle.systemIndex]  = cfg.texture;
    s_systemSamplers[handle.systemIndex]  = cfg.sampler;
    s_systemPixelMode[handle.systemIndex] = cfg.pixelMode;

    s_systemDirty = true;
}

void SetTexture(const ParticleSystemHandle& handle,
                SDL_GPUTexture* texture, SDL_GPUSampler* sampler) {
    if (!handle.valid) return;
    s_systemTextures[handle.systemIndex] = texture;
    s_systemSamplers[handle.systemIndex] = sampler;
    s_systemData[handle.systemIndex].shapeType = texture
        ? static_cast<uint32_t>(ParticleShape::Textured)
        : static_cast<uint32_t>(ParticleShape::SoftCircle); // revert to default shape
    s_systemDirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Custom compute
// ─────────────────────────────────────────────────────────────────────────────

ParticleComputeHandle CreateCustomCompute(const std::string& shaderPath) {
    // Find a free pool slot
    for (uint32_t i = 0; i < MAX_CUSTOM_COMPUTES; ++i) {
        if (s_customComputeUsed[i]) continue;

        ComputePipelineAsset pipeline = Shaders::CreateComputePipeline(
            Renderer::GetDevice(), shaderPath);
        if (!pipeline.pipeline) {
            LOG_ERROR("Particles: failed to create custom compute from '{}'", shaderPath);
            return {};
        }

        s_customComputePool[i] = pipeline;
        s_customComputeUsed[i] = true;
        LOG_INFO("Particles: created custom compute [{}] from '{}'", i, shaderPath);
        return { i, true };
    }

    LOG_ERROR("Particles: no free custom compute slots (MAX_CUSTOM_COMPUTES = {})",
              MAX_CUSTOM_COMPUTES);
    return {};
}

void DestroyCustomCompute(ParticleComputeHandle& handle) {
    if (!handle.valid) return;

    // Clear from any system currently using it
    for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
        if (s_systemCustomCompute[i] == handle.index) {
            s_systemCustomCompute[i]  = INVALID_CUSTOM_COMPUTE;
            s_systemData[i].flags    &= ~2u; // clear custom-compute bit
            s_systemDirty             = true;
        }
    }

    SDL_ReleaseGPUComputePipeline(Renderer::GetDevice(),
                                   s_customComputePool[handle.index].pipeline);
    s_customComputePool[handle.index] = {};
    s_customComputeUsed[handle.index] = false;
    handle = {};
}

void SetCustomCompute(const ParticleSystemHandle& system,
                      const ParticleComputeHandle& compute) {
    if (!system.valid || !compute.valid) return;
    s_systemCustomCompute[system.systemIndex] = compute.index;
    s_systemData[system.systemIndex].flags   |= 2u; // skip built-in update
    s_systemDirty = true;
}

void ClearCustomCompute(const ParticleSystemHandle& system) {
    if (!system.valid) return;
    s_systemCustomCompute[system.systemIndex]  = INVALID_CUSTOM_COMPUTE;
    s_systemData[system.systemIndex].flags    &= ~2u;
    s_systemDirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────

void Update(float deltaTime) {
    if (!s_computePipeline.pipeline) return;

    s_pendingDt    = deltaTime;
    s_accumTime   += deltaTime;
    s_updateQueued = true;

    // Count the total live particle range
    uint32_t total = s_nextParticleSlot;
    if (total == 0) return;

    // Uniform block
    struct ComputeUniforms {
        uint32_t totalParticles;
        float    deltaTime;
        float    time;
        uint32_t _pad;
    } uniforms = {total, deltaTime, s_accumTime, 0u};

    Compute::SetPipeline(s_computePipeline);
    Compute::BindReadBuffer(0, s_systemBuf);
    Compute::BindReadWriteBuffer(0, s_particleBuf);
    Compute::PushUniform(0, uniforms);
    Compute::DispatchAuto(total);  // built-in: skips systems with custom compute flag

    // Dispatch custom compute pipelines (one per system that has one assigned)
    struct CustomUniforms {
        uint32_t particleOffset;
        uint32_t particleCount;
        float    deltaTime;
        float    time;
    };
    for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
        if (!s_systemUsed[i]) continue;
        uint32_t ci = s_systemCustomCompute[i];
        if (ci == INVALID_CUSTOM_COMPUTE) continue;

        CustomUniforms cu = {
            s_slotParticleOffset[i],
            s_slotParticleCount[i],
            deltaTime,
            s_accumTime
        };
        Compute::SetPipeline(s_customComputePool[ci]);
        Compute::BindReadBuffer(0, s_systemBuf);
        Compute::BindReadWriteBuffer(0, s_particleBuf);
        Compute::PushUniform(0, cu);
        Compute::DispatchAuto(s_slotParticleCount[i]);
    }
}

void QueueDraw(const ParticleSystemHandle& handle) {
    if (!handle.valid || !s_renderPass) return;
    s_renderPass->addDraw({
        handle.particleOffset,
        handle.maxParticles,
        s_systemTextures[handle.systemIndex],
        s_systemSamplers[handle.systemIndex],
        s_systemPixelMode[handle.systemIndex],
    });
}

SDL_GPUBuffer*      GetParticleBuffer() { return s_particleBuf; }
SDL_GPUBuffer*      GetSystemBuffer()   { return s_systemBuf; }
ParticleRenderPass* GetRenderPass()     { return s_renderPass; }
SDL_GPUTexture*     GetWhiteTexture()   { return s_whiteTexture; }
SDL_GPUSampler*     GetLinearSampler()  { return s_linearSampler; }

void AttachToFramebuffer(const std::string& fbName) {
    FrameBuffer* fb = Renderer::GetFramebuffer(fbName);
    if (!fb) {
        LOG_ERROR("Particles::AttachToFramebuffer: framebuffer '{}' not found", fbName);
        return;
    }
    SDL_GPUTextureFormat fmt = SDL_GetGPUSwapchainTextureFormat(
        Renderer::GetDevice(), Window::GetWindow());
    // Pass the raw framebuffer dimensions. The render pass uses these together with
    // the live window size to build a corrected camera at draw time (see render()).
    s_renderPass->init(fmt, fb->width, fb->height, "particles");
    Renderer::AttachRenderPassToFrameBuffer(s_renderPass, "particles", fbName);
}

// Called by Renderer::_endFrame() BEFORE Compute::_ExecuteQueued()
void _PrepareFrame(SDL_GPUCommandBuffer* cmdBuf) {
    if (!s_systemDirty) return;
    s_systemDirty = false;

    // Upload system data via a copy pass on the main command buffer
    void* mapped = SDL_MapGPUTransferBuffer(Renderer::GetDevice(), s_systemUploadBuf, false);
    SDL_memcpy(mapped, s_systemData, MAX_SYSTEMS * sizeof(GPUParticleSystem));
    SDL_UnmapGPUTransferBuffer(Renderer::GetDevice(), s_systemUploadBuf);

    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmdBuf);
    SDL_GPUTransferBufferLocation src = {.transfer_buffer = s_systemUploadBuf, .offset = 0};
    SDL_GPUBufferRegion dst = {
        .buffer = s_systemBuf,
        .offset = 0,
        .size   = static_cast<uint32_t>(MAX_SYSTEMS * sizeof(GPUParticleSystem))
    };
    SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
}

} // namespace Particles


// ─────────────────────────────────────────────────────────────────────────────
// ParticleRenderPass — implementation
// ─────────────────────────────────────────────────────────────────────────────

ParticleRenderPass::ParticleRenderPass(SDL_GPUDevice* device)
    : RenderPass(device)
{}

bool ParticleRenderPass::init(SDL_GPUTextureFormat swapchainFormat,
                               uint32_t width, uint32_t height,
                               std::string name, bool logInit,
                               size_t /*capacity*/, bool /*forceNoMSAA*/) {
    passname         = std::move(name);
    m_format         = swapchainFormat;
    m_surfaceWidth   = width;
    m_surfaceHeight  = height;

    // Load shaders from embedded engine bytes
    #if defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        const char* entryPoint = "main0";
    #else
        const char* entryPoint = "main";
    #endif

    SDL_GPUShaderFormat shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
    #if defined(LUMINOVEAU_SHADER_BACKEND_DXIL)
        shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
    #elif defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        shaderFormat = SDL_GPU_SHADERFORMAT_METALLIB;
    #endif

    SDL_GPUShaderCreateInfo vertInfo = {
        .code_size         = Luminoveau::Shaders::Particles_Vert_Size,
        .code              = Luminoveau::Shaders::Particles_Vert,
        .entrypoint        = entryPoint,
        .format            = shaderFormat,
        .stage             = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers      = 0,
        .num_storage_textures = 0,
        .num_storage_buffers  = 2,  // particles + systems
        .num_uniform_buffers  = 1,  // VertUniforms
    };
    m_vertShader = SDL_CreateGPUShader(m_gpu_device, &vertInfo);

    SDL_GPUShaderCreateInfo fragInfo = {
        .code_size         = Luminoveau::Shaders::Particles_Frag_Size,
        .code              = Luminoveau::Shaders::Particles_Frag,
        .entrypoint        = entryPoint,
        .format            = shaderFormat,
        .stage             = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers      = 1,  // gTexture + gSampler (white pixel when no texture set)
        .num_storage_textures = 0,
        .num_storage_buffers  = 0,
        .num_uniform_buffers  = 0,
    };
    m_fragShader = SDL_CreateGPUShader(m_gpu_device, &fragInfo);

    if (!m_vertShader || !m_fragShader) {
        LOG_ERROR("ParticleRenderPass: shader creation failed");
        return false;
    }

    // Additive blending — particles glow
    SDL_GPUColorTargetBlendState additiveBlend = {
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,           // additive
        .color_blend_op        = SDL_GPU_BLENDOP_ADD,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .alpha_blend_op        = SDL_GPU_BLENDOP_ADD,
        .enable_blend          = true,
    };

    // Standard alpha blending — pixel mode (overwrites cleanly at alpha = 1)
    SDL_GPUColorTargetBlendState standardBlend = {
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .color_blend_op        = SDL_GPU_BLENDOP_ADD,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
        .alpha_blend_op        = SDL_GPU_BLENDOP_ADD,
        .enable_blend          = true,
    };

    SDL_GPUColorTargetDescription additiveTargetDesc = {
        .format      = swapchainFormat,
        .blend_state = additiveBlend,
    };
    SDL_GPUColorTargetDescription standardTargetDesc = {
        .format      = swapchainFormat,
        .blend_state = standardBlend,
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
        .vertex_shader   = m_vertShader,
        .fragment_shader = m_fragShader,
        // No vertex buffers — geometry generated entirely in the vertex shader
        .vertex_input_state = {
            .vertex_buffer_descriptions = nullptr,
            .num_vertex_buffers         = 0,
            .vertex_attributes          = nullptr,
            .num_vertex_attributes      = 0,
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = GPUstructs::defaultRasterizerState,
        .multisample_state = {
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .sample_mask  = 0,
            .enable_mask  = false,
        },
        .depth_stencil_state = {
            .enable_depth_test   = false,
            .enable_depth_write  = false,
            .enable_stencil_test = false,
        },
        .target_info = {
            .color_target_descriptions = &additiveTargetDesc,
            .num_color_targets         = 1,
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_INVALID,
            .has_depth_stencil_target  = false,
        },
        .props = 0,
    };

    m_pipeline = SDL_CreateGPUGraphicsPipeline(m_gpu_device, &pipelineInfo);
    if (!m_pipeline) {
        LOG_ERROR("ParticleRenderPass: failed to create pipeline: {}", SDL_GetError());
        return false;
    }

    // Pixel pipeline — identical but with standard alpha blend
    pipelineInfo.target_info.color_target_descriptions = &standardTargetDesc;
    m_pixelPipeline = SDL_CreateGPUGraphicsPipeline(m_gpu_device, &pipelineInfo);
    if (!m_pixelPipeline) {
        LOG_ERROR("ParticleRenderPass: failed to create pixel pipeline: {}", SDL_GetError());
        return false;
    }

    if (logInit) LOG_INFO("ParticleRenderPass '{}' initialized", passname);
    return true;
}

void ParticleRenderPass::release(bool logRelease) {
    if (m_pipeline)      { SDL_ReleaseGPUGraphicsPipeline(m_gpu_device, m_pipeline);      m_pipeline      = nullptr; }
    if (m_pixelPipeline) { SDL_ReleaseGPUGraphicsPipeline(m_gpu_device, m_pixelPipeline); m_pixelPipeline = nullptr; }
    if (m_vertShader)    { SDL_ReleaseGPUShader(m_gpu_device, m_vertShader);               m_vertShader    = nullptr; }
    if (m_fragShader)    { SDL_ReleaseGPUShader(m_gpu_device, m_fragShader);               m_fragShader    = nullptr; }
    if (logRelease)      LOG_INFO("ParticleRenderPass '{}' released", passname);
}

void ParticleRenderPass::render(SDL_GPUCommandBuffer* cmdBuf,
                                SDL_GPUTexture*       target,
                                const glm::mat4&      camera) {
    if (m_drawQueue.empty()) return;

    SDL_GPUColorTargetInfo colorInfo = {
        .texture  = target,
        .load_op  = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
    };

    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmdBuf, &colorInfo, 1, nullptr);

    // Bind particle + system buffers to vertex storage slots 0 and 1
    SDL_GPUBuffer* storageBufs[2] = {
        Particles::GetParticleBuffer(),
        Particles::GetSystemBuffer()
    };
    SDL_BindGPUVertexStorageBuffers(rp, 0, storageBufs, 2);

    // The framebuffer (m_surfaceWidth × m_surfaceHeight) may be larger than the
    // physical window (e.g. windowed mode on a large monitor, or HiDPI).
    // Without a viewport, clip (-1,1) covers the full framebuffer, but the
    // compositor only shows the top-left physW × physH region.
    //
    // We build a corrected orthographic camera whose world-space "right" equals
    //   fbW × logW / physW
    // so that a world position at (logW, logH) lands in the framebuffer at
    // (physW, physH) — exactly the visible corner — regardless of HiDPI factor
    // or window-vs-desktop size difference.  The same value is used as screenSize
    // so billboard pixel sizes stay correct in logical pixels.
    float camW = (float)m_surfaceWidth  * (float)Window::GetWidth()  / (float)Window::GetPhysicalWidth();
    float camH = (float)m_surfaceHeight * (float)Window::GetHeight() / (float)Window::GetPhysicalHeight();
    glm::mat4 correctedCamera = glm::ortho(0.0f, camW, camH, 0.0f);

    struct VertUniforms {
        glm::mat4 camera;
        glm::vec2 screenSize;
        glm::vec2 pad;
    } vu = { correctedCamera, {camW, camH}, {} };
    SDL_PushGPUVertexUniformData(cmdBuf, 0, &vu, sizeof(vu));

    SDL_GPUGraphicsPipeline* activePipeline = nullptr;

    for (const auto& cmd : m_drawQueue) {
        // Switch pipeline only when the blend mode changes
        SDL_GPUGraphicsPipeline* needed = cmd.pixelMode ? m_pixelPipeline : m_pipeline;
        if (needed != activePipeline) {
            SDL_BindGPUGraphicsPipeline(rp, needed);
            activePipeline = needed;
        }

        // Bind texture + sampler (white pixel fallback when no texture is set)
        SDL_GPUTextureSamplerBinding samplerBinding = {
            .texture = cmd.texture ? cmd.texture : Particles::GetWhiteTexture(),
            .sampler = cmd.sampler ? cmd.sampler : Particles::GetLinearSampler(),
        };
        SDL_BindGPUFragmentSamplers(rp, 0, &samplerBinding, 1);

        // 6 vertices per quad; first_instance = particleOffset offsets gl_InstanceIndex
        SDL_DrawGPUPrimitives(rp, 6, cmd.maxParticles, 0, cmd.particleOffset);
    }

    SDL_EndGPURenderPass(rp);
}
