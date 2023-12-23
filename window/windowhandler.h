#pragma once

#include <memory>
#include <string>
#include <optional>

#include "SDL3/SDL.h"
#include "SDL3_image/SDL_image.h"

#include "utils/vectors.h"
#include "utils/lerp.h"
#include "input/inputhandler.h"

#include <chrono>
#include "utils/colors.h"

#ifdef ADD_IMGUI
    #include "imgui.h"
    #include "imgui_impl_sdlrenderer3.h"
    #include "imgui_impl_sdl3.h"

    #ifdef WIN32
    #include "imgui_impl_win32.h"
    #endif
#endif

// SDL Forward Declarations
struct SDL_Window;

union SDL_Event;

using SDL_Window_deleter = void (*)(SDL_Window *);

class Window {
public:


    static void InitWindow(const std::string &title, int width = 800, int height = 600, unsigned int flags = 0)
    {
        get()._initWindow(title, width, height, flags);
    }
    static void Close() { get()._close(); }

    static SDL_Window* GetWindow() { return get()._getWindow(); }
    static SDL_Renderer *GetRenderer() { return get()._getRenderer(); };

    static void SetSize(int width, int height) { get()._setSize(width, height); }
    static vf2d GetSize() { return get()._getSize(); }
    static int GetWidth() { return get()._getSize().x; }
    static int GetHeight() { return get()._getSize().y; }

    static void StartFrame() { get()._startFrame(); }
    static void EndFrame() { get()._endFrame(); }

    static void ClearBackground(Color color) { get()._clearBackground(color); }

    static void ToggleFullscreen() { get()._toggleFullscreen(); }
    static bool IsFullscreen() { return get()._isFullscreen(); }
    static double GetRunTime() { return duration_cast<std::chrono::milliseconds >( get()._currentTime - get()._startTime ).count() / 1000.; }

    static bool ShouldQuit() { return get()._shouldQuit; }
    static double GetFrameTime() { return get()._lastFrameTime; }
    static int GetFPS(float milliseconds = 400.f) { return get()._getFPS(milliseconds); }

    static void HandleInput() { get()._handleInput(); }

private:
    std::unique_ptr<SDL_Window, SDL_Window_deleter> m_window;

    double _getRunTime();
    void _initWindow(const std::string &title, int width = 800, int height = 600, unsigned int flags = 0);
    void _close();
    void _toggleFullscreen();
    bool _isFullscreen();
    SDL_Renderer *_getRenderer();
    vf2d _getSize();
    void _handleInput();
    int _getFPS(float milliseconds);

    SDL_Window* _getWindow();
    void _setSize(int width, int height);
    void _startFrame();
    void _clearBackground(Color color);
    void _endFrame();

    bool _shouldQuit = false;

    int _frameCount;
    int _fps;
    double _lastFrameTime;
    double _fpsAccumulator;
    std::chrono::high_resolution_clock::time_point _startTime;
    std::chrono::high_resolution_clock::time_point _currentTime;
    std::chrono::high_resolution_clock::time_point _previousTime;
#ifdef ADD_IMGUI
    void SetupImGuiStyle();
#endif
public:
    Window(const Window&) = delete;
    static Window& get() { static Window instance; return instance; }
private:
    Window() : m_window(nullptr, SDL_DestroyWindow)
    {
        _fps = 0;
        _frameCount = 0;
        _startTime = std::chrono::high_resolution_clock::now();
        _currentTime = _startTime;

        _fpsAccumulator = 0.f;
    };
};