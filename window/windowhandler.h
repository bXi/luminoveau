#pragma once

#include <memory>
#include <string>
#include <optional>
#include <chrono>

#include "SDL3/SDL.h"

#include "enginestate/enginestate.h"

#include "assettypes/shader.h"
#include "assettypes/texture.h"

#include "input/inputhandler.h"
#include "eventbus/eventbushandler.h"

#include "utils/colors.h"
#include "utils/lerp.h"
#include "utils/vectors.h"
#include "utils/uniformobject.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef ADD_IMGUI
#include "imgui.h"
#include "backends/imgui_impl_sdlgpu3.h"
#include "backends/imgui_impl_sdl3.h"

#ifdef WIN32
#include "backends/imgui_impl_win32.h"
#endif
#endif

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
        get()._initWindow(title, width, height, scale, flags);
    }

    /**
     * @brief Sets the window icon.
     *
     * @param icon The new icon to set as window icon.
     */
    static void SetIcon(Texture icon) { get()._setIcon(icon); }

    static void SetTitle(const std::string &title) { get()._setTitle(title); }

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
    static int GetWidth(bool getRealSize = false) { return (int) get()._getSize(getRealSize).x; }

    /**
     * @brief Retrieves the height of the window.
     *
     * @param getRealSize Flag indicating whether to retrieve the real size (not scaled).
     * @return The height of the window.
     */
    static int GetHeight(bool getRealSize = false) { return (int) get()._getSize(getRealSize).y; }

    /**
     * @brief Starts a new frame for rendering.
     */
    static void StartFrame() { get()._startFrame(); }

    /**
     * @brief Ends the current frame.
     */
    static void EndFrame() { get()._endFrame(); }

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
        return (double) std::chrono::duration_cast<std::chrono::milliseconds>(EngineState::_currentTime - EngineState::_startTime).count() / 1000.;
    }

    /**
     * @brief Checks if the application should quit.
     *
     * @return True if the application should quit, false otherwise.
     */
    static bool ShouldQuit() { return EngineState::_shouldQuit; }

    /**
     * @brief Gets the time taken to render the last frame.
     *
     * @return The time taken to render the last frame in seconds.
     */
    static double GetFrameTime() { return EngineState::_lastFrameTime; }

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

private:
    double _getRunTime();

    void _initWindow(const std::string &title, int width, int height, int scale = 0, unsigned int flags = 0);

    void _setIcon(Texture icon);

    void _setTitle(const std::string &title);

    void _close();

    void _toggleFullscreen();

    bool _isFullscreen();

    vf2d _getSize(bool getRealSize = false);

    void _handleInput();

    int _getFPS(float milliseconds);

    SDL_Window *_getWindow();

    void _setScale(int scalefactor);

    float _getScale();

    void _setSize(int width, int height);

    void _setScaledSize(int width, int height, int scale = 0);

    void _startFrame() const;

    void _endFrame();

    void _toggleDebugMenu();

    SDL_Window *m_window = nullptr;

    int  _lastWindowWidth  = 0;
    int  _lastWindowHeight = 0;
    bool _maximized        = false;

    bool _sizeDirty = false;
#ifdef ADD_IMGUI
    void SetupImGuiStyle();
#endif


public:
    Window(const Window &) = delete;

    static Window &get() {
        static Window instance;
        return instance;
    }

private:
    Window() {
        EngineState::_startTime = std::chrono::high_resolution_clock::now();
    };
};