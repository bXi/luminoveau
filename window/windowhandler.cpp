#include "windowhandler.h"

#include <stdexcept>

void Window::_initWindow(const std::string &title, int width, int height, unsigned int flags) {

    // SDL_InitSubSystem is ref-counted
    SDL_InitSubSystem(SDL_InitFlags::SDL_INIT_VIDEO);
    auto window = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (window) {
        m_window.reset(window);
    } else {
        throw std::runtime_error(SDL_GetError());
    }

    SDL_CreateRenderer(window, nullptr, SDL_RENDERER_ACCELERATED);


}

void Window::_onCloseWindow() {
    // SDL_QuitSubSystem is ref-counted
    SDL_Quit();
}


double Window::_getRunTime() {
    return (double) SDL_GetTicks() * 1000.0;
}


std::optional<SDL_Event> Window::pollEvent() {
    SDL_Event event;
    if (SDL_PollEvent(&event))
        return event;
    else
        return {};
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
