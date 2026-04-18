#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <SDL3/SDL.h>

// ─────────────────────────────────────────────────────────────────────────────
// GPU-side structs (std430 layout — must stay in sync with .comp / .vert)
// ─────────────────────────────────────────────────────────────────────────────

// 48 bytes, std430-compatible
struct alignas(16) GPUParticle {
    glm::vec4 posAndLife;       // xyz = world pos,  w = remaining life (<=0 → dead)
    glm::vec4 velAndMaxLife;    // xyz = velocity,   w = maxLife (for color lerp)
    uint32_t  systemID;         // index into system buffer
    float     respawnTimer;     // counts down; when <=0 and emitting → respawn
    float     _pad0;
    float     _pad1;
};
static_assert(sizeof(GPUParticle) == 48, "GPUParticle size mismatch");

// 96 bytes, std430-compatible
struct alignas(16) GPUParticleSystem {
    glm::vec4 spawnPos;         // xyz = origin,     w = spawn radius
    glm::vec4 spawnVel;         // xyz = base vel,   w = velocity spread
    glm::vec4 gravityAndDrag;   // xyz = gravity,    w = drag coefficient
    glm::vec4 colorStart;       // rgba at birth
    glm::vec4 colorEnd;         // rgba at death
    float     emitRate;         // particles per second (controls respawn delay)
    float     lifetime;         // seconds a particle lives after spawn
    uint32_t  flags;            // bit 0 = emitting
    float     size;             // billboard radius in pixels
};
static_assert(sizeof(GPUParticleSystem) == 96, "GPUParticleSystem size mismatch");

// ─────────────────────────────────────────────────────────────────────────────
// CPU-side configuration (human-friendly; converted to GPUParticleSystem internally)
// ─────────────────────────────────────────────────────────────────────────────

struct ParticleSystemConfig {
    uint32_t  maxParticles   = 1000;

    glm::vec3 spawnPosition  = {0.0f, 0.0f, 0.0f};
    float     spawnRadius    = 0.0f;

    glm::vec3 spawnVelocity  = {0.0f, -120.0f, 0.0f};
    float     velocitySpread = 60.0f;

    glm::vec3 gravity        = {0.0f, 200.0f, 0.0f};
    float     drag           = 0.5f;

    float     lifetime       = 2.0f;         // seconds
    float     emitRate       = 500.0f;       // particles / second

    glm::vec4 colorStart     = {1.0f, 0.8f, 0.3f, 1.0f};
    glm::vec4 colorEnd       = {1.0f, 0.1f, 0.0f, 0.0f};

    float     size           = 6.0f;         // pixels
};

// ─────────────────────────────────────────────────────────────────────────────
// Opaque handle returned to the caller
// ─────────────────────────────────────────────────────────────────────────────

struct ParticleSystemHandle {
    uint32_t systemIndex    = 0;   // slot in the GPU system data buffer
    uint32_t particleOffset = 0;   // first particle index in the global buffer
    uint32_t maxParticles   = 0;
    bool     valid          = false;
};
