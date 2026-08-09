#if defined(__3DS__)
// 3DS raw entry point: we own the platform loop (gfx, hid, aptMainLoop); SDL is linked only for audio.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h> // SDL_SetMainReady (we provide main ourselves)
#include <3ds.h>
#include <unistd.h>
#include "app/app.h"

// Main-thread stack; the devkitPro default is too small for deep game recursion (flood-fill clears).
extern "C" unsigned int __stacksize__ = 1024 * 1024;

// Heap split (declared extern in <3ds/env.h>): favour the linear heap for textures over the default
// 32/24 MB cap. Keep malloc >= ~16 MB — SFX decode to malloc'd float PCM. Sum <= the ~56 MB grant.
u32 __ctru_linear_heap_size = 40u * 1024 * 1024; // textures
u32 __ctru_heap_size        = 16u * 1024 * 1024; // malloc (game state + decoded SFX PCM)

int main(int argc, char *argv[]) {
    consoleDebugInit(debugDevice_SVC); // engine log (stderr) -> svcOutputDebugString
    SDL_SetMainReady();                // we provide main, not SDL_main
    hidInit();                         // normally done by SDL's video driver, which we don't use

    // Boost priority across init so the GSP event thread outranks SDL's ndsp audio thread; restored after.
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x19);

#ifdef LUMI_3DS_SD_ASSETS
    (void)chdir("sdmc:/deltalight2"); // dev: assets on SD card, code-only .3dsx
#else
    romfsInit();
    (void)chdir("romfs:/");
#endif

    void        *appstate = nullptr;
    Lumi::Result r        = AppInit(&appstate, argc, argv);
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x30); // restore normal main-thread priority

    while (r == Lumi::Result::Continue && aptMainLoop() && !Window::ShouldQuit()) {
        hidScanInput(); // refresh libctru input state for this frame
        r = AppIterate(appstate);
    }

    AppQuit(appstate, r);
    hidExit();
#ifndef LUMI_3DS_SD_ASSETS
    romfsExit();
#endif
    return 0;
}

#else
// ── Desktop / web: SDL drives the app via callbacks ──────────────────────────
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
    if (const char *base = SDL_GetBasePath(); base && *base)
        (void)LUMI_CHDIR(base);
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
#endif
