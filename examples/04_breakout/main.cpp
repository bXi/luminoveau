// Example 04 — Breakout
// ---------------------------------------------------------------------------
// A brick-breaker that adds:
//   * a grid of destructible objects + AABB collision resolution
//   * audio: short sound effects on bounce, brick break, and losing the ball
//     (Audio::PlaySound with assets loaded via AssetHandler::GetSound)
//
// Move the mouse to slide the paddle. Clear all the bricks.
//
// Assets (see this folder's assets/): bounce.ogg, brick.ogg, lose.ogg.
// Without them the game still plays — it just runs silently.
// Sound effects courtesy of Kenney — kenney.nl (CC0).

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

int   width       = 800;
int   height      = 600;
int   cols        = 10;
int   rows        = 6;
float brickHeight = 28.0f;
float gap         = 4.0f;
float topPad      = 60.0f;
float paddleWidth = 110.0f;
float paddleHeight = 16.0f;
float ball        = 14.0f;

float brickWidth = (width - gap) / (float)cols - gap;

std::vector<bool> bricks;   // cols*rows, true = the brick is still there
int   bricksLeft = 0;
float paddleX    = (width - paddleWidth) * 0.5f;
vf2d  ballPos    = {width * 0.5f, height * 0.6f};
vf2d  ballVel    = {220.0f, -260.0f};
int   score      = 0;
int   lives      = 3;

FontAsset*  font      = nullptr;
SoundAsset* sfxBounce = nullptr;
SoundAsset* sfxBrick  = nullptr;
SoundAsset* sfxLose   = nullptr;

void Play(SoundAsset* s) { if (s) Audio::PlaySound(*s); }

vf2d BrickPos(int col, int row) {
    return {gap + col * (brickWidth + gap), topPad + row * (brickHeight + gap)};
}

bool Overlaps(vf2d aPos, vf2d aSize, vf2d bPos, vf2d bSize) {
    return aPos.x < bPos.x + bSize.x && aPos.x + aSize.x > bPos.x &&
           aPos.y < bPos.y + bSize.y && aPos.y + aSize.y > bPos.y;
}

void ResetBall() {
    ballPos = {width * 0.5f, height * 0.6f};
    ballVel = {220.0f, -260.0f};
}

void NewBoard() {
    bricks.assign(cols * rows, true);
    bricksLeft = cols * rows;
    score = 0;
    lives = 3;
    ResetBall();
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Breakout", width, height, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({12, 12, 20, 255});

    width  = Window::GetWidth();   // adopt the real canvas size before the first board is laid out
    height = Window::GetHeight();

    Audio::Init();   // start the audio engine before loading/playing any sounds

    font      = &AssetHandler::GetDefaultFont();
    sfxBounce = &AssetHandler::GetSound("assets/bounce.ogg");
    sfxBrick  = &AssetHandler::GetSound("assets/brick.ogg");
    sfxLose   = &AssetHandler::GetSound("assets/lose.ogg");

    NewBoard();
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    float dt = (float)Window::GetFrameTime();

    // Lay out against the live window. The brick zone is a fixed fraction of the play height, so the
    // ball's lane below the bricks keeps the same proportion whatever the window's aspect — a short
    // window no longer squeezes the ball's space, it just makes the bricks a little shorter.
    width  = Window::GetWidth();
    height = Window::GetHeight();
    brickWidth = (width - gap) / (float)cols - gap;
    float playH     = (float)height - topPad;
    float brickZone = playH * 0.36f;
    brickHeight = std::max(10.0f, (brickZone - (rows - 1) * gap) / (float)rows);

    // Ball speed scales with the window so the feel stays constant: a taller field means a longer
    // trip across, so we speed the ball up to match. ballVel is kept in design units (600-tall
    // reference) and only scaled here at integration time — bounces stay resolution-independent.
    float speedScale = (float)height / 600.0f;

    // Paddle follows the mouse's X.
    paddleX = std::clamp(Input::GetMousePosition().x - paddleWidth * 0.5f, 0.0f, width - paddleWidth);
    vf2d paddlePos = {paddleX, height - 40.0f};

    ballPos += ballVel * speedScale * dt;

    // Walls.
    if (ballPos.x < 0.0f)           { ballPos.x = 0.0f;           ballVel.x =  std::abs(ballVel.x); Play(sfxBounce); }
    if (ballPos.x + ball > width)   { ballPos.x = width - ball;   ballVel.x = -std::abs(ballVel.x); Play(sfxBounce); }
    if (ballPos.y < 0.0f)           { ballPos.y = 0.0f;           ballVel.y =  std::abs(ballVel.y); Play(sfxBounce); }

    // Paddle: reflect the ball and steer it based on where it hit.
    if (Overlaps(ballPos, {ball, ball}, paddlePos, {paddleWidth, paddleHeight}) && ballVel.y > 0.0f) {
        ballPos.y = paddlePos.y - ball;
        ballVel.y = -std::abs(ballVel.y);
        float hit = (ballPos.x + ball * 0.5f - paddlePos.x) / paddleWidth - 0.5f;  // -0.5 .. 0.5
        ballVel.x = hit * 520.0f;
        Play(sfxBounce);
    }

    // Bricks — break at most one per frame so the bounce stays clean.
    bool broke = false;
    for (int row = 0; row < rows && !broke; row++) {
        for (int col = 0; col < cols; col++) {
            int i = row * cols + col;
            if (!bricks[i]) continue;
            if (Overlaps(ballPos, {ball, ball}, BrickPos(col, row), {brickWidth, brickHeight})) {
                bricks[i] = false;
                bricksLeft--;
                score += 10;
                ballVel.y = -ballVel.y;
                Play(sfxBrick);
                broke = true;
                break;
            }
        }
    }

    // Lost the ball off the bottom.
    if (ballPos.y > height) {
        lives--;
        Play(sfxLose);
        if (lives <= 0) NewBoard();
        else ResetBall();
    }

    // Cleared the board.
    if (bricksLeft == 0) NewBoard();

    // ── Draw ──────────────────────────────────────────────────────────────────────
    Window::StartFrame();

    // HUD bar: a dark-blue strip across the top (above the first brick row) for the score/lives.
    Draw::RectangleFilled({0.0f, 0.0f}, {(float)width, topPad - gap}, {24, 28, 56, 255});

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            if (!bricks[row * cols + col]) continue;
            Color c = {(uint8_t)(80 + row * 24), (uint8_t)(200 - row * 20), 220, 255};
            Draw::RectangleFilled(BrickPos(col, row), {brickWidth, brickHeight}, c);
        }
    }

    Draw::RectangleFilled(paddlePos, {paddleWidth, paddleHeight}, WHITE);
    Draw::RectangleFilled(ballPos,   {ball, ball},               {255, 230, 120, 255});

    Text::DrawText(*font, {10.0f, 6.0f}, "Score: " + std::to_string(score), WHITE, 28.0f);
    Text::DrawText(*font, {(float)width - 150.0f, 6.0f}, "Lives: " + std::to_string(lives), WHITE, 28.0f);

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
