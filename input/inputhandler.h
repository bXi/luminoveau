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

class Input {
public:

    static void Init() { get()._init(); }

    static InputDevice *GetController(int index) { return get()._getController(index); }

    static void Clear() { get()._clear(); }

    static std::vector<InputDevice *> GetAllInputs() { return get().inputs; }

    static void Update() { get()._update(); }

    static void UpdateTimings() { get()._updateTimings(); }

    static float GetGamepadAxisMovement(int gamepadID, SDL_GamepadAxis axis) { return get()._getGamepadAxisMovement(gamepadID, axis); }

    static bool GamepadButtonPressed(int gamepadID, int button) { return get()._gamepadButtonPressed(gamepadID, button); }

    static bool GamepadButtonDown(int gamepadID, int button) { return get()._gamepadButtonPressed(gamepadID, button); }

    static bool KeyPressed(int key) { return get()._keyPressed(key); }

    static bool KeyReleased(int key) { return get()._keyReleased(key); }

    static bool KeyDown(int key) { return get()._keyDown(key); }

    static vf2d GetMousePosition() { return get()._getMousePosition(); }

    static bool MouseButtonPressed(int button) { return get()._mouseButtonPressed(button); }

    static bool MouseButtonReleased(int button) { return get()._mouseButtonReleased(button); }

    static bool MouseButtonDown(int button) { return get()._mouseButtonDown(button); }

    //For internal use. handle with care
    static void UpdateInputs(std::vector<Uint8> keys, bool held) { get()._updateInputs(keys, held); }

    static void AddGamepadDevice(SDL_JoystickID joystickID) { get()._addGamepadDevice(joystickID); }

    static void RemoveGamepadDevice(SDL_JoystickID joystickID) { get()._removeGamepadDevice(joystickID); }

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

    void _updateInputs(const std::vector<Uint8> &keys, bool held);

    void _addGamepadDevice(SDL_JoystickID joystickID);

    void _removeGamepadDevice(SDL_JoystickID joystickID);

    std::vector<Uint8> currentKeyboardState;
    std::vector<Uint8> previousKeyboardState;

    Uint32 currentMouseButtons = 0;
    Uint32 previousMouseButtons = 0;


    SDL_JoystickID *joystickIds = nullptr;

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
        currentKeyboardState.resize(SDL_NUM_SCANCODES);
        previousKeyboardState.resize(SDL_NUM_SCANCODES);
    }
};