// Example 14 — Doom Fire
// ---------------------------------------------------------------------------
// The classic PSX-Doom fire effect:
//   * a heat field seeded hot along the bottom row
//   * each cell copies the one below it, minus a little random cooling, nudged sideways by "wind"
//   * heat indexes a 37-colour black -> red -> orange -> yellow -> white palette
//
// Pure CPU + Draw::RectangleFilled on a low-res grid scaled to fill the window (resizes cleanly).

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

// Black -> red -> orange -> yellow -> white (37 steps), the traditional Doom fire ramp.
const Color kPalette[37] = {
    {  7,  7,  7,255},{ 31,  7,  7,255},{ 47, 15,  7,255},{ 71, 15,  7,255},{ 87, 23,  7,255},
    {103, 31,  7,255},{119, 31,  7,255},{143, 39,  7,255},{159, 47,  7,255},{175, 63,  7,255},
    {191, 71,  7,255},{199, 71,  7,255},{223, 79,  7,255},{223, 87,  7,255},{223, 87,  7,255},
    {215, 95,  7,255},{215, 95,  7,255},{215,103, 15,255},{207,111, 15,255},{207,119, 15,255},
    {207,127, 15,255},{207,135, 23,255},{199,135, 23,255},{199,143, 23,255},{199,151, 31,255},
    {191,159, 31,255},{191,159, 31,255},{191,167, 39,255},{191,167, 39,255},{191,175, 47,255},
    {183,175, 47,255},{183,183, 47,255},{183,183, 55,255},{207,207,111,255},{223,223,159,255},
    {239,239,199,255},{255,255,255,255},
};

int cellPx = 4;
int gw = 0, gh = 0;
std::vector<uint8_t> heat;     // heat index per cell (0..36)

void FitGrid() {
    int nw = std::max(16, Window::GetWidth()  / cellPx);
    int nh = std::max(16, Window::GetHeight() / cellPx);
    if (nw == gw && nh == gh) return;
    gw = nw; gh = nh;
    heat.assign(gw * gh, 0);
    for (int x = 0; x < gw; x++) heat[(gh - 1) * gw + x] = 36;  // bottom row = white-hot
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Doom Fire", 900, 560, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({7, 7, 7, 255});
    std::srand(1234);
    FitGrid();
    return Lumi::Result::Continue;
}

// One fixed simulation step: propagate heat upward (frame-rate independent).
void Step() {
    for (int y = 1; y < gh; y++) {
        for (int x = 0; x < gw; x++) {
            int below = heat[y * gw + x];
            int decay = (std::rand() % 3 == 0) ? 1 : 0;  // gentle cooling -> tall flames (~3/4 screen)
            int dst   = x + (std::rand() % 3) - 1;        // wind: -1..+1
            if (dst < 0) dst = 0; else if (dst >= gw) dst = gw - 1;
            int v = below - decay;
            heat[(y - 1) * gw + dst] = (uint8_t)(v < 0 ? 0 : v);
        }
    }
}

Lumi::Result AppIterate(void* appstate) {
    FitGrid();

    // Fire rises exactly one cell per step, so its speed IS the step rate. 30 Hz slows the rise
    // while staying an even 2-render-frames-per-step cadence (no judder); the flicker hides it.
    static float acc = 0.0f;
    const float ST = 1.0f / 30.0f;
    acc += (float)Window::GetFrameTime();
    for (int s = 0; s < 3 && acc >= ST; s++) { Step(); acc -= ST; }

    float cw = (float)Window::GetWidth()  / gw;
    float ch = (float)Window::GetHeight() / gh;

    Window::StartFrame();
    for (int y = 0; y < gh; y++) {
        for (int x = 0; x < gw; x++) {
            uint8_t idx = heat[y * gw + x];
            if (idx == 0) continue;                       // 0 = background, leave it dark
            Draw::RectangleFilled({x * cw, y * ch}, {cw + 1.0f, ch + 1.0f}, kPalette[idx]);
        }
    }
    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
