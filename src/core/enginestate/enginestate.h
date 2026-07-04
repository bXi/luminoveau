#pragma once

#include <chrono>

namespace EngineState {

    /// @cond INTERNAL
    //from Window::
    inline int   _scaleFactor      = 1;
    inline float _displayScale     = 1.0f;  // HiDPI scale factor (e.g. 2.0 on Retina)

    // Actual swapchain texture dimensions from the last acquire (the size we truly present
    // into). Authoritative for viewport/blit -- SDL_GetWindowSizeInPixels can disagree on
    // Wayland fractional scaling. 0 = not acquired yet (callers fall back to the SDL size).
    inline int   _swapchainWidth   = 0;
    inline int   _swapchainHeight  = 0;
    inline bool  _shouldQuit       = false;
    inline int  _frameCount       = 0;
    inline bool _debugMenuVisible = false;

    //from Window:: for fps calculations
    inline int                                            _fps            = 0;
    inline double                                         _lastFrameTime  = 0.0;
    inline double                                         _fpsAccumulator = 0.0;
    // Monotonic count of successful swapchain presents — Window::_getFPS samples this
    // over the caller's requested window to compute the actual frames-per-second.
    inline uint64_t                                       _presentCount   = 0;
    inline std::chrono::high_resolution_clock::time_point _startTime;
    inline std::chrono::high_resolution_clock::time_point _currentTime;
    inline std::chrono::high_resolution_clock::time_point _previousTime;
    /// @endcond
}
