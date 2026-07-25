#include "input.h"

#include "platform/window/window.h"
#include "platform/input/mouseinput_backend.h"
#include "renderer/renderer.h"

void Input::_init() {
    if (_didInit)
        return;

    _didInit = true;

    _inputs.push_back(new InputDevice(InputType::MOUSE_KB));

    // Escape hatch: SDL_Init(SDL_INIT_GAMEPAD) can hang indefinitely on some macOS
    // setups (HID/GameController enumeration stall). Set LUMI_NO_GAMEPAD=1 to skip
    // the gamepad subsystem entirely; keyboard + mouse still work.
    if (getenv("LUMI_NO_GAMEPAD"))
        return;

    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ROG_CHAKRAM, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);

    auto gamePadCount = 0;
    _joystickIds      = SDL_GetGamepads(&gamePadCount);

    for (int i = 0; i < gamePadCount; i++) {
        GamepadInfo temp;
        temp.joystickId = _joystickIds[i];
        temp.previousButtonState.resize(SDL_GAMEPAD_BUTTON_COUNT);
        temp.currentButtonState.resize(SDL_GAMEPAD_BUTTON_COUNT);
        temp.gamepad = SDL_OpenGamepad(_joystickIds[i]);
        _gamepads.push_back(temp);
        _inputs.push_back(new InputDevice(InputType::GAMEPAD, i));
    }
}

InputDevice *Input::_getController(int index) {
    return _inputs[index];
}

void Input::_clear() {
    for (auto *device : _inputs)
        delete device;
    _inputs.clear();
}

void Input::_update() {
    UpdateTimings();

    _scrolledUpTicks   = 0;
    _scrolledDownTicks = 0;

    _previousKeyboardState = currentKeyboardState;

    _previousMouseButtons = _currentMouseButtons;
    // Via the backend so the web path can report touch as the left button (pointer events);
    // the SDL backend just forwards SDL_GetMouseState, so native is unchanged.
    _currentMouseButtons = PlatformInputBackend::GetMouseButtons();

    for (auto &gamepad : _gamepads) {
        gamepad.previousButtonState = gamepad.currentButtonState;

        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
            gamepad.currentButtonState[i] = SDL_GetGamepadButton(gamepad.gamepad, static_cast<SDL_GamepadButton>(i));
        }
    }

    // Update virtual controls
    _virtualControls.Update();
}

void Input::_updateTimings() {
    for (const auto &i : _inputs) {
        i->UpdateTimings();
    }
}

float Input::_getGamepadAxisMovement(int gamepadID, SDL_GamepadAxis axis) {
    Sint16 x = SDL_GetGamepadAxis(_gamepads[gamepadID].gamepad, axis);

    if (std::abs(x) < DEADZONE) {
        // Value is within the deadzone, ignore it
        x = 0;
    }

    return ((float)x) / 32768.0f;
}

bool Input::_gamepadButtonPressed(int gamepadID, int button) {

    auto gamepadinfo = _gamepads[gamepadID];
    return gamepadinfo.currentButtonState[button] && !gamepadinfo.previousButtonState[button];
}

bool Input::_gamepadButtonDown(int gamepadID, int button) {
    return SDL_GetGamepadButton(_gamepads[gamepadID].gamepad, static_cast<SDL_GamepadButton>(button));
}

bool Input::_keyPressed(int key) {

    auto curState  = currentKeyboardState;
    auto prevState = _previousKeyboardState;

    auto scancode = SDL_GetScancodeFromKey(key, nullptr);

    return curState[scancode] == 1 && prevState[scancode] == 0;
}

bool Input::_keyReleased(int key) {

    auto curState  = currentKeyboardState;
    auto prevState = _previousKeyboardState;

    auto scancode = SDL_GetScancodeFromKey(key, nullptr);

    return curState[scancode] == 0 && prevState[scancode] == 1;
}

bool Input::_keyDown(int key) {

    auto scancode = SDL_GetScancodeFromKey(key, nullptr);

    return currentKeyboardState[scancode] == 1;
}

vf2d Input::_getMousePosition() {
    vf2d p = PlatformInputBackend::GetMousePosition();
    // The scene renders at canvas/scaleFactor and is upscaled to present (Window::SetScale), so the
    // cursor — reported in canvas pixels — has to be divided by the scale to land in render space.
    float s = Window::GetScale();
    if (s > 1.0f) {
        p.x /= s;
        p.y /= s;
    }
    return p;
}

bool Input::_mouseButtonPressed(int button) {
    auto buttonmask = (1 << ((button)-1));
    return (_currentMouseButtons & buttonmask) != 0 && (_previousMouseButtons & buttonmask) == 0;
}

bool Input::_mouseButtonReleased(int button) {
    auto buttonmask = (1 << ((button)-1));
    return (_currentMouseButtons & buttonmask) == 0 && (_previousMouseButtons & buttonmask) != 0;
}

bool Input::_mouseButtonDown(int button) {
    auto buttonmask = (1 << ((button)-1));
    return (_currentMouseButtons & buttonmask) != 0;
}

void Input::_updateInputs(const std::vector<Uint8> &keys, bool held) {
    if (held) {
        for (auto scancode : keys) {
            currentKeyboardState[scancode] = 1;
        }
    } else {
        for (auto scancode : keys) {
            currentKeyboardState[scancode] = 0;
        }
    }
}

void Input::_addGamepadDevice(SDL_JoystickID joystickID) {
    for (const auto &gamepad : _gamepads) {
        if (gamepad.joystickId == joystickID)
            return;
    }

    int         newgamepadID = _gamepads.size();
    GamepadInfo temp;
    temp.joystickId = joystickID;
    temp.previousButtonState.resize(SDL_GAMEPAD_BUTTON_COUNT);
    temp.currentButtonState.resize(SDL_GAMEPAD_BUTTON_COUNT);
    temp.gamepad = SDL_OpenGamepad(joystickID);
    _gamepads.push_back(temp);
    _inputs.push_back(new InputDevice(InputType::GAMEPAD, newgamepadID));
}

void Input::_removeGamepadDevice(SDL_JoystickID joystickID) {
    // Locate the gamepad. gamepadID on each InputDevice is a direct index into `gamepads`,
    // so removing one entry shifts every later index — we must compact both vectors in lockstep
    // and fix up the surviving devices, or stale indices read the wrong (or an out-of-bounds)
    // gamepad later.
    auto it = std::find_if(_gamepads.begin(), _gamepads.end(),
        [joystickID](const auto &gamepad) {
            return gamepad.joystickId == joystickID;
        });
    if (it == _gamepads.end())
        return;

    int removedIdx = static_cast<int>(std::distance(_gamepads.begin(), it));

    if (it->gamepad)
        SDL_CloseGamepad(it->gamepad);
    _gamepads.erase(it);

    // Drop the matching InputDevice (its gamepadID == removedIdx) and shift down every
    // device that indexed a gamepad after the removed one.
    for (auto deviceIt = _inputs.begin(); deviceIt != _inputs.end();) {
        InputDevice *device = *deviceIt;
        if (device->GetType() != InputType::GAMEPAD) {
            ++deviceIt;
            continue;
        }

        int id = device->GetGamepadID();
        if (id == removedIdx) {
            delete device;
            deviceIt = _inputs.erase(deviceIt);
        } else {
            if (id > removedIdx)
                device->SetGamepadID(id - 1);
            ++deviceIt;
        }
    }
}

void Input::_updateScroll(int scrollDir) {
    if (scrollDir < 0) {
        _scrolledDownTicks++;
    }
    if (scrollDir > 0) {
        _scrolledUpTicks++;
    }
}

void Input::_handleTouchEvent(const SDL_Event *event) {
    _virtualControls.HandleTouchEvent(event);
}
