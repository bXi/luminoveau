#include "windowhandler.h"

#include <stdexcept>
#include "audio/audiohandler.h"
void Window::_initWindow(const std::string &title, int width, int height, unsigned int flags) {

    // SDL_InitSubSystem is ref-counted
    SDL_InitSubSystem(SDL_InitFlags::SDL_INIT_VIDEO);
    auto window = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (window) {
        m_window.reset(window);
    } else {
        throw std::runtime_error(SDL_GetError());
    }

    SDL_CreateRenderer(window, "opengl", SDL_RENDERER_ACCELERATED);

    SDL_SetRenderDrawBlendMode(_getRenderer(), SDL_BLENDMODE_BLEND);

    #ifdef ADD_IMGUI
    ImGui::CreateContext();

    ImGui_ImplSDL3_InitForSDLRenderer(window, GetRenderer());
    ImGui_ImplSDLRenderer3_Init(GetRenderer());
    #endif

}

void Window::_onCloseWindow() {
    // SDL_QuitSubSystem is ref-counted
    Audio::Close();
    SDL_Quit();
    IMG_Quit();
}


double Window::_getRunTime() {
    return (double) SDL_GetTicks() * 1000.0;
}

vi2d Window::_getWindowSize() {
    int w, h;

    auto *window = get().m_window.get();
    SDL_GetWindowSize(window, &w, &h);

    return {w, h};
}

void Window::_toggleFullscreen() {
    auto *window = get().m_window.get();

    auto fullscreenFlag = SDL_WINDOW_FULLSCREEN;
    bool isFullscreen = SDL_GetWindowFlags(window) & fullscreenFlag;

    SDL_SetWindowFullscreen(window, isFullscreen);
}

SDL_Renderer *Window::_getRenderer() {
    auto *window = get().m_window.get();
    return SDL_GetRenderer(window);
}

int Window::_getFPS(float milliseconds) {
    auto seconds = milliseconds / 1000.f;

    if (get()._fpsAccumulator > seconds) {
        get()._fpsAccumulator -= seconds;

        get()._fps = (int)(1. / get()._lastFrameTime);
    }
    return get()._fps;
}

bool Window::_isFullscreen() {
    auto *window = get().m_window.get();

    auto flag = SDL_GetWindowFlags(window);
	auto is_fullscreen  = flag&SDL_WINDOW_FULLSCREEN;
    return is_fullscreen == SDL_WINDOW_FULLSCREEN;
}

