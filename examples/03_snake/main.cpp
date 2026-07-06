// Example 03 — Snake
// ---------------------------------------------------------------------------
// Builds on the basics with:
//   * keyboard input (Input::KeyPressed) to steer
//   * grid-based game logic on a fixed timestep
//   * growing/shrinking state in a std::deque
//
// Arrow keys or WASD to turn. Eat the red food, don't hit yourself or the walls.

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <string>

int cell = 28;          // pixels per grid cell (fixed; the grid count adapts to the window)
int hud  = 40;          // top strip for the score
float stepTime = 0.12f; // seconds between moves

// Grid size — recomputed from the window on each new game (see Reset). Locked in for the whole run
// so a mid-game resize never moves the walls out from under the snake. These seed the initial window.
int cols = 24;
int rows = 18;

int initWidth  = cols * cell;        // initial window size (desktop); the view is resizable
int initHeight = rows * cell + hud;

struct Cell { int x, y; };

std::deque<Cell> snake;   // front() is the head
Cell dir  = {1, 0};       // current heading
Cell next = {1, 0};       // queued heading (applied at the next step)
Cell food = {0, 0};
float stepTimer = 0.0f;
int  score = 0;
bool dead  = false;

FontAsset* font = nullptr;

void PlaceFood() {
    food = {std::rand() % cols, std::rand() % rows};
}

void Reset() {
    // Fit the grid to the current window at the fixed cell size. Done only on a new game, so the
    // playfield stays constant through a run even if the window is resized mid-game.
    cols = std::max(8, Window::GetWidth() / cell);
    rows = std::max(6, (Window::GetHeight() - hud) / cell);

    snake.clear();
    snake.push_back({cols / 2, rows / 2});
    snake.push_back({cols / 2 - 1, rows / 2});
    dir = next = {1, 0};
    score = 0;
    dead = false;
    stepTimer = 0.0f;
    PlaceFood();
}

void Step() {
    // Apply the queued direction, but ignore a straight 180° reversal.
    if (!(next.x == -dir.x && next.y == -dir.y)) dir = next;

    Cell head = {snake.front().x + dir.x, snake.front().y + dir.y};

    // Hitting a wall ends the run.
    if (head.x < 0 || head.x >= cols || head.y < 0 || head.y >= rows) { dead = true; return; }
    // Hitting yourself ends the run.
    for (Cell c : snake) if (c.x == head.x && c.y == head.y) { dead = true; return; }

    snake.push_front(head);
    if (head.x == food.x && head.y == food.y) {
        score++;
        PlaceFood();       // eating grows the snake (we skip removing the tail)
    } else {
        snake.pop_back();
    }
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Snake", initWidth, initHeight, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({16, 20, 16, 255});
    font = &AssetHandler::GetDefaultFont();
    Reset();
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    // Steering: queue the new direction; it takes effect on the next grid step.
    if (Input::KeyPressed(SDLK_LEFT)  || Input::KeyPressed(SDLK_A)) next = {-1, 0};
    if (Input::KeyPressed(SDLK_RIGHT) || Input::KeyPressed(SDLK_D)) next = { 1, 0};
    if (Input::KeyPressed(SDLK_UP)    || Input::KeyPressed(SDLK_W)) next = { 0,-1};
    if (Input::KeyPressed(SDLK_DOWN)  || Input::KeyPressed(SDLK_S)) next = { 0, 1};

    if (dead) {
        if (Input::KeyPressed(SDLK_SPACE)) Reset();
    } else {
        // Fixed timestep: step the grid every stepTime seconds, no matter the frame rate.
        stepTimer += (float)Window::GetFrameTime();
        while (stepTimer >= stepTime) { stepTimer -= stepTime; Step(); if (dead) break; }
    }

    // Centre the board in the current window. The grid count is fixed for the run, so the origin
    // is all that moves when the window is resized — the play stays put and centred.
    float w = (float)Window::GetWidth();
    float h = (float)Window::GetHeight();
    float boardW = (float)(cols * cell);
    float boardH = (float)(rows * cell);
    float ox = (w - boardW) * 0.5f;
    float oy = (float)hud + std::max(0.0f, (h - (float)hud - boardH) * 0.5f);

    Window::StartFrame();

    // HUD bar: a dark-blue strip across the top so the play area's upper edge is clear.
    Draw::RectangleFilled({0.0f, 0.0f}, {w, (float)hud}, {24, 28, 56, 255});

    // Food.
    Draw::RectangleFilled({ox + food.x * cell, oy + food.y * cell},
                          {(float)cell, (float)cell}, {220, 70, 70, 255});

    // Snake body (the head is a lighter green).
    for (int i = 0; i < (int)snake.size(); i++) {
        Color c = (i == 0) ? Color{180, 240, 160, 255} : Color{90, 180, 90, 255};
        Draw::RectangleFilled({ox + snake[i].x * cell + 1, oy + snake[i].y * cell + 1},
                              {(float)(cell - 2), (float)(cell - 2)}, c);
    }

    Text::DrawText(*font, {10.0f, 6.0f}, "Score: " + std::to_string(score), WHITE, 28.0f);
    if (dead) {
        std::string over = "Game Over - press Space";
        float overW = (float)Text::MeasureText(*font, over, 30.0f);
        Text::DrawText(*font, {(w - overW) * 0.5f, h * 0.5f - 16.0f}, over, WHITE, 30.0f);
    }

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
