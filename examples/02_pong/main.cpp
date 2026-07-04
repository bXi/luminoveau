// Example 02 — Pong
// ---------------------------------------------------------------------------
// A complete tiny game. Building on example 01 it adds:
//   * mouse input — the left paddle follows the cursor (Input::GetMousePosition)
//   * a simple AI opponent
//   * axis-aligned collision (ball vs paddles and walls)
//   * game state (scores) and on-screen text with the default font
//
// Move the mouse up/down to play. First to feel like winning wins.

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <string>

int   width        = 800;
int   height       = 600;
float paddleWidth  = 16.0f;
float paddleHeight = 100.0f;
float paddleMargin = 32.0f;    // distance of each paddle from its wall
float ballSize     = 16.0f;
float aiSpeed      = 360.0f;   // max AI paddle speed (px/s)
// Cap the ball's horizontal speed so it can never travel more than a paddle-width in one frame,
// even at low frame rates — otherwise a long rally could let it tunnel straight through a paddle.
float maxBallSpeedX = 900.0f;

float playerY = (height - paddleHeight) * 0.5f;  // top-left Y of the left paddle
float aiY     = (height - paddleHeight) * 0.5f;  // top-left Y of the right paddle

vf2d ballPos = {width * 0.5f, height * 0.5f};
vf2d ballVel = {-300.0f, 180.0f};

int playerScore = 0;
int aiScore     = 0;

// Font is an alias for FontAsset& (a reference), so we hold a pointer and bind it in AppInit.
FontAsset* font = nullptr;

void ResetBall(float directionX) {
    ballPos = {width * 0.5f, height * 0.5f};
    ballVel = {300.0f * directionX, 180.0f};
}

// Do two rectangles overlap? (axis-aligned bounding box test)
bool Overlaps(vf2d aPos, vf2d aSize, vf2d bPos, vf2d bSize) {
    return aPos.x < bPos.x + bSize.x && aPos.x + aSize.x > bPos.x &&
           aPos.y < bPos.y + bSize.y && aPos.y + aSize.y > bPos.y;
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Pong", width, height, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({10, 12, 16, 255});
    font = &AssetHandler::GetDefaultFont();
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    float dt = (float)Window::GetFrameTime();

    // Left paddle tracks the mouse, centred on the cursor.
    vf2d mouse = Input::GetMousePosition();
    playerY = std::clamp(mouse.y - paddleHeight * 0.5f, 0.0f, height - paddleHeight);

    // AI eases the right paddle toward the ball, capped to a max speed.
    float aiCenter = aiY + paddleHeight * 0.5f;
    float aiStep   = std::clamp(ballPos.y - aiCenter, -aiSpeed * dt, aiSpeed * dt);
    aiY = std::clamp(aiY + aiStep, 0.0f, height - paddleHeight);

    // Move the ball.
    ballPos += ballVel * dt;

    // Bounce off the top and bottom walls.
    if (ballPos.y < 0.0f)              { ballPos.y = 0.0f;              ballVel.y =  std::abs(ballVel.y); }
    if (ballPos.y + ballSize > height) { ballPos.y = height - ballSize; ballVel.y = -std::abs(ballVel.y); }

    vf2d playerPos = {paddleMargin, playerY};
    vf2d aiPos     = {width - paddleMargin - paddleWidth, aiY};

    // Paddle hits: push the ball out, reverse its X, and speed it up a touch (capped).
    if (Overlaps(ballPos, {ballSize, ballSize}, playerPos, {paddleWidth, paddleHeight}) && ballVel.x < 0.0f) {
        ballPos.x = playerPos.x + paddleWidth;
        ballVel.x = std::min(std::abs(ballVel.x) * 1.05f, maxBallSpeedX);
    }
    if (Overlaps(ballPos, {ballSize, ballSize}, aiPos, {paddleWidth, paddleHeight}) && ballVel.x > 0.0f) {
        ballPos.x = aiPos.x - ballSize;
        ballVel.x = -std::min(std::abs(ballVel.x) * 1.05f, maxBallSpeedX);
    }

    // Scoring: the ball left the play field.
    if (ballPos.x + ballSize < 0.0f) { aiScore++;     ResetBall(-1.0f); }
    if (ballPos.x > width)           { playerScore++; ResetBall( 1.0f); }

    // ── Draw ─────────────────────────────────────────────────────────────────────
    Window::StartFrame();

    // Centre net (a dashed line of short rectangles).
    for (float y = 0.0f; y < height; y += 32.0f) {
        Draw::RectangleFilled({width * 0.5f - 2.0f, y + 6.0f}, {4.0f, 20.0f}, {40, 48, 60, 255});
    }

    Draw::RectangleFilled(playerPos, {paddleWidth, paddleHeight}, WHITE);
    Draw::RectangleFilled(aiPos,     {paddleWidth, paddleHeight}, WHITE);
    Draw::RectangleFilled(ballPos,   {ballSize, ballSize},        {120, 220, 255, 255});

    Text::DrawText(*font, {width * 0.5f - 80.0f, 24.0f}, std::to_string(playerScore), WHITE, 40.0f);
    Text::DrawText(*font, {width * 0.5f + 50.0f, 24.0f}, std::to_string(aiScore),     WHITE, 40.0f);

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) {
    return Lumi::Result::Continue;
}

void AppQuit(void* appstate, Lumi::Result result) {
    Window::Close();
}
