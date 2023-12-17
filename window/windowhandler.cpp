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

void Window::_close() {
    // SDL_QuitSubSystem is ref-counted
    Audio::Close();
    SDL_Quit();
    IMG_Quit();
}


double Window::_getRunTime() {
    return (double) SDL_GetTicks() * 1000.0;
}

void Window::_toggleFullscreen() {
    auto *window = m_window.get();

    auto fullscreenFlag = SDL_WINDOW_FULLSCREEN;
    bool isFullscreen = SDL_GetWindowFlags(window) & fullscreenFlag;

    SDL_SetWindowFullscreen(window, isFullscreen);
}

SDL_Renderer *Window::_getRenderer() {
    auto *window = m_window.get();
    return SDL_GetRenderer(window);
}

int Window::_getFPS(float milliseconds) {
    auto seconds = milliseconds / 1000.f;

    if (_fpsAccumulator > seconds) {
        _fpsAccumulator -= seconds;

        _fps = (int) (1. / _lastFrameTime);
    }
    return _fps;
}

bool Window::_isFullscreen() {
    auto *window = m_window.get();

    auto flag = SDL_GetWindowFlags(window);
    auto is_fullscreen = flag & SDL_WINDOW_FULLSCREEN;
    return is_fullscreen == SDL_WINDOW_FULLSCREEN;
}

vf2d Window::_getSize() {
    int w, h;

    SDL_GetWindowSize(m_window.get(), &w, &h);

    return {(float) w, (float) h};
}

void Window::_handleInput() {
    Input::Update();

    SDL_Event event;


    while (SDL_PollEvent(&event)) {
#ifdef ADD_IMGUI
        ImGui_ImplSDL3_ProcessEvent(&event);
#endif

        switch (event.type) {
            case SDL_EventType::SDL_EVENT_QUIT:
                _shouldQuit = true;
                break;
        }
    }

}


void Window::_setSize(int width, int height) {
    SDL_SetWindowSize(m_window.get(), width, height);
}


void Window::_clearBackground(Color color) {
    SDL_SetRenderDrawColor(GetRenderer(), color.r, color.g, color.b, color.a);
    SDL_RenderClear(GetRenderer());
}

void Window::_startFrame() {
    Lerp::updateLerps();

    Window::HandleInput();
    SDL_SetRenderDrawColor(GetRenderer(), 0, 0, 0, 255);
    SDL_RenderClear(GetRenderer());
#ifdef ADD_IMGUI
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
#endif
}

void Window::_endFrame() {
#ifdef ADD_IMGUI
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData());
#endif
    SDL_RenderPresent(Window::GetRenderer());

    _frameCount++;
    _previousTime = _currentTime;
    _currentTime = std::chrono::high_resolution_clock::now();
    _lastFrameTime = duration_cast<std::chrono::nanoseconds>(_currentTime - _previousTime).count() / 1000000000.;
    _fpsAccumulator += _lastFrameTime;
}

SDL_Window *Window::_getWindow() {
    return m_window.get();
}


