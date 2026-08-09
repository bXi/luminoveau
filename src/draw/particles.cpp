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

static std::string b64Dec(const char *src) {
    static constexpr signed char kLut[256] = {
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        62,
        -1,
        -1,
        -1,
        63,
        52,
        53,
        54,
        55,
        56,
        57,
        58,
        59,
        60,
        61,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11,
        12,
        13,
        14,
        15,
        16,
        17,
        18,
        19,
        20,
        21,
        22,
        23,
        24,
        25,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        26,
        27,
        28,
        29,
        30,
        31,
        32,
        33,
        34,
        35,
        36,
        37,
        38,
        39,
        40,
        41,
        42,
        43,
        44,
        45,
        46,
        47,
        48,
        49,
        50,
        51,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
    };
    std::string out;
    int         val = 0, bits = -8;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(src); *p; ++p) {
        signed char c = kLut[*p];
        if (c < 0)
            continue;
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
static bool importPreset(const char *encoded, ParticleSystemConfig &cfg) {
    std::string raw = b64Dec(encoded);
    if (raw.size() < 3 || raw.substr(0, 3) != "v1:")
        return false;

    std::vector<float> v;
    std::istringstream ss(raw.substr(3));
    std::string        tok;
    while (std::getline(ss, tok, ',')) {
        try {
            v.push_back(std::stof(tok));
        } catch (...) { return false; }
    }

    // 38 base fields + 3 optional angular velocity fields (added in v1.1)
    if (v.size() < 38)
        return false;

    int i               = 0;
    cfg.emitRate        = v[i++];
    cfg.spawnRadius     = v[i++];
    cfg.spawnVelocity.x = v[i++];
    cfg.spawnVelocity.y = v[i++];
    cfg.velocitySpread  = v[i++];
    cfg.gravity.x       = v[i++];
    cfg.gravity.y       = v[i++];
    cfg.drag            = v[i++];
    cfg.lifetimeMin     = v[i++];
    cfg.lifetimeMax     = v[i++];
    cfg.lifetimeBias    = v[i++];
    cfg.sizeStartMin    = v[i++];
    cfg.sizeStartMax    = v[i++];
    cfg.sizeStartBias   = v[i++];
    cfg.sizeEndMin      = v[i++];
    cfg.sizeEndMax      = v[i++];
    cfg.sizeEndBias     = v[i++];
    cfg.shape           = static_cast<ParticleShape>(static_cast<int>(v[i++]));
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
// state (s_*) directly. Free helpers b64Dec/importPreset above stay file-local.

void Particles::_init() {
    IGpu &gpu = Renderer::GetGpu();

    // Particle buffer: compute RW + vertex read
    _particleBuf = gpu.CreateBuffer({ static_cast<uint32_t>(MAX_PARTICLES * sizeof(GPUParticle)),
        GpuBufferUsage::StorageRead | GpuBufferUsage::StorageWrite });
    if (!_particleBuf)
        LOG_CRITICAL("Particles: failed to create particle buffer");

    // System data buffer: compute RO + vertex read
    _systemBuf = gpu.CreateBuffer({ static_cast<uint32_t>(MAX_SYSTEMS * sizeof(GPUParticleSystem)),
        GpuBufferUsage::StorageRead });
    if (!_systemBuf)
        LOG_CRITICAL("Particles: failed to create system buffer");

    // Transfer buffer for system data uploads
    _systemUploadBuf = gpu.CreateTransferBuffer({ static_cast<uint32_t>(MAX_SYSTEMS * sizeof(GPUParticleSystem)),
        GpuTransferUsage::Upload });
    if (!_systemUploadBuf)
        LOG_CRITICAL("Particles: failed to create system upload buffer");

    // Collider buffer: compute RO
    _colliderBuf = gpu.CreateBuffer({ static_cast<uint32_t>(MAX_COLLIDERS * sizeof(GPUCollider)),
        GpuBufferUsage::StorageRead });
    if (!_colliderBuf)
        LOG_CRITICAL("Particles: failed to create collider buffer");

    _colliderUploadBuf = gpu.CreateTransferBuffer({ static_cast<uint32_t>(MAX_COLLIDERS * sizeof(GPUCollider)),
        GpuTransferUsage::Upload });
    if (!_colliderUploadBuf)
        LOG_CRITICAL("Particles: failed to create collider upload buffer");

    // Zero-init collider and particle buffers so all slots start disabled/dead.
    // (WebGPU spec already zero-inits new buffers; redundant there but cheap.)
    {
        uint32_t                sz     = static_cast<uint32_t>(MAX_COLLIDERS * sizeof(GPUCollider));
        GpuTransferBufferHandle zeroTB = gpu.CreateTransferBuffer({ sz, GpuTransferUsage::Upload });
        void                   *zm     = gpu.MapTransferBuffer(zeroTB, false);
        std::memset(zm, 0, sz);
        gpu.UnmapTransferBuffer(zeroTB);
        GpuCmdBufferHandle cmd = gpu.AcquireCommandBuffer();
        gpu.UploadToBuffer(cmd, zeroTB, 0, _colliderBuf, 0, sz);
        gpu.SubmitCommandBuffer(cmd);
        gpu.WaitIdle();
        gpu.ReleaseTransferBuffer(zeroTB);
    }

    {
        uint32_t                sz     = static_cast<uint32_t>(MAX_PARTICLES * sizeof(GPUParticle));
        GpuTransferBufferHandle zeroTB = gpu.CreateTransferBuffer({ sz, GpuTransferUsage::Upload });
        void                   *mapped = gpu.MapTransferBuffer(zeroTB, false);
        std::memset(mapped, 0, sz);
        gpu.UnmapTransferBuffer(zeroTB);

        GpuCmdBufferHandle cmd = gpu.AcquireCommandBuffer();
        gpu.UploadToBuffer(cmd, zeroTB, 0, _particleBuf, 0, sz);
        gpu.SubmitCommandBuffer(cmd);
        gpu.WaitIdle();
        gpu.ReleaseTransferBuffer(zeroTB);
    }

    // Create 1×1 white fallback texture for non-textured draws
    {
        _whiteTexture                      = gpu.CreateTexture({ 1, 1, 1, 1, GpuTextureFormat::R8G8B8A8_Unorm,
                                 GpuSampleCount::X1, GpuTextureUsage::Sampler | GpuTextureUsage::Transfer });
        uint32_t                whitePixel = 0xFFFFFFFFu;
        GpuTransferBufferHandle wtb        = gpu.CreateTransferBuffer({ sizeof(whitePixel), GpuTransferUsage::Upload });
        void                   *wm         = gpu.MapTransferBuffer(wtb, false);
        std::memcpy(wm, &whitePixel, sizeof(whitePixel));
        gpu.UnmapTransferBuffer(wtb);
        GpuCmdBufferHandle      wcmd = gpu.AcquireCommandBuffer();
        GpuTransferBufferRegion wsrc { wtb, 0, sizeof(whitePixel) };
        GpuTextureRegion        wdst { _whiteTexture, 0, 0, 0, 0, 0, 1, 1, 1 };
        gpu.UploadToTexture(wcmd, wsrc, wdst, false);
        gpu.SubmitCommandBuffer(wcmd);
        gpu.WaitIdle();
        gpu.ReleaseTransferBuffer(wtb);
    }

    // Linear sampler for textured particles
    _linearSampler = gpu.CreateSampler(GpuPresets::LinearClamp);

    // Built-in physics compute pipeline — per-backend builder (SDL: precompiled SPIRV,
    // WebGPU: embedded WGSL string).
    _computePipeline.pipeline     = ParticlesBuiltin::CreateComputePipeline();
    _computePipeline.threadCountX = 64;
    _computePipeline.threadCountY = 1;
    _computePipeline.threadCountZ = 1;

    // Create and attach the render pass to the primary framebuffer
    _renderPass = new ParticleRenderPass();
    _attachToFramebuffer("primaryFramebuffer");

    std::fill(std::begin(_systemCustomCompute), std::end(_systemCustomCompute), INVALID_CUSTOM_COMPUTE);
    std::fill(std::begin(_systemPhysicsCompute), std::end(_systemPhysicsCompute), INVALID_CUSTOM_COMPUTE);
    std::fill(std::begin(_systemSpringCompute), std::end(_systemSpringCompute), INVALID_CUSTOM_COMPUTE);

    LOG_INFO("Particles: initialized (max particles: {}, max systems: {})",
        MAX_PARTICLES, MAX_SYSTEMS);
}

void Particles::_quit() {
    IGpu &gpu = Renderer::GetGpu();

    // Discard any compute dispatches queued this frame before releasing GPU resources.
    Compute::Reset();

    gpu.WaitIdle();

    if (_renderPass) {
        Renderer::RemoveShaderPass("particles");
        _renderPass = nullptr;
    }

    if (_computePipeline.pipeline) {
        gpu.ReleaseComputePipeline(_computePipeline.pipeline);
        _computePipeline = {};
    }

    for (uint32_t i = 0; i < MAX_CUSTOM_COMPUTES; ++i) {
        if (_customComputeUsed[i] && _customComputePool[i].pipeline) {
            gpu.ReleaseComputePipeline(_customComputePool[i].pipeline);
            _customComputePool[i] = {};
            _customComputeUsed[i] = false;
        }
    }

    if (_particleBuf) {
        gpu.ReleaseBuffer(_particleBuf);
        _particleBuf = 0;
    }
    if (_systemBuf) {
        gpu.ReleaseBuffer(_systemBuf);
        _systemBuf = 0;
    }
    if (_systemUploadBuf) {
        gpu.ReleaseTransferBuffer(_systemUploadBuf);
        _systemUploadBuf = 0;
    }
    if (_colliderBuf) {
        gpu.ReleaseBuffer(_colliderBuf);
        _colliderBuf = 0;
    }
    if (_colliderUploadBuf) {
        gpu.ReleaseTransferBuffer(_colliderUploadBuf);
        _colliderUploadBuf = 0;
    }
    _colliderHighWater = 0;
    std::memset(_colliderData, 0, sizeof(_colliderData));
    std::memset(_colliderUsed, 0, sizeof(_colliderUsed));
    if (_whiteTexture) {
        gpu.ReleaseTexture(_whiteTexture);
        _whiteTexture = 0;
    }
    if (_linearSampler) {
        gpu.ReleaseSampler(_linearSampler);
        _linearSampler = 0;
    }

    // Reset CPU-side state so Init() can be called again cleanly.
    _nextParticleSlot = 0;
    _systemDirty      = false;
    _accumTime        = 0.0f;
    _pendingDt        = 0.0f;
    _updateQueued     = false;
    std::fill(std::begin(_systemUsed), std::end(_systemUsed), false);
    std::fill(std::begin(_systemData), std::end(_systemData), GPUParticleSystem {});
    std::fill(std::begin(_systemTextures), std::end(_systemTextures), GpuTextureHandle { 0 });
    std::fill(std::begin(_systemSamplers), std::end(_systemSamplers), GpuSamplerHandle { 0 });
    std::fill(std::begin(_systemPixelMode), std::end(_systemPixelMode), false);
    std::fill(std::begin(_slotParticleOffset), std::end(_slotParticleOffset), 0u);
    std::fill(std::begin(_slotParticleCount), std::end(_slotParticleCount), 0u);
    std::fill(std::begin(_systemCustomCompute), std::end(_systemCustomCompute), INVALID_CUSTOM_COMPUTE);
    std::fill(std::begin(_systemPhysicsCompute), std::end(_systemPhysicsCompute), INVALID_CUSTOM_COMPUTE);
    std::fill(std::begin(_customComputeUsed), std::end(_customComputeUsed), false);
    std::fill(std::begin(_customComputePool), std::end(_customComputePool), ComputePipelineAsset {});
    std::fill(std::begin(_systemSpringCompute), std::end(_systemSpringCompute), INVALID_CUSTOM_COMPUTE);
    std::fill(std::begin(_systemSpringK), std::end(_systemSpringK), 0.f);
    std::fill(std::begin(_systemSpringDamp), std::end(_systemSpringDamp), 0.f);
    std::fill(std::begin(_systemSpringInteract), std::end(_systemSpringInteract), glm::vec4 {});

    LOG_INFO("Particles: shut down");
}

// ─────────────────────────────────────────────────────────────────────────────

uint32_t Particles::_allocateSystemSlot() {
    for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
        if (!_systemUsed[i]) {
            _systemUsed[i] = true;
            return i;
        }
    }
    LOG_CRITICAL("Particles: no free system slots (MAX_SYSTEMS = {})", MAX_SYSTEMS);
    return 0;
}

ParticleSystemHandle Particles::_createSystem(const ParticleSystemConfig &cfg) {
    if (_nextParticleSlot + cfg.maxParticles > MAX_PARTICLES) {
        LOG_ERROR("Particles: not enough particle slots for new system (wanted {}, have {})",
            cfg.maxParticles, MAX_PARTICLES - _nextParticleSlot);
        return {};
    }

    ParticleSystemHandle handle;
    handle.systemIndex    = _allocateSystemSlot();
    handle.particleOffset = _nextParticleSlot;
    handle.maxParticles   = cfg.maxParticles;
    handle.valid          = true;

    _slotParticleOffset[handle.systemIndex] = handle.particleOffset;
    _slotParticleCount[handle.systemIndex]  = handle.maxParticles;
    _nextParticleSlot += cfg.maxParticles;

    // Fill GPU system struct
    GPUParticleSystem &sys = _systemData[handle.systemIndex];
    sys.spawnPos           = glm::vec4(cfg.spawnPosition, cfg.spawnRadius);
    sys.spawnVel           = glm::vec4(cfg.spawnVelocity, cfg.velocitySpread);
    sys.gravityAndDrag     = glm::vec4(cfg.gravity, cfg.drag);
    sys.colors[0]          = cfg.colors[0];
    sys.colors[1]          = cfg.colors[1];
    sys.colors[2]          = cfg.colors[2];
    sys.colors[3]          = cfg.colors[3];
    sys.colorPositions     = cfg.colorPositions;
    sys.sizeStartMin       = cfg.sizeStartMin;
    sys.sizeStartMax       = cfg.sizeStartMax;
    sys.sizeEndMin         = cfg.sizeEndMin;
    sys.sizeEndMax         = cfg.sizeEndMax;
    sys.sizeStartBias      = cfg.sizeStartBias;
    sys.sizeEndBias        = cfg.sizeEndBias;
    sys.lifetimeMin        = cfg.lifetimeMin;
    sys.lifetimeMax        = cfg.lifetimeMax;
    sys.lifetimeBias       = cfg.lifetimeBias;
    // Store emitRate scaled by particle count so the per-particle timer
    // interval (1/storedRate = N/emitRate) gives emitRate spawns/second system-wide.
    sys.emitRate                         = (cfg.maxParticles > 0)
                                ? (cfg.emitRate / static_cast<float>(cfg.maxParticles))
                                : cfg.emitRate;
    sys.flags                            = 0; // not emitting yet
    sys.shapeType                        = cfg.texture
                               ? static_cast<uint32_t>(ParticleShape::Textured)
                               : static_cast<uint32_t>(cfg.shape);
    sys.angVelMin                        = cfg.angVelMin;
    sys.angVelMax                        = cfg.angVelMax;
    sys.angVelBias                       = cfg.angVelBias;
    sys.trailStretch                     = cfg.trailStretch;
    _systemTextures[handle.systemIndex]  = cfg.texture;
    _systemSamplers[handle.systemIndex]  = cfg.sampler;
    _systemPixelMode[handle.systemIndex] = cfg.pixelMode;
    _systemDirty                         = true;

    // Initialise particle slots: staggered respawn timers so emission is smooth
    // from frame 1. All particles start dead with a timer = index / emitRate.
    {
        const uint32_t           n = cfg.maxParticles;
        std::vector<GPUParticle> init(n);
        for (uint32_t i = 0; i < n; ++i) {
            init[i].posAndLife    = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // dead
            init[i].velAndMaxLife = glm::vec4(0.0f, 0.0f, 0.0f, cfg.lifetimeMax);
            init[i].systemID      = handle.systemIndex;
            // Stagger as a normalised fraction [0, 1) so particles are uniformly
            // spread across one respawn period from the very first frame.
            init[i].respawnTimer    = (n > 1) ? (static_cast<float>(i) / static_cast<float>(n)) : 0.0f;
            init[i].startSize       = cfg.sizeStartMin;
            init[i].endSize         = cfg.sizeEndMin;
            init[i].angle           = 0.0f;
            init[i].angularVelocity = 0.0f;
            init[i].pad0            = 0.0f;
            init[i].pad1            = 0.0f;
        }

        IGpu                   &gpu      = Renderer::GetGpu();
        uint32_t                uploadSz = static_cast<uint32_t>(n * sizeof(GPUParticle));
        GpuTransferBufferHandle ptb      = gpu.CreateTransferBuffer({ uploadSz, GpuTransferUsage::Upload });
        void                   *pmapped  = gpu.MapTransferBuffer(ptb, false);
        std::memcpy(pmapped, init.data(), uploadSz);
        gpu.UnmapTransferBuffer(ptb);
        GpuCmdBufferHandle cmd = gpu.AcquireCommandBuffer();
        gpu.UploadToBuffer(cmd, ptb, 0, _particleBuf,
            static_cast<uint32_t>(handle.particleOffset * sizeof(GPUParticle)),
            uploadSz);
        gpu.SubmitCommandBuffer(cmd);
        gpu.WaitIdle();
        gpu.ReleaseTransferBuffer(ptb);
    }

    LOG_INFO("Particles: created system {} ({} particles, offset {})",
        handle.systemIndex, handle.maxParticles, handle.particleOffset);
    return handle;
}

ParticleSystemHandle Particles::_createSystemFromPreset(const char *encoded, uint32_t maxParticles,
    glm::vec3 spawnPosition) {
    ParticleSystemConfig cfg;
    cfg.maxParticles  = maxParticles;
    cfg.spawnPosition = spawnPosition;
    if (!importPreset(encoded, cfg)) {
        LOG_ERROR("Particles: CreateSystemFromPreset: malformed preset string");
        return {};
    }
    return _createSystem(cfg);
}

void Particles::_destroySystem(ParticleSystemHandle &handle) {
    if (!handle.valid)
        return;

    uint32_t idx              = handle.systemIndex;
    _systemUsed[idx]          = false;
    _systemData[idx]          = {};
    _systemTextures[idx]      = 0;
    _systemSamplers[idx]      = 0;
    _systemPixelMode[idx]     = false;
    _systemCustomCompute[idx] = INVALID_CUSTOM_COMPUTE;
    _systemDirty              = true;

    // Reclaim particle slots: walk the bump pointer back as far as possible.
    // This fully handles the common cases (destroy last, destroy all).
    // For out-of-order destruction the freed slots remain unused until
    // all systems above them are also destroyed.
    if (_slotParticleOffset[idx] + _slotParticleCount[idx] == _nextParticleSlot) {
        _nextParticleSlot = _slotParticleOffset[idx];
        // Continue walking back over any other freed slots that are now at the top
        for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
            if (!_systemUsed[i] && _slotParticleOffset[i] + _slotParticleCount[i] == _nextParticleSlot) {
                _nextParticleSlot = _slotParticleOffset[i];
            }
        }
    }
    _slotParticleOffset[idx] = 0;
    _slotParticleCount[idx]  = 0;

    handle = {};
}

void Particles::_start(const ParticleSystemHandle &handle) {
    if (!handle.valid)
        return;
    _systemData[handle.systemIndex].flags |= 1u;
    _systemDirty = true;
}

void Particles::_stop(const ParticleSystemHandle &handle) {
    if (!handle.valid)
        return;
    _systemData[handle.systemIndex].flags &= ~1u;
    _systemDirty = true;
}

void Particles::_setPosition(const ParticleSystemHandle &handle, glm::vec3 worldPos) {
    if (!handle.valid)
        return;
    _systemData[handle.systemIndex].spawnPos.x = worldPos.x;
    _systemData[handle.systemIndex].spawnPos.y = worldPos.y;
    _systemData[handle.systemIndex].spawnPos.z = worldPos.z;
    _systemDirty                               = true;
}

void Particles::_updateConfig(const ParticleSystemHandle &handle, const ParticleSystemConfig &cfg) {
    if (!handle.valid)
        return;

    GPUParticleSystem &sys            = _systemData[handle.systemIndex];
    uint32_t           preservedFlags = sys.flags;               // keep Start/Stop state
    glm::vec3          preservedPos   = glm::vec3(sys.spawnPos); // keep SetPosition value

    sys.spawnPos                         = glm::vec4(preservedPos, cfg.spawnRadius);
    sys.spawnVel                         = glm::vec4(cfg.spawnVelocity, cfg.velocitySpread);
    sys.gravityAndDrag                   = glm::vec4(cfg.gravity, cfg.drag);
    sys.colors[0]                        = cfg.colors[0];
    sys.colors[1]                        = cfg.colors[1];
    sys.colors[2]                        = cfg.colors[2];
    sys.colors[3]                        = cfg.colors[3];
    sys.colorPositions                   = cfg.colorPositions;
    sys.sizeStartMin                     = cfg.sizeStartMin;
    sys.sizeStartMax                     = cfg.sizeStartMax;
    sys.sizeEndMin                       = cfg.sizeEndMin;
    sys.sizeEndMax                       = cfg.sizeEndMax;
    sys.sizeStartBias                    = cfg.sizeStartBias;
    sys.sizeEndBias                      = cfg.sizeEndBias;
    sys.lifetimeMin                      = cfg.lifetimeMin;
    sys.lifetimeMax                      = cfg.lifetimeMax;
    sys.lifetimeBias                     = cfg.lifetimeBias;
    sys.emitRate                         = (handle.maxParticles > 0)
                                ? (cfg.emitRate / static_cast<float>(handle.maxParticles))
                                : cfg.emitRate;
    sys.flags                            = preservedFlags;
    sys.shapeType                        = cfg.texture
                               ? static_cast<uint32_t>(ParticleShape::Textured)
                               : static_cast<uint32_t>(cfg.shape);
    sys.angVelMin                        = cfg.angVelMin;
    sys.angVelMax                        = cfg.angVelMax;
    sys.angVelBias                       = cfg.angVelBias;
    sys.trailStretch                     = cfg.trailStretch;
    _systemTextures[handle.systemIndex]  = cfg.texture;
    _systemSamplers[handle.systemIndex]  = cfg.sampler;
    _systemPixelMode[handle.systemIndex] = cfg.pixelMode;

    _systemDirty = true;
}

ParticleSystemConfig Particles::_getConfig(const ParticleSystemHandle &handle) {
    ParticleSystemConfig cfg;
    if (!handle.valid)
        return cfg;

    const GPUParticleSystem &sys = _systemData[handle.systemIndex];

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
    cfg.pixelMode      = _systemPixelMode[handle.systemIndex];
    cfg.texture        = _systemTextures[handle.systemIndex];
    cfg.sampler        = _systemSamplers[handle.systemIndex];

    return cfg;
}

void Particles::_setTexture(const ParticleSystemHandle &handle,
    GpuTextureHandle texture, GpuSamplerHandle sampler) {
    if (!handle.valid)
        return;
    _systemTextures[handle.systemIndex]       = texture;
    _systemSamplers[handle.systemIndex]       = sampler;
    _systemData[handle.systemIndex].shapeType = texture
        ? static_cast<uint32_t>(ParticleShape::Textured)
        : static_cast<uint32_t>(ParticleShape::SoftCircle); // revert to default shape
    _systemDirty                              = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Custom compute
// ─────────────────────────────────────────────────────────────────────────────

ParticleComputeHandle Particles::_createCustomCompute(const std::string &shaderPath) {
    // Find a free pool slot
    for (uint32_t i = 0; i < MAX_CUSTOM_COMPUTES; ++i) {
        if (_customComputeUsed[i])
            continue;

        ComputePipelineAsset pipeline = Renderer::CreateComputePipelineAsset(shaderPath);
        if (!pipeline.pipeline) {
            LOG_WARNING("Particles: failed to create custom compute from '{}' (shader unsupported on this backend?)", shaderPath);
            return {};
        }

        _customComputePool[i] = pipeline;
        _customComputeUsed[i] = true;
        LOG_INFO("Particles: created custom compute [{}] from '{}'", i, shaderPath);
        return { i, true };
    }

    LOG_WARNING("Particles: no free custom compute slots (MAX_CUSTOM_COMPUTES = {})",
        MAX_CUSTOM_COMPUTES);
    return {};
}

void Particles::_destroyCustomCompute(ParticleComputeHandle &handle) {
    if (!handle.valid)
        return;

    // Clear from any system currently using it (primary or spring slot)
    for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
        if (_systemCustomCompute[i] == handle.index) {
            _systemCustomCompute[i] = INVALID_CUSTOM_COMPUTE;
            _systemData[i].flags &= ~2u;
            _systemDirty = true;
        }
        if (_systemPhysicsCompute[i] == handle.index)
            _systemPhysicsCompute[i] = INVALID_CUSTOM_COMPUTE;
        if (_systemSpringCompute[i] == handle.index)
            _systemSpringCompute[i] = INVALID_CUSTOM_COMPUTE;
    }

    Renderer::GetGpu().ReleaseComputePipeline(_customComputePool[handle.index].pipeline);
    _customComputePool[handle.index] = {};
    _customComputeUsed[handle.index] = false;
    handle                           = {};
}

void Particles::_setCustomCompute(const ParticleSystemHandle &system,
    const ParticleComputeHandle                              &compute) {
    if (!system.valid || !compute.valid)
        return;
    _systemCustomCompute[system.systemIndex] = compute.index;
    _systemData[system.systemIndex].flags |= 2u; // skip built-in update
    _systemDirty = true;
}

void Particles::_clearCustomCompute(const ParticleSystemHandle &system) {
    if (!system.valid)
        return;
    _systemCustomCompute[system.systemIndex] = INVALID_CUSTOM_COMPUTE;
    _systemData[system.systemIndex].flags &= ~2u;
    _systemDirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────

// Called by the user once per logical update. On SDL_AppIterate the host may
// call this many times between actual rendered frames (e.g. when waiting on
// the swapchain). Enqueueing compute work here would have it discarded by
// Renderer::Render's no-swapchain path, dropping most of the simulation. So
// we only accumulate dt + time here; the real dispatch happens in
// _prepareFrame, which the renderer calls once per actual frame.
void Particles::_update(float deltaTime) {
    if (!_computePipeline.pipeline)
        return;
    _pendingDt += deltaTime;
    _accumTime += deltaTime;
    _updateQueued = true;
}

void Particles::_buildDispatches() {
    if (!_computePipeline.pipeline)
        return;

    float deltaTime = _pendingDt;
    _pendingDt      = 0.0f;

    // Count the total live particle range
    uint32_t total = _nextParticleSlot;
    if (total == 0)
        return;

    // Uniform block
    struct ComputeUniforms {
        uint32_t totalParticles;
        float    deltaTime;
        float    time;
        uint32_t numColliders;
    } uniforms = { total, deltaTime, _accumTime, _colliderHighWater };

    Compute::SetPipeline(_computePipeline);
    Compute::BindReadBuffer(0, _systemBuf);
    Compute::BindReadBuffer(1, _colliderBuf);
    Compute::BindReadWriteBuffer(0, _particleBuf);
    Compute::PushUniform(0, uniforms);
    Compute::DispatchAuto(total); // built-in: skips systems with custom compute flag

    // Dispatch custom compute pipelines (one per system that has one assigned)
    struct CustomUniforms {
        uint32_t particleOffset;
        uint32_t particleCount;
        float    deltaTime;
        float    time;
    };
    for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
        if (!_systemUsed[i])
            continue;
        uint32_t ci = _systemCustomCompute[i];
        if (ci == INVALID_CUSTOM_COMPUTE)
            continue;

        CustomUniforms cu = {
            _slotParticleOffset[i],
            _slotParticleCount[i],
            deltaTime,
            _accumTime
        };
        Compute::SetPipeline(_customComputePool[ci]);
        Compute::BindReadBuffer(0, _systemBuf);
        Compute::BindReadWriteBuffer(0, _particleBuf);
        Compute::PushUniform(0, cu);
        Compute::DispatchAuto(_slotParticleCount[i]);
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
            float    pad;
        };
        for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
            uint32_t pc = _systemPhysicsCompute[i];
            if (!_systemUsed[i] || pc == INVALID_CUSTOM_COMPUTE)
                continue;
            PhysicsUniforms pu = {
                _slotParticleOffset[i], _slotParticleCount[i],
                deltaTime, _colliderHighWater,
                _systemPhysicsGravity[i].x, _systemPhysicsGravity[i].y,
                _systemPhysicsDrag[i], 0.f
            };
            Compute::SetPipeline(_customComputePool[pc]);
            Compute::BindReadBuffer(0, _colliderBuf);
            Compute::BindReadWriteBuffer(0, _particleBuf);
            Compute::PushUniform(0, pu);
            Compute::DispatchAuto(_slotParticleCount[i]);
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
            float    pad0;
            float    pad1;
            float    pad2;
        };
        for (uint32_t i = 0; i < MAX_SYSTEMS; ++i) {
            uint32_t sc = _systemSpringCompute[i];
            if (!_systemUsed[i] || sc == INVALID_CUSTOM_COMPUTE)
                continue;
            const glm::vec4 &ia = _systemSpringInteract[i];
            SpringUniforms   su = {
                _slotParticleOffset[i], _slotParticleCount[i],
                deltaTime, _systemSpringK[i],
                _systemSpringDamp[i],
                ia.x, ia.y, ia.z, ia.w,
                0.f, 0.f, 0.f
            };
            Compute::SetPipeline(_customComputePool[sc]);
            Compute::BindReadWriteBuffer(0, _particleBuf);
            Compute::PushUniform(0, su);
            Compute::DispatchAuto(_slotParticleCount[i]);
        }
    }
}

void Particles::_queueDraw(const ParticleSystemHandle &handle) {
    if (!handle.valid || !_renderPass)
        return;
    _renderPass->AddDraw({
        handle.particleOffset,
        handle.maxParticles,
        _systemTextures[handle.systemIndex],
        _systemSamplers[handle.systemIndex],
        _systemPixelMode[handle.systemIndex],
    });
}

GpuBufferHandle     Particles::_getParticleBuffer() { return _particleBuf; }
GpuBufferHandle     Particles::_getSystemBuffer() { return _systemBuf; }
ParticleRenderPass *Particles::_getRenderPass() { return _renderPass; }
GpuTextureHandle    Particles::_getWhiteTexture() { return _whiteTexture; }
GpuSamplerHandle    Particles::_getLinearSampler() { return _linearSampler; }

// ─────────────────────────────────────────────────────────────────────────────
// Physics pass
// ─────────────────────────────────────────────────────────────────────────────

void Particles::_enablePhysicsPass(const ParticleSystemHandle &handle,
    const ParticleComputeHandle                               &physicsCompute,
    glm::vec2 gravity, float drag) {
    if (!handle.valid || !physicsCompute.valid)
        return;
    _systemPhysicsCompute[handle.systemIndex] = physicsCompute.index;
    _systemPhysicsGravity[handle.systemIndex] = gravity;
    _systemPhysicsDrag[handle.systemIndex]    = drag;
}

void Particles::_disablePhysicsPass(const ParticleSystemHandle &handle) {
    if (!handle.valid)
        return;
    _systemPhysicsCompute[handle.systemIndex] = INVALID_CUSTOM_COMPUTE;
}

void Particles::_setPhysicsPassParams(const ParticleSystemHandle &handle,
    glm::vec2 gravity, float drag) {
    if (!handle.valid)
        return;
    _systemPhysicsGravity[handle.systemIndex] = gravity;
    _systemPhysicsDrag[handle.systemIndex]    = drag;
}

// ─────────────────────────────────────────────────────────────────────────────
// Spring pass
// ─────────────────────────────────────────────────────────────────────────────

void Particles::_enableSpringPass(const ParticleSystemHandle &handle,
    const ParticleComputeHandle                              &springCompute,
    float springK, float damping) {
    if (!handle.valid || !springCompute.valid)
        return;
    _systemSpringCompute[handle.systemIndex]  = springCompute.index;
    _systemSpringK[handle.systemIndex]        = springK;
    _systemSpringDamp[handle.systemIndex]     = damping;
    _systemSpringInteract[handle.systemIndex] = {};
}

void Particles::_disableSpringPass(const ParticleSystemHandle &handle) {
    if (!handle.valid)
        return;
    _systemSpringCompute[handle.systemIndex]  = INVALID_CUSTOM_COMPUTE;
    _systemSpringInteract[handle.systemIndex] = {};
}

void Particles::_setSpringParams(const ParticleSystemHandle &handle,
    float springK, float damping) {
    if (!handle.valid)
        return;
    _systemSpringK[handle.systemIndex]    = springK;
    _systemSpringDamp[handle.systemIndex] = damping;
}

void Particles::_setSpringInteraction(const ParticleSystemHandle &handle,
    float x, float y, float r, float f) {
    if (!handle.valid)
        return;
    _systemSpringInteract[handle.systemIndex] = { x, y, r, f };
}

// ─────────────────────────────────────────────────────────────────────────────
// Colliders
// ─────────────────────────────────────────────────────────────────────────────

ColliderHandle Particles::_addCollider(ColliderType type, glm::vec4 params,
    float restitution, float friction) {
    for (uint32_t i = 0; i < MAX_COLLIDERS; ++i) {
        if (_colliderUsed[i])
            continue;
        _colliderData[i] = {
            .params      = params,
            .restitution = restitution,
            .friction    = friction,
            .type        = static_cast<uint32_t>(type),
            .enabled     = 1u,
        };
        _colliderUsed[i]   = true;
        _colliderHighWater = std::max(_colliderHighWater, i + 1u);
        _colliderDirty     = true;
        return { i, true };
    }
    LOG_ERROR("Particles: no free collider slots (MAX_COLLIDERS = {})", MAX_COLLIDERS);
    return {};
}

void Particles::_removeCollider(ColliderHandle &handle) {
    if (!handle.valid)
        return;
    _colliderData[handle.index].enabled = 0u;
    _colliderUsed[handle.index]         = false;
    // Recompute high-water mark
    _colliderHighWater = 0;
    for (uint32_t i = 0; i < MAX_COLLIDERS; ++i)
        if (_colliderUsed[i])
            _colliderHighWater = i + 1u;
    _colliderDirty = true;
    handle         = {};
}

void Particles::_clearColliders() {
    for (uint32_t i = 0; i < MAX_COLLIDERS; ++i) {
        _colliderData[i].enabled = 0u;
        _colliderUsed[i]         = false;
    }
    _colliderHighWater = 0;
    _colliderDirty     = true;
}

void Particles::_updateCollider(const ColliderHandle &handle, glm::vec4 params,
    float restitution, float friction) {
    if (!handle.valid)
        return;
    _colliderData[handle.index].params      = params;
    _colliderData[handle.index].restitution = restitution;
    _colliderData[handle.index].friction    = friction;
    _colliderDirty                          = true;
}

void Particles::_setPOV(bool enabled, float decay) {
    if (_renderPass)
        _renderPass->SetPOV(enabled, decay);
}
bool  Particles::_getPOVEnabled() { return _renderPass ? _renderPass->GetPOVEnabled() : false; }
float Particles::_getPOVDecay() { return _renderPass ? _renderPass->GetPOVDecay() : 0.92f; }

void Particles::_attachToFramebuffer(const std::string &fbName) {
    FrameBuffer *fb = Renderer::GetFramebuffer(fbName);
    if (!fb) {
        LOG_ERROR("Particles::_attachToFramebuffer: framebuffer '{}' not found", fbName);
        return;
    }
    GpuTextureFormat fmt = Renderer::GetGpu().GetSwapchainFormat();
    _renderPass->Init(fmt, fb->width, fb->height, "particles");
    Renderer::AttachRenderPassToFrameBuffer(_renderPass, "particles", fbName);
}

// Called by Renderer::_endFrame() BEFORE Compute::ExecuteQueued()
void Particles::_prepareFrame(GpuCmdBufferHandle cmdBuf) {
    // Build particle compute dispatches for this frame using accumulated dt.
    // Update() only accumulates; the actual enqueue happens here so it survives
    // even when Update() is called multiple times per rendered frame.
    if (_updateQueued) {
        _updateQueued = false;
        _buildDispatches();
    }

    if (!_systemDirty && !_colliderDirty)
        return;

    IGpu &gpu = Renderer::GetGpu();

    if (_systemDirty) {
        _systemDirty    = false;
        uint32_t sz     = static_cast<uint32_t>(MAX_SYSTEMS * sizeof(GPUParticleSystem));
        void    *mapped = gpu.MapTransferBuffer(_systemUploadBuf, false);
        std::memcpy(mapped, _systemData, sz);
        gpu.UnmapTransferBuffer(_systemUploadBuf);
        gpu.UploadToBuffer(cmdBuf, _systemUploadBuf, 0, _systemBuf, 0, sz);
    }

    if (_colliderDirty) {
        _colliderDirty  = false;
        uint32_t sz     = static_cast<uint32_t>(MAX_COLLIDERS * sizeof(GPUCollider));
        void    *mapped = gpu.MapTransferBuffer(_colliderUploadBuf, false);
        std::memcpy(mapped, _colliderData, sz);
        gpu.UnmapTransferBuffer(_colliderUploadBuf);
        gpu.UploadToBuffer(cmdBuf, _colliderUploadBuf, 0, _colliderBuf, 0, sz);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ParticleRenderPass — implementation
// ─────────────────────────────────────────────────────────────────────────────

ParticleRenderPass::ParticleRenderPass()
    : RenderPass() {
}

bool ParticleRenderPass::Init(GpuTextureFormat swapchainFormat,
    uint32_t width, uint32_t height,
    std::string name, bool logInit,
    size_t /*capacity*/, bool /*forceNoMSAA*/) {
    _passname      = std::move(name);
    _format        = swapchainFormat;
    _surfaceWidth  = width;
    _surfaceHeight = height;

#ifdef __3DS__
    // PICA200 has no particle shaders and no compute to drive them. Attach the pass as an
    // inert no-op instead of hitting the fatal LOG_ERROR below (LOG_ERROR is [[noreturn]] —
    // it throws and aborts the app). Render() is likewise a no-op on 3DS.
    if (logInit)
        LOG_INFO("ParticleRenderPass: disabled on 3DS (particles unsupported on PICA200)");
    return true;
#endif

    IGpu &gpu = Renderer::GetGpu();

    const char *vertEntry = Shaders::GetVertexEntryPoint();
    const char *fragEntry = Shaders::GetFragmentEntryPoint();

    _vertShader = gpu.CreateShader({
        Lumi::Shaders::PARTICLES_VERT,
        Lumi::Shaders::PARTICLES_VERT_SIZE,
        vertEntry, GpuShaderStage::Vertex,
        0, 1, 2, 0 // samplers=0, uniforms=1, storageBufs=2, storageTex=0
    });
    _fragShader = gpu.CreateShader({
        Lumi::Shaders::PARTICLES_FRAG,
        Lumi::Shaders::PARTICLES_FRAG_SIZE,
        fragEntry, GpuShaderStage::Fragment,
        1, 0, 0, 0 // samplers=1
    });

    if (!_vertShader || !_fragShader) {
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

    GpuGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.vertexShader             = _vertShader;
    pipelineInfo.fragmentShader           = _fragShader;
    pipelineInfo.colorTargetFormat        = swapchainFormat;
    pipelineInfo.hasDepthTarget           = false;
    pipelineInfo.vertexStorageBufferCount = 2;
    pipelineInfo.blend                    = additiveBlend;
    // Match the framebuffer's MSAA sample count so particles draw into the multisampled target.
    // The pass is re-initialised on sample-count changes, so this recreates correctly.
    // NOTE: the POV accumulation path renders into single-sample POV textures, so POV + MSAA is
    // not supported (would need MSAA POV textures + a resolve before compositing).
    pipelineInfo.sampleCount = Renderer::GetSampleCount();

    _pipeline = gpu.CreateGraphicsPipeline(pipelineInfo);
    if (!_pipeline) {
        LOG_ERROR("ParticleRenderPass: failed to create pipeline");
        return false;
    }

    pipelineInfo.blend = standardBlend;
    _pixelPipeline     = gpu.CreateGraphicsPipeline(pipelineInfo);
    if (!_pixelPipeline) {
        LOG_ERROR("ParticleRenderPass: failed to create pixel pipeline");
        return false;
    }

    // ── POV shaders ───────────────────────────────────────────────────────────
    _povVertShader = gpu.CreateShader({ Lumi::Shaders::PARTICLES_POV_VERT,
        Lumi::Shaders::PARTICLES_POV_VERT_SIZE,
        vertEntry, GpuShaderStage::Vertex,
        0, 1, 0, 0 });
    _povFragShader = gpu.CreateShader({ Lumi::Shaders::PARTICLES_POV_FRAG,
        Lumi::Shaders::PARTICLES_POV_FRAG_SIZE,
        fragEntry, GpuShaderStage::Fragment,
        1, 0, 0, 0 });

    if (!_povVertShader || !_povFragShader) {
        LOG_ERROR("ParticleRenderPass: POV shader creation failed");
        return false;
    }

    // ── POV textures (ping-pong) ───────────────────────────────────────────────
    GpuTextureCreateInfo povTexInfo {};
    povTexInfo.width  = width;
    povTexInfo.height = height;
    povTexInfo.format = swapchainFormat;
    povTexInfo.usage  = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
    _povTex[0]        = gpu.CreateTexture(povTexInfo);
    _povTex[1]        = gpu.CreateTexture(povTexInfo);

    if (!_povTex[0] || !_povTex[1]) {
        LOG_ERROR("ParticleRenderPass: POV texture creation failed");
        return false;
    }

    // Zero-initialize POV textures via clear passes
    {
        GpuCmdBufferHandle initCmd = gpu.AcquireCommandBuffer();
        for (int ti = 0; ti < 2; ti++) {
            GpuColorTargetInfo ci {};
            ci.texture = _povTex[ti];
            ci.loadOp  = GpuLoadOp::Clear;
            ci.storeOp = GpuStoreOp::Store;
            ci.clearR = ci.clearG = ci.clearB = ci.clearA = 0.f;
            GpuRenderPassHandle rp                        = gpu.BeginRenderPass(initCmd, &ci, 1, nullptr);
            gpu.EndRenderPass(rp);
        }
        gpu.SubmitCommandBuffer(initCmd);
    }

    // Nearest sampler
    GpuSamplerCreateInfo povSampInfo {};
    povSampInfo.minFilter = GpuFilter::Nearest;
    povSampInfo.magFilter = GpuFilter::Nearest;
    povSampInfo.mipFilter = GpuFilter::Nearest;
    povSampInfo.addressU  = GpuSamplerAddressMode::ClampToEdge;
    povSampInfo.addressV  = GpuSamplerAddressMode::ClampToEdge;
    povSampInfo.addressW  = GpuSamplerAddressMode::ClampToEdge;
    _povSampler           = gpu.CreateSampler(povSampInfo);

    // ── POV pipelines ─────────────────────────────────────────────────────────
    GpuGraphicsPipelineCreateInfo povBase {};
    povBase.vertexShader      = _povVertShader;
    povBase.fragmentShader    = _povFragShader;
    povBase.colorTargetFormat = swapchainFormat;
    povBase.hasDepthTarget    = false;

    // Decay: no blend
    povBase.blend     = {};
    _povDecayPipeline = gpu.CreateGraphicsPipeline(povBase);

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
    povBase.blend         = povBlend;
    _povCompositePipeline = gpu.CreateGraphicsPipeline(povBase);

    if (!_povDecayPipeline || !_povCompositePipeline) {
        LOG_ERROR("ParticleRenderPass: POV pipeline creation failed");
        return false;
    }

    _povNeedsClear = true;
    _povIndex      = 0;

    if (logInit)
        LOG_INFO("ParticleRenderPass '{}' initialized", _passname);
    return true;
}

void ParticleRenderPass::Release(bool logRelease) {
    IGpu &gpu = Renderer::GetGpu();
    if (_pipeline) {
        gpu.ReleaseGraphicsPipeline(_pipeline);
        _pipeline = 0;
    }
    if (_pixelPipeline) {
        gpu.ReleaseGraphicsPipeline(_pixelPipeline);
        _pixelPipeline = 0;
    }
    if (_vertShader) {
        gpu.ReleaseShader(_vertShader);
        _vertShader = 0;
    }
    if (_fragShader) {
        gpu.ReleaseShader(_fragShader);
        _fragShader = 0;
    }
    if (_povDecayPipeline) {
        gpu.ReleaseGraphicsPipeline(_povDecayPipeline);
        _povDecayPipeline = 0;
    }
    if (_povCompositePipeline) {
        gpu.ReleaseGraphicsPipeline(_povCompositePipeline);
        _povCompositePipeline = 0;
    }
    if (_povVertShader) {
        gpu.ReleaseShader(_povVertShader);
        _povVertShader = 0;
    }
    if (_povFragShader) {
        gpu.ReleaseShader(_povFragShader);
        _povFragShader = 0;
    }
    if (_povTex[0]) {
        gpu.ReleaseTexture(_povTex[0]);
        _povTex[0] = 0;
    }
    if (_povTex[1]) {
        gpu.ReleaseTexture(_povTex[1]);
        _povTex[1] = 0;
    }
    if (_povSampler) {
        gpu.ReleaseSampler(_povSampler);
        _povSampler = 0;
    }
    if (logRelease)
        LOG_INFO("ParticleRenderPass '{}' released", _passname);
}

void ParticleRenderPass::Render(GpuCmdBufferHandle cmdBuf,
    GpuTextureHandle                               target,
    const glm::mat4                               &camera) {
#ifdef __3DS__
    // Particles are disabled on 3DS (no shaders/compute); nothing to draw or resolve.
    (void)cmdBuf; (void)target; (void)camera;
    return;
#endif
    IGpu &gpu = Renderer::GetGpu();

    if (_drawQueue.empty()) {
        // No particles to draw — but if we're the MSAA resolve owner (typically the last pass on
        // the framebuffer), we must still resolve the multisampled target down to fbContent, or the
        // whole frame stays black under MSAA. A draw-less Load+Resolve pass does exactly that.
        if (renderTargetResolve != 0) {
            GpuColorTargetInfo resolveInfo {};
            resolveInfo.texture        = target;
            resolveInfo.resolveTexture = renderTargetResolve;
            resolveInfo.loadOp         = GpuLoadOp::Load;
            resolveInfo.storeOp        = GpuStoreOp::Resolve;
            GpuRenderPassHandle rp     = gpu.BeginRenderPass(cmdBuf, &resolveInfo, 1, nullptr);
            gpu.EndRenderPass(rp);
        }
        return;
    }

    // Scale surface pixels to window-logical so particles at logical (x, y) line up with sprites
    // at logical (x, y). Works for both backends: on SDL the surface is desktop-sized (so camW
    // = desktop-logical); on WebGPU the surface == window-physical (so the ratio collapses to 1
    // and camW = window-logical, matching Renderer::_updateCameraProjection's CSS-pixel contract).
    float camW = (float)_surfaceWidth * (float)Window::GetWidth() / (float)Window::GetPhysicalWidth();
    float camH = (float)_surfaceHeight * (float)Window::GetHeight() / (float)Window::GetPhysicalHeight();

    float     camScale        = Camera::GetScale();
    vf2d      camTarget       = Camera::GetTarget();
    glm::mat4 correctedCamera = glm::ortho(0.0f, camW, camH, 0.0f);
    correctedCamera           = glm::scale(correctedCamera, glm::vec3(camScale, camScale, 1.0f));
    correctedCamera           = glm::translate(correctedCamera, glm::vec3(-camTarget.x, -camTarget.y, 0.0f));

    struct VertUniforms {
        glm::mat4 camera;
        glm::vec2 screenSize;
        glm::vec2 pad;
    } vu = { correctedCamera, { camW, camH }, {} };

    struct POVUniforms {
        float gScale;
        float pad[3];
    };

    GpuBufferHandle storageBufs[2] = {
        Particles::GetParticleBuffer(),
        Particles::GetSystemBuffer()
    };

    auto drawParticles = [&](GpuRenderPassHandle rp) {
        gpu.PushVertexUniformData(cmdBuf, 0, &vu, sizeof(vu));
        GpuGraphicsPipelineHandle activePipeline = 0;
        for (const auto &cmd : _drawQueue) {
            GpuGraphicsPipelineHandle needed = cmd.pixelMode ? _pixelPipeline : _pipeline;
            if (needed != activePipeline) {
                gpu.BindGraphicsPipeline(rp, needed);
                gpu.BindVertexStorageBuffers(rp, 0, storageBufs, 2);
                activePipeline = needed;
            }
            GpuTextureSamplerBinding sb = {
                cmd.texture ? cmd.texture : Particles::GetWhiteTexture(),
                cmd.sampler ? cmd.sampler : Particles::GetLinearSampler(),
            };
            gpu.BindFragmentSamplers(rp, 0, &sb, 1);
            gpu.DrawPrimitives(rp, 6, cmd.maxParticles, 0, cmd.particleOffset);
        }
    };

    if (_povEnabled && _povDecayPipeline && _povCompositePipeline) {
        uint32_t         curr    = _povIndex;
        uint32_t         prev    = 1u - curr;
        GpuTextureHandle povCurr = _povTex[curr];
        GpuTextureHandle povPrev = _povTex[prev];

        // ── 1. Decay pass: fade previous accumulation into current ────────────
        if (!_povNeedsClear) {
            GpuColorTargetInfo decayInfo {};
            decayInfo.texture      = povCurr;
            decayInfo.loadOp       = GpuLoadOp::DontCare;
            decayInfo.storeOp      = GpuStoreOp::Store;
            GpuRenderPassHandle rp = gpu.BeginRenderPass(cmdBuf, &decayInfo, 1, nullptr);
            gpu.BindGraphicsPipeline(rp, _povDecayPipeline);
            GpuTextureSamplerBinding sb = { povPrev, _povSampler };
            gpu.BindFragmentSamplers(rp, 0, &sb, 1);
            POVUniforms pu = { _povDecay, {} };
            gpu.PushVertexUniformData(cmdBuf, 0, &pu, sizeof(pu));
            gpu.DrawPrimitives(rp, 3, 1, 0, 0);
            gpu.EndRenderPass(rp);
        }

        // ── 2. Particle render into POV texture ───────────────────────────────
        {
            GpuColorTargetInfo particleInfo {};
            particleInfo.texture   = povCurr;
            particleInfo.loadOp    = _povNeedsClear ? GpuLoadOp::Clear : GpuLoadOp::Load;
            particleInfo.storeOp   = GpuStoreOp::Store;
            GpuRenderPassHandle rp = gpu.BeginRenderPass(cmdBuf, &particleInfo, 1, nullptr);
            drawParticles(rp);
            gpu.EndRenderPass(rp);
            _povNeedsClear = false;
        }

        // ── 3. Composite POV texture onto swapchain ───────────────────────────
        {
            GpuColorTargetInfo compositeInfo {};
            compositeInfo.texture  = target;
            compositeInfo.loadOp   = GpuLoadOp::Load;
            compositeInfo.storeOp  = GpuStoreOp::Store;
            GpuRenderPassHandle rp = gpu.BeginRenderPass(cmdBuf, &compositeInfo, 1, nullptr);
            gpu.BindGraphicsPipeline(rp, _povCompositePipeline);
            GpuTextureSamplerBinding sb = { povCurr, _povSampler };
            gpu.BindFragmentSamplers(rp, 0, &sb, 1);
            POVUniforms pu = { 1.0f, {} };
            gpu.PushVertexUniformData(cmdBuf, 0, &pu, sizeof(pu));
            gpu.DrawPrimitives(rp, 3, 1, 0, 0);
            gpu.EndRenderPass(rp);
        }

        _povIndex ^= 1u;

    } else {
        // Standard path — render into the framebuffer. Under MSAA this pass is typically last, so
        // it owns the resolve of the multisampled target down to the single-sample fbContent that
        // the final blit reads. Without this the whole frame stays black under MSAA.
        GpuColorTargetInfo colorInfo {};
        colorInfo.texture        = target;
        colorInfo.resolveTexture = renderTargetResolve;
        colorInfo.loadOp         = GpuLoadOp::Load;
        colorInfo.storeOp        = (renderTargetResolve != 0) ? GpuStoreOp::Resolve : GpuStoreOp::Store;
        GpuRenderPassHandle rp   = gpu.BeginRenderPass(cmdBuf, &colorInfo, 1, nullptr);
        drawParticles(rp);
        gpu.EndRenderPass(rp);
    }
}

void ParticleRenderPass::SetPOV(bool enabled, float decay) {
    if (enabled && !_povEnabled)
        _povNeedsClear = true; // flush stale accumulation on re-enable
    _povEnabled = enabled;
    _povDecay   = decay;
}
