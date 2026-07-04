// Example 11 — ShaderToy (custom fullscreen shader)
// ---------------------------------------------------------------------------
// Runs a custom GLSL fragment shader across the whole screen using the effect system:
//   * load a vertex + fragment shader (AssetHandler::GetShader — compiled at runtime)
//   * build an Effect from them (Effects::Create)
//   * feed it uniforms each frame with effect["name"] = value
//   * apply it with Draw::SetEffect; effects post-process the entire framebuffer
//
// The plasma is fully generated in the shader. Move the mouse to shift it.
//
// Assets: assets/shaders/passthrough.vert and assets/shaders/plasma.frag (GLSL).

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <glm/glm.hpp>

EffectAsset plasma;
float elapsed = 0.0f;

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — ShaderToy", 900, 700, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({0, 0, 0, 255});

    ShaderAsset& vert = AssetHandler::GetShader("assets/shaders/passthrough.vert");
    ShaderAsset& frag = AssetHandler::GetShader("assets/shaders/plasma.frag");
    plasma = Effects::Create(vert, frag);
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    elapsed += (float)Window::GetFrameTime();

    // Feed the shader its uniforms (names match the EffectParams block in plasma.frag).
    vf2d mouse = Input::GetMousePosition();
    plasma["time"]  = elapsed;
    plasma["mouse"] = glm::vec2(mouse.x / (float)Window::GetWidth(),
                                mouse.y / (float)Window::GetHeight());

    Window::StartFrame();
    // Fullscreen quad tagged with the effect; the effect fills the framebuffer.
    Draw::SetEffect(plasma);
    Draw::RectangleFilled({0, 0}, {(float)Window::GetWidth(), (float)Window::GetHeight()}, WHITE);
    Draw::ClearEffects();
    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
