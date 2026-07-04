// Scaffold 00_2 — Game Shell (State Machine)
// ---------------------------------------------------------------------------
// Most games aren't just "the gameplay" — they're a title screen, the game, a pause menu, and a
// game-over screen, with transitions between them. This shows how to structure that with a simple
// state machine. The gameplay itself (click the circle before time runs out) is deliberately tiny.
//
// Title:     Space to start
// Playing:   click the circle for points; Esc to pause
// Paused:    Esc to resume, Q to quit to title
// Game Over: Space to play again

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>

namespace {
constexpr int   kWidth    = 800;
constexpr int   kHeight   = 600;
constexpr float kRoundTime = 20.0f;
constexpr float kTargetR   = 34.0f;

enum class State { Title, Playing, Paused, GameOver };

State state    = State::Title;
int   score    = 0;
float timeLeft = kRoundTime;
vf2d  target   = {kWidth * 0.5f, kHeight * 0.5f};

float Rand(float lo, float hi) { return lo + (hi - lo) * (std::rand() / float(RAND_MAX)); }
void  MoveTarget() { target = {Rand(kTargetR, kWidth - kTargetR), Rand(kTargetR + 60, kHeight - kTargetR)}; }

void StartGame() {
    score = 0;
    timeLeft = kRoundTime;
    MoveTarget();
    state = State::Playing;
}

// Each state gets its own update; the shell just dispatches to the current one.
void UpdatePlaying(float dt) {
    timeLeft -= dt;
    if (timeLeft <= 0.0f) { state = State::GameOver; return; }

    if (Input::KeyPressed(SDLK_ESCAPE)) { state = State::Paused; return; }

    if (Input::MouseButtonPressed(SDL_BUTTON_LEFT)) {
        vf2d m = Input::GetMousePosition();
        vf2d d = m - target;
        if (d.x * d.x + d.y * d.y <= kTargetR * kTargetR) { score++; MoveTarget(); }
    }
}

FontAsset* font = nullptr;
void Label(float y, const std::string& s, float size, Color col = WHITE) {
    Text::DrawText(*font, {kWidth * 0.5f - Text::MeasureText(*font, s, size) * 0.5f, y}, s, col, size);
}
}  // namespace

Lumi::Result AppInit(void** /*appstate*/, int /*argc*/, char* /*argv*/[]) {
    Window::InitWindow("Luminoveau Example — Game Shell", kWidth, kHeight, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({16, 18, 26, 255});
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    font = &AssetHandler::GetDefaultFont();
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* /*appstate*/) {
    const float dt = static_cast<float>(Window::GetFrameTime());

    // ── Update: dispatch on the current state ────────────────────────────────────
    switch (state) {
        case State::Title:
            if (Input::KeyPressed(SDLK_SPACE)) StartGame();
            break;
        case State::Playing:
            UpdatePlaying(dt);
            break;
        case State::Paused:
            if (Input::KeyPressed(SDLK_ESCAPE)) state = State::Playing;
            if (Input::KeyPressed(SDLK_Q))      state = State::Title;
            break;
        case State::GameOver:
            if (Input::KeyPressed(SDLK_SPACE)) StartGame();
            break;
    }

    // ── Draw: each state renders its own screen ──────────────────────────────────
    Window::StartFrame();
    switch (state) {
        case State::Title:
            Label(220, "CLICK RUSH", 60);
            Label(320, "Press Space to start", 30, {180, 200, 220, 255});
            break;

        case State::Playing:
        case State::Paused:
            Draw::CircleFilled(target, kTargetR, {120, 220, 255, 255});
            Label(20, "Score: " + std::to_string(score), 28);
            Label(52, "Time: " + std::to_string(int(std::ceil(timeLeft))), 24, {200, 200, 160, 255});
            if (state == State::Paused) {
                Draw::RectangleFilled({0, 0}, {kWidth, kHeight}, {0, 0, 0, 150});
                Label(250, "PAUSED", 54);
                Label(330, "Esc resume  -  Q quit to title", 26, {200, 200, 220, 255});
            }
            break;

        case State::GameOver:
            Label(220, "TIME UP", 60);
            Label(320, "Final score: " + std::to_string(score), 34);
            Label(380, "Press Space to play again", 26, {180, 200, 220, 255});
            break;
    }
    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* /*appstate*/, SDL_Event* /*event*/) { return Lumi::Result::Continue; }
void AppQuit(void* /*appstate*/, Lumi::Result /*result*/) { Window::Close(); }
