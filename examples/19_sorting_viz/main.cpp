// Example 19 — Sorting Visualizer
// ---------------------------------------------------------------------------
// Watch sorting algorithms run as animated bars. Each algorithm is run once on a copy to record its
// exact sequence of compares + swaps, then those operations are replayed a few per frame — so the
// animation is the real algorithm, not a fake. Cycles Bubble -> Insertion -> Selection -> Quicksort.
//
// Pure Draw::RectangleFilled + Text, laid out against the live window (fully resizable).

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

struct Op { int a, b; bool swap; };

const int   N = 90;
const char* NAMES[4] = { "Bubble Sort", "Insertion Sort", "Selection Sort", "Quicksort" };

std::vector<int> bars;
std::vector<Op>  ops;
size_t opIdx = 0;
int    hlA = -1, hlB = -1;
int    algo = 0;
int    phase = 0;                 // 0 = generate, 1 = play, 2 = hold
float  holdT = 0.0f;
float  opsPerSec = 1.0f, opAcc = 0.0f;   // time-based replay rate (frame-rate independent)

void cmp(int i, int j)                 { ops.push_back({i, j, false}); }
void swp(std::vector<int>& v, int i, int j) { ops.push_back({i, j, true}); std::swap(v[i], v[j]); }

void bubble(std::vector<int>& v) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N - 1 - i; j++) { cmp(j, j + 1); if (v[j] > v[j + 1]) swp(v, j, j + 1); }
}
void insertion(std::vector<int>& v) {
    for (int i = 1; i < N; i++)
        for (int j = i; j > 0; j--) { cmp(j - 1, j); if (v[j - 1] > v[j]) swp(v, j - 1, j); else break; }
}
void selection(std::vector<int>& v) {
    for (int i = 0; i < N; i++) {
        int m = i;
        for (int j = i + 1; j < N; j++) { cmp(m, j); if (v[j] < v[m]) m = j; }
        if (m != i) swp(v, i, m);
    }
}
void quick(std::vector<int>& v, int lo, int hi) {
    if (lo >= hi) return;
    int pivot = v[hi], i = lo;
    for (int j = lo; j < hi; j++) { cmp(j, hi); if (v[j] < pivot) { if (i != j) swp(v, i, j); i++; } }
    swp(v, i, hi);
    quick(v, lo, i - 1); quick(v, i + 1, hi);
}

void Shuffle() {
    for (int i = 0; i < N; i++) bars[i] = i + 1;
    for (int i = N - 1; i > 0; i--) std::swap(bars[i], bars[std::rand() % (i + 1)]);
}

void Generate() {
    Shuffle();
    std::vector<int> work = bars;    // record ops from an identical copy, replay swaps onto `bars`
    ops.clear();
    switch (algo) {
        case 0: bubble(work); break;
        case 1: insertion(work); break;
        case 2: selection(work); break;
        default: quick(work, 0, N - 1); break;
    }
    opIdx = 0;
    opsPerSec = std::max(1.0f, ops.size() / 8.0f);   // whole run plays in ~8 seconds
    opAcc = 0.0f;
    phase = 1;
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Sorting Visualizer", 900, 520, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({12, 14, 20, 255});
    std::srand(77);
    bars.assign(N, 0);
    Shuffle();
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    float dt = (float)Window::GetFrameTime();

    if (phase == 0) {
        Generate();
    } else if (phase == 1) {
        opAcc += dt * opsPerSec;
        while (opAcc >= 1.0f && opIdx < ops.size()) {
            Op o = ops[opIdx++];
            hlA = o.a; hlB = o.b;
            if (o.swap) std::swap(bars[o.a], bars[o.b]);
            opAcc -= 1.0f;
        }
        if (opIdx >= ops.size()) { phase = 2; holdT = 0.0f; hlA = hlB = -1; }
    } else {
        holdT += dt;
        if (holdT > 1.2f) { algo = (algo + 1) % 4; phase = 0; }
    }

    float w = (float)Window::GetWidth();
    float h = (float)Window::GetHeight();
    float top = 46.0f;
    float barW = w / N;

    Window::StartFrame();
    for (int i = 0; i < N; i++) {
        float bh = (bars[i] / (float)N) * (h - top - 8.0f);
        bool  hot = (i == hlA || i == hlB);
        Color c = hot ? Color{255, 220, 90, 255}
                      : Color{ (unsigned)(40 + bars[i] * 2), 200u, (unsigned)(150 + bars[i]), 255u };
        Draw::RectangleFilled({i * barW, h - bh}, {barW - 1.0f, bh}, c);
    }
    FontAsset& font = AssetHandler::GetDefaultFont();
    Text::DrawText(font, {12.0f, 8.0f}, NAMES[algo], WHITE, 28.0f);
    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
