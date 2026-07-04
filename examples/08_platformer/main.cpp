// Example 08 — Platformer
// ---------------------------------------------------------------------------
// A side-scroller that introduces:
//   * a tilemap and AABB-vs-tiles collision (resolved one axis at a time)
//   * gravity and jumping
//   * a scrolling 2D camera that follows the player (Camera::SetTarget + Draw::BeginMode2D)
//
// A/D or arrows to move, Space to jump. Fall off the bottom and you respawn.

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <string>
#include <vector>

int   width   = 900;
int   height  = 600;
float tile    = 40.0f;
float gravity = 1800.0f;
float runSpeed  = 260.0f;
float jumpSpeed = 640.0f;

// The level. '#' = solid, '.' = empty, 'P' = player spawn.
std::vector<std::string> level = {
    "................................................",
    "................................................",
    "................................................",
    ".....P..........................................",
    "...####.......................######............",
    "..............................................#.",
    "..........####...........###..................#.",
    "......................................####....#.",
    "...............####........................#..#.",
    "..####..................##.....................#.",
    "......................................######...#.",
    "..............#####.............................",
    "#############################...####..##########",
    "#############################...####..##########",
    "#############################...####..##########",
};

int rows = (int)level.size();
int cols = (int)level[0].size();

vf2d playerSize = {26.0f, 34.0f};
vf2d spawn = {80.0f, 80.0f};
vf2d pos;                    // player top-left, world space
vf2d vel = {0, 0};

bool Solid(int tx, int ty) {
    if (tx < 0 || tx >= cols || ty < 0 || ty >= rows) return false;
    return level[ty][tx] == '#';
}

// After moving one axis, push the player out of any solid tile it now overlaps.
void ResolveAxis(bool horizontal) {
    int left   = (int)(pos.x / tile);
    int right  = (int)((pos.x + playerSize.x) / tile);
    int top    = (int)(pos.y / tile);
    int bottom = (int)((pos.y + playerSize.y) / tile);

    for (int ty = top; ty <= bottom; ty++) {
        for (int tx = left; tx <= right; tx++) {
            if (!Solid(tx, ty)) continue;
            vf2d tilePos = {tx * tile, ty * tile};
            // Do we really overlap this tile?
            if (pos.x < tilePos.x + tile && pos.x + playerSize.x > tilePos.x &&
                pos.y < tilePos.y + tile && pos.y + playerSize.y > tilePos.y) {
                if (horizontal) {
                    if (vel.x > 0) pos.x = tilePos.x - playerSize.x;
                    else if (vel.x < 0) pos.x = tilePos.x + tile;
                    vel.x = 0;
                } else {
                    if (vel.y > 0) pos.y = tilePos.y - playerSize.y;
                    else if (vel.y < 0) pos.y = tilePos.y + tile;
                    vel.y = 0;
                }
            }
        }
    }
}

// Is the player standing on solid ground? We probe a thin band 1px below the feet instead of
// relying on the collision resolve — after landing the player rests exactly touching the tile, so
// an overlap test would flicker on/off frame to frame. Checking 1px below is stable.
bool OnGround() {
    float feetY = pos.y + playerSize.y + 1.0f;
    int   ty    = (int)(feetY / tile);
    int   left  = (int)(pos.x / tile);
    int   right = (int)((pos.x + playerSize.x - 1.0f) / tile);
    for (int tx = left; tx <= right; tx++)
        if (Solid(tx, ty)) return true;
    return false;
}

void FindSpawn() {
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++)
            if (level[y][x] == 'P') spawn = {x * tile, y * tile};
    pos = spawn;
    vel = {0, 0};
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Platformer", width, height, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({22, 24, 34, 255});
    FindSpawn();
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    float dt = (float)Window::GetFrameTime();

    // Horizontal input.
    vel.x = 0;
    if (Input::KeyDown(SDLK_A) || Input::KeyDown(SDLK_LEFT))  vel.x -= runSpeed;
    if (Input::KeyDown(SDLK_D) || Input::KeyDown(SDLK_RIGHT)) vel.x += runSpeed;

    // Jump when standing on ground.
    if (OnGround() && (Input::KeyPressed(SDLK_SPACE) || Input::KeyPressed(SDLK_W) || Input::KeyPressed(SDLK_UP)))
        vel.y = -jumpSpeed;

    // Gravity.
    vel.y += gravity * dt;

    // Move + collide, one axis at a time.
    pos.x += vel.x * dt; ResolveAxis(true);
    pos.y += vel.y * dt; ResolveAxis(false);

    // Fell off the map -> respawn.
    if (pos.y > rows * tile + 200.0f) FindSpawn();

    // Camera follows the player's centre.
    Camera::SetTarget(pos + playerSize * 0.5f);

    // ── Draw ─────────────────────────────────────────────────────────────────────
    Window::StartFrame();

    Draw::BeginMode2D();   // world space — everything drawn now moves with the camera
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            if (level[y][x] != '#') continue;
            Draw::RectangleFilled({x * tile, y * tile}, {tile, tile}, {70, 90, 120, 255});
        }
    }
    Draw::RectangleFilled(pos, playerSize, {120, 220, 160, 255});
    Draw::EndMode2D();     // back to screen space for the HUD

    FontAsset& font = AssetHandler::GetDefaultFont();
    Text::DrawText(font, {10.0f, 6.0f}, "A/D move, Space jump", WHITE, 24.0f);

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
