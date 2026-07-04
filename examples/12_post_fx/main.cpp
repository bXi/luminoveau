// Example 12 — Post-FX Stack
// ---------------------------------------------------------------------------
// Builds a runtime-toggleable chain of full-screen post-processing effects:
//   * several custom fragment shaders loaded as Effects
//   * Draw::SetEffect / Draw::AddEffect to stack them in order over a live scene
//   * effects are ping-ponged across the framebuffer by the renderer
//   * a separate HUD layer (a renderToScreen sprite render target) drawn ON TOP of the
//     post-processed scene, so UI text isn't affected or hidden by the effects
//
// Keys 1/2/3 toggle grayscale / wave / vignette. The scene underneath is a few bouncing
// shapes so you can see the effects act on real content, while the HUD stays crisp on top.
//
// Assets: assets/shaders/passthrough.vert + grayscale.frag, wave.frag, vignette.frag.

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <string>
#include <vector>

int width  = 900;
int height = 700;

EffectAsset grayscale, wave, vignette;
bool grayscaleOn = false, waveOn = false, vignetteOn = true;
float elapsed = 0.0f;

// A bouncing coloured circle — just something for the effects to act on.
struct Blob { vf2d pos, vel; float r; Color col; };
std::vector<Blob> blobs;

EffectAsset MakeEffect(const char* frag) {
    ShaderAsset& v = AssetHandler::GetShader("assets/shaders/passthrough.vert");
    ShaderAsset& f = AssetHandler::GetShader(frag);
    return Effects::Create(v, f);
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Post-FX Stack", width, height, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({18, 20, 30, 255});

    grayscale = MakeEffect("assets/shaders/grayscale.frag");
    wave      = MakeEffect("assets/shaders/wave.frag");
    vignette  = MakeEffect("assets/shaders/vignette.frag");

    // Post-processing effects act on the whole framebuffer, so anything drawn into the same layer
    // as an effect gets post-processed (or overwritten) by it — that's why a HUD drawn alongside
    // an effect scene disappears. The fix is to give the HUD its own layer that composites ON TOP
    // of the (already post-processed) main scene. A sprite render target with renderToScreen=true
    // is exactly that: an extra framebuffer blitted over the primary one each frame. We clear it to
    // transparent so only the HUD's own pixels show; the scene beneath stays visible.
    SpriteRenderTargetConfig hudCfg;
    hudCfg.renderToScreen = true;                 // composite this layer onto the screen
    hudCfg.clearOnLoad    = true;
    hudCfg.clearColor     = {0, 0, 0, 0};         // transparent — only HUD pixels are opaque
    hudCfg.blendMode      = BlendMode::SrcAlpha;   // alpha-blend the HUD over the scene
    Renderer::CreateSpriteRenderTarget("hud", hudCfg);

    Color palette[] = {{255,110,110,255},{110,200,255,255},{160,255,140,255},{255,220,120,255}};
    for (int i = 0; i < 8; i++)
        blobs.push_back({{80.0f + i * 90.0f, 120.0f + (i % 3) * 160.0f},
                         {120.0f + i * 15.0f, 90.0f + i * 10.0f}, 30.0f + i * 4.0f, palette[i % 4]});
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    float dt = (float)Window::GetFrameTime();
    elapsed += dt;

    if (Input::KeyPressed(SDLK_1)) grayscaleOn = !grayscaleOn;
    if (Input::KeyPressed(SDLK_2)) waveOn      = !waveOn;
    if (Input::KeyPressed(SDLK_3)) vignetteOn  = !vignetteOn;

    // Bounce the scene shapes around.
    for (Blob& b : blobs) {
        b.pos += b.vel * dt;
        if (b.pos.x < b.r || b.pos.x > width  - b.r) b.vel.x = -b.vel.x;
        if (b.pos.y < b.r || b.pos.y > height - b.r) b.vel.y = -b.vel.y;
    }

    // Feed uniforms.
    grayscale["amount"]  = 1.0f;
    wave["time"]         = elapsed;
    wave["strength"]     = 0.012f;
    vignette["strength"] = 1.3f;

    // Build the active stack in a fixed order.
    std::vector<EffectAsset*> stack;
    if (grayscaleOn) stack.push_back(&grayscale);
    if (waveOn)      stack.push_back(&wave);
    if (vignetteOn)  stack.push_back(&vignette);

    Window::StartFrame();

    // Apply the stack: first effect via SetEffect, the rest via AddEffect.
    for (int i = 0; i < (int)stack.size(); i++) {
        if (i == 0) Draw::SetEffect(*stack[i]);
        else        Draw::AddEffect(*stack[i]);
    }

    for (Blob& b : blobs) Draw::CircleFilled(b.pos, b.r, b.col);

    Draw::ClearEffects();   // stop tagging draws for the main scene layer

    // Draw the HUD into its own "hud" layer, which renders on top of the post-processed scene.
    // Route draws there with SetTargetRenderPass, then switch back to the default "2dsprites" layer.
    Draw::SetTargetRenderPass("hud");
    FontAsset& font = AssetHandler::GetDefaultFont();
    auto tag = [](bool on) { return on ? "ON" : "off"; };
    Text::DrawText(font, {10.0f, 6.0f},
        std::string("[1] Grayscale ") + tag(grayscaleOn) + "   [2] Wave " + tag(waveOn) + "   [3] Vignette " + tag(vignetteOn),
        WHITE, 24.0f);
    Draw::SetTargetRenderPass("2dsprites");

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) { Window::Close(); }
