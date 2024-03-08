#pragma once

#include <map>
#include <list>

#include "SDL3/SDL.h"

#include "inputconstants.h"

/**
 * @brief Represents an input device for handling user input.
 */
class InputDevice {
public:
    /**
     * @brief Updates the timings of the input device.
     */
    void updateTimings();

    /**
     * @brief Checks if a specific button is in a particular action state.
     *
     * @param button The button to check.
     * @param action The action state to check.
     * @return True if the button is in the specified action state, false otherwise.
     */
    bool is(Buttons button, Action action);

    /**
     * @brief Retrieves the ID of the gamepad associated with the input device.
     *
     * @return The ID of the gamepad.
     */
    [[nodiscard]] int getGamepadID() const;

    /**
     * @brief Constructs an input device with the specified type.
     *
     * @param _type The type of the input device.
     */
    explicit InputDevice(InputType _type);

    /**
     * @brief Constructs an input device with the specified type and gamepad ID.
     *
     * @param _type The type of the input device.
     * @param _gamepadID The ID of the associated gamepad.
     */
    InputDevice(InputType _type, int _gamepadID);

    /**
     * @brief Retrieves the type of the input device.
     *
     * @return The type of the input device.
     */
    InputType getType();

private:
    InputType type;
    int gamepadID;

    std::map<Buttons, std::list<int>> mappingKB = {
            {Buttons::LEFT,        {SDLK_a,      SDLK_LEFT}},
            {Buttons::RIGHT,       {SDLK_d,      SDLK_RIGHT}},
            {Buttons::UP,          {SDLK_w,      SDLK_UP}},
            {Buttons::DOWN,        {SDLK_s,      SDLK_DOWN}},

            {Buttons::ACCEPT,      {SDLK_SPACE,  SDLK_KP_ENTER, SDLK_RETURN}},
            {Buttons::BACK,        {SDLK_ESCAPE, SDLK_BACKSPACE}},

            {Buttons::SWITCH_NEXT, {SDLK_TAB}},
            {Buttons::SWITCH_PREV, {SDLK_BACKQUOTE}},

            {Buttons::RUN,         {SDL_SCANCODE_LSHIFT}},

            {Buttons::SHOOT,       {SDLK_LSHIFT}}

    };

    std::map<Buttons, std::list<int>> mappingGP = {
            {Buttons::ACCEPT,      {SDL_GAMEPAD_BUTTON_SOUTH}},
            {Buttons::BACK,        {SDL_GAMEPAD_BUTTON_EAST}},

            {Buttons::LEFT,        {SDL_GAMEPAD_BUTTON_DPAD_LEFT}},
            {Buttons::RIGHT,       {SDL_GAMEPAD_BUTTON_DPAD_RIGHT}},
            {Buttons::UP,          {SDL_GAMEPAD_BUTTON_DPAD_UP}},
            {Buttons::DOWN,        {SDL_GAMEPAD_BUTTON_DPAD_DOWN}},

            {Buttons::SWITCH_NEXT, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}},
            {Buttons::SWITCH_PREV, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}},

            {Buttons::RUN,         {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}},
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


};