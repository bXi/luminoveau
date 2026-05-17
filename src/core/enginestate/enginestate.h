#pragma once

#include <chrono>

namespace EngineState {

    //from Window::
    inline int   _scaleFactor      = 1;
    inline float _displayScale     = 1.0f;  // HiDPI scale factor (e.g. 2.0 on Retina)
    inline bool  _shouldQuit       = false;
    inline int  _frameCount       = 0;
    inline bool _debugMenuVisible = false;

    //from Window:: for fps calculations
    inline int                                            _fps            = 0;
    inline double                                         _lastFrameTime  = 0.0;
    inline double                                         _fpsAccumulator = 0.0;
    inline std::chrono::high_resolution_clock::time_point _startTime;
    inline std::chrono::high_resolution_clock::time_point _currentTime;
    inline std::chrono::high_resolution_clock::time_point _previousTime;
}
