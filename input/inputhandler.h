#pragma once

#include <vector>
#include <list>
#include <map>
#include <unordered_map>

#include "SDL3/SDL.h"

#include "utils/vectors.h"

enum class InputType { GAMEPAD, MOUSE_KB };
enum class Buttons { LEFT, RIGHT, UP, DOWN, ACCEPT, BACK, SHOOT, SWITCH_NEXT, SWITCH_PREV, RUN };
enum class Action { HELD, PRESSED };

class InputDevice {
private:
    InputType type;
    int gamepadID;

    std::map<Buttons, std::list<int>> mappingKB = {
        { Buttons::LEFT,  { SDLK_a }}, //, KEY_LEFT}},
        { Buttons::RIGHT, { SDLK_d }}, //, KEY_RIGHT}},
        { Buttons::UP,    { SDLK_w }}, //, KEY_UP}},
        { Buttons::DOWN,  { SDLK_s }}, //, KEY_DOWN}},

        { Buttons::ACCEPT, { SDLK_SPACE, SDLK_KP_ENTER, SDLK_RETURN }},
        { Buttons::BACK,   { SDLK_ESCAPE, SDLK_BACKSPACE }},

        { Buttons::SWITCH_NEXT, { SDLK_TAB }},
        { Buttons::SWITCH_PREV, { SDLK_BACKQUOTE }},

        { Buttons::RUN, { SDL_SCANCODE_LSHIFT}},

        { Buttons::SHOOT, { SDLK_LSHIFT }}

    };

    std::map<Buttons, std::list<int>> mappingGP = {
        { Buttons::ACCEPT, { SDL_GAMEPAD_BUTTON_A }},
        { Buttons::BACK,   { SDL_GAMEPAD_BUTTON_B }},

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

    int getGamepadID() const;

    InputDevice(InputType _type);

    InputDevice(InputType _type, int _gamepadID);
    InputType getType();
};


class Input {
public:

    static void Init()
    {
        get()._init();
    }



    static InputDevice* GetController(int index)
    {
        return get()._getController(index);
    }

    static void clear()
    {
        get()._clear();
    }

    static std::vector<InputDevice*> GetAllInputs()
    {
        return get().inputs;
    }

    static void UpdateTimings()
    {
	    for (const auto&  i : get().inputs)
	    {
            i->updateTimings();
	    }
    }

    static float GetGamepadAxisMovement(int gamepadID, int axis)
    {
        return 0.f;
    }

    static bool GamepadButtonPressed(int gamepadID, int key)
    {
        return false;
    }

    static bool GamepadButtonDown(int gamepadID, int key)
    {
        return false;
    }

    static bool KeyPressed(int key)
    {
        return false;//IsKeyPressed(key);
    }

    static bool KeyDown(int key)
    {
        return false;//IsKeyPressed(key);

    static vf2d GetMousePosition()
    {
        float xMouse, yMouse;

        SDL_GetMouseState(&xMouse,&yMouse);

        return {xMouse, yMouse};
    }

    static bool MouseButtonPressed(int key)
    {
        return false;//IsKeyPressed(key);
    }

    static bool MouseButtonDown(int key)
    {
        return false;//IsKeyPressed(key);
    }


private:
    std::vector<InputDevice*> inputs;
    void _init();
    InputDevice* _getController(int index);
    void _clear();
    void _doKeyPolling();




public:
    Input(const Input&) = delete;
    static Input& get() { static Input instance; return instance; }

private:
    Input() = default;
};


//*/