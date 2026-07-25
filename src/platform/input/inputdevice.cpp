#include "inputdevice.h"

#include "platform/window/window.h"

InputType InputDevice::GetType() {
    return _type;
}

void InputDevice::UpdateTimings() {
    SDL_UpdateGamepads();

    for (auto &timing : _pressedTimings) {
        if (timing.second > 0.0f) {
            timing.second -= Window::GetFrameTime();
        } else {
            timing.second = 0.0f;
        }
    }
}

bool InputDevice::_isButtonPressed(Buttons button) {
    int keysHeld = 0;
    switch (_type) {
    case InputType::GAMEPAD:
        if (button == Buttons::LEFT && _pressedTimings[Buttons::LEFT] == 0.0f && Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_LEFTX) != 0.0f) {
            keysHeld += (Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_LEFTX) < 0.0f) ? 1 : 0;
            _pressedTimings[Buttons::LEFT] = _joystickCooldown;
        }
        if (button == Buttons::RIGHT && _pressedTimings[Buttons::RIGHT] == 0.0f && Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_LEFTX) != 0.0f) {
            keysHeld += (Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_LEFTX) > 0.0f) ? 1 : 0;
            _pressedTimings[Buttons::RIGHT] = _joystickCooldown;
        }
        if (button == Buttons::UP && _pressedTimings[Buttons::UP] == 0.0f && Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_LEFTY) != 0.0f) {
            keysHeld += (Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_LEFTY) < 0.0f) ? 1 : 0;
            _pressedTimings[Buttons::UP] = _joystickCooldown;
        }
        if (button == Buttons::DOWN && _pressedTimings[Buttons::DOWN] == 0.0f && Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_LEFTY) != 0.0f) {
            keysHeld += (Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_LEFTY) > 0.0f) ? 1 : 0;
            // TODO figure out this line
            // keysHeld += (Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTY && Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTY) != 0.0f) > 0.0f) ? 1 : 0;
            _pressedTimings[Buttons::DOWN] = _joystickCooldown;
        }

        for (const auto &key : _mappingGP[button])
            keysHeld += static_cast<int>(Input::GamepadButtonPressed(_gamepadID, key));
        return (keysHeld != 0);
    case InputType::MOUSE_KB:

        for (const auto &key : _mappingKB[button])
            keysHeld += static_cast<int>(Input::KeyPressed(key));
        return (keysHeld != 0);
    }
    return false;
}

bool InputDevice::_isButtonHeld(Buttons button) {
    int keysHeld = 0;
    switch (_type) {
    case InputType::GAMEPAD:
        for (const auto &key : _mappingGP[button])
            keysHeld += static_cast<int>(Input::GamepadButtonDown(_gamepadID, key));
        return (keysHeld != 0);
    case InputType::MOUSE_KB:
        if (button == Buttons::SHOOT) {
            keysHeld += static_cast<int>(Input::MouseButtonDown(SDL_BUTTON_LEFT));
        }
        for (const auto &key : _mappingKB[button])
            keysHeld += static_cast<int>(Input::KeyDown(key));
        return (keysHeld != 0);
    }
    return false;
}

bool InputDevice::Is(Buttons button, Action action) {
    switch (action) {
    case Action::HELD:
        return _isButtonHeld(button);
    case Action::PRESSED:
        return _isButtonPressed(button);
    }
    return false;
}

vf2d InputDevice::GetLeftStick() {
    switch (_type) {
    case InputType::GAMEPAD:
        return { Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_LEFTX),
            Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_LEFTY) };
    case InputType::MOUSE_KB:
        return { float(Input::KeyDown(SDLK_D) - Input::KeyDown(SDLK_A)),
            float(Input::KeyDown(SDLK_S) - Input::KeyDown(SDLK_W)) };
    }
    return { 0.0f, 0.0f };
}

vf2d InputDevice::GetRightStick() {
    switch (_type) {
    case InputType::GAMEPAD:
        return { Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_RIGHTX),
            Input::GetGamepadAxisMovement(_gamepadID, SDL_GAMEPAD_AXIS_RIGHTY) };
    case InputType::MOUSE_KB:
        return { float(Input::KeyDown(SDLK_RIGHT) - Input::KeyDown(SDLK_LEFT)),
            float(Input::KeyDown(SDLK_DOWN) - Input::KeyDown(SDLK_UP)) };
    }
    return { 0.0f, 0.0f };
}

int InputDevice::GetGamepadID() const {
    return _gamepadID;
}

void InputDevice::SetGamepadID(int id) {
    _gamepadID = id;
}

InputDevice::InputDevice(InputType type)
    : _type(type)
    , _gamepadID(-1) { }

InputDevice::InputDevice(InputType type, int gamepadId)
    : _type(type)
    , _gamepadID(gamepadId) { }
