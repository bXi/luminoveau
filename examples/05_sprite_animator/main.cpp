// Example 05 — Sprite Animation: Dice Roll
// ---------------------------------------------------------------------------
// The sprite-animation system, shown with a rolling die instead of a walk cycle:
//   * loading a sprite sheet (AssetHandler::GetTexture) with a crisp nearest sampler
//   * picking a frame out of a grid sheet with Draw::TexturePart (source rect in pixels)
//   * driving frames from game state — here a timed "tumble" that decelerates and settles
//
// Press Space to roll. The die tumbles through faces, slows down, and stops on a value.
//
// Asset: assets/diceWhite.png — a 3x2 grid of 64x64 faces (192x128 total). The faces are NOT in
// value order on this sheet (top row 6 1 2, bottom row 5 3 4), so a value->cell lookup maps a die
// value to its sprite — see valueToCell below.
// Dice sprite sheet courtesy of Kenney — kenney.nl (CC0).

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>

int initWidth  = 640;   // initial window size (desktop); the view is fully resizable
int initHeight = 480;

// Sprite-sheet layout.
int   facePx    = 64;   // each face is 64x64 in the sheet
int   sheetCols = 3;
int   sheetRows = 2;
float maxScale  = 3.0f; // cap on-screen size at 3x (192x192); shrinks to fit smaller windows

// The faces on this sheet are NOT in value order. The 3x2 grid reads:
//   cell 0 1 2  ->  6 1 2
//   cell 3 4 5  ->  5 3 4
// So to show a die value we look up which cell holds it. Indexed by (value - 1):
int valueToCell[6] = { 1, 2, 4, 5, 3, 0 };  // value 1->cell1, 2->2, 3->4, 4->5, 5->3, 6->0

// Roll feel.
float rollTime = 1.3f;  // total tumble duration (seconds)
float fastSwap = 0.05f; // face swap interval at the start of a roll
float slowSwap = 0.28f; // face swap interval at the end (visibly decelerating)

TextureAsset sheet;
int   value     = 1;      // current die value (1..6)
bool  rolling   = false;
float rollLeft  = 0.0f;   // seconds remaining in the tumble
float swapTimer = 0.0f;   // counts down to the next face swap

// Interval between face swaps, easing from fast to slow as the roll winds down.
float CurrentSwapInterval() {
    float t = 1.0f - (rollLeft / rollTime);          // 0 at start, 1 at end
    return fastSwap + (slowSwap - fastSwap) * t;      // linear ease-out
}

void StartRoll() {
    rolling   = true;
    rollLeft  = rollTime;
    swapTimer = 0.0f;
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Dice Roll", initWidth, initHeight, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({28, 30, 40, 255});
    std::srand((unsigned)std::time(nullptr));

    sheet = AssetHandler::GetTexture("assets/diceWhite.png");
    sheet.gpuSampler = Renderer::GetSampler(ScaleMode::Nearest);
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    float dt = (float)Window::GetFrameTime();

    if (Input::KeyPressed(SDLK_SPACE) && !rolling) StartRoll();

    if (rolling) {
        rollLeft  -= dt;
        swapTimer -= dt;
        if (swapTimer <= 0.0f) {
            value = std::rand() % 6 + 1;             // show a random value as it tumbles
            swapTimer = CurrentSwapInterval();
        }
        if (rollLeft <= 0.0f) {
            rolling = false;
            value = std::rand() % 6 + 1;             // final settled value
        }
    }

    // Value -> sheet cell -> source rectangle in the sheet (pixels).
    int cell = valueToCell[value - 1];
    int col  = cell % sheetCols;
    int row  = cell / sheetCols;
    rectf src = {(float)(col * facePx), (float)(row * facePx), (float)facePx, (float)facePx};

    // Everything is laid out against the live window size so it always fits and stays centred.
    float w = (float)Window::GetWidth();
    float h = (float)Window::GetHeight();

    const float margin = 40.0f;   // minimum breathing room at the edges
    const float gap    = 26.0f;   // space between the die and the caption

    // Caption first — measure it so we can centre it and reserve its exact height.
    FontAsset& font = AssetHandler::GetDefaultFont();
    std::string caption = rolling ? "Rolling..."
                                  : "You rolled " + std::to_string(value) + " - Space to roll";
    float capSize = 28.0f;
    float capW    = (float)Text::MeasureText(font, caption, capSize);
    if (capW > w - margin) { capSize *= (w - margin) / capW; capW = (float)Text::MeasureText(font, caption, capSize); }
    float capH = Text::GetRenderedTextSize(font, caption, capSize).y;

    // Cap the die at 3x, but shrink it so the whole die + gap + caption block fits with margins.
    float fitW  = (w - margin * 2.0f) / (float)facePx;
    float fitH  = (h - margin * 2.0f - gap - capH) / (float)facePx;
    float scale = std::clamp(std::min({maxScale, fitW, fitH}), 0.5f, maxScale);

    vf2d drawSize = {facePx * scale, facePx * scale};

    // Centre the die + caption as one block. At 3x with room to spare it just sits in the middle;
    // when the window is short the block shrinks (above) and stays centred.
    float blockH = drawSize.y + gap + capH;
    float top    = std::max(margin, (h - blockH) * 0.5f);
    vf2d  drawPos = {(w - drawSize.x) * 0.5f, top};
    float capY    = top + drawSize.y + gap;

    Window::StartFrame();
    Draw::TexturePart(sheet, drawPos, drawSize, src);
    Text::DrawText(font, {(w - capW) * 0.5f, capY}, caption, WHITE, capSize);
    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
