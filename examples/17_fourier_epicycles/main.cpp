// Example 17 — Fourier Epicycles
// ---------------------------------------------------------------------------
// A chain of rotating circles (epicycles) whose tip traces a square wave — the geometric picture of
// a Fourier series. Odd harmonics only: radius 1/k, frequency k, amplitude 4/(PI*k). The pen's
// height is plotted to the right as a scrolling wave, so you watch the circles "draw" the signal and
// the flat-topped square shape build up. (PI comes from the engine's math constants.)
//
// The plot scrolls at a fixed pixels-per-second rate (frame-rate independent) so the square wave is
// slow enough to read, and both the epicycles and the wave are drawn with a soft glow.
//
// Pure vector Draw calls, laid out against the live window size (fully resizable).

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <deque>

const int   TERMS     = 16;         // number of odd harmonics (more = crisper square corners)
const float SPIN      = 1.0f;       // epicycle angular speed
const float SCROLL_PX = 70.0f;      // wave scroll speed (pixels / second) — lower reads slower

float t = 0.0f;
float scrollAcc = 0.0f;
std::deque<float> wave;             // pen-height offsets, one per pixel column (front = newest, left)

// Thick circle outline (Draw::Circle has no width) — a ring of short thick segments.
void ThickCircle(vf2d c, float r, Color col, float width, int seg = 56) {
    vf2d prev = {c.x + r, c.y};
    for (int i = 1; i <= seg; i++) {
        float a = 2.0f * PI * (float)i / (float)seg;
        vf2d p  = {c.x + r * std::cos(a), c.y + r * std::sin(a)};
        Draw::ThickLine(prev, p, col, width);
        prev = p;
    }
}

// A glowing line: a wide, faint underlay plus a bright thin core.
void GlowLine(vf2d a, vf2d b, Color core, Color glow, float coreW = 2.5f, float glowW = 7.0f) {
    Draw::ThickLine(a, b, glow, glowW);
    Draw::ThickLine(a, b, core, coreW);
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Fourier Epicycles", 900, 520, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({8, 10, 16, 255});
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    float dt = std::min((float)Window::GetFrameTime(), 0.1f);
    t += dt * SPIN;

    float w = (float)Window::GetWidth();
    float h = (float)Window::GetHeight();
    float R  = std::min(w, h) * 0.20f;
    float ox = w * 0.26f, oy = h * 0.5f;

    Window::StartFrame();

    // Chain the epicycles; the tip after the last one is the pen.
    float cx = ox, cy = oy;
    for (int i = 0; i < TERMS; i++) {
        int   k   = 2 * i + 1;                     // 1, 3, 5, ...
        // Lanczos sigma factor: roll off the higher harmonics with a sinc so the square wave's corners
        // stop ringing/overshooting (the Gibbs phenomenon). Without it the flat tops overshoot ~9%.
        float sx    = PI * (i + 0.5f) / (float)TERMS;
        float sigma = std::sin(sx) / sx;
        float rad = R * (4.0f / PI) / (float)k * sigma;    // square-wave coefficient, Gibbs-tamed
        float ang = k * t;
        float nx  = cx + rad * std::cos(ang);
        float ny  = cy + rad * std::sin(ang);
        if (rad > 1.5f) ThickCircle({cx, cy}, rad, {95, 120, 165, 230}, 2.5f);
        Draw::ThickLine({cx, cy}, {nx, ny}, {150, 165, 195, 220}, 2.0f);
        cx = nx; cy = ny;
    }
    Draw::CircleFilled({cx, cy}, 5.0f, {120, 235, 205, 255});

    // Scroll the plot at a fixed pixels/second and sample the pen height once per pixel column.
    float plotX = ox + R * 1.5f + 30.0f;
    int   cols  = std::max(1, (int)(w - plotX));
    scrollAcc += SCROLL_PX * dt;
    while (scrollAcc >= 1.0f) {
        wave.push_front(cy - oy);                  // store height as an offset from the centre line
        while ((int)wave.size() > cols) wave.pop_back();
        scrollAcc -= 1.0f;
    }

    // Faint centre line, then the wave with a glow, then the connector from the pen to the plot.
    Draw::Line({plotX, oy}, {plotX + cols, oy}, {40, 48, 66, 255});
    if (!wave.empty()) {
        GlowLine({cx, cy}, {plotX, oy + wave.front()}, {160, 245, 215, 180}, {60, 130, 115, 90});
        for (size_t i = 1; i < wave.size(); i++)
            GlowLine({plotX + (i - 1), oy + wave[i - 1]}, {plotX + i, oy + wave[i]},
                     {150, 245, 215, 255}, {40, 150, 120, 120});
    }

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
