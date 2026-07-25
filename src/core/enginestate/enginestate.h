#pragma once

#include <chrono>

namespace EngineState {

/// @cond INTERNAL
// from Window::
inline int   scaleFactor  = 1;
inline float displayScale = 1.0f; // HiDPI scale factor (e.g. 2.0 on Retina)

// Actual swapchain texture dimensions from the last acquire (the size we truly present
// into). Authoritative for viewport/blit -- SDL_GetWindowSizeInPixels can disagree on
// Wayland fractional scaling. 0 = not acquired yet (callers fall back to the SDL size).
inline int  swapchainWidth   = 0;
inline int  swapchainHeight  = 0;
inline bool shouldQuit       = false;
inline int  frameCount       = 0;
inline bool debugMenuVisible = false;

// from Window:: for fps calculations
inline int    fps            = 0;
inline double lastFrameTime  = 0.0;
inline double fpsAccumulator = 0.0;
// Monotonic count of successful swapchain presents — Window::_getFPS samples this
// over the caller's requested window to compute the actual frames-per-second.
inline uint64_t                                       presentCount = 0;
inline std::chrono::high_resolution_clock::time_point startTime;
inline std::chrono::high_resolution_clock::time_point currentTime;
inline std::chrono::high_resolution_clock::time_point previousTime;
/// @endcond
} // namespace EngineState
