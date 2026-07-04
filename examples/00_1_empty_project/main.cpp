// Scaffold 00_1 — Empty Project
// ---------------------------------------------------------------------------
// A blank starting point. Copy this directory to begin a new Luminoveau project.
// It opens a window, clears it every frame, and does nothing else — the four app
// callbacks below are where your code goes.
//
//   AppInit    — runs once at startup: open the window, load assets, set up state.
//   AppIterate — runs every frame: read input, update your game, draw it.
//   AppEvent   — runs for each OS/input event (optional; usually leave as-is).
//   AppQuit    — runs once at shutdown: free anything you created in AppInit.

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

Lumi::Result AppInit(void** /*appstate*/, int /*argc*/, char* /*argv*/[]) {
    Window::InitWindow("My Luminoveau Project", 1280, 720, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({20, 20, 28, 255});

    // TODO: load assets and initialise your game state here.

    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* /*appstate*/) {
    const float dt = static_cast<float>(Window::GetFrameTime());
    (void)dt;  // delta time in seconds — use it to make movement frame-rate independent.

    // TODO: update your game state here (before StartFrame).

    Window::StartFrame();

    // TODO: draw your frame here, e.g.:
    //   Draw::CircleFilled({640, 360}, 40.0f, WHITE);

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* /*appstate*/, SDL_Event* /*event*/) {
    return Lumi::Result::Continue;
}

void AppQuit(void* /*appstate*/, Lumi::Result /*result*/) {
    // TODO: free anything allocated in AppInit.
    Window::Close();
}
