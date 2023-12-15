#pragma once

#include <map>
#include <list>

#include "SDL3/SDL.h"

#include "inputconstants.h"

class InputDevice {
private:
    InputType type;
    int gamepadID;

    std::map<Buttons, std::list<int>> mappingKB = {
        { Buttons::LEFT,  { SDLK_a , SDLK_LEFT}},
        { Buttons::RIGHT, { SDLK_d , SDLK_RIGHT}},
        { Buttons::UP,    { SDLK_w , SDLK_UP}},
        { Buttons::DOWN,  { SDLK_s , SDLK_DOWN}},

        { Buttons::ACCEPT, { SDLK_SPACE, SDLK_KP_ENTER, SDLK_RETURN }},
        { Buttons::BACK,   { SDLK_ESCAPE, SDLK_BACKSPACE }},

        { Buttons::SWITCH_NEXT, { SDLK_TAB }},
        { Buttons::SWITCH_PREV, { SDLK_BACKQUOTE }},

        { Buttons::RUN, { SDL_SCANCODE_LSHIFT}},

        { Buttons::SHOOT, { SDLK_LSHIFT }}

    };

    std::map<Buttons, std::list<int>> mappingGP = {
        { Buttons::ACCEPT, { SDL_GAMEPAD_BUTTON_SOUTH }},
        { Buttons::BACK,   { SDL_GAMEPAD_BUTTON_EAST }},

        { Buttons::LEFT,  { SDL_GAMEPAD_BUTTON_DPAD_LEFT }},
        { Buttons::RIGHT, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT }},
        { Buttons::UP,    { SDL_GAMEPAD_BUTTON_DPAD_UP }},
        { Buttons::DOWN,  { SDL_GAMEPAD_BUTTON_DPAD_DOWN }},

        { Buttons::SWITCH_NEXT, { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER }},
        { Buttons::SWITCH_PREV, { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER }},

        { Buttons::RUN, {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}},
    };


    bool isButtonPressed(Buttons button);
    bool isButtonHeld(Buttons button);

    std::map<Buttons, float> pressedTimings = {
        {Buttons::LEFT,  0.0f},
        {Buttons::RIGHT, 0.0f},
        {Buttons::UP,    0.0f},
        {Buttons::DOWN,  0.0f},
    };

    float joystickCooldown = 0.10f;


public:
    void updateTimings();

	bool is(Buttons button, Action action);

    [[nodiscard]] int getGamepadID() const;

    explicit InputDevice(InputType _type);

    InputDevice(InputType _type, int _gamepadID);
    InputType getType();
};