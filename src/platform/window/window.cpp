#include "window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include "platform/audio/audio.h"

#include "assets/assethandler.h"

#include "util/helpers.h"

#include "renderer/renderer.h"
#include "platform/window/window_backend.h"
#include "profiler/perf.h"
#include "draw/draw.h"

#include <SDL3_image/SDL_image.h>

#ifdef LUMINOVEAU_WITH_RMLUI
#include "integrations/rmlui/rmlui.h"
#endif

#ifdef LUMINOVEAU_WITH_IMGUI
#include "integrations/imgui/imgui_integration.h"
#endif

void Window::GetDisplayBounds(uint32_t &outW, uint32_t &outH) {
    outW = 3840u;
    outH = 2160u; // safe 4K fallback; backend may override.
    WindowBackend::GetDisplayBounds(outW, outH);
}

void Window::_initWindow(const std::string &title, int width, int height, int scale, unsigned int flags) {

    if (scale > 1) { // when scaling asume width is virtual pixels instead of real screen pixels
        width *= scale;
        height *= scale;

        _setScale(scale);
    }

    _lastWindowWidth  = width;
    _lastWindowHeight = height;

    SDL_Init(SDL_INIT_VIDEO);

    // Always enable high-DPI so the GPU renders at full physical resolution
    // on Retina/HiDPI displays. Without this, the swapchain stays at logical
    // size and SDL_GetWindowSizeInPixels() == SDL_GetWindowSize().
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

    auto window = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (window) {
        _window     = window;
        _mainWindow = window;
        _mainId     = SDL_GetWindowID(window);
        bool resizable = (flags & SDL_WINDOW_RESIZABLE) != 0;
        _registry.push_back({ _mainId, window, 0, 0, 0, 0, resizable });
        if (resizable)
            SDL_SetWindowHitTest(window, &Window::_hitTest, nullptr);
    } else {
        LOG_CRITICAL("couldn't create window: {}", SDL_GetError());
    }

#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::Init(_window);
#endif

    // Query HiDPI display scale factor
    _updateDisplayScale();

    Renderer::InitRendering();

    WindowBackend::PostInit(_window);

    if (!FileHandler::InitPhysFS()) {
        LOG_CRITICAL("AssetHandler::InitPhysFS failed");
    }

    Input::Init();

#ifdef LUMINOVEAU_WITH_RMLUI
    RmlUI::Init();
#endif
}

void Window::_requestClose() {
    LOG_INFO("Shutting down");
    if (_inFrame) {
        // Mid-frame: defer actual close until EndFrame completes
        _pendingClose           = true;
        EngineState::shouldQuit = true;
    } else {
        // Outside frame (e.g. after game loop): close immediately
        _close();
    }
}

void Window::_close() {
    if (!_window)
        return; // Already closed

#ifdef LUMINOVEAU_WITH_RMLUI
    RmlUI::Shutdown();
#endif

#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::Shutdown();
#endif

    // CRITICAL: Clean up assets BEFORE closing renderer/destroying device
    // Otherwise shader modules are released after device destruction
    AssetHandler::Cleanup();

    // Clean up renderer before destroying window
    Renderer::Close();

    // Destroy the window
    if (_window) {
        SDL_DestroyWindow(_window);
        _window = nullptr;
    }

    // SDL_QuitSubSystem is ref-counted
    Audio::Close();

    SDL_Quit();
}

double Window::_getRunTime() {
    return (double)SDL_GetTicks() * 1000.0;
}

void Window::_toggleFullscreen() {
    bool isFullscreen = _isFullscreen();

    if (!isFullscreen) {
        _maximized        = true;
        _lastWindowWidth  = (int)_getSize().x;
        _lastWindowHeight = (int)_getSize().y;

        SDL_SetWindowFullscreen(_window, true);
        SDL_SyncWindow(_window);

        _setSize((int)_getSize().x, (int)_getSize().y);
    } else {

        SDL_SetWindowFullscreen(_window, false);
        SDL_SyncWindow(_window);
        _setSize(_lastWindowWidth, _lastWindowHeight);
    }
}

int Window::_getFPS(float milliseconds) {
    // Compute over the requested averaging window using the renderer-maintained
    // _presentCount. Same parameter semantics as the original API: longer windows
    // = smoother / slower-to-react number, shorter = jumpier / fresh.
    using clock                = std::chrono::high_resolution_clock;
    const double windowSeconds = (double)milliseconds / 1000.0;
    auto         now           = clock::now();

    static auto     sampleStart      = now;
    static uint64_t sampleStartCount = EngineState::presentCount;

    double elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         now - sampleStart)
                         .count()
        / 1e9;

    if (elapsed >= windowSeconds && elapsed > 0.0) {
        uint64_t delta   = EngineState::presentCount - sampleStartCount;
        EngineState::fps = (int)((double)delta / elapsed);
        sampleStart      = now;
        sampleStartCount = EngineState::presentCount;
    }
    return EngineState::fps;
}

// Two-finger pinch → mouse-wheel ticks, so touch devices can zoom demos that use the scroll wheel.
// Driven from SDL finger events (same event stream/timing as the real wheel). tfinger coords are
// normalised (0..1); a spread past a small threshold emits one wheel notch (out = zoom in).
static void handlePinch(const SDL_Event *event) {
    static std::unordered_map<SDL_FingerID, vf2d> fingers;
    static float                                  prevDist       = -1.0f;
    static float                                  accum          = 0.0f;
    const float                                   pinchThreshold = 0.03f;

    SDL_FingerID id = event->tfinger.fingerID;
    if (event->type == SDL_EVENT_FINGER_UP) {
        fingers.erase(id);
        prevDist = -1.0f;
        accum    = 0.0f;
        return;
    }
    fingers[id] = { event->tfinger.x, event->tfinger.y };

    if (fingers.size() == 2) {
        auto it = fingers.begin();
        vf2d a  = it->second;
        ++it;
        vf2d  b = it->second;
        float d = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
        if (prevDist >= 0.0f) {
            accum += d - prevDist;
            while (accum > pinchThreshold) {
                Input::UpdateScroll(1);
                accum -= pinchThreshold;
            }
            while (accum < -pinchThreshold) {
                Input::UpdateScroll(-1);
                accum += pinchThreshold;
            }
        }
        prevDist = d;
    } else {
        prevDist = -1.0f; // need exactly two fingers for a pinch reference
    }
}

// Helper function to process a single event - used by both modes
void Window::_processEvent(SDL_Event *event) {
#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::ProcessEvent(event);
#endif

#ifdef LUMINOVEAU_WITH_RMLUI
    RmlUI::ProcessEvent(*event);
#endif

    switch (event->type) {
    case SDL_EventType::SDL_EVENT_QUIT:
        EngineState::shouldQuit = true;
        break;
    case SDL_EventType::SDL_EVENT_KEY_DOWN:
        _bufferedKeysDown.push_back(event->key.scancode);
        break;
    case SDL_EventType::SDL_EVENT_KEY_UP:
        _bufferedKeysUp.push_back(event->key.scancode);
        break;
    case SDL_EventType::SDL_EVENT_MOUSE_WHEEL:
        Input::UpdateScroll(event->wheel.integer_y);
        break;
    case SDL_EventType::SDL_EVENT_MOUSE_MOTION:
        Input::AccumulateMouseDelta(event->motion.xrel, event->motion.yrel);
        break;
    case SDL_EventType::SDL_EVENT_GAMEPAD_ADDED:
        Input::AddGamepadDevice(event->gdevice.which);
        break;
    case SDL_EventType::SDL_EVENT_GAMEPAD_REMOVED:
        Input::RemoveGamepadDevice(event->gdevice.which);
        break;
    case SDL_EventType::SDL_EVENT_WINDOW_RESIZED: {
        // Only the primary drives the engine's canvas/framebuffer resize. Secondary windows
        // adapt per-frame in Window::_activateWindow (their swapchain size is re-read there),
        // so applying their resize to the shared engine state here would corrupt the primary.
        if (event->window.windowID != _mainId)
            break;

        EventData resizeEventData;
        resizeEventData.emplace("width", event->window.data1);
        resizeEventData.emplace("height", event->window.data2);
        EventBus::Fire(SystemEvent::WINDOW_RESIZE, resizeEventData);

        if (WindowBackend::HandleResize(event->window.data1, event->window.data2, _webGpuScaleMode)) {
            _sizeDirty = true;
        } else {
            _setSize(event->window.data1, event->window.data2);
        }

        if (!_maximized) {
            _lastWindowWidth  = event->window.data1;
            _lastWindowHeight = event->window.data2;
        }
        break;
    }
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP: {
        Input::HandleTouchEvent(event);
        handlePinch(event); // two-finger spread → mouse-wheel ticks (zoom)
        break;
    }
    case SDL_EventType::SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
        _updateDisplayScale();
        _sizeDirty = true;
        break;
    }
    case SDL_EventType::SDL_EVENT_WINDOW_MAXIMIZED: {
        _maximized = true;
        break;
    }
    case SDL_EventType::SDL_EVENT_WINDOW_RESTORED: {
        _maximized = false;
        EventData restoreEventData;
        restoreEventData.emplace("width", _lastWindowWidth);
        restoreEventData.emplace("height", _lastWindowHeight);

        _setSize(_lastWindowWidth, _lastWindowHeight);

        EventBus::Fire(SystemEvent::WINDOW_RESIZE, restoreEventData);
        break;
    }
    case SDL_EventType::SDL_EVENT_TEXT_INPUT:
        if (_textInputCallback) {
            _textInputCallback(event->text.text);
        }
        break;
    }
}

#ifndef LUMINOVEAU_USE_CALLBACKS
// Traditional main() loop mode - polls events in batch
void Window::_handleInput() {
    // Snapshot previous state BEFORE applying this frame's events
    Input::Update();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        _processEvent(&event); // Buffers key events
    }

    // Apply buffered keyboard events after Update() snapshot
    Input::UpdateInputs(_bufferedKeysDown, true);
    Input::UpdateInputs(_bufferedKeysUp, false);
    _bufferedKeysDown.clear();
    _bufferedKeysUp.clear();

#ifdef LUMINOVEAU_WITH_IMGUI
    if (Input::KeyPressed(SDLK_F11) && Input::KeyDown(SDLK_LSHIFT)) {
        EngineState::debugMenuVisible = !EngineState::debugMenuVisible;
    }
#endif
}
#else
// SDL3 callback mode - handles events one at a time
void Window::_handleInput() {
    // Snapshot previous state BEFORE applying this frame's buffered events
    Input::Update();

    // Now apply the buffered keyboard events from ProcessEvent()
    Input::UpdateInputs(_bufferedKeysDown, true);
    Input::UpdateInputs(_bufferedKeysUp, false);
    _bufferedKeysDown.clear();
    _bufferedKeysUp.clear();

#ifdef LUMINOVEAU_WITH_IMGUI
    if (Input::KeyPressed(SDLK_F11) && Input::KeyDown(SDLK_LSHIFT)) {
        EngineState::debugMenuVisible = !EngineState::debugMenuVisible;
    }
#endif
}
#endif
// ProcessEvent (callback mode) is defined inline in window.h.

bool Window::_isFullscreen() {
    auto flag         = SDL_GetWindowFlags(_window);
    auto isFullscreen = flag & SDL_WINDOW_FULLSCREEN;
    return isFullscreen == SDL_WINDOW_FULLSCREEN;
}

vf2d Window::_getSize(bool getRealSize) {
    int w, h;

    // WebGPU backend reports canvas-CSS-pixel swapchain dims; SDL backend defers
    // (returns false) and we fall through to the normal SDL size logic below.
    vf2d backendSize;
    if (WindowBackend::GetSizeOverride(_window, _webGpuScaleMode,
            _webGpuRenderWidth, _webGpuRenderHeight,
            backendSize)) {
        // Apply the SetScale render divisor here too — same as the SDL path below. Without this the
        // backend (web) reports the full canvas while the camera ortho and mouse mapping are divided
        // by the scale, so GetWidth/Height disagree with draw + input space under SetScale(N>1).
        if (!getRealSize && EngineState::scaleFactor > 1) {
            backendSize.x /= (float)EngineState::scaleFactor;
            backendSize.y /= (float)EngineState::scaleFactor;
        }
        return backendSize;
    }

#ifdef LUMI_USE_PHYSICAL_PIXELS
    // Physical pixel mode: always return actual device pixels
    SDL_GetWindowSizeInPixels(_window, &w, &h);
#else
    // Virtual pixel mode (default): return logical points
    if (_isFullscreen()) {
        const SDL_DisplayMode *dm;

        int windowX = 10;
        int windowY = 10;

        SDL_GetWindowPosition(Window::GetWindow(), &windowX, &windowY);

        const SDL_Point *point = new const SDL_Point({ windowX + 10, windowY + 10 });

        dm = SDL_GetCurrentDisplayMode(
            SDL_GetDisplayForPoint(point));

        w = dm->w;
        h = dm->h;
    } else {
        SDL_GetWindowSize(_window, &w, &h);
    }
#endif

    if (!getRealSize && EngineState::scaleFactor > 1) {
        w /= EngineState::scaleFactor;
        h /= EngineState::scaleFactor;
    }

    return { (float)w, (float)h };
}

vf2d Window::_getPhysicalSize() {
    // WebGPU backend reports the swapchain's canvas-pixel dims here when in Native scale
    // mode (SDL_GetWindowSizeInPixels can disagree with the browser canvas attribute on
    // emscripten/SDL3 builds). SDL backend defers and we use the SDL value.
    vf2d size;
    vf2d backendSize;
    if (WindowBackend::GetPhysicalSizeOverride(_window, _webGpuScaleMode, backendSize)) {
        size = backendSize;
    } else if (EngineState::swapchainWidth > 0 && EngineState::swapchainHeight > 0) {
        // Prefer the actual swapchain dimensions from the last acquire -- that's the size we truly
        // present into, and the viewport/blit must match it. SDL_GetWindowSizeInPixels can disagree
        // on Wayland fractional scaling (scene rendered bigger than the window).
        size = { (float)EngineState::swapchainWidth, (float)EngineState::swapchainHeight };
    } else {
        // Fall back to the SDL size before the first frame's acquire, or if a backend reports nothing.
        int w, h;
        SDL_GetWindowSizeInPixels(_window, &w, &h);
        size = { (float)w, (float)h };
    }

    // Integer render scale (Window::SetScale): render the whole scene into a canvas/scaleFactor
    // region of the framebuffer; the final blit upscales that region back to the full canvas, so
    // every pixel — sprites, text AND fullscreen shaders — gets the same chunky N:N enlargement.
    // This is the "render resolution divisor" model; WebGpuScaleMode still governs canvas-vs-app fit
    // on top. scaleFactor == 1 is a no-op.
    int s = EngineState::scaleFactor > 1 ? EngineState::scaleFactor : 1;
    if (s > 1) {
        size.x = (float)std::max(1, (int)size.x / s);
        size.y = (float)std::max(1, (int)size.y / s);
    }
    return size;
}

void Window::_updateDisplayScale() {
    if (!_window)
        return;
    float scale = SDL_GetWindowDisplayScale(_window);
    if (scale > 0.0f) {
        EngineState::displayScale = scale;
    }
}

void Window::_setSize(int width, int height) {
    // In fullscreen the size is the display's; SDL_SetWindowSize would only rewrite the
    // pending windowed size (clobbering what _toggleFullscreen saved to restore) and on some
    // backends drops the window out of fullscreen. Still refresh the projection below so the
    // fullscreen-entry call from _toggleFullscreen keeps doing its job.
    if (!_isFullscreen()) {
        SDL_SetWindowSize(_window, width, height);
        SDL_SyncWindow(_window);
    }

    // Update camera immediately so rendering adapts to new size
    Renderer::UpdateCameraProjection();

    _sizeDirty = true;
}

void Window::_startFrame() {
    _inFrame = true;
    Lerp::UpdateLerps();

    Window::HandleInput();

    // A pending reset (format/MSAA-class change) needs a full rebuild; a plain resize only
    // needs the cheap path (camera + window-sized MSAA targets) — pipelines and desktop-sized
    // targets/geometry survive, so we skip the costly pass release()+init() on resize.
    if (Renderer::ConsumePendingReset())
        Renderer::Reset();
    if (_sizeDirty) {
        Renderer::OnResize();
        _sizeDirty = false;
    }

    EngineState::frameCount++;
    EngineState::previousTime = EngineState::currentTime;
    EngineState::currentTime  = std::chrono::high_resolution_clock::now();

    double rawFrameTime = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
                              EngineState::currentTime - EngineState::previousTime)
                              .count()
        / 1e9;

    // The first frame's "delta" spans all of startup (asset loading, shader + pipeline
    // compilation) and can be several seconds. Report 0 for it so nothing integrates that
    // startup time. Cap every other frame too, so a hitch — a debugger pause, a stall, a
    // window drag — can't inject a multi-second step into gameplay or the GPU particle sim
    // (which would otherwise emit one huge synchronized burst). 100 ms == a 10 FPS floor.
    constexpr double kMaxFrameTime = 0.1;
    EngineState::lastFrameTime     = (EngineState::frameCount <= 1) ? 0.0 : std::min(rawFrameTime, kMaxFrameTime);

    Renderer::StartFrame();

    Perf::FrameStart(); // mark the start of this frame's CPU work
}

void Window::_endFrame() {

    Perf::FrameEnd(); // CPU ms + sample + draw the perf HUD (before the frame is submitted)

#ifdef LUMINOVEAU_WITH_IMGUI
    if (EngineState::debugMenuVisible) {
        ImGuiIntegration::DrawDebugMenu();
    }
#endif

    // Engine-drawn perf HUD: renders into its own render-to-screen overlay framebuffer
    // (created last -> composited on top of everything). No-op if hidden.
    Perf::Render();

    Renderer::EndFrame();

    _inFrame = false;

    // Deferred close: if user called Window::Close() during update/draw,
    // perform the actual teardown now that the frame is fully submitted
    if (_pendingClose) {
        _pendingClose = false;
        _close();
    }
}

SDL_Window *Window::_getWindow() {
    return _window;
}

bool Window::_hasInputFocus() {
    if (!_window)
        return true;
    return (SDL_GetWindowFlags(_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-window
// ─────────────────────────────────────────────────────────────────────────────

Window::WindowEntry *Window::_entry(WindowHandle w) {
    for (auto &e : _registry)
        if (e.id == w)
            return &e;
    return nullptr;
}

Window::WindowEntry *Window::_entryBySdl(SDL_Window *s) {
    for (auto &e : _registry)
        if (e.sdl == s)
            return &e;
    return nullptr;
}

SDL_Window *Window::_sdlOf(WindowHandle w) {
    WindowEntry *e = _entry(w);
    return e ? e->sdl : nullptr;
}

std::vector<Window::WindowHandle> Window::_allWindows() {
    std::vector<WindowHandle> out;
    out.reserve(_registry.size());
    for (auto &e : _registry)
        out.push_back(e.id);
    return out;
}

Window::WindowHandle Window::_createWindow(const WindowDesc &desc) {
    Uint32 flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.borderless)
        flags |= SDL_WINDOW_BORDERLESS;
    if (desc.resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (desc.alwaysOnTop)
        flags |= SDL_WINDOW_ALWAYS_ON_TOP;

    SDL_Window *w = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (!w) {
        LOG_ERROR("Window::Create failed: {}", SDL_GetError());
        return InvalidWindow;
    }
    if (desc.x >= 0 && desc.y >= 0)
        SDL_SetWindowPosition(w, desc.x, desc.y);
    if (desc.parent != InvalidWindow) {
        if (SDL_Window *p = _sdlOf(desc.parent))
            SDL_SetWindowParent(w, p);
    }
    if (!Renderer::HasGpu() || !Renderer::GetGpu().ClaimWindow(w)) {
        LOG_ERROR("Window::Create: GPU ClaimWindow failed: {}", SDL_GetError());
        SDL_DestroyWindow(w);
        return InvalidWindow;
    }
    WindowHandle id = SDL_GetWindowID(w);
    _registry.push_back({ id, w, 0, 0, 0, 0, desc.resizable });
    SDL_SetWindowHitTest(w, &Window::_hitTest, nullptr);
    return id;
}

void Window::_destroyWindow(WindowHandle w) {
    if (w == _mainId)
        return; // never tear down the primary this way
    WindowEntry *e = _entry(w);
    if (!e)
        return;
    SDL_Window *sdl = e->sdl;
    if (Renderer::HasGpu())
        Renderer::GetGpu().ReleaseWindow(sdl);
    if (_window == sdl)
        _window = _mainWindow;
    _registry.erase(std::remove_if(_registry.begin(), _registry.end(),
                        [w](const WindowEntry &x) { return x.id == w; }),
        _registry.end());
    SDL_DestroyWindow(sdl);
}

void Window::_activateWindow(const WindowEntry &e) {
    _window   = e.sdl;
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(e.sdl, &pw, &ph);
    if (pw <= 0)
        pw = 1;
    if (ph <= 0)
        ph = 1;
    // Publish this window's present size so the pass viewport + blit UV + GetPhysicalWidth
    // all follow it while it renders (matches what EndFrame's swapchain acquire will report,
    // so the resize-detect path in _endFrame stays quiet).
    EngineState::swapchainWidth  = pw;
    EngineState::swapchainHeight = ph;
    Renderer::SetActiveSwapchainWindow(e.sdl);
    Renderer::SetCanvasSize((uint32_t)pw, (uint32_t)ph); // also refreshes the camera projection
}

void Window::_renderAll(const std::function<void(WindowHandle)> &fn) {
    if (!_window || _registry.empty())
        return;

    // ── per-tick prologue (mirrors _startFrame up to Renderer::StartFrame) ──────
    _inFrame = true;
    Lerp::UpdateLerps();
    Window::HandleInput();

    if (Renderer::ConsumePendingReset())
        Renderer::Reset();
    if (_sizeDirty) {
        Renderer::OnResize();
        _sizeDirty = false;
    }

    EngineState::frameCount++;
    EngineState::previousTime = EngineState::currentTime;
    EngineState::currentTime  = std::chrono::high_resolution_clock::now();
    double rawFrameTime       = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
                              EngineState::currentTime - EngineState::previousTime)
                              .count()
        / 1e9;
    constexpr double kMaxFrameTime = 0.1;
    EngineState::lastFrameTime     = (EngineState::frameCount <= 1) ? 0.0 : std::min(rawFrameTime, kMaxFrameTime);

    Perf::FrameStart();

    // ── render every live window ────────────────────────────────────────────────
    // Snapshot ids: the render callback must NOT Destroy a window mid-loop (defer that to
    // after RenderAll), but a window may vanish between ticks, so re-resolve each id.
    std::vector<WindowHandle> ids = _allWindows();
    for (WindowHandle id : ids) {
        WindowEntry *e = _entry(id);
        if (!e)
            continue;
        // Skip windows hidden (e.g. minimised to a system tray) — no swapchain to present.
        if (SDL_GetWindowFlags(e->sdl) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED))
            continue;
        _activateWindow(*e);
        Renderer::StartFrame();
        fn(id);
        if (id == _mainId) {
#ifdef LUMINOVEAU_WITH_IMGUI
            if (EngineState::debugMenuVisible)
                ImGuiIntegration::DrawDebugMenu();
#endif
            Perf::Render();
        }
        Renderer::EndFrame();

        // All windows composite through ONE shared framebuffer (Renderer's primary
        // fbContent), so this window's blit-read of it must finish before the next window
        // clears+writes it — otherwise the two per-window submits race and the primary
        // flickers between windows. Serialize on the GPU between windows. A chat app's GPU
        // load is trivial, so the stall is free; a per-window framebuffer would remove it.
        if (_registry.size() > 1 && Renderer::HasGpu())
            Renderer::GetGpu().WaitIdle();
    }

    // Restore the primary as the active window for any between-tick queries.
    if (WindowEntry *m = _entry(_mainId))
        _activateWindow(*m);

    // ── per-tick epilogue (mirrors _endFrame after Renderer::EndFrame) ──────────
    Audio::UpdateMusicStreams();
    Perf::FrameEnd();
    _inFrame = false;
    if (_pendingClose) {
        _pendingClose = false;
        _close();
    }
}

vf2d Window::_getSizeOf(WindowHandle w, bool real) {
    SDL_Window *s = _sdlOf(w);
    if (!s)
        return { 0, 0 };
    int ww = 0, hh = 0;
    if (real)
        SDL_GetWindowSizeInPixels(s, &ww, &hh);
    else
        SDL_GetWindowSize(s, &ww, &hh);
    return { (float)ww, (float)hh };
}

void Window::_setTitleOf(WindowHandle w, const std::string &t) {
    if (SDL_Window *s = _sdlOf(w))
        SDL_SetWindowTitle(s, t.c_str());
}

void Window::_focusWindow(WindowHandle w) {
    if (SDL_Window *s = _sdlOf(w))
        SDL_RaiseWindow(s);
}

void Window::_minimize(WindowHandle w) {
    if (SDL_Window *s = _sdlOf(w))
        SDL_MinimizeWindow(s);
}

bool Window::_hasInputFocusOf(WindowHandle w) {
    SDL_Window *s = _sdlOf(w);
    return s && (SDL_GetWindowFlags(s) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

void Window::_startTextInput(WindowHandle w) {
    if (SDL_Window *s = _sdlOf(w))
        SDL_StartTextInput(s);
}

vf2d Window::_localMouse(WindowHandle w) {
    SDL_Window *s = _sdlOf(w);
    if (!s)
        return { 0, 0 };
    float gx = 0, gy = 0;
    SDL_GetGlobalMouseState(&gx, &gy);
    int wx = 0, wy = 0;
    SDL_GetWindowPosition(s, &wx, &wy);
    return { gx - (float)wx, gy - (float)wy };
}

bool Window::_containsMouse(WindowHandle w) {
    return SDL_GetMouseFocus() == _sdlOf(w);
}

Window::WindowHandle Window::_hoveredWindow() {
    if (SDL_Window *s = SDL_GetMouseFocus())
        if (WindowEntry *e = _entryBySdl(s))
            return e->id;
    return InvalidWindow;
}

Window::WindowHandle Window::_focusedWindow() {
    if (SDL_Window *s = SDL_GetKeyboardFocus())
        if (WindowEntry *e = _entryBySdl(s))
            return e->id;
    return InvalidWindow;
}

void Window::_setDragRegion(WindowHandle w, float x, float y, float ww, float hh) {
    if (WindowEntry *e = _entry(w)) {
        e->dragX = x;
        e->dragY = y;
        e->dragW = ww;
        e->dragH = hh;
        // Ensure the hit-test is installed (idempotent). The primary window doesn't get it
        // at creation, so this lazily enables dragging when an app marks a region.
        SDL_SetWindowHitTest(e->sdl, &Window::_hitTest, nullptr);
    }
}

SDL_HitTestResult SDLCALL Window::_hitTest(SDL_Window *win, const SDL_Point *area, void * /*data*/) {
    Window      &self = Window::Get();
    WindowEntry *e    = self._entryBySdl(win);
    if (!e)
        return SDL_HITTEST_NORMAL;

    // A fullscreen window has no edges to grab and nowhere to be dragged to; the OS would
    // otherwise honour a RESIZE_*/DRAGGABLE hit and yank it out of fullscreen. Checked per
    // window (not via _isFullscreen(), which only looks at the primary).
    if ((SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0)
        return SDL_HITTEST_NORMAL;

    // Resize edges/corners take priority so the title bar's drag region doesn't swallow them.
    if (e->resizable) {
        int W = 0, H = 0;
        SDL_GetWindowSize(win, &W, &H);
        const int b = 4; // grab margin (kept small so it doesn't eat title-bar buttons)
        bool      L = area->x < b, R = area->x >= W - b, T = area->y < b, B = area->y >= H - b;
        if (T && L) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (T && R) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (B && L) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (B && R) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (L) return SDL_HITTEST_RESIZE_LEFT;
        if (R) return SDL_HITTEST_RESIZE_RIGHT;
        if (T) return SDL_HITTEST_RESIZE_TOP;
        if (B) return SDL_HITTEST_RESIZE_BOTTOM;
    }

    if (e->dragW > 0 && e->dragH > 0 &&
        area->x >= e->dragX && area->x < e->dragX + e->dragW &&
        area->y >= e->dragY && area->y < e->dragY + e->dragH)
        return SDL_HITTEST_DRAGGABLE;

    return SDL_HITTEST_NORMAL;
}

void Window::_toggleDebugMenu() {
#ifdef LUMINOVEAU_WITH_IMGUI
    EngineState::debugMenuVisible = !EngineState::debugMenuVisible;
#endif
}

void Window::_setScale(int scalefactor) {
    if (scalefactor < 1)
        scalefactor = 1;
    if (EngineState::scaleFactor == scalefactor)
        return;
    EngineState::scaleFactor = scalefactor;
    // A runtime scale change has to rebuild the camera projection (and refresh pass surfaces) so 2D
    // content rescales immediately — that's what makes SetScale work outside of InitWindow. Skip
    // before the first frame: the renderer isn't up yet and the projection is built on init anyway.
    if (EngineState::swapchainWidth > 0)
        Renderer::OnResize();
}

void Window::_setScaledSize(int widthInScaledPixels, int heightInScaledPixels, int scale) {

    if (scale > 0) {
        SetScale(scale);
    }

    _setSize(EngineState::scaleFactor * widthInScaledPixels, EngineState::scaleFactor * heightInScaledPixels);
}

float Window::_getScale() {
    return (float)EngineState::scaleFactor;
}

void Window::_setIcon(const std::string &filename) {
    auto          icon        = FileHandler::GetFileFromPhysFS(filename);
    SDL_IOStream *io          = SDL_IOFromMem(icon.data, icon.fileSize);
    SDL_Surface  *iconSurface = IMG_Load_IO(io, true); // SDL_TRUE = close IO after reading

    if (iconSurface) {
        SDL_SetWindowIcon(_getWindow(), iconSurface);
        SDL_DestroySurface(iconSurface);
    }

    free(icon.data);
}

void Window::_setTitle(const std::string &title) {
    SDL_SetWindowTitle(_getWindow(), title.c_str());
}

void Window::_setCursor(const std::string &filename) {
    auto          icon          = FileHandler::GetFileFromPhysFS(filename);
    SDL_IOStream *io            = SDL_IOFromMem(icon.data, icon.fileSize);
    SDL_Surface  *cursorSurface = IMG_Load_IO(io, true); // SDL_TRUE = close IO after reading

    SDL_Cursor *cursor = nullptr;

    cursor = SDL_GetCursor();
    SDL_DestroyCursor(cursor);

    if (cursorSurface) {
        cursor = SDL_CreateColorCursor(cursorSurface, 0, 0);
        SDL_SetCursor(cursor);
        SDL_DestroySurface(cursorSurface);
    }
    free(icon.data);
}

void Window::_setRelativeMouseMode(bool enabled) {
    if (_window)
        SDL_SetWindowRelativeMouseMode(_window, enabled);
}

void Window::_takeScreenshot(const std::string &filename) {
    // Defer screenshot until end of frame
    _pendingScreenshot         = true;
    _pendingScreenshotFilename = filename;
}