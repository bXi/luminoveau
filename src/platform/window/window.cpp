#include "window.h"

#include <stdexcept>
#include "platform/audio/audio.h"

#include "assets/assethandler.h"

#include "util/helpers.h"

#include "renderer/renderer.h"

#include <SDL3_image/SDL_image.h>

#ifdef LUMINOVEAU_WITH_RMLUI
#include "integrations/rmlui/rmlui.h"
#endif

#ifdef LUMINOVEAU_WITH_IMGUI
#include "integrations/imgui/imgui_integration.h"
#endif

void Window::GetDisplayBounds(uint32_t& outW, uint32_t& outH) {
#ifdef LUMINOVEAU_WEBGPU_BACKEND
    // Browser canvas: SDL display queries are unreliable, use window size.
    outW = (uint32_t) Window::GetWidth();
    outH = (uint32_t) Window::GetHeight();
#else
    outW = 3840u; outH = 2160u; // safe 4K fallback
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    SDL_Rect bounds = {};
    if (SDL_GetDisplayBounds(display, &bounds)) {
        outW = (uint32_t) bounds.w;
        outH = (uint32_t) bounds.h;
    }
#endif
}

void Window::_initWindow(const std::string &title, int width, int height, int scale, unsigned int flags) {

    if (scale > 1) { //when scaling asume width is virtual pixels instead of real screen pixels
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
        m_window = window;
    } else {
        LOG_CRITICAL("couldn't create window: {}", SDL_GetError());
    }

#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::Init(m_window);
#endif

    // Query HiDPI display scale factor
    _updateDisplayScale();

    Renderer::InitRendering();

#ifdef LUMINOVEAU_WEBGPU_BACKEND
    // Update the camera to match the canvas size that WebGpuGpuBackend::init() set.
    // Do NOT call _setSize() here — that would set _sizeDirty and trigger _reset()
    // on frame 0, destroying and recreating pipelines that were just compiled and
    // waited on. The framebuffers and render passes are already sized correctly.
    Renderer::UpdateCameraProjection();
#endif

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
        _pendingClose = true;
        EngineState::_shouldQuit = true;
    } else {
        // Outside frame (e.g. after game loop): close immediately
        _close();
    }
}

void Window::_close() {
    if (!m_window) return;  // Already closed

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
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    
    // SDL_QuitSubSystem is ref-counted
    Audio::Close();
    
    SDL_Quit();
}

double Window::_getRunTime() {
    return (double) SDL_GetTicks() * 1000.0;
}

void Window::_toggleFullscreen() {
    bool isFullscreen = _isFullscreen();

    if (!isFullscreen) {
        _maximized = true;
        _lastWindowWidth  = (int) _getSize().x;
        _lastWindowHeight = (int) _getSize().y;

        SDL_SetWindowFullscreen(m_window, true);
        SDL_SyncWindow(m_window);

        _setSize((int) _getSize().x, (int) _getSize().y);
    } else {

        SDL_SetWindowFullscreen(m_window, false);
        SDL_SyncWindow(m_window);
        _setSize(_lastWindowWidth, _lastWindowHeight);
    }
}

int Window::_getFPS(float milliseconds) {
    auto seconds = milliseconds / 1000.f;

    if (EngineState::_fpsAccumulator > seconds) {
        EngineState::_fpsAccumulator -= seconds;

        EngineState::_fps = (int) (1. / EngineState::_lastFrameTime);
    }
    return EngineState::_fps;
}

// Helper function to process a single event - used by both modes
void Window::_processEvent(SDL_Event* event) {
#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::ProcessEvent(event);
#endif

#ifdef LUMINOVEAU_WITH_RMLUI
    RmlUI::ProcessEvent(*event);
#endif

    switch (event->type) {
        case SDL_EventType::SDL_EVENT_QUIT:
            EngineState::_shouldQuit = true;
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
        case SDL_EventType::SDL_EVENT_GAMEPAD_ADDED:
            Input::AddGamepadDevice(event->gdevice.which);
            break;
        case SDL_EventType::SDL_EVENT_GAMEPAD_REMOVED:
            Input::RemoveGamepadDevice(event->gdevice.which);
            break;
        case SDL_EventType::SDL_EVENT_WINDOW_RESIZED: {
            EventData resizeEventData;
            resizeEventData.emplace("width", event->window.data1);
            resizeEventData.emplace("height", event->window.data2);
            EventBus::Fire(SystemEvent::WINDOW_RESIZE, resizeEventData);

#if defined(LUMINOVEAU_WEBGPU_BACKEND) && defined(__EMSCRIPTEN__)
            // The browser already resized the canvas; calling SDL_SetWindowSize
            // here would DPI-scale the logical dimensions onto canvas.style.*,
            // inflating window.innerWidth and the swapchain beyond the viewport.
            if (_webGpuScaleMode == WebGpuScaleMode::Native) {
                Renderer::UpdateCameraProjection();
                _sizeDirty = true;
            } else {
                _setSize(event->window.data1, event->window.data2);
            }
#else
            _setSize(event->window.data1, event->window.data2);
#endif

            if (!_maximized) {
                _lastWindowWidth = event->window.data1;
                _lastWindowHeight = event->window.data2;
            }
            break;
        }
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP: {
            Input::HandleTouchEvent(event);
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

#ifndef SDL_MAIN_USE_CALLBACKS
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
        EngineState::_debugMenuVisible = !EngineState::_debugMenuVisible;
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
        EngineState::_debugMenuVisible = !EngineState::_debugMenuVisible;
    }
#endif
}

// Public wrapper for event processing in callback mode
void Window::ProcessEvent(SDL_Event* event) {
    _processEvent(event);
}
#endif

bool Window::_isFullscreen() {
    auto flag          = SDL_GetWindowFlags(m_window);
    auto is_fullscreen = flag & SDL_WINDOW_FULLSCREEN;
    return is_fullscreen == SDL_WINDOW_FULLSCREEN;
}

vf2d Window::_getSize(bool getRealSize) {
    int w, h;

#ifdef LUMINOVEAU_WEBGPU_BACKEND
    if (_webGpuScaleMode == WebGpuScaleMode::Native) {
#ifdef __EMSCRIPTEN__
        // Use actual swapchain dimensions so game coordinates match the framebuffer.
        // SDL_GetWindowSizeInPixels applies DPI scaling that diverges from
        // the CSS-pixel-based swapchain size acquireSwapchainTexture sets.
        uint32_t cw = Renderer::GetCanvasWidth();
        uint32_t ch = Renderer::GetCanvasHeight();
        if (cw > 0 && ch > 0) return {(float)cw, (float)ch};
#endif
        SDL_GetWindowSizeInPixels(m_window, &w, &h);
        return {(float)w, (float)h};
    }
    return {(float)_webGpuRenderWidth, (float)_webGpuRenderHeight};
#endif

#ifdef LUMI_USE_PHYSICAL_PIXELS
    // Physical pixel mode: always return actual device pixels
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
#else
    // Virtual pixel mode (default): return logical points
    if (_isFullscreen()) {
        const SDL_DisplayMode *dm;

        int windowX = 10;
        int windowY = 10;

        SDL_GetWindowPosition(Window::GetWindow(), &windowX, &windowY);

        const SDL_Point *point = new const SDL_Point({windowX + 10, windowY + 10});

        dm = SDL_GetCurrentDisplayMode(
            SDL_GetDisplayForPoint(point)
        );

        w = dm->w;
        h = dm->h;
    } else {
        SDL_GetWindowSize(m_window, &w, &h);
    }
#endif

    if (!getRealSize && EngineState::_scaleFactor > 1) {
        w /= EngineState::_scaleFactor;
        h /= EngineState::_scaleFactor;
    }

    return {(float) w, (float) h};
}

vf2d Window::_getPhysicalSize() {
    int w, h;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
    return {(float) w, (float) h};
}

void Window::_updateDisplayScale() {
    if (!m_window) return;
    float scale = SDL_GetWindowDisplayScale(m_window);
    if (scale > 0.0f) {
        EngineState::_displayScale = scale;
    }
}

void Window::_setSize(int width, int height) {
    SDL_SetWindowSize(m_window, width, height);
    SDL_SyncWindow(m_window);

    // Update camera immediately so rendering adapts to new size
    Renderer::UpdateCameraProjection();

    _sizeDirty = true;
}

void Window::_startFrame() {
    _inFrame = true;
    Lerp::updateLerps();

    Window::HandleInput();

    // Only update camera on resize - render passes stay at desktop size
    if (_sizeDirty || Renderer::ConsumePendingReset()) {
        Renderer::OnResize();
        _sizeDirty = false;
    }

    EngineState::_frameCount++;
    EngineState::_previousTime = EngineState::_currentTime;
    EngineState::_currentTime  = std::chrono::high_resolution_clock::now();
    EngineState::_lastFrameTime =
        (double) std::chrono::duration_cast<std::chrono::nanoseconds>(EngineState::_currentTime - EngineState::_previousTime).count() /
        1000000000.;
    EngineState::_fpsAccumulator += EngineState::_lastFrameTime;

    Renderer::StartFrame();
}

void Window::_endFrame() {

#ifdef LUMINOVEAU_WITH_IMGUI
    if (EngineState::_debugMenuVisible) {
        ImGuiIntegration::DrawDebugMenu();
    }
#endif

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
    return m_window;
}

void Window::_toggleDebugMenu() {
#ifdef LUMINOVEAU_WITH_IMGUI
    EngineState::_debugMenuVisible = !EngineState::_debugMenuVisible;
#endif
}

void Window::_setScale(int scalefactor) {
    EngineState::_scaleFactor = scalefactor;
}

void Window::_setScaledSize(int widthInScaledPixels, int heightInScaledPixels, int scale) {

    if (scale > 0) {
        SetScale(scale);
    }

    _setSize(EngineState::_scaleFactor * widthInScaledPixels, EngineState::_scaleFactor * heightInScaledPixels);
}

float Window::_getScale() {
    return (float) EngineState::_scaleFactor;
}

void Window::_setIcon(const std::string &filename) {
    auto icon = FileHandler::GetFileFromPhysFS(filename);
    SDL_IOStream* io = SDL_IOFromMem(icon.data, icon.fileSize);
    SDL_Surface* iconSurface = IMG_Load_IO(io, true); // SDL_TRUE = close IO after reading

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
    auto icon = FileHandler::GetFileFromPhysFS(filename);
    SDL_IOStream* io = SDL_IOFromMem(icon.data, icon.fileSize);
    SDL_Surface* cursorSurface = IMG_Load_IO(io, true); // SDL_TRUE = close IO after reading

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

void Window::_takeScreenshot(const std::string& filename) {
    // Defer screenshot until end of frame
    _pendingScreenshot = true;
    _pendingScreenshotFilename = filename;
}