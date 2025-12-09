#pragma once

#include <vector>
#include <list>
#include <map>
#include <unordered_map>
#include <algorithm>

#include "SDL3/SDL.h"

#include "utils/vectors.h"
#include "utils/helpers.h"

#include "inputconstants.h"
#include "inputdevice.h"

static const int DEADZONE = 8000;

/**
 * @brief Provides functionality for handling user input.
 */
class Input {
public:
    /**
     * @brief Initializes the input system.
     */
    static void Init() { get()._init(); }

    /**
     * @brief Retrieves the input device for the specified controller index.
     *
     * @param index The index of the controller.
     * @return A pointer to the input device.
     */
    static InputDevice *GetController(int index) { return get()._getController(index); }

    /**
     * @brief Clears the input system.
     */
    static void Clear() { get()._clear(); }

    /**
     * @brief Retrieves all input devices.
     *
     * @return A vector containing pointers to all input devices.
     */
    static std::vector<InputDevice *> GetAllInputs() { return get().inputs; }

    /**
     * @brief Updates the input system.
     */
    static void Update() { get()._update(); }

    /**
     * @brief Updates the timings of all input devices.
     */
    static void UpdateTimings() { get()._updateTimings(); }

    /**
     * @brief Retrieves the movement value of the specified gamepad axis.
     *
     * @param gamepadID The ID of the gamepad.
     * @param axis The axis to retrieve the movement value for.
     * @return The movement value of the axis.
     */
    static float GetGamepadAxisMovement(int gamepadID, SDL_GamepadAxis axis) { return get()._getGamepadAxisMovement(gamepadID, axis); }

    /**
     * @brief Checks if the specified button on the gamepad is pressed.
     *
     * @param gamepadID The ID of the gamepad.
     * @param button The button to check.
     * @return True if the button is pressed, false otherwise.
     */
    static bool GamepadButtonPressed(int gamepadID, int button) { return get()._gamepadButtonPressed(gamepadID, button); }

    /**
     * @brief Checks if the specified button on the gamepad is down.
     *
     * @param gamepadID The ID of the gamepad.
     * @param button The button to check.
     * @return True if the button is down, false otherwise.
     */
    static bool GamepadButtonDown(int gamepadID, int button) { return get()._gamepadButtonDown(gamepadID, button); }

    /**
     * @brief Checks if the specified key is pressed.
     *
     * @param key The key to check.
     * @return True if the key is pressed, false otherwise.
     */
    static bool KeyPressed(int key) { return get()._keyPressed(key); }

    /**
     * @brief Checks if the specified key is released.
     *
     * @param key The key to check.
     * @return True if the key is released, false otherwise.
     */
    static bool KeyReleased(int key) { return get()._keyReleased(key); }

    /**
     * @brief Checks if the specified key is down.
     *
     * @param key The key to check.
     * @return True if the key is down, false otherwise.
     */
    static bool KeyDown(int key) { return get()._keyDown(key); }

    /**
     * @brief Retrieves the current mouse position.
     *
     * @return The current mouse position.
     */
    static vf2d GetMousePosition() { return get()._getMousePosition(); }

    /**
     * @brief Checks if the specified mouse button is pressed.
     *
     * @param button The mouse button to check.
     * @return True if the mouse button is pressed, false otherwise.
     */
    static bool MouseButtonPressed(int button) { return get()._mouseButtonPressed(button); }

    /**
     * @brief Checks if the specified mouse button is released.
     *
     * @param button The mouse button to check.
     * @return True if the mouse button is released, false otherwise.
     */
    static bool MouseButtonReleased(int button) { return get()._mouseButtonReleased(button); }

    /**
     * @brief Checks if the specified mouse button is down.
     *
     * @param button The mouse button to check.
     * @return True if the mouse button is down, false otherwise.
     */
    static bool MouseButtonDown(int button) { return get()._mouseButtonDown(button); };

    static Uint32 MouseScrolledUp() { return get()._mouseScrolledUp(); };

    static Uint32 MouseScrolledDown() { return get()._mouseScrolledDown(); };

    //For internal use. handle with care
    static void UpdateInputs(std::vector<Uint8> keys, bool held) { get()._updateInputs(keys, held); }

    static void AddGamepadDevice(SDL_JoystickID joystickID) { get()._addGamepadDevice(joystickID); }

    static void RemoveGamepadDevice(SDL_JoystickID joystickID) { get()._removeGamepadDevice(joystickID); }

    static void UpdateScroll(int scrollDir) { get()._updateScroll(scrollDir); }

    std::vector<Uint8> currentKeyboardState;
private:
    std::vector<InputDevice *> inputs;

    void _init();

    InputDevice *_getController(int index);

    void _clear();

    void _update();

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

    Uint32 _mouseScrolledUp() { return get().scrolledUpTicks; };

    Uint32 _mouseScrolledDown() { return get().scrolledDownTicks; };

    void _updateInputs(const std::vector<Uint8> &keys, bool held);

    void _addGamepadDevice(SDL_JoystickID joystickID);

    void _removeGamepadDevice(SDL_JoystickID joystickID);

    void _updateScroll(int scrollDir);

    std::vector<Uint8> previousKeyboardState;

    Uint32 currentMouseButtons = 0;
    Uint32 previousMouseButtons = 0;

    Uint32 scrolledUpTicks = 0;
    Uint32 scrolledDownTicks = 0;

    const SDL_JoystickID *joystickIds = nullptr;

    struct gamepadInfo {
        SDL_JoystickID joystickId;
        SDL_Gamepad *gamepad;
        std::vector<bool> currentButtonState;
        std::vector<bool> previousButtonState;
    };

    std::vector<gamepadInfo> gamepads;
public:
    Input(const Input &) = delete;

    static Input &get() {
        static Input instance;
        return instance;
    }

private:
    Input() {
        SDL_PumpEvents();
        currentKeyboardState.resize(SDL_SCANCODE_COUNT);
        previousKeyboardState.resize(SDL_SCANCODE_COUNT);
    }
};