#pragma once

#include <cstdint>
#include <algorithm>
#include <initializer_list>
#include <glm/glm.hpp>

#include "gpu/types.h"

// ─────────────────────────────────────────────────────────────────────────────
// GPU-side structs (std430 layout — must stay in sync with .comp / .vert)
// ─────────────────────────────────────────────────────────────────────────────

// 48 bytes, std430-compatible
struct alignas(16) GPUParticle {
    glm::vec4 posAndLife;       // xyz = world pos,  w = remaining life (<=0 → dead)
    glm::vec4 velAndMaxLife;    // xyz = velocity,   w = maxLife (for t lerp)
    uint32_t  systemID;         // index into system buffer
    float     respawnTimer;     // counts down; when <=0 and emitting → respawn
    float     startSize;        // billboard size at birth (pixels)
    float     endSize;          // billboard size at death (pixels)
};
static_assert(sizeof(GPUParticle) == 48, "GPUParticle size mismatch");

// 176 bytes, std430-compatible
struct alignas(16) GPUParticleSystem {
    glm::vec4 spawnPos;         // xyz = origin,     w = spawn radius
    glm::vec4 spawnVel;         // xyz = base vel,   w = velocity spread
    glm::vec4 gravityAndDrag;   // xyz = gravity,    w = drag coefficient
    glm::vec4 colors[4];        // up to 4 color stops (rgba)
    glm::vec4 colorPositions;   // t-position of each stop in [0,1]; -1 = unused
    float     sizeStartMin;     // birth size range
    float     sizeStartMax;
    float     sizeEndMin;       // death size range
    float     sizeEndMax;
    float     sizeStartBias;    // 1.0 = uniform, >1 = skew toward min
    float     sizeEndBias;
    float     lifetimeMin;      // particle lifetime range (seconds)
    float     lifetimeMax;
    float     lifetimeBias;     // 1.0 = uniform, >1 = skew toward min
    float     emitRate;         // particles per second
    uint32_t  flags;            // bit 0 = emitting
    uint32_t  shapeType;        // see ParticleShape enum
};
static_assert(sizeof(GPUParticleSystem) == 176, "GPUParticleSystem size mismatch");

// ─────────────────────────────────────────────────────────────────────────────
// Shape types
// ─────────────────────────────────────────────────────────────────────────────

enum class ParticleShape : uint32_t {
    SoftCircle = 0,   // smooth radial falloff (default)
    HardCircle = 1,   // crisp circle, no fade
    Square     = 2,   // solid square billboard
    SoftSquare = 3,   // square with softened edges
    Ring       = 4,   // hollow circle / donut
    Textured   = 5,   // sample a per-system texture; set via ParticleSystemConfig::texture
};

// ─────────────────────────────────────────────────────────────────────────────
// CPU-side configuration (converted to GPUParticleSystem internally)
// ─────────────────────────────────────────────────────────────────────────────

struct ParticleSystemConfig {
    uint32_t  maxParticles   = 1000;

    glm::vec3 spawnPosition  = {0.0f, 0.0f, 0.0f};
    float     spawnRadius    = 0.0f;

    glm::vec3 spawnVelocity  = {0.0f, -120.0f, 0.0f};
    float     velocitySpread = 60.0f;

    glm::vec3 gravity        = {0.0f, 200.0f, 0.0f};
    float     drag           = 0.5f;

    float     lifetimeMin    = 2.0f;
    float     lifetimeMax    = 2.0f;
    float     lifetimeBias   = 1.0f;    // 1.0 = uniform; >1 = more toward min
    float     emitRate       = 500.0f;

    // Color gradient — use SetColors() rather than setting these directly.
    // Defaults to a single opaque white stop.
    glm::vec4 colors[4]        = {
        {1.0f, 1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    glm::vec4 colorPositions = {0.0f, -1.0f, -1.0f, -1.0f};

    // Configure up to 4 color stops.
    //   SetColors({orange, red})              — 2 stops, auto-spaced at 0 and 1
    //   SetColors({orange, yellow, red})      — 3 stops, auto-spaced at 0, 0.5, 1
    //   SetColors({a, b, c}, {0, 0.2f, 1})   — 3 stops at explicit positions
    void SetColors(std::initializer_list<glm::vec4> colorList,
                   std::initializer_list<float>     positions = {})
    {
        int n = std::min((int)colorList.size(), 4);
        int i = 0;
        for (const auto& c : colorList) {
            if (i >= 4) break;
            colors[i++] = c;
        }

        colorPositions = glm::vec4(-1.0f);

        if (positions.size() == 0) {
            for (int j = 0; j < n; ++j)
                colorPositions[j] = (n <= 1) ? 0.0f : (float)j / (float)(n - 1);
        } else {
            int j = 0;
            for (float p : positions) {
                if (j >= n) break;
                colorPositions[j++] = p;
            }
            // Colors without an explicit position are treated as unused (position stays -1)
        }
    }

    // Size — constant by default (startMin == startMax, endMin == endMax)
    float     sizeStartMin   = 6.0f;
    float     sizeStartMax   = 6.0f;
    float     sizeStartBias  = 1.0f;   // 1.0 = uniform; >1 = skew toward min
    float     sizeEndMin     = 6.0f;
    float     sizeEndMax     = 6.0f;
    float     sizeEndBias    = 1.0f;

    ParticleShape shape      = ParticleShape::SoftCircle;

    // When true the system is rendered with standard alpha blending
    // (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) instead of additive.  Combine with
    // shape = Square and size = 1 to render each particle as a single opaque pixel.
    bool          pixelMode  = false;

    // Optional texture — when set, overrides shape with Textured rendering.
    // The colour gradient acts as a per-particle tint multiplied over the image.
    // Set sampler to 0 to let the particle system pick a linear sampler automatically.
    GpuTextureHandle texture = 0;
    GpuSamplerHandle sampler = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Opaque handle returned to the caller
// ─────────────────────────────────────────────────────────────────────────────

struct ParticleSystemHandle {
    uint32_t systemIndex    = 0;
    uint32_t particleOffset = 0;
    uint32_t maxParticles   = 0;
    bool     valid          = false;
};

/// Opaque handle to a user-supplied compute pipeline used as a custom particle
/// update. Create with Particles::CreateCustomCompute(), destroy with
/// Particles::DestroyCustomCompute(). Can be shared across multiple systems.
struct ParticleComputeHandle {
    uint32_t index = 0;
    bool     valid = false;
};
