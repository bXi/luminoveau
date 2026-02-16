#include "inputhandler.h"

#include "window/windowhandler.h"

void Input::_init() {
    if (_didInit) return;

    _didInit = true;

    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ROG_CHAKRAM, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");


    SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);

    inputs.push_back(new InputDevice(InputType::MOUSE_KB));

    auto gamePadCount = 0;
    joystickIds = SDL_GetGamepads(&gamePadCount);

    for (int i = 0; i < gamePadCount; i++) {
        gamepadInfo temp;
        temp.joystickId = joystickIds[i];
        temp.previousButtonState.resize(SDL_GAMEPAD_BUTTON_COUNT);
        temp.currentButtonState.resize(SDL_GAMEPAD_BUTTON_COUNT);
        temp.gamepad = SDL_OpenGamepad(joystickIds[i]);
        gamepads.push_back(temp);
        inputs.push_back(new InputDevice(InputType::GAMEPAD, i));
    }
}

InputDevice *Input::_getController(int index) {
    return inputs[index];
}

void Input::_clear() {
    inputs.clear();
}

void Input::_update() {
    UpdateTimings();

    scrolledUpTicks = 0;
    scrolledDownTicks = 0;

    previousKeyboardState = currentKeyboardState;

    previousMouseButtons = currentMouseButtons;
    currentMouseButtons = SDL_GetMouseState(nullptr, nullptr);

    for (auto &gamepad: gamepads) {
        gamepad.previousButtonState = gamepad.currentButtonState;

        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
            gamepad.currentButtonState[i] = SDL_GetGamepadButton(gamepad.gamepad, static_cast<SDL_GamepadButton>(i));
        }
    }

    // Update virtual controls
    virtualControls.Update();
}

void Input::_updateTimings() {
    for (const auto &i: inputs) {
        i->updateTimings();
    }
}

float Input::_getGamepadAxisMovement(int gamepadID, SDL_GamepadAxis axis) {
    Sint16 x = SDL_GetGamepadAxis(gamepads[gamepadID].gamepad, axis);

    if (std::abs(x) < DEADZONE) {
        // Value is within the deadzone, ignore it
        x = 0;
    }


    return ((float) x) / 32768.0f;
}

bool Input::_gamepadButtonPressed(int gamepadID, int button) {

    auto gamepadinfo = gamepads[gamepadID];
    return gamepadinfo.currentButtonState[button] && !gamepadinfo.previousButtonState[button];
}


bool Input::_gamepadButtonDown(int gamepadID, int button) {
    return SDL_GetGamepadButton(gamepads[gamepadID].gamepad, static_cast<SDL_GamepadButton>(button));
}

bool Input::_keyPressed(int key) {

    auto curState = currentKeyboardState;
    auto prevState = previousKeyboardState;

    auto scancode = SDL_GetScancodeFromKey(key, nullptr);

    return curState[scancode] == 1 && prevState[scancode] == 0;
}

bool Input::_keyReleased(int key) {

    auto curState = currentKeyboardState;
    auto prevState = previousKeyboardState;

    auto scancode = SDL_GetScancodeFromKey(key, nullptr);

    return curState[scancode] == 0 && prevState[scancode] == 1;
}

bool Input::_keyDown(int key) {

    auto scancode = SDL_GetScancodeFromKey(key, nullptr);

    return currentKeyboardState[scancode] == 1;
}


vf2d Input::_getMousePosition() {
    int windowX = 0;
    int windowY = 0;

    SDL_GetWindowPosition(Window::GetWindow(), &windowX, &windowY);

    float absMouseX = 0;
    float absMouseY = 0;

    SDL_GetGlobalMouseState(&absMouseX, &absMouseY);

    // SDL_GetGlobalMouseState returns coordinates in logical points (OS units)
    // SDL_GetWindowPosition also returns logical points, so subtraction gives
    // the mouse position relative to the window in logical points.
    float relX = (absMouseX - (float)windowX) / Window::GetScale();
    float relY = (absMouseY - (float)windowY) / Window::GetScale();

#ifdef LUMI_USE_PHYSICAL_PIXELS
    // Physical pixel mode: scale from logical points to device pixels
    float displayScale = Window::GetDisplayScale();
    relX *= displayScale;
    relY *= displayScale;
#endif

    return { relX, relY };
}


bool Input::_mouseButtonPressed(int button) {
    auto buttonmask = (1 << ((button) - 1));
    return (currentMouseButtons & buttonmask) != 0 && (previousMouseButtons & buttonmask) == 0;
}

bool Input::_mouseButtonReleased(int button) {
    auto buttonmask = (1 << ((button) - 1));
    return (currentMouseButtons & buttonmask) == 0 && (previousMouseButtons & buttonmask) != 0;
}


bool Input::_mouseButtonDown(int button) {
    auto buttonmask = (1 << ((button) - 1));
    return (currentMouseButtons & buttonmask) != 0;
}

void Input::_updateInputs(const std::vector<Uint8> &keys, bool held) {
    if (held) {
        for (auto scancode: keys) {
            currentKeyboardState[scancode] = 1;
        }
    } else {
        for (auto scancode: keys) {
            currentKeyboardState[scancode] = 0;
        }
    }
}

void Input::_addGamepadDevice(SDL_JoystickID joystickID) {
    for (const auto &gamepad: gamepads) {
        if (gamepad.joystickId == joystickID) return;
    }


    int newgamepadID = gamepads.size();
    gamepadInfo temp;
    temp.joystickId = joystickID;
    temp.previousButtonState.resize(SDL_GAMEPAD_BUTTON_COUNT);
    temp.currentButtonState.resize(SDL_GAMEPAD_BUTTON_COUNT);
    temp.gamepad = SDL_OpenGamepad(joystickID);
    gamepads.push_back(temp);
    inputs.push_back(new InputDevice(InputType::GAMEPAD, newgamepadID));
}

void Input::_removeGamepadDevice(SDL_JoystickID joystickID) {
    // Use std::remove_if to move the gamepad with the specified joystickID to the end
    auto newEnd = std::remove_if(gamepads.begin(), gamepads.end(),
                                 [joystickID](const auto &gamepad) {
                                     return gamepad.joystickId == joystickID;
                                 });

    // Erase the removed gamepads from the vector
    gamepads.erase(newEnd, gamepads.end());
}

void Input::_updateScroll(int scrollDir) {
    if (scrollDir < 0) {
        scrolledDownTicks++;
    }
    if (scrollDir > 0) {
        scrolledUpTicks++;
    }
}

void Input::_handleTouchEvent(const SDL_Event* event) {
    virtualControls.HandleTouchEvent(event);
}
