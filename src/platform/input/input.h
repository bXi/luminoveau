#pragma once

#include <vector>
#include <list>
#include <map>
#include <unordered_map>
#include <algorithm>

#include "SDL3/SDL.h"

#include "math/vectors.h"
#include "util/helpers.h"

#include "platform/input/inputconstants.h"
#include "platform/input/inputdevice.h"
#include "platform/input/virtualcontrols.h"

static const int DEADZONE = 8000;

/**
 * @brief Provides functionality for handling user input.
 */
class Input {
public:
    /**
     * @brief Initializes the input system.
     */
    static void Init() { Get()._init(); }

    /**
     * @brief Retrieves the input device for the specified controller index.
     *
     * @param index The index of the controller.
     * @return A pointer to the input device.
     */
    static InputDevice *GetController(int index) { return Get()._getController(index); }

    /**
     * @brief Clears the input system.
     */
    static void Clear() { Get()._clear(); }

    /**
     * @brief Retrieves all input devices.
     *
     * @return A vector containing pointers to all input devices.
     */
    static std::vector<InputDevice *> GetAllInputs() { return Get()._inputs; }

    /**
     * @brief Updates the input system.
     */
    static void Update() { Get()._update(); }

    /**
     * @brief Updates the timings of all input devices.
     */
    static void UpdateTimings() { Get()._updateTimings(); }

    /**
     * @brief Retrieves the movement value of the specified gamepad axis.
     *
     * @param gamepadID The ID of the gamepad.
     * @param axis The axis to retrieve the movement value for.
     * @return The movement value of the axis.
     */
    static float GetGamepadAxisMovement(int gamepadID, SDL_GamepadAxis axis) { return Get()._getGamepadAxisMovement(gamepadID, axis); }

    /**
     * @brief Checks if the specified button on the gamepad is pressed.
     *
     * @param gamepadID The ID of the gamepad.
     * @param button The button to check.
     * @return True if the button is pressed, false otherwise.
     */
    static bool GamepadButtonPressed(int gamepadID, int button) { return Get()._gamepadButtonPressed(gamepadID, button); }

    /**
     * @brief Checks if the specified button on the gamepad is down.
     *
     * @param gamepadID The ID of the gamepad.
     * @param button The button to check.
     * @return True if the button is down, false otherwise.
     */
    static bool GamepadButtonDown(int gamepadID, int button) { return Get()._gamepadButtonDown(gamepadID, button); }

    /**
     * @brief Checks if the specified key is pressed.
     *
     * @param key The key to check.
     * @return True if the key is pressed, false otherwise.
     */
    static bool KeyPressed(int key) { return Get()._keyPressed(key); }

    /**
     * @brief Checks if the specified key is released.
     *
     * @param key The key to check.
     * @return True if the key is released, false otherwise.
     */
    static bool KeyReleased(int key) { return Get()._keyReleased(key); }

    /**
     * @brief Checks if the specified key is down.
     *
     * @param key The key to check.
     * @return True if the key is down, false otherwise.
     */
    static bool KeyDown(int key) { return Get()._keyDown(key); }

    /**
     * @brief Retrieves the current mouse position.
     *
     * @return The current mouse position.
     */
    static vf2d GetMousePosition() { return Get()._getMousePosition(); }

    /**
     * @brief Checks if the specified mouse button is pressed.
     *
     * @param button The mouse button to check.
     * @return True if the mouse button is pressed, false otherwise.
     */
    static bool MouseButtonPressed(int button) { return Get()._mouseButtonPressed(button); }

    /**
     * @brief Checks if the specified mouse button is released.
     *
     * @param button The mouse button to check.
     * @return True if the mouse button is released, false otherwise.
     */
    static bool MouseButtonReleased(int button) { return Get()._mouseButtonReleased(button); }

    /**
     * @brief Checks if the specified mouse button is down.
     *
     * @param button The mouse button to check.
     * @return True if the mouse button is down, false otherwise.
     */
    static bool MouseButtonDown(int button) { return Get()._mouseButtonDown(button); };

    /// @brief Returns the mouse wheel scroll-up amount this frame (0 if none).
    static Uint32 MouseScrolledUp() { return Get()._mouseScrolledUp(); };

    /// @brief Returns the mouse wheel scroll-down amount this frame (0 if none).
    static Uint32 MouseScrolledDown() { return Get()._mouseScrolledDown(); };

    // === Virtual Controls (Onscreen Joystick/Buttons) ===
    /**
     * @brief Get the virtual controls instance
     */
    static VirtualControls &GetVirtualControls() { return Get()._virtualControls; }

    /**
     * @brief Handle touch events for virtual controls
     */
    static void HandleTouchEvent(const SDL_Event *event) { Get()._handleTouchEvent(event); }

    /// @cond INTERNAL
    // For internal use. handle with care
    static void UpdateInputs(std::vector<Uint8> keys, bool held) { Get()._updateInputs(keys, held); }

    static void AddGamepadDevice(SDL_JoystickID joystickID) { Get()._addGamepadDevice(joystickID); }

    static void RemoveGamepadDevice(SDL_JoystickID joystickID) { Get()._removeGamepadDevice(joystickID); }

    static void UpdateScroll(int scrollDir) { Get()._updateScroll(scrollDir); }
    /// @endcond

    /**
     * @brief Accumulates relative mouse motion (from SDL mouse-motion events).
     * Used for FPS-style mouse look in relative mouse mode.
     */
    static void AccumulateMouseDelta(float dx, float dy) { Get()._accumulateMouseDelta(dx, dy); }

    /**
     * @brief Returns relative mouse motion accumulated since the last frame.
     */
    static vf2d GetMouseDelta() { return Get()._getMouseDelta(); }

    /// @cond INTERNAL
    std::vector<Uint8> currentKeyboardState;
    /// @endcond
private:
    std::vector<InputDevice *> _inputs;

    void _init();

    InputDevice *_getController(int index);

    void _clear();

    void _update();

    void _accumulateMouseDelta(float dx, float dy) {
        _mouseDelta.x += dx;
        _mouseDelta.y += dy;
    }
    vf2d _getMouseDelta() {
        vf2d d      = _mouseDelta;
        _mouseDelta = { 0.0f, 0.0f };
        return d;
    } // read-and-clear
    vf2d _mouseDelta { 0.0f, 0.0f };

    void _updateTimings();

    float _getGamepadAxisMovement(int gamepadID, SDL_GamepadAxis axis);

    bool _gamepadButtonPressed(int gamepadID, int button);

    bool _gamepadButtonDown(int gamepadID, int button);

    bool _keyPressed(int key);

    bool _keyReleased(int key);

    bool _keyDown(int key);

    vf2d _getMousePosition();

    bool _mouseButtonPressed(int button);

    bool _mouseButtonReleased(int button);

    bool _mouseButtonDown(int button);

    Uint32 _mouseScrolledUp() { return Get()._scrolledUpTicks; };

    Uint32 _mouseScrolledDown() { return Get()._scrolledDownTicks; };

    void _updateInputs(const std::vector<Uint8> &keys, bool held);

    void _addGamepadDevice(SDL_JoystickID joystickID);

    void _removeGamepadDevice(SDL_JoystickID joystickID);

    void _updateScroll(int scrollDir);

    std::vector<Uint8> _previousKeyboardState;

    Uint32 _currentMouseButtons  = 0;
    Uint32 _previousMouseButtons = 0;

    Uint32 _scrolledUpTicks   = 0;
    Uint32 _scrolledDownTicks = 0;

    const SDL_JoystickID *_joystickIds = nullptr;

    /// @cond INTERNAL
    struct GamepadInfo {
        SDL_JoystickID    joystickId;
        SDL_Gamepad      *gamepad;
        std::vector<bool> currentButtonState;
        std::vector<bool> previousButtonState;
    };
    /// @endcond

    std::vector<GamepadInfo> _gamepads;

    VirtualControls _virtualControls;

    bool _didInit = false;
    void _handleTouchEvent(const SDL_Event *event);

public:
    /// @cond INTERNAL
    Input(const Input &) = delete;

    static Input &Get() {
        static Input instance;
        return instance;
    }
    /// @endcond

private:
    Input() {
        SDL_PumpEvents();
        currentKeyboardState.resize(SDL_SCANCODE_COUNT);
        _previousKeyboardState.resize(SDL_SCANCODE_COUNT);
    }
};