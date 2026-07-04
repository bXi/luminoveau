#include "particles.h"
#include "gpu/IGpu.h"
#include "gpu/presets.h"

#include <cstring>
#include <algorithm>
#include <sstream>

#include "renderer/renderer.h"
#include "renderer/shaders.h"
#include "renderer/compute.h"
#include "draw/particles_builtin.h"
#include "platform/window/window.h"
#include "core/log/log.h"
#include "assets/compute/computepipeline.h"
#include "gpu/buffer/uniformobject.h"
#include "scene/camera.h"
#include "assets/shaders_generated.h"

#include <glm/gtc/type_ptr.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Preset import helpers (file-scope, not part of public API)
// ─────────────────────────────────────────────────────────────────────────────

static std::string B64Dec(const char* src) {
    static constexpr signed char kLut[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    std::string out;
    int val = 0, bits = -8;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(src); *p; ++p) {
        signed char c = kLut[*p];
        if (c < 0) continue;
        val = (val << 6) | c;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// Deserialises a preset string produced by the particle editor into cfg.
// maxParticles and spawnPosition are not stored in the preset — set them
// before or after calling this. Returns false on malformed input.
static bool ImportPreset(const char* encoded, ParticleSystemConfig& cfg) {
    std::string raw = B64Dec(encoded);
    if (raw.size() < 3 || raw.substr(0, 3) != "v1:") return false;

    std::vector<float> v;
    std::istringstream ss(raw.substr(3));
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        try { v.push_back(std::stof(tok)); } catch (...) { return false; }
    }

    // 38 base fields + 3 optional angular velocity fields (added in v1.1)
    if (v.size() < 38) return false;

    int i = 0;
    cfg.emitRate          = v[i++];
    cfg.spawnRadius       = v[i++];
    cfg.spawnVelocity.x   = v[i++];
    cfg.spawnVelocity.y   = v[i++];
    cfg.velocitySpread    = v[i++];
    cfg.gravity.x         = v[i++];
    cfg.gravity.y         = v[i++];
    cfg.drag              = v[i++];
    cfg.lifetimeMin       = v[i++];
    cfg.lifetimeMax       = v[i++];
    cfg.lifetimeBias      = v[i++];
    cfg.sizeStartMin      = v[i++];
    cfg.sizeStartMax      = v[i++];
    cfg.sizeStartBias     = v[i++];
    cfg.sizeEndMin        = v[i++];
    cfg.sizeEndMax        = v[i++];
    cfg.sizeEndBias       = v[i++];
    cfg.shape             = static_cast<ParticleShape>(static_cast<int>(v[i++]));
    for (int c = 0; c < 4; ++c)
        for (int j = 0; j < 4; ++j)
            cfg.colors[c][j] = v[i++];
    for (int c = 0; c < 4; ++c)
        cfg.colorPositions[c] = v[i++];

    // Angular velocity (optional — older presets default to no spin)
    cfg.angVelMin    = (v.size() > 38) ? v[i++] : 0.0f;
    cfg.angVelMax    = (v.size() > 39) ? v[i++] : 0.0f;
    cfg.angVelBias   = (v.size() > 40) ? v[i++] : 1.0f;
    cfg.trailStretch = (v.size() > 41) ? v[i++] : 0.0f;

    cfg.texture = 0;
    cfg.sampler = 0;

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Particles namespace — implementation
// ─────────────────────────────────────────────────────────────────────────────

// Particle system state and implementation methods are members of the Particles
// singleton (declared in particles.h); the bodies below reference that instance
// state (s_*) directly. Free helpers B64Dec/ImportPreset above stay file-local.

void Particles::_init() {
    IGpu& gpu = Renderer::GetGpu();

    // Particle buffer: compute RW + vertex read
    s_particleBuf = gpu.createBuffer({
        static_cast<uint32_t>(MAX_PARTICLES * sizeof(GPUParticle)),
        GpuBufferUsage::StorageRead | GpuBufferUsage::StorageWrite
    });
    if (!s_particleBuf) LOG_CRITICAL("Particles: failed to create particle buffer");

    // System data buffer: compute RO + vertex read
    s_systemBuf = gpu.createBuffer({
        static_cast<uint32_t>(MAX_SYSTEMS * sizeof(GPUParticleSystem)),
        GpuBufferUsage::StorageRead
    });
    if (!s_systemBuf) LOG_CRITICAL("Particles: failed to create system buffer");

    // Transfer buffer for system data uploads
    s_systemUploadBuf = gpu.createTransferBuffer({
        static_cast<uint32_t>(MAX_SYSTEMS * sizeof(GPUParticleSystem)),
        GpuTransferUsage::Upload
    });
    if (!s_systemUploadBuf) LOG_CRITICAL("Particles: failed to create system upload buffer");

    // Collider buffer: compute RO
    s_colliderBuf = gpu.createBuffer({
        static_cast<uint32_t>(MAX_COLLIDERS * sizeof(GPUCollider)),
        GpuBufferUsage::StorageRead
    });
    if (!s_colliderBuf) LOG_CRITICAL("Particles: failed to create collider buffer");

    s_colliderUploadBuf = gpu.createTransferBuffer({
        static_cast<uint32_t>(MAX_COLLIDERS * sizeof(GPUCollider)),
        GpuTransferUsage::Upload
    });
    if (!s_colliderUploadBuf) LOG_CRITICAL("Particles: failed to create collider upload buffer");

    // Zero-init collider and particle buffers so all slots start disabled/dead.
    // (WebGPU spec already zero-inits new buffers; redundant there but cheap.)
    {
        uint32_t sz = static_cast<uint32_t>(MAX_COLLIDERS * sizeof(GPUCollider));
        GpuTransferBufferHandle zeroTB = gpu.createTransferBuffer({ sz, GpuTransferUsage::Upload });
        void* zm = gpu.mapTransferBuffer(zeroTB, false);
        std::memset(zm, 0, sz);
        gpu.unmapTransferBuffer(zeroTB);
        GpuCmdBufferHandle cmd = gpu.acquireCommandBuffer();
        gpu.uploadToBuffer(cmd, zeroTB, 0, s_colliderBuf, 0, sz);
        gpu.submitCommandBuffer(cmd);
        gpu.waitIdle();
        gpu.releaseTransferBuffer(zeroTB);
    }

    {
        uint32_t sz = static_cast<uint32_t>(MAX_PARTICLES * sizeof(GPUParticle));
        GpuTransferBufferHandle zeroTB = gpu.createTransferBuffer({ sz, GpuTransferUsage::Upload });
        void* mapped = gpu.mapTransferBuffer(zeroTB, false);
        std::memset(mapped, 0, sz);
        gpu.unmapTransferBuffer(zeroTB);

        GpuCmdBufferHandle cmd = gpu.acquireCommandBuffer();
        gpu.uploadToBuffer(cmd, zeroTB, 0, s_particleBuf, 0, sz);
        gpu.submitCommandBuffer(cmd);
        gpu.waitIdle();
        gpu.releaseTransferBuffer(zeroTB);
    }

    // Create 1×1 white fallback texture for non-textured draws
    {
        s_whiteTexture = gpu.createTexture({
            1, 1, 1, 1, GpuTextureFormat::R8G8B8A8_Unorm,
            GpuSampleCount::x1, GpuTextureUsage::Sampler | GpuTextureUsage::Transfer
        });
        uint32_t whitePixel = 0xFFFFFFFFu;
        GpuTransferBufferHandle wtb = gpu.createTransferBuffer({ sizeof(whitePixel), GpuTransferUsage::Upload });
        void* wm = gpu.mapTransferBuffer(wtb, false);
        std::memcpy(wm, &whitePixel, sizeof(whitePixel));
        gpu.unmapTransferBuffer(wtb);
        GpuCmdBufferHandle wcmd = gpu.acquireCommandBuffer();
        GpuTransferBufferRegion wsrc{ wtb, 0, sizeof(whitePixel) };
        GpuTextureRegion wdst{ s_whiteTexture, 0, 0, 0, 0, 0, 1, 1, 1 };
        gpu.uploadToTexture(wcmd, wsrc, wdst, false);
        gpu.submitCommandBuffer(wcmd);
        gpu.waitIdle();
        gpu.releaseTransferBuffer(wtb);
    }

    // Linear sampler for textured particles
    s_linearSampler = gpu.createSampler(GpuPresets::LinearClamp);

    // Built-in physics compute pipeline — per-backend builder (SDL: precompiled SPIRV,
    // WebGPU: embedded WGSL string).
    s_computePipeline.pipeline      = ParticlesBuiltin::CreateComputePipeline();
    s_computePipeline.threadcount_x = 64;
    s_computePipeline.threadcount_y = 1;
    s_computePipeline.threadcount_z = 1;

    // Create and attach the render pass to the primary framebuffer
    s_renderPass = new ParticleRenderPass();
    _attachToFramebuffer("primaryFramebuffer");

    std::fill(std::begin(s_systemCustomCompute),  std::end(s_systemCustomCompute),  INVALID_CUSTOM_COMPUTE);
    std::fill(std::begin(s_systemPhysicsCompute), std::end(s_systemPhysicsCompute), INVALID_CUSTOM_COMPUTE);
    std::fill(std::begin(s_systemSpringCompute),  std::end(s_systemSpringCompute),  INVALID_CUSTOM_COMPUTE);

    LOG_INFO("Particles: initialized (max particles: {}, max systems: {})",
             MAX_PARTICLES, MAX_SYSTEMS);
}

void Particles::_quit() {
    IGpu& gpu = Renderer::GetGpu();

    // Discard any compute dispatches queued this frame before releasing GPU resources.
    Compute::_Reset();

    gpu.waitIdle();

    if (s_renderPass) {
        Renderer::RemoveShaderPass("particles");
        s_renderPass = nullptr;
    }

    if (s_computePipeline.pipeline) {
        gpu.releaseComputePipeline(s_computePipeline.pipeline);
        s_computePipeline = {};
    }

    for (uint32_t i = 0; i < MAX_CUSTOM_COMPUTES; ++i) {
        if (s_customComputeUsed[i] && s_customComputePool[i].pipeline) {
            gpu.releaseComputePipeline(s_customComputePool[i].pipeline);
            s_customComputePool[i] = {};
            s_customComputeUsed[i] = false;
        }
    }

    if (s_particleBuf)      { gpu.releaseBuffer(s_particleBuf);              s_particleBuf       = 0; }
    if (s_systemBuf)        { gpu.releaseBuffer(s_systemBuf);                s_systemBuf         = 0; }
    if (s_systemUploadBuf)  { gpu.releaseTransferBuffer(s_systemUploadBuf);  s_systemUploadBuf   = 0; }
    if (s_colliderBuf)      { gpu.releaseBuffer(s_colliderBuf);              s_colliderBuf       = 0; }
    if (s_colliderUploadBuf){ gpu.releaseTransferBuffer(s_colliderUploadBuf);s_colliderUploadBuf = 0; }
    s_colliderHighWater = 0;
    std::memset(s_colliderData, 0, sizeof(s_colliderData));
    std::memset(s_colliderUsed, 0, sizeof(s_colliderUsed));
    if (s_whiteTexture)  { gpu.releaseTexture(s_whiteTexture);  s_whiteTexture  = 0; }
    if (s_linearSampler) { gpu.releaseSampler(s_linearSampler); s_linearSampler = 0; }

    // Reset CPU-side state so Init() can be called again cleanly.
    s_nextParticleSlot = 0;
    s_systemDirty      = false;
    s_accumTime        = 0.0f;
    s_pendingDt        = 0.0f;
    s_updateQueued     = false;
    std::fill(std::begin(s_systemUsed),          std::end(s_systemUsed),          false);
    std::fill(std::begin(s_systemData),          std::end(s_systemData),          GPUParticleSystem{});
    std::fill(std::begin(s_systemTextures),      std::end(s_systemTextures),      GpuTextureHandle{0});
    std::fill(std::begin(s_systemSamplers),      std::end(s_systemSamplers),      GpuSamplerHandle{0});
    std::fill(std::begin(s_systemPixelMode),     std::end(s_systemPixelMode),     false);
    std::fill(std::begin(s_slotParticleOffset),  std::end(s_slotParticleOffset),  0u);
    std::fill(std::begin(s_slotParticleCount),   std::end(s_slotParticleCount),   0u);
    std::fill(std::begin(s_systemCustomCompute),  std::end(s_systemCustomCompute),  INVALID_CUSTOM_COMPUTE);
    std::fill(std::begin(s_systemPhysicsCompute), std::end(s_systemPhysicsCompute), INVALID_CUSTOM_COMPUTE);
    std::fill(std::begin(s_customComputeUsed),    std::end(s_customComputeUsed),    false);
    std::fill(std::begin(s_customComputePool),    std::end(s_customComputePool),    ComputePipelineAsset{});
    std::fill(std::begin(s_systemSpringCompute),  std::end(s_systemSpringCompute),  INVALID_CUSTOM_COMPUTE);
    std::fill(std::begin(s_systemSpringK),       std::end(s_systemSpringK),       0.f);
    std::fill(std::begin(s_systemSpringDamp),    std::end(s_systemSpringDamp),    0.f);
    std::fill(std::begin(s_systemSpringInteract),std::end(s_systemSpringInteract),glm::vec4{});

    LOG_INFO("Particles: shut down");
}

// ─────────────────────────────────────────────────────────────────────────────

uint32_t Particles::_allocateSystemSlot() {
    for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
        if (!s_systemUsed[i]) {
            s_systemUsed[i] = true;
            return i;
        }
    }
    LOG_CRITICAL("Particles: no free system slots (MAX_SYSTEMS = {})", MAX_SYSTEMS);
    return 0;
}

ParticleSystemHandle Particles::_createSystem(const ParticleSystemConfig& cfg) {
    if (s_nextParticleSlot + cfg.maxParticles > MAX_PARTICLES) {
        LOG_ERROR("Particles: not enough particle slots for new system (wanted {}, have {})",
                  cfg.maxParticles, MAX_PARTICLES - s_nextParticleSlot);
        return {};
    }

    ParticleSystemHandle handle;
    handle.systemIndex    = _allocateSystemSlot();
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
    sys.angVelMin        = cfg.angVelMin;
    sys.angVelMax        = cfg.angVelMax;
    sys.angVelBias       = cfg.angVelBias;
    sys.trailStretch     = cfg.trailStretch;
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
            init[i].angle         = 0.0f;
            init[i].angularVelocity = 0.0f;
            init[i]._pad0         = 0.0f;
            init[i]._pad1         = 0.0f;
        }

        IGpu& gpu = Renderer::GetGpu();
        uint32_t uploadSz = static_cast<uint32_t>(n * sizeof(GPUParticle));
        GpuTransferBufferHandle ptb = gpu.createTransferBuffer({ uploadSz, GpuTransferUsage::Upload });
        void* pmapped = gpu.mapTransferBuffer(ptb, false);
        std::memcpy(pmapped, init.data(), uploadSz);
        gpu.unmapTransferBuffer(ptb);
        GpuCmdBufferHandle cmd = gpu.acquireCommandBuffer();
        gpu.uploadToBuffer(cmd, ptb, 0, s_particleBuf,
                           static_cast<uint32_t>(handle.particleOffset * sizeof(GPUParticle)),
                           uploadSz);
        gpu.submitCommandBuffer(cmd);
        gpu.waitIdle();
        gpu.releaseTransferBuffer(ptb);
    }

    LOG_INFO("Particles: created system {} ({} particles, offset {})",
             handle.systemIndex, handle.maxParticles, handle.particleOffset);
    return handle;
}

ParticleSystemHandle Particles::_createSystemFromPreset(const char* encoded, uint32_t maxParticles,
                                             glm::vec3 spawnPosition) {
    ParticleSystemConfig cfg;
    cfg.maxParticles  = maxParticles;
    cfg.spawnPosition = spawnPosition;
    if (!ImportPreset(encoded, cfg)) {
        LOG_ERROR("Particles: CreateSystemFromPreset: malformed preset string");
        return {};
    }
    return _createSystem(cfg);
}

void Particles::_destroySystem(ParticleSystemHandle& handle) {
    if (!handle.valid) return;

    uint32_t idx = handle.systemIndex;
    s_systemUsed[idx]          = false;
    s_systemData[idx]          = {};
    s_systemTextures[idx]      = 0;
    s_systemSamplers[idx]      = 0;
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

void Particles::_start(const ParticleSystemHandle& handle) {
    if (!handle.valid) return;
    s_systemData[handle.systemIndex].flags |= 1u;
    s_systemDirty = true;
}

void Particles::_stop(const ParticleSystemHandle& handle) {
    if (!handle.valid) return;
    s_systemData[handle.systemIndex].flags &= ~1u;
    s_systemDirty = true;
}

void Particles::_setPosition(const ParticleSystemHandle& handle, glm::vec3 worldPos) {
    if (!handle.valid) return;
    s_systemData[handle.systemIndex].spawnPos.x = worldPos.x;
    s_systemData[handle.systemIndex].spawnPos.y = worldPos.y;
    s_systemData[handle.systemIndex].spawnPos.z = worldPos.z;
    s_systemDirty = true;
}

void Particles::_updateConfig(const ParticleSystemHandle& handle, const ParticleSystemConfig& cfg) {
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
    sys.angVelMin        = cfg.angVelMin;
    sys.angVelMax        = cfg.angVelMax;
    sys.angVelBias       = cfg.angVelBias;
    sys.trailStretch     = cfg.trailStretch;
    s_systemTextures[handle.systemIndex]  = cfg.texture;
    s_systemSamplers[handle.systemIndex]  = cfg.sampler;
    s_systemPixelMode[handle.systemIndex] = cfg.pixelMode;

    s_systemDirty = true;
}

ParticleSystemConfig Particles::_getConfig(const ParticleSystemHandle& handle) {
    ParticleSystemConfig cfg;
    if (!handle.valid) return cfg;

    const GPUParticleSystem& sys = s_systemData[handle.systemIndex];

    cfg.maxParticles   = handle.maxParticles;
    cfg.spawnPosition  = glm::vec3(sys.spawnPos);
    cfg.spawnRadius    = sys.spawnPos.w;
    cfg.spawnVelocity  = glm::vec3(sys.spawnVel);
    cfg.velocitySpread = sys.spawnVel.w;
    cfg.gravity        = glm::vec3(sys.gravityAndDrag);
    cfg.drag           = sys.gravityAndDrag.w;
    cfg.colors[0]      = sys.colors[0];
    cfg.colors[1]      = sys.colors[1];
    cfg.colors[2]      = sys.colors[2];
    cfg.colors[3]      = sys.colors[3];
    cfg.colorPositions = sys.colorPositions;
    cfg.sizeStartMin   = sys.sizeStartMin;
    cfg.sizeStartMax   = sys.sizeStartMax;
    cfg.sizeStartBias  = sys.sizeStartBias;
    cfg.sizeEndMin     = sys.sizeEndMin;
    cfg.sizeEndMax     = sys.sizeEndMax;
    cfg.sizeEndBias    = sys.sizeEndBias;
    cfg.lifetimeMin    = sys.lifetimeMin;
    cfg.lifetimeMax    = sys.lifetimeMax;
    cfg.lifetimeBias   = sys.lifetimeBias;
    cfg.emitRate       = (handle.maxParticles > 0)
                             ? sys.emitRate * static_cast<float>(handle.maxParticles)
                             : sys.emitRate;
    cfg.shape          = static_cast<ParticleShape>(sys.shapeType);
    cfg.angVelMin      = sys.angVelMin;
    cfg.angVelMax      = sys.angVelMax;
    cfg.angVelBias     = sys.angVelBias;
    cfg.trailStretch   = sys.trailStretch;
    cfg.pixelMode      = s_systemPixelMode[handle.systemIndex];
    cfg.texture        = s_systemTextures[handle.systemIndex];
    cfg.sampler        = s_systemSamplers[handle.systemIndex];

    return cfg;
}

void Particles::_setTexture(const ParticleSystemHandle& handle,
                GpuTextureHandle texture, GpuSamplerHandle sampler) {
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

ParticleComputeHandle Particles::_createCustomCompute(const std::string& shaderPath) {
    // Find a free pool slot
    for (uint32_t i = 0; i < MAX_CUSTOM_COMPUTES; ++i) {
        if (s_customComputeUsed[i]) continue;

        ComputePipelineAsset pipeline = Renderer::CreateComputePipelineAsset(shaderPath);
        if (!pipeline.pipeline) {
            LOG_WARNING("Particles: failed to create custom compute from '{}' (shader unsupported on this backend?)", shaderPath);
            return {};
        }

        s_customComputePool[i] = pipeline;
        s_customComputeUsed[i] = true;
        LOG_INFO("Particles: created custom compute [{}] from '{}'", i, shaderPath);
        return { i, true };
    }

    LOG_WARNING("Particles: no free custom compute slots (MAX_CUSTOM_COMPUTES = {})",
                MAX_CUSTOM_COMPUTES);
    return {};
}

void Particles::_destroyCustomCompute(ParticleComputeHandle& handle) {
    if (!handle.valid) return;

    // Clear from any system currently using it (primary or spring slot)
    for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
        if (s_systemCustomCompute[i] == handle.index) {
            s_systemCustomCompute[i]  = INVALID_CUSTOM_COMPUTE;
            s_systemData[i].flags    &= ~2u;
            s_systemDirty             = true;
        }
        if (s_systemPhysicsCompute[i] == handle.index)
            s_systemPhysicsCompute[i] = INVALID_CUSTOM_COMPUTE;
        if (s_systemSpringCompute[i] == handle.index)
            s_systemSpringCompute[i]  = INVALID_CUSTOM_COMPUTE;
    }

    Renderer::GetGpu().releaseComputePipeline(s_customComputePool[handle.index].pipeline);
    s_customComputePool[handle.index] = {};
    s_customComputeUsed[handle.index] = false;
    handle = {};
}

void Particles::_setCustomCompute(const ParticleSystemHandle& system,
                      const ParticleComputeHandle& compute) {
    if (!system.valid || !compute.valid) return;
    s_systemCustomCompute[system.systemIndex] = compute.index;
    s_systemData[system.systemIndex].flags   |= 2u; // skip built-in update
    s_systemDirty = true;
}

void Particles::_clearCustomCompute(const ParticleSystemHandle& system) {
    if (!system.valid) return;
    s_systemCustomCompute[system.systemIndex]  = INVALID_CUSTOM_COMPUTE;
    s_systemData[system.systemIndex].flags    &= ~2u;
    s_systemDirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────

// Called by the user once per logical update. On SDL_AppIterate the host may
// call this many times between actual rendered frames (e.g. when waiting on
// the swapchain). Enqueueing compute work here would have it discarded by
// Renderer::Render's no-swapchain path, dropping most of the simulation. So
// we only accumulate dt + time here; the real dispatch happens in
// _PrepareFrame, which the renderer calls once per actual frame.
void Particles::_update(float deltaTime) {
    if (!s_computePipeline.pipeline) return;
    s_pendingDt    += deltaTime;
    s_accumTime    += deltaTime;
    s_updateQueued  = true;
}

void Particles::_buildDispatches() {
    if (!s_computePipeline.pipeline) return;

    float deltaTime = s_pendingDt;
    s_pendingDt = 0.0f;

    // Count the total live particle range
    uint32_t total = s_nextParticleSlot;
    if (total == 0) return;

    // Uniform block
    struct ComputeUniforms {
        uint32_t totalParticles;
        float    deltaTime;
        float    time;
        uint32_t numColliders;
    } uniforms = {total, deltaTime, s_accumTime, s_colliderHighWater};

    Compute::SetPipeline(s_computePipeline);
    Compute::BindReadBuffer(0, s_systemBuf);
    Compute::BindReadBuffer(1, s_colliderBuf);
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

    // Physics pass — user-provided pipeline, runs after custom computes
    {
        struct PhysicsUniforms {
            uint32_t particleOffset;
            uint32_t particleCount;
            float    deltaTime;
            uint32_t numColliders;
            float    gravityX;
            float    gravityY;
            float    drag;
            float    _pad;
        };
        for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
            uint32_t pc = s_systemPhysicsCompute[i];
            if (!s_systemUsed[i] || pc == INVALID_CUSTOM_COMPUTE) continue;
            PhysicsUniforms pu = {
                s_slotParticleOffset[i], s_slotParticleCount[i],
                deltaTime, s_colliderHighWater,
                s_systemPhysicsGravity[i].x, s_systemPhysicsGravity[i].y,
                s_systemPhysicsDrag[i], 0.f
            };
            Compute::SetPipeline(s_customComputePool[pc]);
            Compute::BindReadBuffer(0, s_colliderBuf);
            Compute::BindReadWriteBuffer(0, s_particleBuf);
            Compute::PushUniform(0, pu);
            Compute::DispatchAuto(s_slotParticleCount[i]);
        }
    }

    // Spring pass — elastic deformation after custom computes (e.g. Mandelbrot).
    // Pipeline is user-provided via EnableSpringPass (loaded from assets, not embedded).
    {
        struct SpringUniforms {
            uint32_t particleOffset;
            uint32_t particleCount;
            float    deltaTime;
            float    springK;
            float    damping;
            float    interactX;
            float    interactY;
            float    interactR;
            float    interactF;
            float    _pad0;
            float    _pad1;
            float    _pad2;
        };
        for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
            uint32_t sc = s_systemSpringCompute[i];
            if (!s_systemUsed[i] || sc == INVALID_CUSTOM_COMPUTE) continue;
            const glm::vec4& ia = s_systemSpringInteract[i];
            SpringUniforms su = {
                s_slotParticleOffset[i], s_slotParticleCount[i],
                deltaTime, s_systemSpringK[i],
                s_systemSpringDamp[i],
                ia.x, ia.y, ia.z, ia.w,
                0.f, 0.f, 0.f
            };
            Compute::SetPipeline(s_customComputePool[sc]);
            Compute::BindReadWriteBuffer(0, s_particleBuf);
            Compute::PushUniform(0, su);
            Compute::DispatchAuto(s_slotParticleCount[i]);
        }
    }
}

void Particles::_queueDraw(const ParticleSystemHandle& handle) {
    if (!handle.valid || !s_renderPass) return;
    s_renderPass->addDraw({
        handle.particleOffset,
        handle.maxParticles,
        s_systemTextures[handle.systemIndex],
        s_systemSamplers[handle.systemIndex],
        s_systemPixelMode[handle.systemIndex],
    });
}

GpuBufferHandle     Particles::_getParticleBuffer() { return s_particleBuf; }
GpuBufferHandle     Particles::_getSystemBuffer()   { return s_systemBuf; }
ParticleRenderPass* Particles::_getRenderPass()     { return s_renderPass; }
GpuTextureHandle    Particles::_getWhiteTexture()   { return s_whiteTexture; }
GpuSamplerHandle    Particles::_getLinearSampler()  { return s_linearSampler; }

// ─────────────────────────────────────────────────────────────────────────────
// Physics pass
// ─────────────────────────────────────────────────────────────────────────────

void Particles::_enablePhysicsPass(const ParticleSystemHandle& handle,
                       const ParticleComputeHandle& physicsCompute,
                       glm::vec2 gravity, float drag) {
    if (!handle.valid || !physicsCompute.valid) return;
    s_systemPhysicsCompute[handle.systemIndex] = physicsCompute.index;
    s_systemPhysicsGravity[handle.systemIndex] = gravity;
    s_systemPhysicsDrag   [handle.systemIndex] = drag;
}

void Particles::_disablePhysicsPass(const ParticleSystemHandle& handle) {
    if (!handle.valid) return;
    s_systemPhysicsCompute[handle.systemIndex] = INVALID_CUSTOM_COMPUTE;
}

void Particles::_setPhysicsPassParams(const ParticleSystemHandle& handle,
                          glm::vec2 gravity, float drag) {
    if (!handle.valid) return;
    s_systemPhysicsGravity[handle.systemIndex] = gravity;
    s_systemPhysicsDrag   [handle.systemIndex] = drag;
}

// ─────────────────────────────────────────────────────────────────────────────
// Spring pass
// ─────────────────────────────────────────────────────────────────────────────

void Particles::_enableSpringPass(const ParticleSystemHandle& handle,
                      const ParticleComputeHandle& springCompute,
                      float springK, float damping) {
    if (!handle.valid || !springCompute.valid) return;
    s_systemSpringCompute [handle.systemIndex] = springCompute.index;
    s_systemSpringK       [handle.systemIndex] = springK;
    s_systemSpringDamp    [handle.systemIndex] = damping;
    s_systemSpringInteract[handle.systemIndex] = {};
}

void Particles::_disableSpringPass(const ParticleSystemHandle& handle) {
    if (!handle.valid) return;
    s_systemSpringCompute [handle.systemIndex] = INVALID_CUSTOM_COMPUTE;
    s_systemSpringInteract[handle.systemIndex] = {};
}

void Particles::_setSpringParams(const ParticleSystemHandle& handle,
                     float springK, float damping) {
    if (!handle.valid) return;
    s_systemSpringK   [handle.systemIndex] = springK;
    s_systemSpringDamp[handle.systemIndex] = damping;
}

void Particles::_setSpringInteraction(const ParticleSystemHandle& handle,
                          float x, float y, float r, float f) {
    if (!handle.valid) return;
    s_systemSpringInteract[handle.systemIndex] = {x, y, r, f};
}

// ─────────────────────────────────────────────────────────────────────────────
// Colliders
// ─────────────────────────────────────────────────────────────────────────────

ColliderHandle Particles::_addCollider(ColliderType type, glm::vec4 params,
                           float restitution, float friction) {
    for (uint32_t i = 0; i < MAX_COLLIDERS; ++i) {
        if (s_colliderUsed[i]) continue;
        s_colliderData[i] = {
            .params      = params,
            .restitution = restitution,
            .friction    = friction,
            .type        = static_cast<uint32_t>(type),
            .enabled     = 1u,
        };
        s_colliderUsed[i]   = true;
        s_colliderHighWater = std::max(s_colliderHighWater, i + 1u);
        s_colliderDirty     = true;
        return { i, true };
    }
    LOG_ERROR("Particles: no free collider slots (MAX_COLLIDERS = {})", MAX_COLLIDERS);
    return {};
}

void Particles::_removeCollider(ColliderHandle& handle) {
    if (!handle.valid) return;
    s_colliderData[handle.index].enabled = 0u;
    s_colliderUsed[handle.index]         = false;
    // Recompute high-water mark
    s_colliderHighWater = 0;
    for (uint32_t i = 0; i < MAX_COLLIDERS; ++i)
        if (s_colliderUsed[i]) s_colliderHighWater = i + 1u;
    s_colliderDirty = true;
    handle          = {};
}

void Particles::_clearColliders() {
    for (uint32_t i = 0; i < MAX_COLLIDERS; ++i) {
        s_colliderData[i].enabled = 0u;
        s_colliderUsed[i]         = false;
    }
    s_colliderHighWater = 0;
    s_colliderDirty     = true;
}

void Particles::_updateCollider(const ColliderHandle& handle, glm::vec4 params,
                    float restitution, float friction) {
    if (!handle.valid) return;
    s_colliderData[handle.index].params      = params;
    s_colliderData[handle.index].restitution = restitution;
    s_colliderData[handle.index].friction    = friction;
    s_colliderDirty = true;
}

void  Particles::_setPOV(bool enabled, float decay) { if (s_renderPass) s_renderPass->SetPOV(enabled, decay); }
bool  Particles::_getPOVEnabled()                   { return s_renderPass ? s_renderPass->getPOVEnabled() : false; }
float Particles::_getPOVDecay()                     { return s_renderPass ? s_renderPass->getPOVDecay()   : 0.92f; }

void Particles::_attachToFramebuffer(const std::string& fbName) {
    FrameBuffer* fb = Renderer::GetFramebuffer(fbName);
    if (!fb) {
        LOG_ERROR("Particles::_attachToFramebuffer: framebuffer '{}' not found", fbName);
        return;
    }
    GpuTextureFormat fmt = Renderer::GetGpu().getSwapchainFormat();
    s_renderPass->init(fmt, fb->width, fb->height, "particles");
    Renderer::AttachRenderPassToFrameBuffer(s_renderPass, "particles", fbName);
}

// Called by Renderer::_endFrame() BEFORE Compute::_ExecuteQueued()
void Particles::_prepareFrame(GpuCmdBufferHandle cmdBuf) {
    // Build particle compute dispatches for this frame using accumulated dt.
    // Update() only accumulates; the actual enqueue happens here so it survives
    // even when Update() is called multiple times per rendered frame.
    if (s_updateQueued) {
        s_updateQueued = false;
        _buildDispatches();
    }

    if (!s_systemDirty && !s_colliderDirty) return;

    IGpu& gpu = Renderer::GetGpu();

    if (s_systemDirty) {
        s_systemDirty = false;
        uint32_t sz = static_cast<uint32_t>(MAX_SYSTEMS * sizeof(GPUParticleSystem));
        void* mapped = gpu.mapTransferBuffer(s_systemUploadBuf, false);
        std::memcpy(mapped, s_systemData, sz);
        gpu.unmapTransferBuffer(s_systemUploadBuf);
        gpu.uploadToBuffer(cmdBuf, s_systemUploadBuf, 0, s_systemBuf, 0, sz);
    }

    if (s_colliderDirty) {
        s_colliderDirty = false;
        uint32_t sz = static_cast<uint32_t>(MAX_COLLIDERS * sizeof(GPUCollider));
        void* mapped = gpu.mapTransferBuffer(s_colliderUploadBuf, false);
        std::memcpy(mapped, s_colliderData, sz);
        gpu.unmapTransferBuffer(s_colliderUploadBuf);
        gpu.uploadToBuffer(cmdBuf, s_colliderUploadBuf, 0, s_colliderBuf, 0, sz);
    }
}



// ─────────────────────────────────────────────────────────────────────────────
// ParticleRenderPass — implementation
// ─────────────────────────────────────────────────────────────────────────────

ParticleRenderPass::ParticleRenderPass()
    : RenderPass()
{
}

bool ParticleRenderPass::init(GpuTextureFormat swapchainFormat,
                               uint32_t width, uint32_t height,
                               std::string name, bool logInit,
                               size_t /*capacity*/, bool /*forceNoMSAA*/) {
    passname         = std::move(name);
    m_format         = swapchainFormat;
    m_surfaceWidth   = width;
    m_surfaceHeight  = height;

    IGpu& gpu = Renderer::GetGpu();

    const char* vertEntry = Shaders::GetVertexEntryPoint();
    const char* fragEntry = Shaders::GetFragmentEntryPoint();

    m_vertShader = gpu.createShader({
        Luminoveau::Shaders::Particles_Vert,
        Luminoveau::Shaders::Particles_Vert_Size,
        vertEntry, GpuShaderStage::Vertex,
        0, 1, 2, 0  // samplers=0, uniforms=1, storageBufs=2, storageTex=0
    });
    m_fragShader = gpu.createShader({
        Luminoveau::Shaders::Particles_Frag,
        Luminoveau::Shaders::Particles_Frag_Size,
        fragEntry, GpuShaderStage::Fragment,
        1, 0, 0, 0  // samplers=1
    });

    if (!m_vertShader || !m_fragShader) {
        LOG_ERROR("ParticleRenderPass: shader creation failed");
        return false;
    }

    // Additive blending — particles glow
    GpuColorTargetBlendState additiveBlend = {
        .blendEnabled   = true,
        .srcColorFactor = GpuBlendFactor::SrcAlpha,
        .dstColorFactor = GpuBlendFactor::One,
        .colorOp        = GpuBlendOp::Add,
        .srcAlphaFactor = GpuBlendFactor::One,
        .dstAlphaFactor = GpuBlendFactor::One,
        .alphaOp        = GpuBlendOp::Add,
    };

    // Standard alpha blending — pixel mode
    GpuColorTargetBlendState standardBlend = {
        .blendEnabled   = true,
        .srcColorFactor = GpuBlendFactor::SrcAlpha,
        .dstColorFactor = GpuBlendFactor::OneMinusSrcAlpha,
        .colorOp        = GpuBlendOp::Add,
        .srcAlphaFactor = GpuBlendFactor::One,
        .dstAlphaFactor = GpuBlendFactor::Zero,
        .alphaOp        = GpuBlendOp::Add,
    };

    GpuGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertexShader       = m_vertShader;
    pipelineInfo.fragmentShader     = m_fragShader;
    pipelineInfo.colorTargetFormat  = swapchainFormat;
    pipelineInfo.hasDepthTarget     = false;
    pipelineInfo.vertexStorageBufferCount = 2;
    pipelineInfo.blend              = additiveBlend;
    // Match the framebuffer's MSAA sample count so particles draw into the multisampled target.
    // The pass is re-initialised on sample-count changes, so this recreates correctly.
    // NOTE: the POV accumulation path renders into single-sample POV textures, so POV + MSAA is
    // not supported (would need MSAA POV textures + a resolve before compositing).
    pipelineInfo.sampleCount        = Renderer::GetSampleCount();

    m_pipeline = gpu.createGraphicsPipeline(pipelineInfo);
    if (!m_pipeline) { LOG_ERROR("ParticleRenderPass: failed to create pipeline"); return false; }

    pipelineInfo.blend = standardBlend;
    m_pixelPipeline = gpu.createGraphicsPipeline(pipelineInfo);
    if (!m_pixelPipeline) { LOG_ERROR("ParticleRenderPass: failed to create pixel pipeline"); return false; }

    // ── POV shaders ───────────────────────────────────────────────────────────
    m_povVertShader = gpu.createShader({
        Luminoveau::Shaders::ParticlesPov_Vert,
        Luminoveau::Shaders::ParticlesPov_Vert_Size,
        vertEntry, GpuShaderStage::Vertex,
        0, 1, 0, 0
    });
    m_povFragShader = gpu.createShader({
        Luminoveau::Shaders::ParticlesPov_Frag,
        Luminoveau::Shaders::ParticlesPov_Frag_Size,
        fragEntry, GpuShaderStage::Fragment,
        1, 0, 0, 0
    });

    if (!m_povVertShader || !m_povFragShader) {
        LOG_ERROR("ParticleRenderPass: POV shader creation failed");
        return false;
    }

    // ── POV textures (ping-pong) ───────────────────────────────────────────────
    GpuTextureCreateInfo povTexInfo{};
    povTexInfo.width    = width;
    povTexInfo.height   = height;
    povTexInfo.format   = swapchainFormat;
    povTexInfo.usage    = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
    m_povTex[0] = gpu.createTexture(povTexInfo);
    m_povTex[1] = gpu.createTexture(povTexInfo);

    if (!m_povTex[0] || !m_povTex[1]) {
        LOG_ERROR("ParticleRenderPass: POV texture creation failed");
        return false;
    }

    // Zero-initialize POV textures via clear passes
    {
        GpuCmdBufferHandle initCmd = gpu.acquireCommandBuffer();
        for (int ti = 0; ti < 2; ti++) {
            GpuColorTargetInfo ci{};
            ci.texture    = m_povTex[ti];
            ci.loadOp     = GpuLoadOp::Clear;
            ci.storeOp    = GpuStoreOp::Store;
            ci.clearR = ci.clearG = ci.clearB = ci.clearA = 0.f;
            GpuRenderPassHandle rp = gpu.beginRenderPass(initCmd, &ci, 1, nullptr);
            gpu.endRenderPass(rp);
        }
        gpu.submitCommandBuffer(initCmd);
    }

    // Nearest sampler
    GpuSamplerCreateInfo povSampInfo{};
    povSampInfo.minFilter = GpuFilter::Nearest;
    povSampInfo.magFilter = GpuFilter::Nearest;
    povSampInfo.mipFilter = GpuFilter::Nearest;
    povSampInfo.addressU  = GpuSamplerAddressMode::ClampToEdge;
    povSampInfo.addressV  = GpuSamplerAddressMode::ClampToEdge;
    povSampInfo.addressW  = GpuSamplerAddressMode::ClampToEdge;
    m_povSampler = gpu.createSampler(povSampInfo);

    // ── POV pipelines ─────────────────────────────────────────────────────────
    GpuGraphicsPipelineCreateInfo povBase{};
    povBase.vertexShader      = m_povVertShader;
    povBase.fragmentShader    = m_povFragShader;
    povBase.colorTargetFormat = swapchainFormat;
    povBase.hasDepthTarget    = false;

    // Decay: no blend
    povBase.blend = {};
    m_povDecayPipeline = gpu.createGraphicsPipeline(povBase);

    // Composite: ONE+ONE additive
    GpuColorTargetBlendState povBlend = {
        .blendEnabled   = true,
        .srcColorFactor = GpuBlendFactor::One,
        .dstColorFactor = GpuBlendFactor::One,
        .colorOp        = GpuBlendOp::Add,
        .srcAlphaFactor = GpuBlendFactor::One,
        .dstAlphaFactor = GpuBlendFactor::One,
        .alphaOp        = GpuBlendOp::Add,
    };
    povBase.blend = povBlend;
    m_povCompositePipeline = gpu.createGraphicsPipeline(povBase);

    if (!m_povDecayPipeline || !m_povCompositePipeline) {
        LOG_ERROR("ParticleRenderPass: POV pipeline creation failed");
        return false;
    }

    m_povNeedsClear = true;
    m_povIndex      = 0;

    if (logInit) LOG_INFO("ParticleRenderPass '{}' initialized", passname);
    return true;
}

void ParticleRenderPass::release(bool logRelease) {
    IGpu& gpu = Renderer::GetGpu();
    if (m_pipeline)             { gpu.releaseGraphicsPipeline(m_pipeline);             m_pipeline             = 0; }
    if (m_pixelPipeline)        { gpu.releaseGraphicsPipeline(m_pixelPipeline);        m_pixelPipeline        = 0; }
    if (m_vertShader)           { gpu.releaseShader(m_vertShader);                     m_vertShader           = 0; }
    if (m_fragShader)           { gpu.releaseShader(m_fragShader);                     m_fragShader           = 0; }
    if (m_povDecayPipeline)     { gpu.releaseGraphicsPipeline(m_povDecayPipeline);     m_povDecayPipeline     = 0; }
    if (m_povCompositePipeline) { gpu.releaseGraphicsPipeline(m_povCompositePipeline); m_povCompositePipeline = 0; }
    if (m_povVertShader)        { gpu.releaseShader(m_povVertShader);                  m_povVertShader        = 0; }
    if (m_povFragShader)        { gpu.releaseShader(m_povFragShader);                  m_povFragShader        = 0; }
    if (m_povTex[0])            { gpu.releaseTexture(m_povTex[0]);                     m_povTex[0]            = 0; }
    if (m_povTex[1])            { gpu.releaseTexture(m_povTex[1]);                     m_povTex[1]            = 0; }
    if (m_povSampler)           { gpu.releaseSampler(m_povSampler);                    m_povSampler           = 0; }
    if (logRelease)             LOG_INFO("ParticleRenderPass '{}' released", passname);
}

void ParticleRenderPass::render(GpuCmdBufferHandle cmdBuf,
                                GpuTextureHandle   target,
                                const glm::mat4&   camera) {
    IGpu& gpu = Renderer::GetGpu();

    if (m_drawQueue.empty()) {
        // No particles to draw — but if we're the MSAA resolve owner (typically the last pass on
        // the framebuffer), we must still resolve the multisampled target down to fbContent, or the
        // whole frame stays black under MSAA. A draw-less Load+Resolve pass does exactly that.
        if (renderTargetResolve != 0) {
            GpuColorTargetInfo resolveInfo{};
            resolveInfo.texture        = target;
            resolveInfo.resolveTexture = renderTargetResolve;
            resolveInfo.loadOp         = GpuLoadOp::Load;
            resolveInfo.storeOp        = GpuStoreOp::Resolve;
            GpuRenderPassHandle rp = gpu.beginRenderPass(cmdBuf, &resolveInfo, 1, nullptr);
            gpu.endRenderPass(rp);
        }
        return;
    }

    // Scale surface pixels to window-logical so particles at logical (x, y) line up with sprites
    // at logical (x, y). Works for both backends: on SDL the surface is desktop-sized (so camW
    // = desktop-logical); on WebGPU the surface == window-physical (so the ratio collapses to 1
    // and camW = window-logical, matching Renderer::_updateCameraProjection's CSS-pixel contract).
    float camW = (float)m_surfaceWidth  * (float)Window::GetWidth()  / (float)Window::GetPhysicalWidth();
    float camH = (float)m_surfaceHeight * (float)Window::GetHeight() / (float)Window::GetPhysicalHeight();

    float     camScale  = Camera::GetScale();
    vf2d      camTarget = Camera::GetTarget();
    glm::mat4 correctedCamera = glm::ortho(0.0f, camW, camH, 0.0f);
    correctedCamera = glm::scale(correctedCamera, glm::vec3(camScale, camScale, 1.0f));
    correctedCamera = glm::translate(correctedCamera, glm::vec3(-camTarget.x, -camTarget.y, 0.0f));

    struct VertUniforms {
        glm::mat4 camera;
        glm::vec2 screenSize;
        glm::vec2 pad;
    } vu = { correctedCamera, {camW, camH}, {} };

    struct POVUniforms { float gScale; float _pad[3]; };

    GpuBufferHandle storageBufs[2] = {
        Particles::GetParticleBuffer(),
        Particles::GetSystemBuffer()
    };

    auto drawParticles = [&](GpuRenderPassHandle rp) {
        gpu.pushVertexUniformData(cmdBuf, 0, &vu, sizeof(vu));
        GpuGraphicsPipelineHandle activePipeline = 0;
        for (const auto& cmd : m_drawQueue) {
            GpuGraphicsPipelineHandle needed = cmd.pixelMode ? m_pixelPipeline : m_pipeline;
            if (needed != activePipeline) {
                gpu.bindGraphicsPipeline(rp, needed);
                gpu.bindVertexStorageBuffers(rp, 0, storageBufs, 2);
                activePipeline = needed;
            }
            GpuTextureSamplerBinding sb = {
                cmd.texture ? cmd.texture : Particles::GetWhiteTexture(),
                cmd.sampler ? cmd.sampler : Particles::GetLinearSampler(),
            };
            gpu.bindFragmentSamplers(rp, 0, &sb, 1);
            gpu.drawPrimitives(rp, 6, cmd.maxParticles, 0, cmd.particleOffset);
        }
    };

    if (m_povEnabled && m_povDecayPipeline && m_povCompositePipeline) {
        uint32_t         curr    = m_povIndex;
        uint32_t         prev    = 1u - curr;
        GpuTextureHandle povCurr = m_povTex[curr];
        GpuTextureHandle povPrev = m_povTex[prev];

        // ── 1. Decay pass: fade previous accumulation into current ────────────
        if (!m_povNeedsClear) {
            GpuColorTargetInfo decayInfo{};
            decayInfo.texture = povCurr;
            decayInfo.loadOp  = GpuLoadOp::DontCare;
            decayInfo.storeOp = GpuStoreOp::Store;
            GpuRenderPassHandle rp = gpu.beginRenderPass(cmdBuf, &decayInfo, 1, nullptr);
            gpu.bindGraphicsPipeline(rp, m_povDecayPipeline);
            GpuTextureSamplerBinding sb = { povPrev, m_povSampler };
            gpu.bindFragmentSamplers(rp, 0, &sb, 1);
            POVUniforms pu = { m_povDecay, {} };
            gpu.pushVertexUniformData(cmdBuf, 0, &pu, sizeof(pu));
            gpu.drawPrimitives(rp, 3, 1, 0, 0);
            gpu.endRenderPass(rp);
        }

        // ── 2. Particle render into POV texture ───────────────────────────────
        {
            GpuColorTargetInfo particleInfo{};
            particleInfo.texture = povCurr;
            particleInfo.loadOp  = m_povNeedsClear ? GpuLoadOp::Clear : GpuLoadOp::Load;
            particleInfo.storeOp = GpuStoreOp::Store;
            GpuRenderPassHandle rp = gpu.beginRenderPass(cmdBuf, &particleInfo, 1, nullptr);
            drawParticles(rp);
            gpu.endRenderPass(rp);
            m_povNeedsClear = false;
        }

        // ── 3. Composite POV texture onto swapchain ───────────────────────────
        {
            GpuColorTargetInfo compositeInfo{};
            compositeInfo.texture = target;
            compositeInfo.loadOp  = GpuLoadOp::Load;
            compositeInfo.storeOp = GpuStoreOp::Store;
            GpuRenderPassHandle rp = gpu.beginRenderPass(cmdBuf, &compositeInfo, 1, nullptr);
            gpu.bindGraphicsPipeline(rp, m_povCompositePipeline);
            GpuTextureSamplerBinding sb = { povCurr, m_povSampler };
            gpu.bindFragmentSamplers(rp, 0, &sb, 1);
            POVUniforms pu = { 1.0f, {} };
            gpu.pushVertexUniformData(cmdBuf, 0, &pu, sizeof(pu));
            gpu.drawPrimitives(rp, 3, 1, 0, 0);
            gpu.endRenderPass(rp);
        }

        m_povIndex ^= 1u;

    } else {
        // Standard path — render into the framebuffer. Under MSAA this pass is typically last, so
        // it owns the resolve of the multisampled target down to the single-sample fbContent that
        // the final blit reads. Without this the whole frame stays black under MSAA.
        GpuColorTargetInfo colorInfo{};
        colorInfo.texture        = target;
        colorInfo.resolveTexture = renderTargetResolve;
        colorInfo.loadOp         = GpuLoadOp::Load;
        colorInfo.storeOp        = (renderTargetResolve != 0) ? GpuStoreOp::Resolve : GpuStoreOp::Store;
        GpuRenderPassHandle rp = gpu.beginRenderPass(cmdBuf, &colorInfo, 1, nullptr);
        drawParticles(rp);
        gpu.endRenderPass(rp);
    }
}

void ParticleRenderPass::SetPOV(bool enabled, float decay) {
    if (enabled && !m_povEnabled)
        m_povNeedsClear = true;  // flush stale accumulation on re-enable
    m_povEnabled = enabled;
    m_povDecay   = decay;
}
