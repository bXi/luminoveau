// Example 10 — Particle Fountain
// ---------------------------------------------------------------------------
// Uses the GPU particle system:
//   * Particles::Init() sets up the compute-driven particle pass (it auto-attaches to the
//     primary framebuffer)
//   * a ParticleSystemConfig describes emission, gravity, lifetime, colour gradient and size
//   * the fountain follows the mouse via Particles::SetPosition each frame
//
// Move the mouse to aim the fountain. Everything is simulated on the GPU.

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <string>

ParticleSystemHandle fountain;

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — Particle Fountain", 900, 700, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({8, 8, 14, 255});

    // Note: the engine already calls Particles::Init() during renderer setup (InitWindow),
    // which attaches the particle pass to the primary framebuffer. We just create a system.
    ParticleSystemConfig cfg;
    // Big pool + high emit rate for a dense, smooth stream. Two rules to avoid a pulsing look:
    //   1. keep maxParticles >> emitRate * lifetimeMax (staggered respawn stays smooth)
    //   2. keep the physics gentle — very fast launch + strong gravity bunches particles into
    //      visible arcs that read as pulses even when emission is steady.
    // emitRate is particles PER SECOND; small values (1, 10) look sparse — this is not a bug.
    cfg.maxParticles   = 200000;
    cfg.spawnPosition  = {450.0f, 500.0f, 0.0f};
    cfg.spawnRadius    = 18.0f;
    cfg.spawnVelocity  = {0.0f, -140.0f, 0.0f};  // gentle upward (screen +Y is down)
    cfg.velocitySpread = 60.0f;
    cfg.gravity        = {0.0f, 110.0f, 0.0f};    // gentle pull back down
    cfg.drag           = 0.7f;
    cfg.lifetimeMin    = 2.0f;
    cfg.lifetimeMax    = 4.0f;
    cfg.lifetimeBias   = 2.0f;
    cfg.emitRate       = 2500.0f;
    cfg.sizeStartMin   = 4.0f;
    cfg.sizeStartMax   = 8.0f;
    cfg.sizeEndMin     = 0.5f;
    cfg.sizeEndMax     = 1.5f;
    cfg.shape          = ParticleShape::SoftCircle;
    cfg.SetColors({
        {1.0f, 0.95f, 0.5f, 1.0f},   // bright yellow
        {1.0f, 0.5f,  0.1f, 0.9f},   // orange
        {0.9f, 0.1f,  0.1f, 0.0f},   // fading red
    });

    fountain = Particles::CreateSystem(cfg);
    Particles::Start(fountain);
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    // GetFrameTime() is 0 on the first frame and capped thereafter (the engine clamps it), so a
    // slow startup or a hitch can't dump a huge dt into the particle sim.
    float dt = (float)Window::GetFrameTime();

    vf2d mouse = Input::GetMousePosition();
    Particles::SetPosition(fountain, {mouse.x, mouse.y, 0.0f});

    Particles::Update(dt);

    Window::StartFrame();
    Draw::Particles(fountain);   // queues the system for the particle pass to draw

    FontAsset& font = AssetHandler::GetDefaultFont();
    Text::DrawText(font, {10.0f, 6.0f}, "Move the mouse to aim the fountain", WHITE, 24.0f);

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) {
    Particles::DestroySystem(fountain);   // engine calls Particles::Quit() itself on shutdown
    Window::Close();
}
