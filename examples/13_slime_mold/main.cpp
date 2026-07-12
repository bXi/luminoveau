// Example 13 — Slime Mold (Physarum)
// ---------------------------------------------------------------------------
// An agent-based reaction pattern that self-organises into branching networks:
//   * thousands of agents each steer toward the strongest scent (three sensors)
//   * every agent drops a scent trail; the trail field diffuses + decays each frame
//   * the emergent structure is drawn straight from that CPU trail field
//
// Pure CPU + Draw::RectangleFilled — a low-res simulation grid scaled to fill the window, so it
// resizes cleanly and needs no shaders.
//
// Interactive: hold the left mouse button to paint a blob of scent — the agents smell it and swarm
// toward the cursor, so you can lead the slime around. Press R to reseed from scratch.

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

const float TAU = 6.2831853f;

int cellPx = 5;                 // one simulation cell = this many screen pixels (pre-scale)
int gw = 0, gh = 0;             // simulation grid size (in cells)
std::vector<float> trail;       // scent field
std::vector<float> blurred;     // scratch for the diffuse pass

struct Agent { float x, y, a; };
std::vector<Agent> agents;

// Behaviour knobs (in grid units).
float senseDist = 4.0f, senseAng = 0.5f, turn = 0.45f, speed = 0.5f;   // slower agents (still 60 Hz)
float deposit   = 0.9f, decay = 0.93f, diffuse = 0.5f;                 // trails linger a touch longer

// Mouse interaction (set each frame in AppIterate, read by the sim Step).
bool  painting = false;                 // left button held
float paintGx = 0.0f, paintGy = 0.0f;   // cursor position in grid cells

float frand() { return (float)std::rand() / (float)RAND_MAX; }

float wrap(float v, float hi) { v = std::fmod(v, hi); return v < 0 ? v + hi : v; }
int   wrapi(int v, int hi)    { v %= hi; return v < 0 ? v + hi : v; }

// Clear the scent field and respawn agents at random (uses the current grid size).
void Reset() {
    trail.assign(gw * gh, 0.0f);
    blurred.assign(gw * gh, 0.0f);
    int n = std::max(200, gw * gh / 16);
    agents.resize(n);
    for (auto& ag : agents) { ag.x = frand() * gw; ag.y = frand() * gh; ag.a = frand() * TAU; }
}

// Rebuild the grid + reseed when the window size changes.
void FitGrid() {
    int nw = std::max(16, Window::GetWidth()  / cellPx);
    int nh = std::max(16, Window::GetHeight() / cellPx);
    if (nw == gw && nh == gh) return;
    gw = nw; gh = nh;
    Reset();
}

// Paint a disc of strong scent at a screen position (the mouse) so agents are drawn to it.
void PaintAt(float sx, float sy) {
    if (gw == 0 || gh == 0) return;
    float cw = (float)Window::GetWidth() / gw, ch = (float)Window::GetHeight() / gh;
    int cx = (int)(sx / cw), cy = (int)(sy / ch);
    int rad = std::max(2, gw / 70);
    for (int dy = -rad; dy <= rad; dy++)
        for (int dx = -rad; dx <= rad; dx++) {
            if (dx * dx + dy * dy > rad * rad) continue;
            int gx = wrapi(cx + dx, gw), gy = wrapi(cy + dy, gh);
            trail[gy * gw + gx] = std::min(trail[gy * gw + gx] + 10.0f, 30.0f);
        }
}

float Sense(const Agent& ag, float off) {
    float sx = wrap(ag.x + std::cos(ag.a + off) * senseDist, (float)gw);
    float sy = wrap(ag.y + std::sin(ag.a + off) * senseDist, (float)gh);
    return trail[(int)sy * gw + (int)sx];
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Slime Mold", 900, 560, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({6, 8, 12, 255});
    std::srand(1337);
    FitGrid();
    return Lumi::Result::Continue;
}

// One fixed simulation step (frame-rate independent — see the accumulator in AppIterate).
void Step() {
    // Steer + move + deposit.
    for (auto& ag : agents) {
        float f = Sense(ag, 0.0f), l = Sense(ag, senseAng), r = Sense(ag, -senseAng);
        if (f >= l && f >= r)      { /* keep heading */ }
        else if (l > r)             ag.a += turn;
        else if (r > l)             ag.a -= turn;
        else                        ag.a += (frand() - 0.5f) * turn * 2.0f;

        // While the mouse is held, agents within reach also steer toward the cursor so the swarm
        // visibly rushes it (on top of the scent gradient, which alone only pulls close neighbours).
        if (painting) {
            float ddx = paintGx - ag.x, ddy = paintGy - ag.y;
            float ar  = gw * 0.5f;
            if (ddx * ddx + ddy * ddy < ar * ar) {
                float da = std::atan2(paintGy - ag.y, paintGx - ag.x) - ag.a;
                ag.a += std::atan2(std::sin(da), std::cos(da)) * 0.20f;   // normalised turn toward cursor
            }
        }

        ag.x = wrap(ag.x + std::cos(ag.a) * speed, (float)gw);
        ag.y = wrap(ag.y + std::sin(ag.a) * speed, (float)gh);
        trail[(int)ag.y * gw + (int)ag.x] += deposit;
    }

    // Diffuse (3x3 box blur) then decay.
    for (int y = 0; y < gh; y++) {
        for (int x = 0; x < gw; x++) {
            float sum = 0.0f;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    sum += trail[wrapi(y + dy, gh) * gw + wrapi(x + dx, gw)];
            float b = sum / 9.0f;
            float orig = trail[y * gw + x];
            blurred[y * gw + x] = (orig + (b - orig) * diffuse) * decay;
        }
    }
    trail.swap(blurred);
}

Lumi::Result AppIterate(void* appstate) {
    FitGrid();

    if (Input::KeyPressed(SDLK_R)) Reset();
    // While the left button is held: paint scent under the cursor AND let the sim steer agents at it.
    painting = Input::MouseButtonDown(1);
    if (painting) {
        vf2d m = Input::GetMousePosition();
        paintGx = m.x * gw / (float)Window::GetWidth();
        paintGy = m.y * gh / (float)Window::GetHeight();
        PaintAt(m.x, m.y);
    }

    // Advance the sim at a fixed 60 Hz regardless of render frame rate, so it looks the same at
    // 60 fps or 3000 fps. Cap the catch-up so a slow frame can't spiral.
    static float acc = 0.0f;
    const float ST = 1.0f / 60.0f;
    acc += (float)Window::GetFrameTime();
    for (int s = 0; s < 4 && acc >= ST; s++) { Step(); acc -= ST; }

    // Render the field, one rect per lit cell, scaled to fill the window.
    float cw = (float)Window::GetWidth()  / gw;
    float ch = (float)Window::GetHeight() / gh;

    Window::StartFrame();
    for (int y = 0; y < gh; y++) {
        for (int x = 0; x < gw; x++) {
            float v = trail[y * gw + x];
            if (v < 0.02f) continue;
            float i = v > 1.5f ? 1.0f : v / 1.5f;
            Color c{ (unsigned)(i * 90.0f), (unsigned)(i * 235.0f), (unsigned)(i * 205.0f), 255u };
            Draw::RectangleFilled({x * cw, y * ch}, {cw + 1.0f, ch + 1.0f}, c);
        }
    }
    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
