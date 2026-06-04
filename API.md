# Luminoveau Engine — Game-Builder API Reference

Practical guide for building a game on top of Luminoveau. Reflects the current state of the SDL/WebGPU backend-split codebase. Pair this with the architecture diagram at `E:\temp\luminoveau_class_diagram.html`.

---

## 1. Project skeleton

### CMake

A consuming project links against the `luminoveau` static lib + `lumi_main` (the SDL3 entry-point glue). Minimum `CMakeLists.txt`:

```cmake
add_subdirectory(path/to/luminoveau)

add_executable(mygame
    src/main.cpp
    # ... your other .cpp ...
)
target_link_libraries(mygame PRIVATE luminoveau lumi_main)
```

### Backend selection

Selected at configure time:

| Variable | Effect |
|---|---|
| `LUMINOVEAU_WEBGPU_BACKEND=ON` | Builds the WebGPU path (Emscripten or Dawn on desktop). Implies WGSL shader backend. |
| `LUMINOVEAU_WEBGPU_BACKEND=OFF` (default) | SDL_GPU backend. Picks `METALLIB` on Apple, `SPIRV` everywhere else (override with `LUMINOVEAU_GPU_BACKEND=DXIL` if needed). |
| `LUMINOVEAU_WITH_IMGUI` | Adds ImGui (default ON). |
| `LUMINOVEAU_WITH_RMLUI` | Adds RmlUI (SDL only; OFF on web). |

The engine never `#ifdef`s on the backend in shared engine files — backend differences live in per-backend directories (`sdl/`, `webgpu/`).

### Build commands

```
cmake -S . -B build-sdl
cmake --build build-sdl --config Release

# Web build (Emscripten toolchain assumed on PATH)
emcmake cmake -S . -B build-web -DLUMINOVEAU_WEBGPU_BACKEND=ON
cmake --build build-web
```

### Shader build step

WGSL shaders are transpiled at configure time from GLSL via `cmake/ShaderTranspile.cmake`. SPIRV blobs for built-in engine shaders are baked under `src/assets/shaders/` and embedded in the binary. User-supplied shaders go in `<project>/assets/shaders/` and are loaded at runtime.

---

## 2. Entry point

Implement four functions in your code; the engine's SDL3 callback shim (`app/lumi_main.cpp`) calls them.

```cpp
#include "luminoveau.h"
#include "MyMenu.h"

static MyMenu* menu = nullptr;

Lumi::Result AppInit(int /*argc*/, char* /*argv*/[]) {
    Window::InitWindow("My Game", 1280, 720, /*scale=*/1, SDL_WINDOW_RESIZABLE);
    menu = new MyMenu();
    menu->Init();
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* /*appstate*/) {
    // Optional global hotkeys
    if (Input::KeyDown(SDLK_RALT) && Input::KeyPressed(SDLK_RETURN)) Window::ToggleFullscreen();
    if (Input::KeyDown(SDLK_LALT) && Input::KeyPressed(SDLK_F12))   Window::TakeScreenshot();

    menu->Update((float)Window::GetFrameTime());

    Window::StartFrame();      // begins the engine frame
    menu->Render();
    Window::EndFrame();        // submits + presents
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* /*appstate*/, SDL_Event* /*event*/) {
    // Engine already routed the event through Window::ProcessEvent → ImGui / RmlUI / Input.
    return Lumi::Result::Continue;
}

void AppQuit(void* /*appstate*/, Lumi::Result /*result*/) {
    delete menu;
    Window::Close();
}
```

`Lumi::Result::Continue` keeps the loop running; `Stop` exits cleanly.

---

## 3. The frame

```
AppIterate
  ├─ Update (you call this — pass GetFrameTime as dt)
  ├─ Window::StartFrame
  │   ├─ Window::HandleInput       (processes buffered events, updates Input)
  │   ├─ _lastFrameTime updated    (real wall-clock delta)
  │   └─ Renderer::StartFrame
  │       └─ acquireSwapchainTexture (may return null → frame skipped)
  ├─ menu->Render                  (queue Draw::* / Particles::QueueDraw)
  └─ Window::EndFrame
      └─ Renderer::EndFrame
          ├─ runPasses(preComputeFlush = true)   (scene-into-RT passes)
          ├─ Compute::dispatchAll                (particle physics, SDF, etc.)
          ├─ runPasses(rest)                      (everything else)
          ├─ renderFrameBuffer                    (primary FB → swapchain blit)
          ├─ ImGui / RmlUI overlay
          ├─ submitCommandBuffer + presentSwapchain
          └─ EngineState::_presentCount++
```

Key invariants:

- **Primary framebuffer is desktop-sized.** Resizing the window does **not** recreate textures. Render passes write to the window's physical-pixel region; the blit samples just that region.
- **Logical coords are the canvas size** (`Window::GetWidth/Height`). Mouse, draw positions, etc. all use logical coords.
- **`Window::GetFrameTime()` is the inter-StartFrame delta** in seconds. Use for variable-step integration.
- **`Window::GetFPS(ms)` averages present count** over the given window (ms). Decoupled from `GetFrameTime` so it stays accurate even if `AppIterate` spins faster than vsync on native SDL builds.

---

## 4. Window

```cpp
Window::InitWindow(title, w, h, scale = 1, flags = SDL_WINDOW_RESIZABLE);
Window::SetTitle(string);
Window::SetIcon(physfs_path);
Window::SetCursor(physfs_path);

Window::GetWidth() / GetHeight();                  // logical (canvas) pixels
Window::GetPhysicalWidth() / GetPhysicalHeight();  // physical canvas pixels
Window::GetSize();                                 // vf2d logical
Window::GetDisplayBounds(outW, outH);              // monitor size
Window::GetScale();                                // user-set logical→physical
Window::GetDisplayScale();                         // OS DPI scale

Window::ToggleFullscreen();
Window::IsFullscreen();
Window::SetSize(w, h);                             // logical
Window::SetScaledSize(w, h, scale);

Window::TakeScreenshot(filename = "");             // PNG, defers to end of frame
Window::ToggleDebugMenu();                         // ImGui debug overlay
```

### WebGPU scaling mode

Default is `WebGpuScaleMode::Native` — the swapchain matches the canvas's actual pixel buffer. Switch only if you want a fixed render resolution that gets stretched:

```cpp
Window::SetWebGpuScaling(WebGpuScaleMode::Contain, 1280, 720);
// or Fill, Stretch, Native
```

---

## 5. Input

Polled state. All coordinates are logical-pixel.

```cpp
Input::Init();                       // called from Window::InitWindow

// Keyboard
Input::KeyDown(SDLK_W);              // held this frame
Input::KeyPressed(SDLK_SPACE);       // edge: down this frame, up last
Input::KeyReleased(SDLK_ESCAPE);

// Mouse
Input::GetMousePosition();           // vf2d in logical coords
Input::MouseButtonDown(SDL_BUTTON_LEFT);
Input::MouseButtonPressed(SDL_BUTTON_RIGHT);
Input::MouseButtonReleased(SDL_BUTTON_MIDDLE);
Input::GetScroll();                  // accumulated wheel delta this frame

// Gamepad (auto-tracked when controllers plug in)
Input::GamepadButtonDown(deviceIdx, SDL_GAMEPAD_BUTTON_SOUTH);
Input::GamepadAxis(deviceIdx, SDL_GAMEPAD_AXIS_LEFTX);   // -1..+1 (deadzoned)

// Touch (auto-translated from finger events)
Input::HasTouchInput();
Input::GetTouchPoints();             // vector<vf2d>

// Text input
Window::SetTextInputCallback([](const char* utf8){ ... });
Window::StartTextInput();
Window::StopTextInput();
```

Virtual controls (logical actions like `"Jump"`) live in `VirtualControls` — see `src/platform/input/virtualcontrols.h` for the binding/serialization API.

---

## 6. State machine pattern

The recommended structure is a `BaseState` per game screen + a `MenuManager`-style switcher. Each screen implements `init / update / render / close`.

```cpp
class TestBase {
public:
    rectf renderArea;                          // engine fills this for you
    virtual ~TestBase() = default;
    virtual bool init() = 0;
    virtual void update(float dt) = 0;
    virtual void render() = 0;
    virtual void close() = 0;
    virtual std::string getTitleInfo() { return {}; }
};
```

Wire your states into the menu (or your own switcher). Examples live in `E:\lumifps\src\` (LightToy, SpriteCountTest, Test3D, EffectTest).

---

## 7. Assets

`AssetHandler` is a singleton cache. Asset getters are idempotent — the second call returns the same handle.

```cpp
TextureAsset       tex      = AssetHandler::GetTexture("sprites/player.png");
SoundAsset         shoot    = AssetHandler::GetSound("sfx/laser.wav");
MusicAsset         track    = AssetHandler::GetMusic("music/level1.ogg");
FontAsset          font16   = AssetHandler::GetFont("fonts/main.ttf", 16);    // MSDF
ShaderAsset        vert     = AssetHandler::GetShader("shaders/post.vert", SDL_GPU_SHADERSTAGE_VERTEX);
ShaderAsset        frag     = AssetHandler::GetShader("shaders/post.frag", SDL_GPU_SHADERSTAGE_FRAGMENT);
ComputePipelineAsset comp   = AssetHandler::GetComputePipeline("shaders/sim.comp");
ModelAsset*        cube     = AssetHandler::LoadModel("models/cube.gltf");

// Asset construction helpers
TextureAsset empty = AssetHandler::CreateEmptyTexture({ 256, 256 });
TextureAsset hdr   = AssetHandler::CreateEmptyTexture({ 256, 256 }, GpuTextureFormat::R16G16B16A16_Float);
TextureAsset white = AssetHandler::CreateWhitePixel();

AssetHandler::Cleanup();   // called by Window::Close
```

Built-in default font: `AssetHandler::GetDefaultFont()` returns a Droid Sans Mono MSDF atlas baked into the binary; no asset file needed.

### Filesystem rules

- Read paths route through PhysFS (search path = `assets/` next to the exe plus any mounted `.zip`/`.pak`).
- Writes use `FileHandler::GetWritableDirectory()` (OS user data) or `GetSystemDirectory()` (engine internal — shader/font cache).
- For browser-persistent caches use the persistent storage API:
  ```cpp
  FileHandler::InitPersistentStorage();
  std::string path = FileHandler::GetPersistentStorageDirectory() + "save.dat";
  FileHandler::WriteFile(path, blob.data(), blob.size());
  FileHandler::FlushPersistentStorage();   // pushes MEMFS → IndexedDB on web
  ```

---

## 8. Drawing — the high-level `Draw` API

Issue draws inside your `render()` (between `StartFrame` and `EndFrame`). The target framebuffer defaults to the primary FB's sprite pass.

```cpp
// Textures
Draw::Texture(textureAsset.textureView, pos, size);      // textureView is a Texture struct
Draw::Texture(tex, x, y);                                // size from tex.width/height
Draw::Sprite(tex, x, y, srcRect, scale, rotation, color);

// Primitives
Draw::Rectangle({100, 100}, {200, 50}, WHITE);           // outline
Draw::RectangleFilled({100, 100}, {200, 50}, RED);
Draw::Circle({400, 300}, 60.0f, GREEN);
Draw::CircleFilled({400, 300}, 60.0f, BLUE);
Draw::Line({0,0}, {800, 600}, YELLOW);
Draw::RoundedRectangle(pos, size, cornerRadius, color);

// Z-ordering
Draw::SetZIndex(10);   // higher = drawn later
int z = Draw::GetZIndex();

// Choosing a target render pass (any custom RT or "uiLayer", etc.)
Draw::SetTargetRenderPass("uiLayer");
Draw::Rectangle(...);
Draw::ResetTargetRenderPass();
```

Colors are 0–255 RGBA — predefined names live in `src/config.h` (`WHITE`, `RED`, `SKYBLUE`, …). Construct your own with `Color{r, g, b, a}`.

---

## 9. Text

```cpp
Text::DrawText(font, {x, y}, "Hello world", WHITE);
Text::DrawText(font, pos, str, color, /*renderSize=*/24.0f);

Text::DrawWrappedText(font, {x, y}, longString, /*maxWidth=*/300.0f, WHITE);

vf2d size  = Text::GetRenderedTextSize(font, "abc", 16.0f);
int  width = Text::MeasureText(font, "abc", 16.0f);

// Draw text into a one-off texture for re-use
TextureAsset textTex = Text::DrawTextToTexture(font, "Static label", WHITE);
```

Fonts are MSDF (multi-channel signed distance field) — bring any TTF, the engine generates the atlas on first load and caches it.

---

## 10. Effects (sprite post-processing)

Effects are vert+frag shader pairs applied as ping-pong passes between sprite drawing and final composite. Stack multiple to chain them.

```cpp
EffectAsset glow    = Draw::CreateEffect("shaders/effect_glow.vert",    "shaders/effect_glow.frag");
EffectAsset outline = Draw::CreateEffect("shaders/effect_outline.vert", "shaders/effect_outline.frag");

// Single effect on one sprite
Draw::SetEffect(glow);
Draw::Texture(playerTex, pos, size);
Draw::ClearEffects();

// Chain — applied in the order they're added
Draw::SetEffect(outline);
Draw::AddEffect(glow);
Draw::Texture(enemyTex, pos, size);
Draw::ClearEffects();

// Effect with custom uniforms (variable name must match the GLSL uniform block field)
glow.uniforms->Set("u_intensity", 2.5f);
glow.uniforms->Set("u_color", glm::vec4{1, 0.5f, 0, 1});

// Extra sampler textures (sampler binding indices > 0)
Draw::SetEffectTexture(/*binding=*/1, normalMapTex.gpuTexture, ScaleMode::Linear);
```

Effect fragment shaders sample the previous result via `sampler2D s_input` (binding 0) and any extras via additional samplers.

---

## 11. Particles

GPU-driven particle systems with built-in physics compute. Configure once, update each frame, draw via the engine's particle render pass.

```cpp
// Init (typically in AppInit, after Renderer::InitRendering)
Particles::Init();

// Define a system
ParticleSystemConfig fire{};
fire.maxParticles    = 5000;
fire.spawnPos        = { 400, 300, 0, /*radius=*/8.0f };
fire.spawnVel        = { 0, -120, 0, /*magnitude=*/40.0f };
fire.gravityAndDrag  = { 0, -50, 0, /*drag=*/0.5f };
fire.lifetimeMin     = 0.5f;
fire.lifetimeMax     = 1.8f;
fire.emitRate        = 500.0f;
fire.sizeStartMin    = 8.0f; fire.sizeStartMax = 14.0f;
fire.sizeEndMin      = 0.0f; fire.sizeEndMax   = 2.0f;
fire.colors[0] = {1.0f, 0.9f, 0.3f, 1.0f};   // start
fire.colors[3] = {0.5f, 0.0f, 0.0f, 0.0f};   // end
fire.colorPositions = {0.0f, 0.4f, 0.7f, 1.0f};
fire.flags = ParticleSystemFlags::Emitting;

ParticleSystemHandle h = Particles::CreateSystem(fire);

// Per frame
Particles::Update(Window::GetFrameTime());       // accumulate dt (safe to call many times)
Particles::QueueDraw(h);                          // OR: Draw::Particles(h)

// Optional 2D colliders (planes and circles)
Particles::SetColliders({
    { ColliderKind::Plane,  /*normal+offset*/{0, 1, 100, 0}, /*restitution=*/0.5f, /*friction=*/0.1f },
    { ColliderKind::Circle, /*center+radius*/{200, 400, 32, 0}, 0.8f, 0.05f },
});

// Preset workflow (export from the in-engine particle editor)
ParticleSystemHandle p = Particles::CreateSystemFromPreset(base64String, /*maxParticles=*/3000);

// Shutdown
Particles::Quit();   // before Renderer::Close (Window::Close handles this)
```

Particle limits in `src/config.h`: `LUMINOVEAU_MAX_PARTICLES` (50M native, 1.5M web). The renderer can also be in `Particles::PovMode` for shrink-on-distance camera-space rendering.

---

## 12. Compute dispatches

For your own compute work — postprocess, sims, etc.

```cpp
auto pipe = AssetHandler::GetComputePipeline("shaders/blur.comp");

Compute::SetPipeline(pipe);
Compute::BindReadTexture(0, srcTex);
Compute::BindReadWriteTexture(0, dstTex);
Compute::BindReadBuffer(0, paramsBuf);
Compute::BindReadWriteBuffer(0, scratchBuf);

struct Uniforms { uint32_t w, h; float strength; uint32_t pad; } u { width, height, 1.5f, 0 };
Compute::PushUniform(0, &u, sizeof(u));

Compute::DispatchAuto(width, height, 1);    // rounds to workgroup size from reflection
```

Dispatches are deferred to `_PrepareFrame` inside `Renderer::EndFrame` so the engine always builds them at frame boundaries (avoids SDL_AppIterate spinning issues).

---

## 13. 3D models + scenes

```cpp
ModelAsset* cube = AssetHandler::LoadModel("models/cube.gltf");

ModelInstance instance;
instance.model     = cube;
instance.position  = {0, 0, 0};
instance.rotation  = {0, 0, 0};
instance.scale     = {1, 1, 1};
Scene::AddModel(cube, instance);

// Camera
Camera3D cam;
cam.position = {3, 2, 5};
cam.target   = {0, 0, 0};
cam.fov      = 60.0f;
Scene::SetCamera(cam);

// Lights
Light sun;
sun.type       = LightType::Directional;
sun.direction  = {-0.3f, -1.0f, -0.2f};
sun.color      = {1.0f, 0.95f, 0.85f};
sun.intensity  = 1.0f;
Scene::AddLight(sun);
```

`Model3DRenderPass` is attached to the primary FB by default and renders everything in `Scene::GetModels()`. Edit `models[i].rotation.y += dt * speed` from your `update()` to spin things.

---

## 14. Custom render targets

Use for postprocessing, mini-maps, light buffers — anything you want to render off-screen and either sample later or composite onto the swapchain.

```cpp
// Render-to-screen UI layer at logical-window size
Renderer::CreateFrameBuffer("uiLayer");                  // size defaults to display bounds
Renderer::SetFramebufferRenderToScreen("uiLayer", true); // composite at present time

auto* uiPass = new SpriteRenderPass();
uiPass->UpdateRenderPassBlendState(GpuPresets::AlphaBlendPreserveAlpha);
uiPass->init(Renderer::GetGpu().getSwapchainFormat(),
             Window::GetWidth(), Window::GetHeight(), "uiLayer");
uiPass->color_target_info_loadop = GpuLoadOp::Clear;
Renderer::AttachRenderPassToFrameBuffer(uiPass, "uiLayer", "uiLayer");

// Offscreen RT for postprocessing (fixed-size, sampled later)
SpriteRenderTargetConfig cfg;
cfg.width  = 1280; cfg.height = 720;
cfg.format = GpuTextureFormat::R16G16B16A16_Float;       // HDR
cfg.clearOnLoad     = true;
cfg.clearColor      = {0, 0, 0, 0};
cfg.blendMode       = BlendMode::None;
cfg.preComputeFlush = true;                              // fires BEFORE compute (lets compute read it)
Renderer::CreateSpriteRenderTarget("scene", cfg);

// During render
Draw::SetTargetRenderPass("scene");
Draw::Texture(...);
Draw::ResetTargetRenderPass();

// Sample the RT later
TextureAsset sceneView = Renderer::GetFramebuffer("scene_framebuffer")->textureView;
Draw::Texture(sceneView, {0, 0}, {1280, 720});

// Cleanup when the state owning the RT exits
Renderer::RemoveSpriteRenderTarget("scene");
```

`fixedSize` RTs (any RT created with explicit `width`/`height`) receive an FB-sized camera ortho instead of the canvas camera. So a 1280×720 RT lets you draw at coords 0..1280 directly.

---

## 15. Custom render passes

Inherit `RenderPass` if you need bespoke draw logic. Standard pattern:

```cpp
class MyPass : public RenderPass {
public:
    bool init(GpuTextureFormat fmt, uint32_t w, uint32_t h, std::string name,
              bool logInit = true, size_t cap = 0, bool noMSAA = false) override;
    void release(bool logRelease = true) override;
    void render(GpuCmdBufferHandle cmd, GpuTextureHandle target, const glm::mat4& camera) override;
    void addToRenderQueue(const Renderable& r) override { /* ... */ }
    void resetRenderQueue() override { /* clear */ }
    UniformBuffer& getUniformBuffer() override { return m_uniformBuffer; }
};

Renderer::AttachRenderPassToFrameBuffer(new MyPass(), "myPassName", "primaryFramebuffer");
```

For sprite-style passes attach to the primary FB; for compute-feeding scene passes use a custom FB with `preComputeFlush = true`.

---

## 16. ImGui debug menu

`<F11>+<LShift>` toggles a built-in debug overlay (frame time, FPS, sample count, GPU memory, render passes). Disable in release if you don't want it shipped.

Custom ImGui inside your `render()`:

```cpp
ImGui::Begin("Tweaks");
ImGui::SliderFloat("Gravity", &gravity, -200.0f, 0.0f);
if (ImGui::Button("Reset")) reset();
ImGui::End();
```

ImGui's input is consumed automatically — check `ImGui::GetIO().WantCaptureMouse / WantCaptureKeyboard` in your input handling to avoid double-handling.

---

## 17. Audio

```cpp
Audio::Init();              // called from Window::InitWindow

SoundAsset shoot = AssetHandler::GetSound("sfx/laser.wav");
Audio::PlaySound(shoot);
Audio::PlaySound(shoot, /*volume=*/0.8f, /*pitch=*/1.2f);

MusicAsset track = AssetHandler::GetMusic("music/level.ogg");
Audio::PlayMusic(track, /*loop=*/true);
Audio::StopMusic();

Audio::SetMasterVolume(0.7f);
Audio::SetSoundVolume(1.0f);
Audio::SetMusicVolume(0.6f);

Audio::Close();             // called from Window::Close
```

Backed by miniaudio — supports WAV, OGG, MP3, FLAC. PCM sounds (synthesized) live in `PCMSound`.

---

## 18. Events + global state

```cpp
EventBus::Subscribe(SystemEvent::WINDOW_RESIZE, [](const EventData& d) {
    int w = std::any_cast<int>(d.at("width"));
    int h = std::any_cast<int>(d.at("height"));
    LOG_INFO("Resized to {}x{}", w, h);
});

EventBus::Fire(MyAppEvent::PlayerDied, { {"score", 1200} });
```

App-defined events: add entries to your own enum (the bus is type-tagged).

---

## 19. Logging

```cpp
LOG_INFO("Loaded {} sprites", count);
LOG_WARNING("Texture {} not found, using fallback", path);
LOG_ERROR("Failed to compile shader: {}", err);
LOG_CRITICAL("GPU device lost — bailing");
```

Output goes to stdout + an in-engine ring buffer the debug menu shows.

---

## 20. Settings (persisted config)

```cpp
Settings::Load("settings.ini");

int   width   = Settings::GetInt("graphics.width", 1280);
bool  vsync   = Settings::GetBool("graphics.vsync", true);
float vol     = Settings::GetFloat("audio.master_volume", 0.8f);
std::string lang = Settings::GetString("locale", "en");

Settings::Set("audio.master_volume", 0.5f);
Settings::Save();   // writes back to the .ini
```

File lives under `FileHandler::GetWritableDirectory()`.

---

## 21. Common pitfalls

### Frame timing
- `GetFrameTime()` is per-StartFrame interval (can be µs-scale on native SDL_AppIterate if SDL spins). Use it for variable-step integration but **don't compute FPS as `1/GetFrameTime()`** — use `Window::GetFPS(ms)` which counts real presents.

### Coordinate systems
- Draw at logical (canvas) coords. The engine takes care of physical-pixel mapping.
- `_getSize()` returns canvas dims; `_getPhysicalSize()` returns the actual swapchain pixel buffer. Match the right one to your use case.

### LightToy-style custom RT pipelines
- If you size an RT at `Window::GetPhysicalWidth()`, recreate it when the window grows past that size (engine doesn't auto-resize fixed-size RTs).

### WebGPU specifics
- `Window::GetPhysicalWidth/Height` on Native scale mode returns the canvas's actual pixel dims (not SDL_GetWindowSizeInPixels which can drift on emscripten).
- The Float32-Filterable WebGPU feature isn't always available — `rgba32f` textures are bound as `UnfilterableFloat` with a `NonFiltering` sampler. Use linear filtering only on `rgba8`/`bgra8`/`r16f` targets.
- Persistent storage requires `FileHandler::InitPersistentStorage()` + `FlushPersistentStorage()` after writes.

### SDL specifics
- Window flags: `SDL_WINDOW_HIGH_PIXEL_DENSITY` is set automatically. HighDPI swapchain by default.
- Screenshots use the engine's `stb_image_write` path — same code on both backends.

### Particles
- Don't call `Particles::Update` from `render()` — call it from `update()` so the dt accumulation lines up with the compute dispatch at frame boundary.
- Compute pipeline is built lazily on first `Particles::Init()`. Call after `Renderer::InitRendering` (done implicitly if your AppInit creates objects after `Window::InitWindow`).

---

## 22. Useful reference paths

| Topic | File |
|---|---|
| Window/lifecycle | `src/platform/window/window.h` |
| Input | `src/platform/input/input.h` |
| Drawing | `src/draw/draw.h` |
| Particles | `src/draw/particles.h` |
| Renderer / FBs | `src/renderer/renderer.h` |
| Asset loading | `src/assets/assethandler.h` |
| IGpu abstraction | `src/gpu/IGpu.h` |
| Type defs (handles, enums) | `src/gpu/types.h` |
| Presets (blend states, samplers) | `src/gpu/presets.h` |
| Color constants + config | `src/config.h` |
| Math (vectors, rectangles, easings) | `src/math/` |
| Test/example states | `E:\lumifps\src\` (LightToy, Test3D, EffectTest, SpriteCountTest) |
| Backend split rules | this doc §1 + the architecture diagram |

---

## 23. Minimum viable game

```cpp
#include "luminoveau.h"

struct Player { vf2d pos = {640, 360}; float speed = 300; } player;
TextureAsset sprite;

Lumi::Result AppInit(int, char*[]) {
    Window::InitWindow("Tiny", 1280, 720, 1, SDL_WINDOW_RESIZABLE);
    sprite = AssetHandler::GetTexture("sprites/ship.png");
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void*) {
    float dt = (float)Window::GetFrameTime();
    if (Input::KeyDown(SDLK_W)) player.pos.y -= player.speed * dt;
    if (Input::KeyDown(SDLK_S)) player.pos.y += player.speed * dt;
    if (Input::KeyDown(SDLK_A)) player.pos.x -= player.speed * dt;
    if (Input::KeyDown(SDLK_D)) player.pos.x += player.speed * dt;

    Window::StartFrame();
    Draw::Texture(sprite, player.pos - vf2d{32, 32}, {64, 64});
    Text::DrawText(AssetHandler::GetDefaultFont(),
                   {10, 10},
                   Helpers::TextFormat("%d fps", Window::GetFPS()),
                   WHITE);
    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void*, SDL_Event*) { return Lumi::Result::Continue; }
void AppQuit(void*, Lumi::Result) { Window::Close(); }
```

That's it. Build on this skeleton.
