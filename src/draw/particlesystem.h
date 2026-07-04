#pragma once

#include <cstdint>
#include <algorithm>
#include <initializer_list>
#include <glm/glm.hpp>

#include "gpu/types.h"

// ─────────────────────────────────────────────────────────────────────────────
// GPU-side structs (std430 layout — must stay in sync with .comp / .vert)
// ─────────────────────────────────────────────────────────────────────────────

// 64 bytes, std430-compatible
struct alignas(16) GPUParticle {
    glm::vec4 posAndLife;       // xyz = world pos,  w = remaining life (<=0 → dead)
    glm::vec4 velAndMaxLife;    // xyz = velocity,   w = maxLife (for t lerp)
    uint32_t  systemID;         // index into system buffer
    float     respawnTimer;     // counts down; when <=0 and emitting → respawn
    float     startSize;        // billboard size at birth (pixels)
    float     endSize;          // billboard size at death (pixels)
    float     angle;            // current rotation (radians)
    float     angularVelocity;  // radians per second; set on spawn, persists
    float     _pad0;
    float     _pad1;
};
static_assert(sizeof(GPUParticle) == 64, "GPUParticle size mismatch");

// 192 bytes, std430-compatible
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
    uint32_t  flags;            // bit 0 = emitting, bit 1 = custom compute
    uint32_t  shapeType;        // see ParticleShape enum
    float     angVelMin;        // angular velocity range (rad/s); can be negative
    float     angVelMax;
    float     angVelBias;       // 1.0 = uniform, >1 = skew toward min magnitude
    float     trailStretch;     // >0 = elongate billboard along velocity; 0 = off
};
static_assert(sizeof(GPUParticleSystem) == 192, "GPUParticleSystem size mismatch");

// ─────────────────────────────────────────────────────────────────────────────
// Collider types
// ─────────────────────────────────────────────────────────────────────────────

enum class ColliderType : uint32_t {
    HalfPlane = 0,  // params: (nx, ny, d, 0)  — normal points away from solid; solid where dot(pos,n)<d
    Circle    = 1,  // params: (cx, cy, r, 0)  — particles bounce off outside
};

// 32 bytes, std430-compatible
struct alignas(16) GPUCollider {
    glm::vec4 params;       // type-specific (see ColliderType)
    float     restitution;  // bounce factor [0,1]: 1=elastic, 0=inelastic
    float     friction;     // tangential damping [0,1]: 0=frictionless
    uint32_t  type;         // ColliderType
    uint32_t  enabled;      // 1 = active
};
static_assert(sizeof(GPUCollider) == 32, "GPUCollider size mismatch");

/// @brief Opaque handle to a collider registered on a particle system.
struct ColliderHandle {
    uint32_t index = 0;      ///< Slot index of the collider within its system.
    bool     valid = false;  ///< True if the handle refers to a live collider.
};

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

/**
 * @brief CPU-side configuration for a particle system.
 *
 * Fill this in and pass it to Particles::CreateSystem(). Values are converted to the
 * packed GPU representation internally, so you never touch the GPU structs directly.
 * Ranges (min/max) are sampled per particle at spawn; the matching *Bias skews the
 * random pick (1.0 = uniform, >1 = toward the min).
 */
struct ParticleSystemConfig {
    uint32_t  maxParticles   = 1000;               ///< Maximum live particles the system can hold.

    glm::vec3 spawnPosition  = {0.0f, 0.0f, 0.0f}; ///< World-space emitter origin.
    float     spawnRadius    = 0.0f;               ///< Particles spawn within this radius of the origin.

    glm::vec3 spawnVelocity  = {0.0f, -120.0f, 0.0f}; ///< Base velocity given to each particle at birth.
    float     velocitySpread = 60.0f;              ///< Random deviation added to the base velocity.

    glm::vec3 gravity        = {0.0f, 200.0f, 0.0f}; ///< Constant acceleration applied each frame.
    float     drag           = 0.5f;               ///< Velocity damping coefficient (higher = more slowdown).

    float     lifetimeMin    = 2.0f;               ///< Minimum particle lifetime in seconds.
    float     lifetimeMax    = 2.0f;               ///< Maximum particle lifetime in seconds.
    float     lifetimeBias   = 1.0f;               ///< Lifetime range bias; 1.0 = uniform, >1 = toward min.
    float     emitRate       = 500.0f;             ///< Particles emitted per second while emitting.

    /// Color gradient stops (RGBA). Prefer SetColors() over setting these directly; defaults to one opaque white stop.
    glm::vec4 colors[4]        = {
        {1.0f, 1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    glm::vec4 colorPositions = {0.0f, -1.0f, -1.0f, -1.0f}; ///< t-position of each color stop in [0,1]; -1 = unused.

    /**
     * @brief Sets up to 4 color gradient stops.
     *
     * Examples:
     *   - `SetColors({orange, red})` — 2 stops, auto-spaced at 0 and 1
     *   - `SetColors({orange, yellow, red})` — 3 stops, auto-spaced at 0, 0.5, 1
     *   - `SetColors({a, b, c}, {0, 0.2f, 1})` — 3 stops at explicit positions
     *
     * @param colorList Up to 4 colors (extras ignored).
     * @param positions Optional explicit t-positions; if empty, stops are auto-spaced.
     */
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

    float     sizeStartMin   = 6.0f;   ///< Minimum birth size in pixels (constant when min == max).
    float     sizeStartMax   = 6.0f;   ///< Maximum birth size in pixels.
    float     sizeStartBias  = 1.0f;   ///< Birth-size range bias; 1.0 = uniform, >1 = toward min.
    float     sizeEndMin     = 6.0f;   ///< Minimum death size in pixels.
    float     sizeEndMax     = 6.0f;   ///< Maximum death size in pixels.
    float     sizeEndBias    = 1.0f;   ///< Death-size range bias; 1.0 = uniform, >1 = toward min.

    ParticleShape shape      = ParticleShape::SoftCircle; ///< Billboard shape (ignored when a texture is set).

    /// Minimum angular velocity in rad/s applied per particle at spawn; may be negative.
    float     angVelMin      = 0.0f;
    /// Maximum angular velocity in rad/s; set min < 0 and max > 0 for bidirectional spin.
    float     angVelMax      = 0.0f;
    float     angVelBias     = 1.0f;   ///< Angular-velocity range bias; 1.0 = uniform, >1 = toward min magnitude.

    /**
     * @brief Stretches the billboard along the velocity vector, proportional to speed.
     *
     * 0 = off (default); 0.05–0.2 suits fire/spark trails. When active the particle
     * orients along its velocity and angular rotation is ignored.
     */
    float     trailStretch   = 0.0f;

    /**
     * @brief Renders with standard alpha blending instead of additive.
     *
     * (SRC_ALPHA, ONE_MINUS_SRC_ALPHA). Combine with shape = Square and size = 1 to
     * render each particle as a single opaque pixel.
     */
    bool          pixelMode  = false;

    /// Optional texture; when set, overrides shape with Textured rendering and the color gradient tints it.
    GpuTextureHandle texture = 0;
    /// Sampler for the texture; leave 0 to let the system auto-pick a linear sampler.
    GpuSamplerHandle sampler = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Opaque handle returned to the caller
// ─────────────────────────────────────────────────────────────────────────────

/// @brief Opaque handle to a live particle system, returned by Particles::CreateSystem().
struct ParticleSystemHandle {
    uint32_t systemIndex    = 0;   ///< Index of the system in the GPU system buffer.
    uint32_t particleOffset = 0;   ///< Offset of this system's particles in the shared particle buffer.
    uint32_t maxParticles   = 0;   ///< Number of particle slots reserved for this system.
    bool     valid          = false; ///< True if the handle refers to a live system.
};

/**
 * @brief Opaque handle to a user-supplied compute pipeline used as a custom particle update.
 *
 * Create with Particles::CreateCustomCompute(), destroy with Particles::DestroyCustomCompute().
 * Can be shared across multiple systems.
 */
struct ParticleComputeHandle {
    uint32_t index = 0;      ///< Slot index of the custom compute pipeline.
    bool     valid = false;  ///< True if the handle refers to a live pipeline.
};
