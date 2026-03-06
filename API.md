# Luminoveau API Reference

All classes are singletons accessed via static methods. Include `luminoveau.h` to get everything.

---

## Type Aliases

These aliases are used throughout the API. They are **references**, not values.

| Alias      | Resolves to          |
|------------|----------------------|
| `Texture`  | `TextureAsset&`      |
| `Font`     | `FontAsset&`         |
| `Sound`    | `SoundAsset&`        |
| `Music`    | `MusicAsset&`        |
| `Shader`   | `ShaderAsset&`       |
| `Effect`   | `EffectAsset&`       |
| `Model`    | `ModelAsset&`        |

---

## Vector Types

Template struct `v2d_generic<T>` with specialisations:

| Type    | Underlying type          |
|---------|--------------------------|
| `vi2d`  | `v2d_generic<int32_t>`   |
| `vu2d`  | `v2d_generic<uint32_t>`  |
| `vf2d`  | `v2d_generic<float>`     |
| `vd2d`  | `v2d_generic<double>`    |
| `vi3d`  | `v3d_generic<int32_t>`   |
| `vu3d`  | `v3d_generic<uint32_t>`  |
| `vf3d`  | `v3d_generic<float>`     |
| `vd3d`  | `v3d_generic<double>`    |

### `v2d_generic<T>` members

```cpp
T x, y;

T    mag() const;
T    mag2() const;
v2d  norm() const;
v2d  perp() const;
v2d  floor() const;
v2d  ceil() const;
v2d  clamp(const rect_generic<T>& target) const;
v2d  max(const v2d& v) const;
v2d  min(const v2d& v) const;
v2d  cart();                          // polar->cartesian
v2d  polar();                         // cartesian->polar
T    dot(const v2d& rhs) const;
T    cross(const v2d& rhs) const;
T    getAngle() const;
void rotateBy(float l);
float distanceTo(const v2d other);
v2d  reflectOn(const v2d normal);
std::string str() const;              // "(x.xx,y.yy)"
const char* c_str() const;

// Operators: +, -, *, /, +=, -=, *=, /= (scalar and vector forms)
// Comparison: ==, !=, <, >
// Implicit conversions between int/float/double specialisations
// Optional implicit conversions to b2Vec2, glm::vec2, ImVec2 (when available)
```

### `v3d_generic<T>` members

```cpp
T x, y, z;

T    mag() const;
T    mag2() const;
v3d  norm() const;
T    dot(const v3d& rhs) const;
v3d  cross(const v3d& rhs) const;
std::string str() const;

// Operators: +, -, *, /, +=, -=, *=, /=
// Comparison: ==, !=
// Implicit conversions between int/float/double specialisations
```

---

## Rectangle Types

Template union `rect_generic<T>` with specialisations:

| Type    | Underlying type           |
|---------|---------------------------|
| `recti` | `rect_generic<int32_t>`   |
| `rectu` | `rect_generic<uint32_t>`  |
| `rectf` | `rect_generic<float>`     |
| `rectd` | `rect_generic<double>`    |

### `rect_generic<T>` members

```cpp
// Accessible as any of:
T x, y, width, height;
T x, y, w, h;           // alternative names
v2d_generic<T> pos;
v2d_generic<T> size;

bool contains(const v2d_generic<T>& point) const;
bool intersects(const rect_generic<T>& other) const;

// Implicit conversions (when SDL3 available):
operator SDL_FRect();
operator SDL_Rect();
```

---

## Color

```cpp
struct Color {
    unsigned int r, g, b, a;   // 0–255

    Color();
    Color(unsigned int r, unsigned int g, unsigned int b, unsigned int a);
    Color(uint32_t colorCode);  // format: 0xRRGGBBAA

    void  CreateFromFloats(float r, float g, float b, float a);  // 0.0–1.0 input

    float getRFloat() const;
    float getGFloat() const;
    float getBFloat() const;
    float getAFloat() const;

    glm::vec4 asVec4();  // when glm is available

    // Explicit conversions (when SDL3 available):
    explicit operator SDL_Color() const;
    explicit operator SDL_FColor() const;
};
```

### Predefined Color Constants

```
RED, GREEN, BLUE, BLACK, WHITE
YELLOW, CYAN, MAGENTA, PURPLE, ORANGE, PINK, LIME
LIGHTGRAY, GRAY, DARKGRAY
DARKRED, DARKGREEN, DARKBLUE, DARKYELLOW
SKYBLUE, NAVY, VIOLET
BROWN, MAROON, BEIGE
GOLD, SILVER
```

---

## Angle Helpers

```cpp
constexpr float deg(float degrees);   // converts degrees to radians
constexpr float rad(float radians);   // identity, for explicit clarity
```

### Constants

```cpp
PI       // 3.14159265...
EPSILON  // 0.000001f
DEG2RAD  // PI / 180.0f
RAD2DEG  // 180.0f / PI
```

---

## Window

```cpp
class Window {
    static void InitWindow(const std::string& title,
                           int width = 800, int height = 600,
                           int scale = 1, unsigned int flags = 0);

    static void SetIcon(const std::string& filename);
    static void SetCursor(const std::string& filename);
    static void SetTitle(const std::string& title);

    // Close is safe to call mid-frame; actual cleanup is deferred to EndFrame()
    static void Close();

    static SDL_Window* GetWindow();

    static void  SetScale(int scalefactor);
    static float GetScale();

    static void SetSize(int width, int height);

    // SetScaledSize: width/height in virtual pixels, scale=0 keeps current scale
    static void SetScaledSize(int width, int height, int scale = 0);

    // GetSize: logical (virtual) pixels. getRealSize=true skips user-scale division
    static vf2d GetSize(bool getRealSize = false);
    static int  GetWidth(bool getRealSize = false);
    static int  GetHeight(bool getRealSize = false);

    // Physical (device) pixels — may be larger on HiDPI/Retina displays
    static vf2d GetPhysicalSize();
    static int  GetPhysicalWidth();
    static int  GetPhysicalHeight();

    // Ratio of physical to logical pixels (e.g. 2.0 on Retina)
    static float GetDisplayScale();

    static void StartFrame();
    static void EndFrame();

    static void ToggleFullscreen();
    static bool IsFullscreen();

    static double GetRunTime();     // seconds since window creation
    static bool   ShouldQuit();
    static void   SignalEndLoop();

    static double GetFrameTime();              // last frame duration in seconds
    static int    GetFPS(float milliseconds = 400.f);

    static void HandleInput();
    static void ToggleDebugMenu();

    static void        TakeScreenshot(const std::string& filename = "");
    static bool        HasPendingScreenshot();
    static std::string GetAndClearPendingScreenshot();

    // Receives raw text input (keyboard typing, IME, etc.)
    static void SetTextInputCallback(std::function<void(const char*)> callback);

    // SDL3 callback-mode only:
    static void ProcessEvent(SDL_Event* event);
};
```

---

## Renderer

```cpp
enum class BlendMode { Default, SrcAlpha, Additive, None };

struct SpriteRenderTargetConfig {
    BlendMode blendMode  = BlendMode::SrcAlpha;
    bool      clearOnLoad    = true;
    Color     clearColor     = BLACK;
    bool      renderToScreen = false;
};

class Renderer {
    static void InitRendering();
    static void Close();

    static SDL_GPUDevice* GetDevice();

    static void StartFrame();
    static void EndFrame();
    static void Reset();

    static void ClearBackground(Color color);

    static void AddToRenderQueue(const std::string& passname,
                                 const Renderable& renderable);

    static void AddShaderPass(const std::string& passname,
                              const ShaderAsset& vertShader,
                              const ShaderAsset& fragShader,
                              std::vector<std::string> targetBuffers = {});

    static void RemoveShaderPass(const std::string& passname);

    static void AttachRenderPassToFrameBuffer(RenderPass* renderPass,
                                              const std::string& passname,
                                              const std::string& fbName);

    static UniformBuffer& GetUniformBuffer(const std::string& passname);

    static void CreateFrameBuffer(const std::string& fbname);
    static void SetFramebufferRenderToScreen(const std::string& fbName, bool render);
    static FrameBuffer* GetFramebuffer(std::string fbname);

    static uint32_t GetZIndex();  // auto-incrementing per-frame depth counter

    static SDL_GPUSampler* GetSampler(ScaleMode scalemode);

    static SDL_GPURenderPass* GetRenderPass(const std::string& passname);
    static RenderPass*        FindRenderPass(const std::string& passname);

    static void SetScissorMode(std::string passname, rectf cliprect);

    static void OnResize();
    static void UpdateCameraProjection();

    static Texture     WhitePixel();
    static Geometry2D* GetQuadGeometry();
    static Geometry2D* GetCircleGeometry(int segments = 32);
    static Geometry2D* GetRoundedRectGeometry(float cornerRadiusX,
                                               float cornerRadiusY,
                                               int cornerSegments = 8);

    static SDL_GPUSampleCount GetSampleCount();
    static void               SetSampleCount(SDL_GPUSampleCount sampleCount);

    // Convenience: creates framebuffer + render pass in one call
    static void CreateSpriteRenderTarget(const std::string& name,
                                         const SpriteRenderTargetConfig& config = {});
    static void RemoveSpriteRenderTarget(const std::string& name,
                                          bool removeFramebuffer = true);
};
```

---

## Draw

```cpp
struct Mode7Parameters {
    int h, v;                      // scroll offsets
    int x0, y0;                    // rotation/scale origin
    int a, b, c, d;                // 2x2 transformation matrix elements
    int snesScreenWidth  = 256;
    int snesScreenHeight = 224;
};

class Draw {
    // --- Pixels ---
    static void Pixel(vi2d pos, Color color);

    // FlushPixels submits all queued Pixel() calls. Called automatically by every
    // other Draw method. Only needed if you want explicit control over layering
    // (e.g. to interleave pixel draws between other draw calls).
    static void FlushPixels();

    // --- Lines ---
    static void Line(vf2d start, vf2d end, Color color);
    static void ThickLine(vf2d start, vf2d end, Color color, float width);

    // --- Outlined shapes ---
    static void Triangle(vf2d v1, vf2d v2, vf2d v3, Color color);
    static void Rectangle(vf2d pos, vf2d size, Color color);
    static void RectangleRounded(vf2d pos, vf2d size, float radius, Color color);
    static void Circle(vf2d pos, float radius, Color color, int segments = 32);
    static void Ellipse(vf2d center, float radiusX, float radiusY, Color color);
    static void Arc(vf2d center, float radius,
                    float startAngle, float endAngle,   // radians
                    int segments, Color color);

    // --- Filled shapes ---
    static void TriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color);
    static void RectangleFilled(vf2d pos, vf2d size, Color color);
    static void RectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color);
    static void CircleFilled(vf2d pos, float radius, Color color);
    static void EllipseFilled(vf2d center, float radiusX, float radiusY, Color color);
    static void ArcFilled(vf2d center, float radius,
                          float startAngle, float endAngle,  // radians
                          int segments, Color color);

    // --- Textures ---
    static void Texture(Texture texture, vf2d pos, vf2d size, Color color = WHITE);

    static void TexturePart(Texture texture, vf2d pos, vf2d size,
                            rectf src, Color color = WHITE);

    // pivot is in normalised texture space; {0.5, 0.5} = center
    static void RotatedTexture(Texture texture, vf2d pos, vf2d size,
                               float angle,                  // radians
                               vf2d pivot = {0.5f, 0.5f},
                               Color color = WHITE);

    static void RotatedTexturePart(Texture texture, vf2d pos, vf2d size,
                                   rectf src, float angle,   // radians
                                   vf2d pivot, Color color = WHITE);

    // Single affine Mode 7 transform across the whole texture
    static void Mode7Texture(Texture texture, vf2d pos, vf2d size,
                             const Mode7Parameters& params, Color color = WHITE);

    // Per-scanline Mode 7 (callback receives scanline index, returns params)
    // scanlineStep: process every Nth scanline (higher = better perf, fewer strips)
    static void Mode7TextureScanline(Texture texture, vf2d pos, vf2d size,
                                     std::function<Mode7Parameters(int)> getParamsForLine,
                                     Color color = WHITE, int scanlineStep = 1);

    // --- Scissor / render pass control ---
    static void SetScissorMode(const rectf& area);

    static void BeginMode2D();
    static void EndMode2D();

    static void        SetTargetRenderPass(const std::string& newTargetRenderPass);
    static void        ResetTargetRenderPass();   // resets to "2dsprites"
    static std::string GetTargetRenderPass();

    // --- Effects (shader post-processing) ---

    // SetEffect replaces the entire effect stack with a single effect
    static void SetEffect(const EffectAsset& effect);

    // AddEffect appends to the effect stack
    static void AddEffect(const EffectAsset& effect);

    static void RemoveEffect(const EffectAsset& effect);
    static void ClearEffects();

    // Bind an extra texture for the current effect (binding index matches shader layout)
    static void SetEffectTexture(uint32_t binding, const TextureAsset& texture);
    static void ClearEffectTextures();

    // Internal — for use by the renderer:
    static const std::vector<EffectAsset>& GetEffectStack();
    static const std::unordered_map<uint32_t, SDL_GPUTexture*>& GetEffectTextures();
    static const std::vector<std::vector<EffectAsset>>& GetEffectStore();
    static void ResetEffectStore();
    static void ReleaseFramePixelTextures();  // called automatically by Renderer::EndFrame
};
```

---

## Text

```cpp
class Text {
    // renderSize: pixels to render at. -1 = use the font's generated atlas size.
    static void DrawText(Font font, const vf2d& pos,
                         const std::string& textToDraw, Color color,
                         float renderSize = -1.0f);

    static void DrawWrappedText(Font font, vf2d pos,
                                std::string textToDraw, float maxWidth,
                                Color color, float renderSize = -1.0f);

    static int   MeasureText(Font font, std::string text,
                             float renderSize = -1.0f);        // returns width in pixels

    static vf2d  GetRenderedTextSize(Font font, std::string text,
                                     float renderSize = -1.0f);

    static TextureAsset DrawTextToTexture(Font font, std::string textToDraw,
                                          Color color);
};
```

---

## Audio

```cpp
enum class AudioChannel : uint8_t {
    Master,   // Engine master (not a mix group)
    SFX,
    Voice,
    Music,
    Count     // Sizing only — do not pass to API
};

// Callback signatures (audio thread — must be lock-free, no allocs/mutexes/I/O):
using PCMGenerateCallback = void(*)(float* output,  uint32_t frameCount,
                                    uint32_t channels, void* userData);
using PCMEffectCallback   = void(*)(float* samples, uint32_t frameCount,
                                    uint32_t channels, void* userData);

struct PCMFormat {
    uint32_t sampleRate = 48000;
    uint32_t channels   = 2;
};

class Audio {
    // --- Lifecycle ---
    // SetNumberOfChannels must be called BEFORE Init()
    static void SetNumberOfChannels(int newNumberOfChannels);
    static void Init();
    static void Close();
    static void UpdateMusicStreams();   // call every frame

    // --- Music (always routes through AudioChannel::Music) ---
    static void PlayMusic(Music& music);
    static void StopMusic();
    static void RewindMusic(Music& music);
    static void SetMusicVolume(Music& music, float volume);
    static bool IsMusicPlaying();

    // --- Sound effects ---
    // Non-polyphonic: reuses the pre-loaded ma_sound
    static void PlaySound(Sound sound,
                          AudioChannel channel = AudioChannel::SFX);

    // Polyphonic: volume 0.0–1.0, panning -1.0 (left) to 1.0 (right)
    static void PlaySound(Sound sound, float volume, float panning,
                          AudioChannel channel = AudioChannel::SFX);

    // --- Channel control ---
    static void  SetChannelVolume(AudioChannel channel, float volume);  // 0.0–1.0
    static float GetChannelVolume(AudioChannel channel);

    // No effect on Master channel
    static void  SetChannelPanning(AudioChannel channel, float panning); // -1.0–1.0
    static float GetChannelPanning(AudioChannel channel);                // always 0.0 for Master

    static void MuteChannel(AudioChannel channel, bool muted);
    static bool IsChannelMuted(AudioChannel channel);

    // --- PCM generators ---
    // userData lifetime is owned by the caller
    static PCMSound CreatePCMGenerator(const PCMFormat& format,
                                       PCMGenerateCallback callback,
                                       void* userData = nullptr);
    static void PlayPCMSound(PCMSound& sound,
                             AudioChannel channel = AudioChannel::SFX);
    static void StopPCMSound(PCMSound& sound);
    static void DestroyPCMSound(PCMSound& sound);  // do not use sound after this

    // --- Channel insert effects ---
    // One effect per channel; calling again replaces the previous one.
    // Master effect runs on the final mixed output before the device.
    // userData lifetime is owned by the caller.
    static void SetChannelEffect(AudioChannel channel,
                                 PCMEffectCallback callback,
                                 void* userData = nullptr);
    static void RemoveChannelEffect(AudioChannel channel);

    // --- Low-level access ---
    static ma_engine*      GetAudioEngine();
    static ma_sound_group* GetChannelGroup(AudioChannel channel); // nullptr for Master
};
```

---

## AssetHandler

```cpp
enum class ScaleMode { NEAREST, LINEAR };

class AssetHandler {
    // --- Textures ---
    static Texture     GetTexture(const char* fileName);
    static void        LoadTexture(const char* fileName);
    static TextureAsset LoadFromPixelData(const vf2d& size, void* pixelData,
                                          std::string fileName);
    static TextureAsset CreateEmptyTexture(const vf2d& size);
    static void         SaveTextureAsPNG(Texture texture, const char* fileName);

    static void      SetDefaultTextureScaleMode(ScaleMode mode);
    static ScaleMode GetDefaultTextureScaleMode();

    static std::unordered_map<std::string, TextureAsset> GetTextures();

    // --- Fonts ---
    // fontSize: size to generate MSDF atlas at (recommend 64–128 for quality)
    static Font GetFont(const char* fileName, int fontSize);
    static Font GetDefaultFont();   // built-in DroidSansMono

    // --- Audio ---
    static Sound  GetSound(const char* fileName);
    static Music& GetMusic(const char* fileName);

    // --- Shaders ---
    static Shader GetShader(const char* fileName);

    // --- Models ---
    static ModelAsset CreateCube(float size = 1.0f,
                                 CubeUVLayout layout = CubeUVLayout::Atlas4x4);

    // --- Generic deletion ---
    template<typename T>
    static void Delete(T& asset);

    static void Cleanup();   // called automatically on shutdown
};
```

---

## Input

```cpp
// Key values: SDL scancodes/keycodes (e.g. SDLK_A, SDL_SCANCODE_LSHIFT)
// Mouse button values: SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE, etc.

class Input {
    static void Init();
    static void Update();
    static void UpdateTimings();
    static void Clear();

    // --- Keyboard ---
    static bool KeyPressed(int key);    // true on the frame the key goes down
    static bool KeyReleased(int key);   // true on the frame the key comes up
    static bool KeyDown(int key);       // true while key is held

    // --- Mouse ---
    static vf2d GetMousePosition();

    static bool   MouseButtonPressed(int button);   // true on press frame
    static bool   MouseButtonReleased(int button);  // true on release frame
    static bool   MouseButtonDown(int button);      // true while held

    static Uint32 MouseScrolledUp();    // non-zero on the frame of a scroll-up event
    static Uint32 MouseScrolledDown();  // non-zero on the frame of a scroll-down event

    // --- Gamepad ---
    static float GetGamepadAxisMovement(int gamepadID, SDL_GamepadAxis axis);
    static bool  GamepadButtonPressed(int gamepadID, int button);
    static bool  GamepadButtonDown(int gamepadID, int button);

    // --- Device management ---
    static InputDevice*              GetController(int index);
    static std::vector<InputDevice*> GetAllInputs();

    static void AddGamepadDevice(SDL_JoystickID joystickID);
    static void RemoveGamepadDevice(SDL_JoystickID joystickID);

    // --- Touch / virtual controls ---
    static VirtualControls& GetVirtualControls();
    static void             HandleTouchEvent(const SDL_Event* event);

    // --- Internal (handle with care) ---
    static void UpdateInputs(std::vector<Uint8> keys, bool held);
    static void UpdateScroll(int scrollDir);
};
```

---

## InputDevice

Represents a single physical controller (keyboard/mouse or gamepad).

```cpp
enum class InputType { GAMEPAD, MOUSE_KB };
enum class Buttons   { LEFT, RIGHT, UP, DOWN, ACCEPT, BACK, SHOOT,
                       SWITCH_NEXT, SWITCH_PREV, RUN };
enum class Action    { HELD, PRESSED };

class InputDevice {
    InputDevice(InputType type);
    InputDevice(InputType type, int gamepadID);

    InputType getType();
    int       getGamepadID() const;

    // Returns true if button matches the action state this frame
    bool is(Buttons button, Action action);

    void updateTimings();
};
```

### Default Keyboard/Mouse Mappings

| Button        | Keys                                      |
|---------------|-------------------------------------------|
| LEFT          | A, Left Arrow                             |
| RIGHT         | D, Right Arrow                            |
| UP            | W, Up Arrow                               |
| DOWN          | S, Down Arrow                             |
| ACCEPT        | Space, KP Enter, Return                   |
| BACK          | Escape, Backspace                         |
| SWITCH_NEXT   | Tab                                       |
| SWITCH_PREV   | Grave (`)                                 |
| RUN           | Left Shift (scancode)                     |
| SHOOT         | Left Shift (keycode) or Left Mouse Button |

### Default Gamepad Mappings

| Button        | Gamepad input                   |
|---------------|---------------------------------|
| ACCEPT        | South button (A/Cross)          |
| BACK          | East button (B/Circle)          |
| LEFT          | D-Pad Left or Left Stick X < 0  |
| RIGHT         | D-Pad Right or Left Stick X > 0 |
| UP            | D-Pad Up or Left Stick Y < 0    |
| DOWN          | D-Pad Down or Left Stick Y > 0  |
| SWITCH_NEXT   | Right Shoulder                  |
| SWITCH_PREV   | Left Shoulder                   |
| RUN           | Right Trigger axis              |

---

## VirtualControls

Accessed via `Input::GetVirtualControls()`.

```cpp
enum class JoystickMode { DISABLED, STATIC, RELATIVE };

class VirtualControls {
    // --- Lifecycle ---
    void Update();
    void HandleTouchEvent(const SDL_Event* event);
    void Render();

    // --- Enable/disable ---
    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    // --- Joystick ---
    void        SetJoystickMode(JoystickMode mode);
    JoystickMode GetJoystickMode() const;

    // Offset from bottom-left corner (STATIC mode only)
    void SetJoystickPosition(const vf2d& offset);
    vf2d GetJoystickPosition() const;

    void  SetJoystickRadius(float radius);
    void  SetJoystickDeadZone(float deadZone);  // 0.0–1.0

    void SetJoystickBaseTexture(TextureAsset* texture);    // nullptr = default
    void SetJoystickStickTexture(TextureAsset* texture);   // nullptr = default

    const JoystickState& GetJoystickState() const;
    vf2d  GetJoystickDirection() const;   // normalised direction vector
    float GetJoystickMagnitude() const;   // 0.0–1.0

    // --- Buttons (index 0–3) ---
    void SetButtonCount(int count);                              // 0–4
    void SetButtonGroupOffset(const vf2d& offset);              // from bottom-right
    vf2d GetButtonGroupOffset() const;
    vf2d GetButtonAnchorOffset() const;
    void SetButtonTexture(int buttonIndex, TextureAsset* texture); // nullptr = default

    bool IsButtonPressed(int buttonIndex) const;       // held this frame
    bool IsButtonJustPressed(int buttonIndex) const;   // went down this frame
    bool IsButtonJustReleased(int buttonIndex) const;  // went up this frame

    // --- ImGui debug (when LUMINOVEAU_WITH_IMGUI defined) ---
    void RenderDebugWindow();
    void ShowDebugWindow(bool show);
    bool IsDebugWindowVisible() const;
};
```

---

## EventBus

```cpp
using EventData         = std::unordered_map<std::string, std::variant<int, float, std::string>>;
using EventCallback     = std::function<void()>;
using EventCallbackData = std::function<void(EventData)>;

enum class SystemEvent {
    GAMEPAD_CONNECTED,
    GAMEPAD_DISCONNECTED,
    WINDOW_RESIZE,
    WINDOW_FULLSCREEN,
};

class EventBus {
    // --- Custom events ---
    static void Register(std::string eventName, EventCallback callback);
    static void Register(std::string eventName, EventCallbackData callback);

    static void Fire(std::string eventName);
    static void Fire(std::string eventName, EventData eventData);

    // --- System events ---
    static void Register(SystemEvent eventName, EventCallbackData callback);
    static void Fire(SystemEvent eventName, EventData eventData);
};
```

---

## State

```cpp
class BaseState {
    virtual void load()   = 0;
    virtual void unload() = 0;
    virtual void draw()   = 0;
};

class State {
    static void AddState(std::string stateName, BaseState* state);
    static void Init(std::string stateName);   // sets the initial active state
    static void SetState(std::string newState);
    static void Load();
    static void Unload();
    static void Draw();
};
```

---

## Camera (2D)

```cpp
class Camera {
    static void  Activate();
    static void  Deactivate();
    static bool  IsActive();

    // SetTarget throws std::logic_error if the camera is locked
    static void  SetTarget(const vf2d& newTarget);
    static vf2d  GetTarget();

    static void  SetScale(float newScale);
    static float GetScale();

    // Lock captures current Target/Scale; unlocks re-enable movement
    static void Lock();
    static void Unlock();
    static bool IsLocked();
    static bool HasMoved();

    // Coordinate conversion
    static vf2d ToScreenSpace(const vf2d& worldSpace);
    static vf2d ToWorldSpace(const vf2d& screenSpace);
};
```

---

## Camera3D

POD struct, not a singleton.

```cpp
struct Camera3D {
    vf3d  position  = {0.f, 0.f, 5.f};
    vf3d  target    = {0.f, 0.f, 0.f};
    vf3d  up        = {0.f, 1.f, 0.f};
    float fov       = 60.f;   // degrees
    float nearPlane = 0.1f;
    float farPlane  = 100.f;

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float aspectRatio) const;
    glm::mat4 GetViewProjectionMatrix(float aspectRatio) const;
};
```

---

## Scene (3D)

```cpp
enum class LightType { Point, Directional, Spot };

struct Light {
    LightType type        = LightType::Point;
    vf3d      position    = {0, 0, 0};
    vf3d      direction   = {0, -1, 0};
    Color     color       = WHITE;
    float     intensity   = 1.f;
    float     constant    = 1.f;
    float     linear      = 0.09f;
    float     quadratic   = 0.032f;
    float     cutoffAngle      = 12.5f;   // spot inner cone (degrees)
    float     outerCutoffAngle = 17.5f;   // spot outer cone (degrees)
};

struct ModelInstance {
    ModelAsset* model    = nullptr;
    vf3d        position = {0, 0, 0};
    vf3d        rotation = {0, 0, 0};  // Euler angles in degrees (Z, Y, X applied order)
    vf3d        scale    = {1, 1, 1};
    Color       tint     = WHITE;
    TextureAsset textureOverride;        // overrides model default if set

    glm::mat4 GetModelMatrix() const;
};

class Scene {
    // --- Scene management ---
    static void        New(const std::string& name);
    static void        Switch(const std::string& name);
    static std::string GetCurrentSceneName();
    static void        Delete(const std::string& name);  // cannot delete default scene

    // --- Camera ---
    static void      SetCamera(vf3d position, vf3d target);
    static void      SetCameraFOV(float fov);            // degrees
    static void      SetCameraClipPlanes(float nearPlane, float farPlane);
    static Camera3D& GetCamera();

    // --- Models ---
    static ModelInstance& AddModel(ModelAsset* model,
                                   vf3d position = {0,0,0},
                                   vf3d rotation = {0,0,0},
                                   vf3d scale    = {1,1,1});
    static std::vector<ModelInstance>& GetModels();
    static void ClearModels();

    // --- Lights ---
    static Light& AddPointLight(const Light& light);
    static Light& AddPointLight(vf3d position,
                                Color color = WHITE, float intensity = 1.f);

    static Light& AddDirectionalLight(const Light& light);
    static Light& AddDirectionalLight(vf3d direction,
                                      Color color = WHITE, float intensity = 1.f);

    static Light& AddSpotLight(const Light& light);

    static std::vector<Light>& GetLights();
    static void ClearLights();

    static void  SetAmbientLight(Color color);
    static Color GetAmbientLight();

    // --- Combined clear ---
    static void Clear();
};
```

---

## ModelAsset

```cpp
struct ModelAsset {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    TextureAsset          texture;    // defaults to white pixel
    const char*           name = nullptr;

    // GPU buffers (managed internally):
    SDL_GPUBuffer*         vertexBuffer;
    SDL_GPUBuffer*         indexBuffer;
    SDL_GPUTransferBuffer* vertexTransferBuffer;
    SDL_GPUTransferBuffer* indexTransferBuffer;

    size_t GetVertexCount() const;
    size_t GetIndexCount() const;

    // Only valid for cubes (24 vertices = 6 faces x 4 vertices)
    void SetCubeFaceUVs(CubeFace face, const FaceUV& uvs);

    void release(SDL_GPUDevice* device);
};

enum class CubeFace { Front, Back, Top, Bottom, Right, Left };
enum class CubeUVLayout { SingleTexture, Atlas4x4, Atlas3x2, Skybox, Custom };

struct FaceUV {
    float uMin, vMin, uMax, vMax;
    FaceUV(float u0 = 0.f, float v0 = 0.f, float u1 = 1.f, float v1 = 1.f);
};
```

---

## Settings

```cpp
class Settings {
    static void Init();
    static void saveSettings();

    static void setRes(int width, int height);
    static void ToggleFullscreen();
    static void toggleVsync();
    static bool getVsync();

    static std::vector<std::pair<int,int>> resolutions();

    static int getMonitorRefreshRate();

    static void  setMasterVolume(float volume);   // 0.0–1.0
    static float getMasterVolume();

    static void  setMusicVolume(float volume);
    static float getMusicVolume();

    static void  setSoundVolume(float volume);
    static float getSoundVolume();
};
```

---

## EffectHandler / Effects

```cpp
class EffectHandler {
    // Creates a new EffectAsset with its own uniform buffer, populated from
    // shader reflection data. Each call returns an independent instance.
    static EffectAsset Create(const ShaderAsset& vertShader,
                              const ShaderAsset& fragShader);
};

// Convenience namespace alias:
namespace Effects {
    EffectAsset Create(const ShaderAsset& vertShader,
                       const ShaderAsset& fragShader);
}
```

### EffectAsset

```cpp
struct EffectAsset {
    ShaderAsset                     vertShader;
    ShaderAsset                     fragShader;
    std::shared_ptr<UniformBuffer>  uniforms;

    // Set a uniform variable by name (must match the shader):
    effect["myFloat"] = 0.5f;
    effect["myVec2"]  = glm::vec2{1.f, 0.f};
};
```

---

## Shaders

```cpp
class Shaders {
    static void Init();
    static void Quit();

    static PhysFSFileData   GetShader(const std::string& filename);
    static ShaderMetadata   GetShaderMetadata(const std::string& filename);
    static SDL_GPUShaderFormat GetShaderFormat(const std::string& filename);

    static SDL_GPUShader* CreateGPUShader(SDL_GPUDevice* device,
                                           const std::string& filename,
                                           SDL_GPUShaderStage stage);

    static ShaderAsset CreateShaderAsset(SDL_GPUDevice* device,
                                          const std::string& filename,
                                          SDL_GPUShaderStage stage);
};
```

---

## FileHandler

```cpp
class FileHandler {
    // --- Configuration (call before reading assets) ---
    static void SetOrganizationName(const std::string& name);
    static void SetApplicationName(const std::string& name);

    // --- Paths ---
    static std::string GetWritableDirectory();   // trailing slash
    static std::string GetSystemDirectory();     // GetWritable() + "LumiSystem/"
    static std::string GetBaseDirectory();       // executable directory

    // --- PhysFS (asset loading) ---
    static bool          InitPhysFS();
    static PhysFSFileData ReadFile(const std::string& filename);
    static PhysFSFileData GetFileFromPhysFS(const std::string& filename); // alias

    // --- File reading ---
    static std::string         ReadTextFile(const std::string& filepath);
    static std::vector<uint8_t> ReadBinaryFile(const std::string& filepath);

    // --- File writing ---
    static bool WriteFile(const std::string& filepath,
                          const void* data, size_t size);
    static bool WriteTextFile(const std::string& filepath,
                              const std::string& text);

    // --- Queries ---
    static bool   FileExists(const std::string& filepath);
    static bool   DirectoryExists(const std::string& dirpath);
    static size_t GetFileSize(const std::string& filepath);

    // --- Deletion ---
    static bool DeleteFile(const std::string& filepath);
    static bool DeleteDirectory(const std::string& dirpath);
    static bool ClearSystemDirectory();
    static bool DeleteShaderCache();   // deletes LumiSystem/shader.cache
    static bool ClearLogs();           // deletes all .log files in LumiSystem/
};
```

---

## Lerp

```cpp
struct LerpAnimator {
    const char* name;
    float time;
    float startValue;
    float change;
    float duration;
    bool  started;
    bool  canDelete;
    bool  shouldDelete;

    // Easing callback: signature matches all EaseXxx functions in easings.h
    std::function<float(float time, float startValue, float change, float duration)> callback;

    bool  isFinished() const;
    float getValue();        // clamped to [startValue, startValue+change]
};

class Lerp {
    // Get or create a lerp by name. Creates on first call; returns existing on subsequent calls.
    static LerpAnimator* getLerp(const char* name,
                                  float startValue, float change, float duration);

    // Get an existing lerp by name (returns nullptr if not found)
    static LerpAnimator* getLerp(const char* name);

    static void resetTime(const char* name);
    static void updateLerps();   // call every frame
};
```

---

## Easing Functions

All have the signature `float EaseXxx(float t, float b, float c, float d)`:

- `t` — current time (any unit, same as `d`)
- `b` — starting value
- `c` — total change (end value = `b + c`)
- `d` — total duration

```
EaseLinearNone / EaseLinearIn / EaseLinearOut / EaseLinearInOut
EaseSineIn / EaseSineOut / EaseSineInOut
EaseCircIn / EaseCircOut / EaseCircInOut
EaseCubicIn / EaseCubicOut / EaseCubicInOut
EaseQuadIn / EaseQuadOut / EaseQuadInOut
EaseExpoIn / EaseExpoOut / EaseExpoInOut
EaseBackIn / EaseBackOut / EaseBackInOut
EaseBounceIn / EaseBounceOut / EaseBounceInOut
EaseElasticIn / EaseElasticOut / EaseElasticInOut
```

---

## UniformBuffer

Used by `EffectAsset` and render passes.

```cpp
class UniformBuffer {
    void addVariable(const std::string& name, size_t typeSize, size_t offset);
    void setAlignment(size_t newAlignment);

    template<typename T>
    void setVariable(const std::string& name, const T& value);

    template<typename T>
    T getVariable(const std::string& name) const;

    const void* getBufferPointer() const;
    size_t      getBufferSize() const;

    // Proxy-based assignment:
    buffer["variableName"] = someValue;
};
```

---

## ResourcePack

```cpp
class ResourcePack {
    ResourcePack(std::string sFile, std::string sKey);

    bool AddFile(const std::string& sFile);
    bool AddFile(const std::string& sFile, std::vector<unsigned char> bytes);
    bool HasFile(const std::string& sFile);
    bool LoadPack();
    bool SavePack();
    ResourceBuffer GetFileBuffer(const std::string& sFile);
    bool Loaded();
};
```

---

## BufferManager

```cpp
class BufferManager {
    template<typename T>
    static Buffer<T>* Create(const std::string& name, size_t capacity,
                              BufferType type = BufferType::CPU);

    static void   ResetAll();
    static void   DestroyAll();
    static size_t TotalBytesUsed();
    static size_t TotalBytesAllocated();
    static size_t BufferCount();
    static const std::vector<std::unique_ptr<BufferBase>>& GetBuffers();
};

template<typename T>
class Buffer : public BufferBase {
    T*     Add();             // default-constructs a new item in-place
    T*     Add(const T& item);

    T&     operator[](size_t i);
    T*     Data();
    size_t Count() const;
    size_t Capacity() const;
    size_t BytesUsed() const;
    size_t BytesAllocated() const;
    size_t HighWatermark() const;

    void Reset();    // resets count; destroys non-trivial items
    void Release();  // frees memory
};
```

---

## Log (Macros)

Use the macros — do not call the implementation methods directly.

```cpp
LOG_DEBUG("message {}", value);     // verbose debug info
LOG_INFO("message {}", value);      // general info
LOG_WARNING("message {}", value);   // non-critical warning
LOG_ERROR("message {}", value);     // logs then throws std::runtime_error
LOG_CRITICAL("message {}", value);  // logs, flushes, then calls std::exit(EXIT_FAILURE)
```

Format strings use `{fmt}` style.

---

## QuadTree

```cpp
struct QuadTree::qtPoint {
    float x, y;
    void* entity = nullptr;
};

struct QuadTree::AABB {
    float _left, _top, _width, _height;
    float getLeft(); float getRight();
    float getTop();  float getBottom();
    bool containsPoint(const qtPoint& point);
    bool intersectsAABB(const AABB& other);
    rectf getRectangle();
    AABB(float left, float top, float width, float height);
};

struct QuadTree::AABBCircle {
    float _x, _y, _r;
    bool containsPoint(const qtPoint& point);
    bool intersectsAABB(const AABB& range);
    AABBCircle(float x, float y, float r);
};

class QuadTree {
    QuadTree(rectf boundary);

    bool insert(const qtPoint& point);
    void subdivide();

    void query(AABB range,       std::vector<void*>* found);
    void query(AABBCircle range, std::vector<void*>* found);

    void reset();   // clears points and deletes child nodes (frees memory)

    void draw(Color col);
    void draw(int x, int y, Color col);
    void draw();    // draws in white
};
```

---

## Helpers

```cpp
class Helpers {
    static int   clamp(int input, int min, int max);
    static float mapValues(float x, float in_min, float in_max,
                           float out_min, float out_max);

    static bool  randomChance(float required);     // required: 0.0–1.0
    static int   GetRandomValue(int min, int max);

    static bool  lineIntersectsRectangle(vf2d lineStart, vf2d lineEnd, rectf rect);
    static std::vector<std::pair<vf2d, vf2d>> getLinesFromRectangle(rectf rect);

    // printf-style formatting into a rotating buffer (4 slots, max 1024 chars each)
    static const char* TextFormat(const char* text, ...);

    static std::string Slugify(std::string input);
    static uint64_t    GetTotalSystemMemory();
    static time_t      GetFileModificationTime(const std::string& filepath);

    static void DrawMainMenu();         // internal debug menu

    // ImGui panel visibility flags (when LUMINOVEAU_WITH_IMGUI defined):
    static bool imguiTexturesVisible;
    static bool imguiAudioVisible;
    static bool imguiInputVisible;
    static bool imguiDemoVisible;
};
```
