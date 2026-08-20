#pragma once

#include <memory>
#include <string>
#include <optional>
#include <chrono>
#include <functional>
#include <vector>

/// Scaling mode for the WebGPU canvas blit. No-op on SDL/native builds.
enum class WebGpuScaleMode {
    Contain, ///< Scale to fit, maintain aspect ratio (letterbox/pillarbox)
    Fill,    ///< Scale to fill, maintain aspect ratio (crops edges)
    Stretch, ///< Stretch to exactly fill the canvas (distorts aspect)
    Native,  ///< Render at canvas resolution; Window::GetWidth/Height return canvas size
};

#include "SDL3/SDL.h"

#include "core/enginestate/enginestate.h"

#include "assets/shader/shader.h"
#include "assets/texture/texture.h"

#include "platform/input/input.h"
#include "core/eventbus/eventbus.h"

#include "types/color.h"
#include "util/lerp.h"
#include "math/vectors.h"
#include "gpu/buffer/uniformobject.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <utility>

// SDL Forward Declarations
struct SDL_Window;
union SDL_Event;

/**
 * @brief Provides functionality for managing the application window.
 */
class Window {
public:
    /**
     * @brief Initializes the application window.
     *
     * @param title The title of the window.
     * @param width The width of the window.
     * @param height The height of the window.
     * @param scale The scale factor of the window.
     * @param flags Additional flags for window creation.
     */
    static void InitWindow(const std::string &title, int width = 800, int height = 600, int scale = 1, unsigned int flags = 0) {
        Get()._initWindow(title, width, height, scale, flags);
    }

    /**
     * @brief Sets the window icon.
     *
     * @param filename Path to the icon file.
     */
    static void SetIcon(const std::string &filename) { Get()._setIcon(filename); }

    /**
     * @brief Sets the mouse cursor icon.
     *
     * @param filename Path to the cursor icon file.
     */
    static void SetCursor(const std::string &filename) { Get()._setCursor(filename); }

    /**
     * @brief Enables/disables relative mouse mode (FPS capture): hides and
     * confines the cursor and delivers continuous relative motion.
     *
     * @param enabled true to capture the mouse, false to release it.
     */
    static void SetRelativeMouseMode(bool enabled) { Get()._setRelativeMouseMode(enabled); }

    /**
     * @brief Sets the window title.
     *
     * @param title The new title to set as the window title.
     */
    static void SetTitle(const std::string &title) { Get()._setTitle(title); }

    /**
     * @brief Closes the application window.
     *
     * If called during a frame (e.g. from update logic), the actual cleanup
     * is deferred until EndFrame() completes. This prevents GPU operations
     * from running after the device is destroyed.
     */
    static void Close() { Get()._requestClose(); }

    /**
     * @brief Retrieves the SDL window object.
     *
     * @return Pointer to the SDL window object.
     */
    static SDL_Window *GetWindow() { return Get()._getWindow(); }

    /**
     * @brief Whether the window currently holds keyboard/input focus.
     *
     * False while another window is focused or during an OS modal move/resize of this
     * window. Useful to release relative-mouse capture so the title bar can be dragged.
     */
    static bool HasInputFocus() { return Get()._hasInputFocus(); }

    /**
     * @brief Sets the scale factor of the window.
     *
     * @param scalefactor The scale factor to set.
     */
    static void SetScale(int scalefactor) { Get()._setScale(scalefactor); }

    /**
     * @brief Gets the scale factor of the window.
     *
     * @return The currently used scaling factor.
     */
    static float GetScale() { return Get()._getScale(); }

    /**
     * @brief Sets the size of the window.
     *
     * @param width The width of the window.
     * @param height The height of the window.
     */
    static void SetSize(int width, int height) { Get()._setSize(width, height); }

    /**
     * @brief Sets the scaled size of the window.
     *
     * @param width The width of the window in virtual pixels.
     * @param height The height of the window in virtual pixels.
     * @param scale The scale factor of the window (0 to keep current scale).
     */
    static void SetScaledSize(int width, int height, int scale = 0) { Get()._setScaledSize(width, height, scale); }

    /**
     * @brief Retrieves the size of the window in logical (virtual) pixels.
     *
     * @param getRealSize Flag indicating whether to retrieve the real size (not divided by user scale).
     * @return The size of the window.
     */
    static vf2d GetSize(bool getRealSize = false) { return Get()._getSize(getRealSize); }

    /**
     * @brief Retrieves the width of the window in logical (virtual) pixels.
     *
     * @param getRealSize Flag indicating whether to retrieve the real size (not divided by user scale).
     * @return The width of the window.
     */
    static int GetWidth(bool getRealSize = false) { return (int)Get()._getSize(getRealSize).x; }

    /**
     * @brief Retrieves the height of the window in logical (virtual) pixels.
     *
     * @param getRealSize Flag indicating whether to retrieve the real size (not divided by user scale).
     * @return The height of the window.
     */
    static int GetHeight(bool getRealSize = false) { return (int)Get()._getSize(getRealSize).y; }

    /**
     * @brief Retrieves the size of the window in physical (device) pixels.
     *
     * On HiDPI/Retina displays, this returns the actual framebuffer pixel dimensions
     * which may be 2x or 3x the logical size. Used internally by the renderer.
     *
     * @return The physical pixel size of the window.
     */
    static vf2d GetPhysicalSize() { return Get()._getPhysicalSize(); }

    /**
     * @brief Retrieves the width of the window in physical (device) pixels.
     * @return The physical pixel width of the window.
     */
    static int GetPhysicalWidth() { return (int)Get()._getPhysicalSize().x; }

    /**
     * @brief Retrieves the height of the window in physical (device) pixels.
     * @return The physical pixel height of the window.
     */
    static int GetPhysicalHeight() { return (int)Get()._getPhysicalSize().y; }

    /**
     * @brief Gets the HiDPI display scale factor.
     *
     * Returns the ratio of physical to logical pixels (e.g. 2.0 on Retina displays).
     *
     * @return The display scale factor.
     */
    static float GetDisplayScale() { return EngineState::displayScale; }

    /**
     * @brief Returns the display bounds the window currently lives on.
     *
     * On native (SDL) backends this queries the primary display via SDL.
     * On WebGPU/browser backends SDL display queries are unreliable, so
     * this falls back to the current window/canvas size.
     *
     * @param outW Receives display width in pixels.
     * @param outH Receives display height in pixels.
     */
    static void GetDisplayBounds(uint32_t &outW, uint32_t &outH);

    /**
     * @brief Starts a new frame for rendering.
     */
    static void StartFrame() { Get()._startFrame(); }

    /**
     * @brief Ends the current frame.
     */
    static void EndFrame() { Get()._endFrame(); }

    /**
     * @brief Toggles the fullscreen mode of the window.
     */
    static void ToggleFullscreen() { Get()._toggleFullscreen(); }

    /**
     * @brief Checks if the window is currently in fullscreen mode.
     *
     * @return True if the window is in fullscreen mode, false otherwise.
     */
    static bool IsFullscreen() { return Get()._isFullscreen(); }

    /**
     * @brief Gets the total runtime of the application.
     *
     * @return The total runtime of the application in seconds.
     */
    static double GetRunTime() {
        return (double)std::chrono::duration_cast<std::chrono::milliseconds>(EngineState::currentTime - EngineState::startTime).count() / 1000.;
    }

    /**
     * @brief Checks if the application should quit.
     *
     * @return True if the application should quit, false otherwise.
     */
    static bool ShouldQuit() { return EngineState::shouldQuit; }

    /**
     * @brief Signals that the application should quit.
     */
    static void SignalEndLoop() { EngineState::shouldQuit = true; }

    /**
     * @brief Gets the time taken to render the last frame.
     *
     * @return The time taken to render the last frame in seconds.
     */
    // Real inter-frame delta in seconds. Non-consuming — can be read freely from
    // multiple call sites within a frame. The engine paces SDL_AppIterate to roughly
    // the display refresh rate (see Window::_startFrame), so this stays close to the
    // actual frame interval even on platforms where AppIterate would otherwise spin
    // far faster than vsync.
    static double GetFrameTime() { return EngineState::lastFrameTime; }

    /**
     * @brief Gets the frames per second (FPS) of the application.
     *
     * @param milliseconds The time interval to calculate FPS over.
     * @return The frames per second (FPS) of the application.
     */
    static int GetFPS(float milliseconds = 400.f) { return Get()._getFPS(milliseconds); }

    /**
     * @brief Handles input events.
     */
    static void HandleInput() { Get()._handleInput(); }

    /**
     * @brief Toggles the debug menu visibility.
     */
    static void ToggleDebugMenu() { Get()._toggleDebugMenu(); }

    /**
     * @brief Take a screenshot and save it to a file
     * @param filename Optional filename (default: screenshot_TIMESTAMP.png)
     */
    static void TakeScreenshot(const std::string &filename = "") { Get()._takeScreenshot(filename); }

    /**
     * @brief Sets the WebGPU canvas scaling mode and internal render resolution.
     *
     * On non-WebGPU builds this is a no-op. Call before Window::InitWindow().
     *
     * @param mode     How the internal framebuffer is scaled to the canvas.
     * @param renderWidth  Internal render width  (ignored in Native mode).
     * @param renderHeight Internal render height (ignored in Native mode).
     */
    static void SetWebGpuScaling(WebGpuScaleMode mode, int renderWidth = 1280, int renderHeight = 720) {
        Get()._webGpuScaleMode    = mode;
        Get()._webGpuRenderWidth  = renderWidth;
        Get()._webGpuRenderHeight = renderHeight;
    }

    /// @brief Sets the OS window background color used to fill the strip exposed during a live
    ///        resize (before the swapchain present catches up). Set it to the app's window-edge
    ///        color to hide the black flash when dragging a window larger. No-op on web.
    static void SetResizeFillColor(Color c) { Get()._setResizeFillColor(c); }

    /// @brief Returns the WebGPU canvas scaling mode (no-op on native builds).
    static WebGpuScaleMode GetWebGpuScaleMode() { return Get()._webGpuScaleMode; }
    /// @brief Returns the WebGPU internal render width in pixels (web builds).
    static int GetWebGpuRenderWidth() { return Get()._webGpuRenderWidth; }
    /// @brief Returns the WebGPU internal render height in pixels (web builds).
    static int GetWebGpuRenderHeight() { return Get()._webGpuRenderHeight; }

    /**
     * @brief Check if there's a pending screenshot
     */
    static bool HasPendingScreenshot() { return Get()._pendingScreenshot; }

    /**
     * @brief Get pending screenshot filename and clear the flag
     */
    static std::string GetAndClearPendingScreenshot() {
        std::string filename     = Get()._pendingScreenshotFilename;
        Get()._pendingScreenshot = false;
        Get()._pendingScreenshotFilename.clear();
        return filename;
    }

    /**
     * @brief Sets a callback function for text input events.
     *
     * @param callback Function to call when text input is received.
     */
    static void SetTextInputCallback(std::function<void(const char *)> callback) {
        Get()._textInputCallback = std::move(callback);
    }

#ifdef LUMINOVEAU_USE_CALLBACKS
    /**
     * @brief Processes a single SDL event (SDL3 callback mode only).
     *
     * This method is only available when using SDL3's callback-based main loop.
     * It handles window events, input events, and integrates with the Input system.
     *
     * @param event Pointer to the SDL event to process.
     */
    static void ProcessEvent(SDL_Event *event) { Get()._processEvent(event); }
#endif

    // ─────────────────────────────────────────────────────────────────────────
    // Multi-window (native desktop). One GPU device drives several OS windows.
    // The primary is the window InitWindow created; secondaries come from Create()
    // and are rendered by RenderAll(). The no-arg size/window getters above follow
    // whichever window is *active* (the one currently being rendered inside RenderAll,
    // else the primary), so per-window draw code "just works" with GetWidth()/Draw::.
    // ─────────────────────────────────────────────────────────────────────────
    using WindowHandle                          = uint32_t; // SDL_WindowID; 0 == invalid
    static constexpr WindowHandle InvalidWindow = 0;

    struct WindowDesc {
        std::string  title;
        int          width = 320, height = 240;
        int          x = -1, y = -1; // -1 => OS-chosen / centered
        bool         borderless  = false;
        bool         resizable   = false;
        bool         alwaysOnTop  = false;
        WindowHandle parent       = InvalidWindow; // owner window (stacking/focus)
    };

    static WindowHandle              Main() { return Get()._mainId; }
    static WindowHandle              Create(const WindowDesc &desc) { return Get()._createWindow(desc); }
    static void                      Destroy(WindowHandle w) { Get()._destroyWindow(w); }
    static bool                      Exists(WindowHandle w) { return Get()._entry(w) != nullptr; }
    static std::vector<WindowHandle> All() { return Get()._allWindows(); }

    // Engine iterates every live window: for each -> activate, call renderFn(handle),
    // present. Call once per tick in AppIterate INSTEAD of StartFrame/EndFrame.
    static void RenderAll(const std::function<void(WindowHandle)> &renderFn) {
        Get()._renderAll(renderFn);
    }

    // Handle-scoped queries (the no-arg versions act on the active/primary window).
    static vf2d        GetSize(WindowHandle w, bool real = false) { return Get()._getSizeOf(w, real); }
    static int         GetWidth(WindowHandle w) { return (int)Get()._getSizeOf(w, false).x; }
    static int         GetHeight(WindowHandle w) { return (int)Get()._getSizeOf(w, false).y; }
    static void        SetTitle(WindowHandle w, const std::string &t) { Get()._setTitleOf(w, t); }
    static void        Focus(WindowHandle w) { Get()._focusWindow(w); }
    static void        Minimize(WindowHandle w) { Get()._minimize(w); }
    static bool        HasInputFocus(WindowHandle w) { return Get()._hasInputFocusOf(w); }
    static SDL_Window *GetWindow(WindowHandle w) { return Get()._sdlOf(w); }
    static void        StartTextInput(WindowHandle w) { Get()._startTextInput(w); }

    // Per-window input. Mouse position is client-relative and valid even when w isn't
    // focused (derived from the global cursor), so cross-window hover works.
    static vf2d         GetLocalMousePosition(WindowHandle w) { return Get()._localMouse(w); }
    static bool         ContainsMouse(WindowHandle w) { return Get()._containsMouse(w); }
    static WindowHandle HoveredWindow() { return Get()._hoveredWindow(); }
    static WindowHandle FocusedWindow() { return Get()._focusedWindow(); }

    // Marks a client-area rect (title bar) OS-draggable via SDL hit-testing (w/h<=0 clears).
    static void SetDragRegion(WindowHandle w, float x, float y, float width, float height) {
        Get()._setDragRegion(w, x, y, width, height);
    }

private:
    double _getRunTime();

    void _initWindow(const std::string &title, int width, int height, int scale = 0, unsigned int flags = 0);

    void _setIcon(const std::string &filename);

    void _setCursor(const std::string &filename);

    void _setRelativeMouseMode(bool enabled);

    void _setTitle(const std::string &title);

    void _requestClose();
    void _close();

    void _toggleFullscreen();

    bool _isFullscreen();

    vf2d _getSize(bool getRealSize = false);

    vf2d _getPhysicalSize();

    void _updateDisplayScale();

    void _handleInput();

    int _getFPS(float milliseconds);

    SDL_Window *_getWindow();
    bool        _hasInputFocus();

    void _setScale(int scalefactor);

    float _getScale();

    void _setSize(int width, int height);

    void _setResizeFillColor(Color c);

    void _setScaledSize(int width, int height, int scale = 0);

    void _startFrame();

    void _endFrame();

    void _toggleDebugMenu();

    void _takeScreenshot(const std::string &filename);

    void _processEvent(SDL_Event *event);

    // ── multi-window internals ────────────────────────────────────────────────
    struct WindowEntry {
        WindowHandle id  = 0;
        SDL_Window  *sdl = nullptr;
        // Client-relative title-bar rect marked OS-draggable (0 size = none).
        float dragX = 0, dragY = 0, dragW = 0, dragH = 0;
        bool  resizable = false; // borderless resize via edge hit-test zones
    };

    WindowEntry              *_entry(WindowHandle w);
    WindowEntry              *_entryBySdl(SDL_Window *s);
    SDL_Window               *_sdlOf(WindowHandle w);
    std::vector<WindowHandle> _allWindows();
    WindowHandle              _createWindow(const WindowDesc &desc);
    void                      _destroyWindow(WindowHandle w);
    void                      _renderAll(const std::function<void(WindowHandle)> &fn);
    void                      _activateWindow(const WindowEntry &e);
    vf2d                      _getSizeOf(WindowHandle w, bool real);
    void                      _setTitleOf(WindowHandle w, const std::string &t);
    void                      _focusWindow(WindowHandle w);
    void                      _minimize(WindowHandle w);
    bool                      _hasInputFocusOf(WindowHandle w);
    void                      _startTextInput(WindowHandle w);
    vf2d                      _localMouse(WindowHandle w);
    bool                      _containsMouse(WindowHandle w);
    WindowHandle              _hoveredWindow();
    WindowHandle              _focusedWindow();
    void                      _setDragRegion(WindowHandle w, float x, float y, float ww, float hh);

    static SDL_HitTestResult SDLCALL _hitTest(SDL_Window *win, const SDL_Point *area, void *data);

    std::vector<WindowEntry> _registry;
    SDL_Window              *_mainWindow = nullptr;
    WindowHandle             _mainId     = 0;

    SDL_Window *_window = nullptr;

    WebGpuScaleMode _webGpuScaleMode    = WebGpuScaleMode::Native;
    int             _webGpuRenderWidth  = 1280;
    int             _webGpuRenderHeight = 720;

    int  _lastWindowWidth  = 0;
    int  _lastWindowHeight = 0;
    bool _maximized        = false;

    bool        _inFrame           = false;
    bool        _pendingClose      = false;

    // Live-resize redraw. While a window is dragged/resized the OS runs its own modal loop; SDL keeps
    // AppIterate alive through it via a sparse WM_TIMER, so frames still fire but too rarely to keep up
    // with the growing window — the just-exposed edge shows black between ticks. An SDL event watch
    // fires synchronously inside that loop on every WINDOW_RESIZED, so we repaint the affected window
    // right there and present at the new size before the OS shows the exposed edge.
    std::function<void(WindowHandle)> _renderFn; // most recent RenderAll callback
    static bool SDLCALL               _resizeWatch(void *userdata, SDL_Event *event);
    void                              _renderWindowNow(WindowHandle id);

    bool        _pendingScreenshot = false;
    std::string _pendingScreenshotFilename;

    std::function<void(const char *)> _textInputCallback = nullptr;

    bool _sizeDirty = false;

    // Buffer keyboard events so they're applied AFTER
    // Input::Update() snapshots previousKeyboardState
    std::vector<Uint8> _bufferedKeysDown;
    std::vector<Uint8> _bufferedKeysUp;

public:
    /// @cond INTERNAL
    Window(const Window &) = delete;

    static Window &Get() {
        static Window instance;
        return instance;
    }
    /// @endcond

private:
    Window() {
        EngineState::startTime = std::chrono::high_resolution_clock::now();
    }
};