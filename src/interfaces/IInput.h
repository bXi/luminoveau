#pragma once

#include <vector>
#include <cstdint>

#include "math/vectors.h"

struct SDL_GamepadAxis;
struct SDL_JoystickID;
union SDL_Event;
class InputDevice;
class VirtualControls;

class IInput {
public:
    virtual ~IInput() = default;

    virtual void init() = 0;
    virtual void clear() = 0;
    virtual void update() = 0;
    virtual void updateTimings() = 0;

    virtual InputDevice*             getController(int index) = 0;
    virtual std::vector<InputDevice*> getAllInputs() = 0;

    virtual bool  keyPressed(int key) = 0;
    virtual bool  keyReleased(int key) = 0;
    virtual bool  keyDown(int key) = 0;

    virtual vf2d  getMousePosition() = 0;
    virtual bool  mouseButtonPressed(int button) = 0;
    virtual bool  mouseButtonReleased(int button) = 0;
    virtual bool  mouseButtonDown(int button) = 0;
    virtual uint32_t mouseScrolledUp() = 0;
    virtual uint32_t mouseScrolledDown() = 0;

    virtual float gamepadAxisMovement(int gamepadID, int axis) = 0;
    virtual bool  gamepadButtonPressed(int gamepadID, int button) = 0;
    virtual bool  gamepadButtonDown(int gamepadID, int button) = 0;

    virtual VirtualControls& getVirtualControls() = 0;
    virtual void handleTouchEvent(const SDL_Event* event) = 0;

    virtual void updateInputs(std::vector<uint8_t> keys, bool held) = 0;
    virtual void addGamepadDevice(uint32_t joystickID) = 0;
    virtual void removeGamepadDevice(uint32_t joystickID) = 0;
    virtual void updateScroll(int scrollDir) = 0;
};
