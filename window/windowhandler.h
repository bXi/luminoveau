#pragma once

#include <memory>
#include <string>
#include <optional>

#include "SDL3/SDL.h"

#include "utils/vectors.h"
#include "utils/lerp.h"
#include "input/inputhandler.h"
#include "eventbus/eventbushandler.h"

#include <chrono>
#include "utils/colors.h"
#include "assettypes/texture.h"

#include "renderpass.h"
#include "renderable.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef ADD_IMGUI
#include "imgui.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_impl_sdl3.h"

#ifdef WIN32
#include "imgui_impl_win32.h"
#endif
#endif

// SDL Forward Declarations
struct SDL_Window;
union SDL_Event;

class RenderPass;

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
        get()._initWindow(title, width, height, scale, flags);
    }

    /**
     * @brief Sets the window icon.
     *
     * @param icon The new icon to set as window icon.
     */
    static void SetIcon(Texture icon) { get()._setIcon(icon); }

    /**
     * @brief Closes the application window.
     */
    static void Close() { get()._close(); }

    /**
     * @brief Retrieves the SDL window object.
     *
     * @return Pointer to the SDL window object.
     */
    static SDL_Window *GetWindow() { return get()._getWindow(); }

    /**
     * @brief Retrieves the SDL renderer object.
     *
     * @return Pointer to the SDL renderer object.
     */
    static SDL_Renderer *GetRenderer() { return get()._getRenderer(); };

    /**
     * @brief Retrieves the SDL device object.
     *
     * @return Pointer to the SDL device object.
     */
    static SDL_GPUDevice *GetDevice() { return get()._getDevice(); };

    static SDL_GPUTransferBuffer *GetTransferBuffer() { return get()._getTransferBuffer(); };

    /**
     * @brief Sets the scale factor of the window.
     *
     * @param scalefactor The scale factor to set.
     */
    static void SetScale(int scalefactor) { get()._setScale(scalefactor); }

    /**
     * @brief Gets the scale factor of the window.
     *
     * @return The currently used scaling factor.
     */

    static float GetScale() { return get()._getScale(); }

    /**
     * @brief Sets the size of the window.
     *
     * @param width The width of the window.
     * @param height The height of the window.
     */
    static void SetSize(int width, int height) { get()._setSize(width, height); }

    /**
     * @brief Sets the scaled size of the window.
     *
     * @param width The width of the window.
     * @param height The height of the window.
     * @param scale The scale factor of the window.
     */
    static void SetScaledSize(int width, int height, int scale = 0) { get()._setScaledSize(width, height, scale); };

    /**
     * @brief Retrieves the size of the window.
     *
     * @param getRealSize Flag indicating whether to retrieve the real size (not scaled).
     * @return The size of the window.
     */
    static vf2d GetSize(bool getRealSize = false) { return get()._getSize(getRealSize); }

    /**
     * @brief Retrieves the width of the window.
     *
     * @param getRealSize Flag indicating whether to retrieve the real size (not scaled).
     * @return The width of the window.
     */
    static int GetWidth(bool getRealSize = false) { return get()._getSize(getRealSize).x; }

    /**
     * @brief Retrieves the height of the window.
     *
     * @param getRealSize Flag indicating whether to retrieve the real size (not scaled).
     * @return The height of the window.
     */
    static int GetHeight(bool getRealSize = false) { return get()._getSize(getRealSize).y; }

    /**
     * @brief Starts a new frame for rendering.
     */
    static void StartFrame() { get()._startFrame(); }

    /**
     * @brief Ends the current frame.
     */
    static void EndFrame() { get()._endFrame(); }

    /**
     * @brief Sets the render target to the specified texture.
     *
     * @param target The texture to set as the render target.
     */
    static void SetRenderTarget(Texture target) { get()._setRenderTarget(target); }

    /**
     * @brief Resets the render target to the window.
     */
    static void ResetRenderTarget() { get()._resetRenderTarget(); }

    /**
     * @brief Clears the background of the window with the specified color.
     *
     * @param color The color to clear the background with.
     */
    static void ClearBackground(Color color) { get()._clearBackground(color); }

    /**
     * @brief Toggles the fullscreen mode of the window.
     */
    static void ToggleFullscreen() { get()._toggleFullscreen(); }

    /**
     * @brief Checks if the window is currently in fullscreen mode.
     *
     * @return True if the window is in fullscreen mode, false otherwise.
     */
    static bool IsFullscreen() { return get()._isFullscreen(); }

    /**
     * @brief Gets the total runtime of the application.
     *
     * @return The total runtime of the application in seconds.
     */
    static double GetRunTime() {
        return (double)std::chrono::duration_cast<std::chrono::milliseconds>(get()._currentTime - get()._startTime).count() / 1000.;
    }

    /**
     * @brief Checks if the application should quit.
     *
     * @return True if the application should quit, false otherwise.
     */
    static bool ShouldQuit() { return get()._shouldQuit; }

    /**
     * @brief Gets the time taken to render the last frame.
     *
     * @return The time taken to render the last frame in seconds.
     */
    static double GetFrameTime() { return get()._lastFrameTime; }

    /**
     * @brief Gets the frames per second (FPS) of the application.
     *
     * @param milliseconds The time interval to calculate FPS over.
     * @return The frames per second (FPS) of the application.
     */
    static int GetFPS(float milliseconds = 400.f) { return get()._getFPS(milliseconds); }

    /**
     * @brief Handles input events.
     */
    static void HandleInput() { get()._handleInput(); }

    /**
     * @brief Toggles the debug menu.
     */
    static void ToggleDebugMenu() { get()._toggleDebugMenu(); }

    static void AddToRenderQueue(const std::string &passname, const Renderable &renderable) { get()._addToRenderQueue(passname, renderable); };

    static uint32_t GetZIndex() {
        return get()._zIndex--;
    }

private:
    SDL_Window            *m_window;
    SDL_GPUDevice         *m_device;
    SDL_GPUCommandBuffer  *m_cmdbuf;
    SDL_GPUTransferBuffer *m_transbuf;
    uint32_t              _zIndex = INT_MAX;

    std::unordered_map<std::string, RenderPass *> renderpasses;

    void _addToRenderQueue(const std::string &passname, const Renderable &renderable);

    double _getRunTime();

    void _initWindow(const std::string &title, int width, int height, int scale = 0, unsigned int flags = 0);

    void _setIcon(Texture icon);

    void _close();

    void _toggleFullscreen();

    bool _isFullscreen();

    SDL_Renderer *_getRenderer();

    SDL_GPUDevice *_getDevice();

    SDL_GPUTransferBuffer *_getTransferBuffer();

    vf2d _getSize(bool getRealSize = false);

    void _handleInput();

    int _getFPS(float milliseconds);

    SDL_Window *_getWindow();

    void _setScale(int scalefactor);

    float _getScale();

    void _setSize(int width, int height);

    void _setScaledSize(int width, int height, int scale = 0);

    void _startFrame();

    void _clearBackground(Color color);

    void _endFrame();

    void _setRenderTarget(Texture target);

    void _resetRenderTarget();

    void _toggleDebugMenu();

    TextureAsset _screenBuffer;

    int _lastWindowWidth  = 0;
    int _lastWindowHeight = 0;

    bool _sizeDirty = false;

    int _scaleFactor = 1;

    bool _shouldQuit = false;

    int    _frameCount;
    int    _fps;
    double _lastFrameTime;
    double _fpsAccumulator;

    std::chrono::high_resolution_clock::time_point _startTime;
    std::chrono::high_resolution_clock::time_point _currentTime;
    std::chrono::high_resolution_clock::time_point _previousTime;
#ifdef ADD_IMGUI
    void SetupImGuiStyle();

    bool _debugMenuVisible;
#endif
public:
    Window(const Window &) = delete;

    static Window &get() {
        static Window instance;
        return instance;
    }

private:
    Window() : m_window(nullptr) {
        _fps         = 0;
        _frameCount  = 0;
        _startTime   = std::chrono::high_resolution_clock::now();
        _currentTime = _startTime;

        _fpsAccumulator = 0.f;
#ifdef ADD_IMGUI
        _debugMenuVisible = false;
#endif
    };
};