// Example 09 — 3D Walker
// ---------------------------------------------------------------------------
// A first-person walk through a small 3D scene, showing:
//   * a 3D camera driven by mouse-look (relative mouse mode + GetMouseDelta) and WASD movement
//   * dynamic lights: a directional "sun" that casts shadows, plus an orbiting point light
//   * many lit model instances placed with per-instance transforms
//
// Mouse to look, WASD to move, Esc to release the mouse (click to recapture).
//
// The engine's default framebuffer already contains the 3D pass, so adding models to the Scene
// and calling StartFrame/EndFrame is all it takes to see them.
//
// The OLC::SANITY::CUBE texture (the labelled orientation cube) is by OneLoneCoder (javidx9) —
// github.com/OneLoneCoder.

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>

float moveSpeed  = 6.0f;      // units/sec
float mouseSens  = 0.0022f;   // radians per pixel of mouse movement
float pitchLimit = 1.4f;      // ~80°, so we can't flip over

ModelAsset* cube       = new ModelAsset();   // plain floor + pillars
ModelAsset* sanityCube = new ModelAsset();   // textured showcase cube

vf3d  camPos  = {0.0f, 1.6f, 8.0f};    // stand on the south side...
float yaw     = 3.14159f * 1.5f;        // ...looking toward +Z (North), so North reads "forward"
float pitch   = 0.0f;
float elapsed = 0.0f;
bool  mouseCaptured = true;

// The direction the camera is facing, from yaw + pitch.
vf3d Forward() {
    return {std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
}

// A point light that reaches across the room. The engine's default attenuation fades to nothing
// within a few units, which makes a room-scale scene look unlit; these gentler falloffs light
// things ~15 units out.
Light& AddRoomLight(vf3d pos, Color color, float intensity) {
    Light& L = Scene::AddPointLight(pos, color, intensity);
    L.linear    = 0.05f;
    L.quadratic = 0.008f;
    return L;
}

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("Luminoveau Example — 3D Walker", 1024, 720, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({12, 14, 22, 255});

    Scene::SetCameraFOV(70.0f);
    Scene::SetAmbientLight({26, 28, 38, 255});   // low ambient so the dynamic lights read clearly

    Window::SetRelativeMouseMode(true);          // capture the mouse for look control

    *cube = AssetHandler::CreateCube(1.0f, CubeUVLayout::SingleTexture);

    Scene::AddModel(cube, {0, -0.5f, 0}, {0, 0, 0}, {40, 1, 40});   // floor

    // A scatter of pillars at varied heights (all the same cube mesh, different transforms).
    struct Pillar { vf3d pos; float height; };
    Pillar pillars[] = {
        {{-4, 0, -3}, 3.0f}, {{3, 0, -5}, 2.0f}, {{6, 0, 2}, 4.0f}, {{-6, 0, 4}, 1.5f},
        {{0, 0, -8}, 5.0f},  {{-2, 0, 6}, 2.5f}, {{5, 0, -1}, 1.5f}, {{-7, 0, -6}, 3.5f},
    };
    for (Pillar& p : pillars) {
        vf3d pos = {p.pos.x, p.height * 0.5f, p.pos.z};   // rest the pillar on the floor
        Scene::AddModel(cube, pos, {0, 0, 0}, {1.0f, p.height, 1.0f});
    }

    // A textured cube in the middle, to show textured models lit and shadowed next to plain ones.
    *sanityCube = AssetHandler::CreateCube(1.0f, CubeUVLayout::Atlas4x4);
    sanityCube->texture = AssetHandler::GetTexture("assets/sanity_cube.png");
    sanityCube->texture.gpuSampler = Renderer::GetSampler(ScaleMode::Linear);
    Scene::AddModel(sanityCube, {0.0f, 1.0f, 0.0f}, {0, 0, 0}, {2.0f, 2.0f, 2.0f});

    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    float dt = (float)Window::GetFrameTime();
    elapsed += dt;

    // ── Mouse capture toggle ─────────────────────────────────────────────────────
    if (Input::KeyPressed(SDLK_ESCAPE)) {
        mouseCaptured = false;
        Window::SetRelativeMouseMode(false);
    }
    if (!mouseCaptured && Input::MouseButtonPressed(SDL_BUTTON_LEFT)) {
        mouseCaptured = true;
        Window::SetRelativeMouseMode(true);
    }

    // ── Look: mouse movement (relative mode) turns the camera ────────────────────
    if (mouseCaptured) {
        vf2d md = Input::GetMouseDelta();
        yaw   -= md.x * mouseSens;
        pitch -= md.y * mouseSens;
        pitch = std::clamp(pitch, -pitchLimit, pitchLimit);
    }

    // ── Move on the ground plane relative to where we're facing ──────────────────
    vf3d fwd   = Forward();
    vf3d flat  = vf3d{fwd.x, 0.0f, fwd.z}.norm();   // forward, flattened to the ground
    vf3d right = vf3d{flat.z, 0.0f, -flat.x};       // 90° to the right of "flat"
    if (Input::KeyDown(SDLK_W)) camPos += flat  * (moveSpeed * dt);
    if (Input::KeyDown(SDLK_S)) camPos -= flat  * (moveSpeed * dt);
    if (Input::KeyDown(SDLK_D)) camPos += right * (moveSpeed * dt);
    if (Input::KeyDown(SDLK_A)) camPos -= right * (moveSpeed * dt);

    Scene::SetCamera(camPos, camPos + Forward());

    // ── Lights, rebuilt each frame (max 4, the shader's limit) ───────────────────
    Scene::ClearLights();
    // Directional "sun" — casts the shadow map. The direction points TOWARD the light.
    Scene::AddDirectionalLight({0.6f, 0.65f, 0.4f}, {255, 245, 220, 255}, 0.9f);
    // A bright lamp orbiting overhead, casting moving shadows across the floor.
    AddRoomLight({std::cos(elapsed * 0.4f) * 7.0f, 6.0f, std::sin(elapsed * 0.4f) * 7.0f},
                 {255, 220, 170, 255}, 3.5f);

    // ── Draw ─────────────────────────────────────────────────────────────────────
    Window::StartFrame();
    // 3D models are drawn by the default framebuffer's 3D pass. We add a 2D crosshair on top.
    float cx = Window::GetWidth() * 0.5f, cy = Window::GetHeight() * 0.5f;
    Draw::RectangleFilled({cx - 1, cy - 6}, {2, 12}, {255, 255, 255, 160});
    Draw::RectangleFilled({cx - 6, cy - 1}, {12, 2}, {255, 255, 255, 160});

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) {
    Window::SetRelativeMouseMode(false);
    Scene::Clear();
    delete cube;
    delete sanityCube;
    Window::Close();
}
