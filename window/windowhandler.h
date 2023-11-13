#pragma once

#include <memory>
#include <string>
#include <optional>

#include "SDL3/SDL.h"

#include "utils/vectors.h"
#include "input/inputhandler.h"

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

    static void SetSize(int width, int height) {

    }

    static void StartFrame() {
        Window::HandleInput();
        SDL_SetRenderDrawColor(GetRenderer(), 0, 0,0,255);
        SDL_RenderClear(GetRenderer());
    }

    static void EndFrame() {
                SDL_RenderPresent(Window::GetRenderer());
    }
    static void ToggleFullscreen()
    {
        get()._toggleFullscreen();
    }

    static std::optional<SDL_Event> pollEvent();

    static double GetRunTime() { return get()._getRunTime(); }
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
        //TODO: actually calculate fps here
        return 1./60.;
    }

    static int GetFPS() {
        //TODO: actually calculate fps here
        return 60;
    }

    static void HandleInput() {
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
    bool _shouldQuit = false;
    std::unique_ptr<SDL_Window, SDL_Window_deleter> m_window;
    double _getRunTime();
    void _initWindow(const std::string &title, int width = 800, int height = 600, unsigned int flags = 0);
    void _onCloseWindow();
    vi2d _getWindowSize();
    void _toggleFullscreen();
    SDL_Renderer *_getRenderer();
public:
    Window(const Window&) = delete;
    static Window& get() { static Window instance; return instance; }
private:
    Window() : m_window(nullptr, SDL_DestroyWindow)
    {

    };
};