#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_filesystem.h>
#include "app/app.h"

#if defined(_WIN32)
#include <direct.h>
#define LUMI_CHDIR _chdir
#else
#include <unistd.h>
#define LUMI_CHDIR chdir
#endif

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    // Anchor the working directory to the executable's folder before anything touches the
    // filesystem. Double-clicking from Finder (or Explorer) launches with CWD = "/" or the
    // user's home, which breaks relative asset/pak mounts. SDL_GetBasePath resolves the
    // executable dir on every platform (and Contents/Resources for a macOS .app bundle).
    // **Persistent storage is deliberately not mounted here.** It is the obvious place, and it
    // is wrong: the initial IndexedDB read suspends the C stack through Asyncify, and this
    // function is called from SDL_EnterAppMainCallbacks before SDL_Init, before a window, and
    // before the Emscripten main loop exists. Unwinding out of that leaves nothing sane to
    // rewind into. FileHandler mounts it lazily on first use instead, which lands inside the
    // game's own AppInit — the same neighbourhood as the AssetHandler constructor, whose font
    // cache has always slept there safely.
    return static_cast<SDL_AppResult>(AppInit(appstate, argc, argv));
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    if (Window::ShouldQuit())
        return SDL_APP_SUCCESS;
    return static_cast<SDL_AppResult>(AppIterate(appstate));
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    Window::ProcessEvent(event);
    return static_cast<SDL_AppResult>(AppEvent(appstate, event));
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    AppQuit(appstate, static_cast<Lumi::Result>(result));
}
