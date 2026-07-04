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

#include <cstdlib>
#include <deque>
#include <string>

int cols = 24;
int rows = 18;
int cell = 28;          // pixels per grid cell
int hud  = 40;          // top strip for the score
float stepTime = 0.12f; // seconds between moves

int width  = cols * cell;
int height = rows * cell + hud;

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
    Window::InitWindow("Luminoveau Example — Snake", width, height, 1, SDL_WINDOW_RESIZABLE);
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

    Window::StartFrame();

    // HUD bar: a dark-blue strip across the top so the play area's upper edge is clear.
    Draw::RectangleFilled({0.0f, 0.0f}, {(float)width, (float)hud}, {24, 28, 56, 255});

    // Food.
    Draw::RectangleFilled({(float)(food.x * cell), (float)(food.y * cell + hud)},
                          {(float)cell, (float)cell}, {220, 70, 70, 255});

    // Snake body (the head is a lighter green).
    for (int i = 0; i < (int)snake.size(); i++) {
        Color c = (i == 0) ? Color{180, 240, 160, 255} : Color{90, 180, 90, 255};
        Draw::RectangleFilled({(float)(snake[i].x * cell + 1), (float)(snake[i].y * cell + hud + 1)},
                              {(float)(cell - 2), (float)(cell - 2)}, c);
    }

    Text::DrawText(*font, {10.0f, 6.0f}, "Score: " + std::to_string(score), WHITE, 28.0f);
    if (dead) {
        Text::DrawText(*font, {width * 0.5f - 150.0f, height * 0.5f - 16.0f},
                       "Game Over - press Space", WHITE, 30.0f);
    }

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
