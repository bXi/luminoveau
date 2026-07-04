// Example 01 — Bouncing Ball
// ---------------------------------------------------------------------------
// The smallest useful Luminoveau program. It shows:
//   * the four app callbacks the engine drives (AppInit / AppIterate / AppEvent / AppQuit)
//   * opening a window and running a frame loop (StartFrame / EndFrame)
//   * drawing a filled shape with the immediate-mode Draw API
//   * moving something using per-frame delta time so speed is frame-rate independent
//
// No assets, no input — just a ball bouncing inside the window.

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <cmath>

// The window size, the ball's radius, and where the ball is + how fast it moves.
// These live for the whole program.
int   windowWidth  = 800;
int   windowHeight = 600;
float radius       = 24.0f;

vf2d ballPos = {windowWidth * 0.5f, windowHeight * 0.5f};
vf2d ballVel = {320.0f, 240.0f};  // pixels per second

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Bouncing Ball",
                       windowWidth, windowHeight, 1, SDL_WINDOW_RESIZABLE);

    // The window is cleared to this colour at the start of every frame.
    Renderer::ClearBackground({18, 18, 24, 255});
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    // Move the ball. GetFrameTime() is the seconds since the last frame, so multiplying by it
    // keeps the speed the same whether we run at 60 or 240 fps.
    ballPos += ballVel * Window::GetFrameTime();

    float width  = (float)Window::GetWidth();
    float height = (float)Window::GetHeight();

    // Bounce off the walls, nudging the ball back inside so it never sticks past an edge.
    if (ballPos.x - radius < 0.0f)   { ballPos.x = radius;          ballVel.x =  std::abs(ballVel.x); }
    if (ballPos.x + radius > width)  { ballPos.x = width - radius;  ballVel.x = -std::abs(ballVel.x); }
    if (ballPos.y - radius < 0.0f)   { ballPos.y = radius;          ballVel.y =  std::abs(ballVel.y); }
    if (ballPos.y + radius > height) { ballPos.y = height - radius; ballVel.y = -std::abs(ballVel.y); }

    Window::StartFrame();
    Draw::CircleFilled(ballPos, radius, {120, 200, 255, 255});
    Window::EndFrame();

    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) {
    return Lumi::Result::Continue;
}

void AppQuit(void* appstate, Lumi::Result result) {
    Window::Close();
}
