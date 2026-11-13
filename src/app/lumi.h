#pragma once

#include <cstddef>
#include <cstdint>

#include "config.h"

namespace Lumi {
/// @brief Result returned from app lifecycle callbacks to drive the main loop.
enum class Result {
    Continue = 0, ///< Keep running; proceed to the next frame.
    Success  = 1, ///< Exit the app reporting success.
    Failure  = 2  ///< Exit the app reporting failure.
};

/// @brief What the engine sets up at Window::InitWindow, per app. The defaults are the
///        engine-wide ones from config.h, so an app only names what it changes.
struct Config {
    bool     particles    = true;                     ///< Create the particle system (GPU buffers, compute, render pass).
    uint32_t maxParticles = LUMINOVEAU_MAX_PARTICLES; ///< Particle slots across all systems (when particles is on).
    size_t   maxSprites   = MAX_SPRITES;              ///< Sprite batch capacity of the primary 2D pass ("2dsprites").
};

/// @cond INTERNAL
inline Config configValue;
inline bool   configLocked = false; // set by Window::InitWindow
/// @endcond

/// @brief Sets the engine config. Call before Window::InitWindow; returns false (and changes
///        nothing) once the window exists.
inline bool Configure(const Config &config) {
    if (configLocked)
        return false;
    configValue = config;
    return true;
}

/// @brief The config in use.
inline const Config &GetConfig() { return configValue; }
} // namespace Lumi
