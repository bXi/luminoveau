// Example 18 — 2D Raycaster
// ---------------------------------------------------------------------------
// A Wolfenstein-style pseudo-3D renderer built from a 2D grid map:
//   * one ray per screen column, marched through the grid with a DDA
//   * each wall strip is procedurally BRICK-TEXTURED (per-texel), shaded by side, distance fog and a
//     warm torch falloff around the camera
//   * a minimap in the corner shows the map, the player and a fan of the cast rays
//   * Doom controls: W/S walk, A/D turn, Q/E strafe (arrow keys work too), with wall collision
//
// Pure CPU + Draw::RectangleFilled, laid out against the live window (fully resizable).

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>

const int MW = 16, MH = 16;
const int MAP[MH][MW] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,1},
    {1,0,1,1,0,1,0,2,0,1,1,1,0,1,0,1},
    {1,0,1,0,0,1,0,0,0,0,0,1,0,1,0,1},
    {1,0,1,0,3,3,3,0,1,1,0,1,0,0,0,1},
    {1,0,0,0,3,0,3,0,0,1,0,0,0,1,0,1},
    {1,0,1,0,3,0,0,0,0,1,1,1,0,1,0,1},
    {1,0,1,0,3,3,3,0,1,0,0,0,0,0,0,1},
    {1,0,1,0,0,0,0,0,1,0,1,1,1,1,0,1},
    {1,0,1,1,1,2,1,0,1,0,0,0,0,1,0,1},
    {1,0,0,0,0,2,0,0,0,0,1,1,0,1,0,1},
    {1,0,1,1,0,2,0,1,1,0,1,0,0,0,0,1},
    {1,0,1,0,0,0,0,1,0,0,1,0,1,1,1,1},
    {1,0,1,0,1,1,0,1,0,1,1,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,1,1,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

float px = 1.5f, py = 1.5f, ang = 0.4f;

int mapAt(int x, int y) {
    if (x < 0 || y < 0 || x >= MW || y >= MH) return 1;
    return MAP[y][x];
}

// Try to move the player by (dx,dy), sliding along walls: each axis is applied only if the target
// cell (plus a small radius buffer) is empty, so you slide instead of sticking.
void tryMove(float dx, float dy) {
    const float rad = 0.15f;
    if (mapAt((int)(px + dx + (dx > 0 ? rad : -rad)), (int)py) == 0) px += dx;
    if (mapAt((int)px, (int)(py + dy + (dy > 0 ? rad : -rad))) == 0) py += dy;
}

// Procedural brick pattern -> brightness multiplier. u runs along the wall (0..1 per cell), v runs
// down the wall; rows are offset every other course and the mortar gaps are darker, with a little
// per-brick variation so the texture doesn't look mechanical.
float brickTex(float u, float v) {
    const float bh = 0.25f, bw = 0.5f;                 // brick height / width in cell units
    float row    = std::floor(v / bh);
    float offset = ((int)row & 1) ? 0.5f : 0.0f;
    float bu = u / bw + offset; bu -= std::floor(bu);
    float bv = v / bh;          bv -= std::floor(bv);
    if (bu < 0.06f || bu > 0.94f || bv < 0.10f || bv > 0.90f) return 0.4f;   // mortar
    float col  = std::floor(u / bw + offset);
    float rnd  = std::sin(row * 12.9898f + col * 78.233f) * 43758.5453f;
    rnd -= std::floor(rnd);
    return 0.8f + 0.35f * rnd;
}

Color wallColor(int v, int side, float dist, float tex) {
    Color base = v == 2 ? Color{205, 75, 70, 255}
               : v == 3 ? Color{70, 165, 205, 255}
                        : Color{175, 165, 155, 255};
    float fog   = 1.0f / (1.0f + dist * dist * 0.018f);
    float torch = 0.55f + 0.9f / (1.0f + dist * 0.6f);           // warm falloff near the camera
    float shade = (side == 1 ? 0.62f : 1.0f) * fog * torch * tex;
    shade = std::min(shade, 1.4f);
    return { (unsigned)std::min(255.0f, base.r * shade),
             (unsigned)std::min(255.0f, base.g * shade * 0.96f),  // nudge warm
             (unsigned)std::min(255.0f, base.b * shade * 0.9f), 255u };
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — 2D Raycaster", 900, 520, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({0, 0, 0, 255});
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    float dt = std::min((float)Window::GetFrameTime(), 0.05f);

    // Doom controls: turn, walk forward/back, strafe — with wall collision (slide) via tryMove.
    const float turn = 2.2f, moveSpd = 2.6f;
    if (Input::KeyDown(SDLK_A) || Input::KeyDown(SDLK_LEFT))  ang -= turn * dt;
    if (Input::KeyDown(SDLK_D) || Input::KeyDown(SDLK_RIGHT)) ang += turn * dt;
    float dirx = std::cos(ang), diry = std::sin(ang);
    float strafex = -diry, strafey = dirx;
    if (Input::KeyDown(SDLK_W) || Input::KeyDown(SDLK_UP))    tryMove(dirx * moveSpd * dt, diry * moveSpd * dt);
    if (Input::KeyDown(SDLK_S) || Input::KeyDown(SDLK_DOWN))  tryMove(-dirx * moveSpd * dt, -diry * moveSpd * dt);
    if (Input::KeyDown(SDLK_Q)) tryMove(-strafex * moveSpd * dt, -strafey * moveSpd * dt);
    if (Input::KeyDown(SDLK_E)) tryMove( strafex * moveSpd * dt,  strafey * moveSpd * dt);

    float w = (float)Window::GetWidth();
    float h = (float)Window::GetHeight();
    float planex = -diry * 0.66f, planey = dirx * 0.66f;
    const int colStep = 2;

    Window::StartFrame();
    // Sky gradient (a few bands) + dark floor.
    for (int b = 0; b < 6; b++) {
        float f = (float)b / 6.0f;
        Draw::RectangleFilled({0, h * 0.5f * f}, {w, h * 0.5f / 6.0f + 1.0f},
                              {(unsigned)(20 + 22 * f), (unsigned)(24 + 26 * f), (unsigned)(34 + 30 * f), 255u});
    }
    Draw::RectangleFilled({0, h * 0.5f}, {w, h * 0.5f}, {16, 15, 18, 255});

    for (int x = 0; x < (int)w; x += colStep) {
        float cameraX = 2.0f * x / w - 1.0f;
        float rdx = dirx + planex * cameraX;
        float rdy = diry + planey * cameraX;

        int mx = (int)px, my = (int)py;
        float ddx = std::fabs(1.0f / rdx), ddy = std::fabs(1.0f / rdy);
        int stepX, stepY; float sdx, sdy;
        if (rdx < 0) { stepX = -1; sdx = (px - mx) * ddx; } else { stepX = 1; sdx = (mx + 1 - px) * ddx; }
        if (rdy < 0) { stepY = -1; sdy = (py - my) * ddy; } else { stepY = 1; sdy = (my + 1 - py) * ddy; }

        int side = 0, hitVal = 0;
        for (int guard = 0; guard < 64 && hitVal == 0; guard++) {
            if (sdx < sdy) { sdx += ddx; mx += stepX; side = 0; }
            else           { sdy += ddy; my += stepY; side = 1; }
            hitVal = mapAt(mx, my);
        }
        float perp = side == 0 ? (sdx - ddx) : (sdy - ddy);
        if (perp < 0.0001f) perp = 0.0001f;

        // Where along the wall the ray hit (0..1) -> horizontal texture coordinate.
        float wallX = side == 0 ? (py + perp * rdy) : (px + perp * rdx);
        wallX -= std::floor(wallX);

        float lineH  = h / perp;
        float topF   = h * 0.5f - lineH * 0.5f;              // unclamped strip top (for texel mapping)
        float startY = std::max(0.0f, topF);
        float endY   = std::min(h, h * 0.5f + lineH * 0.5f);

        // Draw the strip as vertical texels. A FIXED screen-space texel (not lineH/N) keeps the brick
        // pattern high-res when the wall is close/clipped and aligned column-to-column, instead of
        // collapsing into a few giant blocks the moment the wall no longer fits on screen.
        const float texel = 3.0f;
        for (float y = startY; y < endY; y += texel) {
            float v = (y + texel * 0.5f - topF) / lineH;     // sample at texel centre, 0..1 down wall
            Color c = wallColor(hitVal, side, perp, brickTex(wallX, v));
            Draw::RectangleFilled({(float)x, y}, {(float)colStep, std::min(texel, endY - y)}, c);
        }
    }

    // ---- Minimap (top-right) -------------------------------------------------------------------
    float cell = std::max(4.0f, std::min(w, h) * 0.016f);
    float mx0 = w - MW * cell - 10.0f, my0 = 10.0f;
    Draw::RectangleFilled({mx0 - 3, my0 - 3}, {MW * cell + 6, MH * cell + 6}, {0, 0, 0, 150});
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            int v = MAP[y][x];
            if (!v) continue;
            Color c = v == 2 ? Color{150, 55, 55, 255} : v == 3 ? Color{55, 120, 150, 255}
                                                                : Color{90, 90, 105, 255};
            Draw::RectangleFilled({mx0 + x * cell, my0 + y * cell}, {cell - 1, cell - 1}, c);
        }
    // A fan of rays + the player.
    for (int r = -3; r <= 3; r++) {
        float cx = r / 3.0f;
        float rx = dirx + planex * cx, ry = diry + planey * cx;
        Draw::Line({mx0 + px * cell, my0 + py * cell},
                   {mx0 + (px + rx * 3.0f) * cell, my0 + (py + ry * 3.0f) * cell}, {70, 180, 90, 120});
    }
    Draw::CircleFilled({mx0 + px * cell, my0 + py * cell}, cell * 0.35f, {120, 240, 130, 255});

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
