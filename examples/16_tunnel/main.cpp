// Example 16 — Tunnel (GPU fragment shader)
// ---------------------------------------------------------------------------
// The classic demoscene tunnel, generated per-pixel on the GPU via the effect system:
//   * load a passthrough vertex shader + tunnel fragment shader (compiled at runtime)
//   * build an Effect and feed it time + resolution uniforms each frame
//   * one fullscreen quad tagged with the effect fills the framebuffer
//
// Because the work is per-pixel on the GPU it runs at full resolution for free — no CPU cell grid.
// (11_shadertoy shows the same mechanism with a plasma; here the shader is a tunnel.)
//
// Move the mouse left/right of centre to steer — the tunnel curves that way (gradual: a few pixels
// barely bends, all the way over is the full ~30-degree lean).
//
// Keys 1-4 set the render scale via Window::SetScale — the whole scene renders at canvas/N and is
// integer-upscaled, so pixels get N:N chunky. The label keeps a constant on-screen size (32/scale).
// Heads up: the font goes funky at 3x. 32/3 ≈ 10.7px isn't a whole multiple of Kenney Mini's 8px
// grid, so the glyphs come out uneven. 1x/2x/4x (32/16/8) land on the grid and stay crisp. That's
// just how integer scaling works when the text size doesn't divide cleanly.
//
// Assets: assets/shaders/passthrough.vert and assets/shaders/tunnel.frag (GLSL).
// Kenney Mini font courtesy of Kenney — kenney.nl (CC0).

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <string>
#include <glm/glm.hpp>

EffectAsset tunnel;
FontAsset*  font = nullptr;
float elapsed = 0.0f;
int   pixelScale = 1;      // 1x..4x — how many screen pixels each rendered pixel covers (keys 1-4)

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Tunnel", 900, 560, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({0, 0, 0, 255});

    ShaderAsset& vert = AssetHandler::GetShader("assets/shaders/passthrough.vert");
    ShaderAsset& frag = AssetHandler::GetShader("assets/shaders/tunnel.frag");
    tunnel = Effects::Create(vert, frag);

    // Kenney Mini — a chunky pixel font; the second arg is the atlas render size.
    font = &AssetHandler::GetFont("assets/fonts/Kenney Mini.ttf", 32);

    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    elapsed += (float)Window::GetFrameTime();

    // Keys 1-4 pick the render scale. The engine's WebGpuScaleMode does the upscale for us: render
    // the shader into a canvas/N framebuffer and Stretch blows it back up to the window — a cheap,
    // crunchy retro pixel size. 1x = Native (render straight at canvas resolution, sharp).
    if (Input::KeyPressed(SDLK_1)) pixelScale = 1;
    if (Input::KeyPressed(SDLK_2)) pixelScale = 2;
    if (Input::KeyPressed(SDLK_3)) pixelScale = 3;
    if (Input::KeyPressed(SDLK_4)) pixelScale = 4;

    Window::SetScale(pixelScale);

    // Steer with the mouse: horizontal offset from the window centre, -1 (full left) .. +1 (right).
    float halfW = std::max(1.0f, (float)Window::GetWidth() * 0.5f);
    float steer = std::clamp((Input::GetMousePosition().x - halfW) / halfW, -1.0f, 1.0f);

    // GetWidth/Height now report the internal render size, so the shader's aspect stays correct.
    tunnel["time"]       = elapsed;
    tunnel["resolution"] = glm::vec2((float)Window::GetWidth(), (float)Window::GetHeight());
    tunnel["steer"]      = steer;

    Window::StartFrame();
    Draw::SetEffect(tunnel);
    Draw::RectangleFilled({0, 0}, {(float)Window::GetWidth(), (float)Window::GetHeight()}, WHITE);
    Draw::ClearEffects();

    // Constant ~32px on-screen label (see the header note about 3x looking a touch uneven).
    Text::DrawText(*font, {8.f / (float)pixelScale, 4.f / (float)pixelScale}, "Scale: " + std::to_string(pixelScale) + "x  (keys 1-4)",
                   WHITE, 32.0f / (float)pixelScale);
    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
