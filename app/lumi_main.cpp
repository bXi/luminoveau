#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include "app/app.h"

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    return static_cast<SDL_AppResult>(AppInit(appstate, argc, argv));
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    if (Window::ShouldQuit()) return SDL_APP_SUCCESS;
    return static_cast<SDL_AppResult>(AppIterate(appstate));
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    Window::ProcessEvent(event);
    return static_cast<SDL_AppResult>(AppEvent(appstate, event));
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    AppQuit(appstate, static_cast<Lumi::Result>(result));
}
