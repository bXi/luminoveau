#pragma once

#include <memory>
#include <string>
#include <optional>

#include "SDL3/SDL.h"

#include "utils/vectors.h"
#include "utils/lerp.h"
#include "input/inputhandler.h"

#include <chrono>


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

    static vf2d GetSize() {
        int w, h;

        SDL_GetWindowSize(get().m_window.get(), &w, &h);

        return {(float)w, (float)h};
    }

    static void SetSize(int width, int height) {
        SDL_SetWindowSize(get().m_window.get(), width, height);
    }

    static void StartFrame() {
        Lerp::updateLerps();

        Window::HandleInput();
        SDL_SetRenderDrawColor(GetRenderer(), 0, 0,0,255);
        SDL_RenderClear(GetRenderer());
    }

    static void EndFrame() {
        SDL_RenderPresent(Window::GetRenderer());



        get()._frameCount++;

        get()._previousTime = get()._currentTime;
        get()._currentTime = std::chrono::high_resolution_clock::now();

        get()._lastFrameTime = duration_cast<std::chrono::nanoseconds >( get()._currentTime - get()._previousTime ).count() / 1000000000.;

        get()._fpsAccumulator += get()._lastFrameTime;

    }
    static void ToggleFullscreen()
    {
        get()._toggleFullscreen();
    }



    static double GetRunTime() { return duration_cast<std::chrono::seconds >( get()._currentTime - get()._startTime ).count(); }
    static int GetWidth() { return get()._getWindowSize().x; }
    static int GetHeight() { return get()._getWindowSize().y; }
    static bool ShouldQuit() { return get()._shouldQuit; }

    static vf2d GetMousePosition()
    {
        float xMouse, yMouse;

        SDL_GetMouseState(&xMouse,&yMouse);

        return {xMouse, yMouse};
    }

    static double GetFrameTime()
    {
        return get()._lastFrameTime;
    }

    static int GetFPS(float milliseconds = 400.f) {

        return get()._getFPS(milliseconds);
    }



    static std::optional<SDL_Event> pollEvent();

    static void HandleInput() {

        Input::Update();
        std::optional<SDL_Event> event;
        while ((event = Window::pollEvent()))
            switch (event->type) {
                case SDL_EventType::SDL_EVENT_QUIT:
                    get()._shouldQuit = true;
                    break;
            }

    }

    static void Close() {
        get()._onCloseWindow();
    }

    static SDL_Renderer *GetRenderer() { return get()._getRenderer(); };

private:
    std::unique_ptr<SDL_Window, SDL_Window_deleter> m_window;


    bool _shouldQuit = false;
    double _getRunTime();
    void _initWindow(const std::string &title, int width = 800, int height = 600, unsigned int flags = 0);
    void _onCloseWindow();
    vi2d _getWindowSize();
    void _toggleFullscreen();
    SDL_Renderer *_getRenderer();


    int _frameCount;
    int _fps;
    double _lastFrameTime;

    double _fpsAccumulator;
    std::chrono::high_resolution_clock::time_point _startTime;

    std::chrono::high_resolution_clock::time_point _currentTime;
    std::chrono::high_resolution_clock::time_point _previousTime;

    int _getFPS(float milliseconds);


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