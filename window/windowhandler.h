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

    static SDL_Window* GetWindow() {
        return get().m_window.get();
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
    #ifdef ADD_IMGUI
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    #endif
    }

    static void ClearBackground(Color color) {
        SDL_SetRenderDrawColor(GetRenderer(), color.r, color.g,color.b,color.a);
        SDL_RenderClear(GetRenderer());
    }

    static void EndFrame() {
        #ifdef ADD_IMGUI
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData());
        #endif
        SDL_RenderPresent(Window::GetRenderer());

        get()._frameCount++;
        get()._previousTime = get()._currentTime;
        get()._currentTime = std::chrono::high_resolution_clock::now();
        get()._lastFrameTime = duration_cast<std::chrono::nanoseconds >( get()._currentTime - get()._previousTime ).count() / 1000000000.;
        get()._fpsAccumulator += get()._lastFrameTime;
    }
    static void ToggleFullscreen() { get()._toggleFullscreen(); }
    static bool IsFullscreen() { return get()._isFullscreen(); }
    static double GetRunTime() { return duration_cast<std::chrono::milliseconds >( get()._currentTime - get()._startTime ).count() / 1000.; }
    static int GetWidth() { return get()._getWindowSize().x; }
    static int GetHeight() { return get()._getWindowSize().y; }
    static bool ShouldQuit() { return get()._shouldQuit; }
    static double GetFrameTime() { return get()._lastFrameTime; }
    static int GetFPS(float milliseconds = 400.f) { return get()._getFPS(milliseconds); }

    static void HandleInput() {
        Input::Update();

        SDL_Event event;


        while (SDL_PollEvent(&event)) {
            #ifdef ADD_IMGUI
            ImGui_ImplSDL3_ProcessEvent(&event);
            #endif

            switch (event.type) {
                case SDL_EventType::SDL_EVENT_QUIT:
                    get()._shouldQuit = true;
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                {
                    Input::UpdateMousePos((float)event.motion.x, (float)event.motion.y);
                }
            }
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
    bool _isFullscreen();
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