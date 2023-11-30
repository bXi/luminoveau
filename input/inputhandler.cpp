#include "inputhandler.h"

#include "window/windowhandler.h"


bool InputDevice::isButtonPressed(Buttons button)
{
	int keysHeld = 0;
	switch (type) {
	case InputType::GAMEPAD:
		if (button == Buttons::LEFT && pressedTimings[Buttons::LEFT] == 0.0f && Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTX) != 0.0f) {
			keysHeld += (Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTX) < 0.0f) ? 1 : 0;
			pressedTimings[Buttons::LEFT] = joystickCooldown;
		}
		if (button == Buttons::RIGHT && pressedTimings[Buttons::RIGHT] == 0.0f && Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTX) != 0.0f) {
			keysHeld += (Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTX) > 0.0f) ? 1 : 0;
			pressedTimings[Buttons::RIGHT] = joystickCooldown;
		}
		if (button == Buttons::UP && pressedTimings[Buttons::UP] == 0.0f && Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTY) != 0.0f) {
			keysHeld += (Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTY) < 0.0f) ? 1 : 0;
			pressedTimings[Buttons::UP] = joystickCooldown;
		}
		if (button == Buttons::DOWN && pressedTimings[Buttons::DOWN] == 0.0f  && Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTY) != 0.0f) {
			keysHeld += (Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTY) > 0.0f) ? 1 : 0;
            //TODO figure out this line
			//keysHeld += (Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTY && Input::GetGamepadAxisMovement(gamepadID, SDL_GAMEPAD_AXIS_LEFTY) != 0.0f) > 0.0f) ? 1 : 0;
			pressedTimings[Buttons::DOWN] = joystickCooldown;
		}

		for (const auto& key : mappingGP[button])
			keysHeld += static_cast<int>(Input::GamepadButtonPressed(gamepadID, key));
		return (keysHeld != 0);
	case InputType::MOUSE_KB:

		for (const auto& key : mappingKB[button])
			keysHeld += static_cast<int>(Input::KeyPressed(key));
		return (keysHeld != 0);
	}
	return false;
}

bool InputDevice::isButtonHeld(Buttons button)
{
	int keysHeld = 0;
	switch (type) {
	case InputType::GAMEPAD:
		for (const auto& key : mappingGP[button])
			keysHeld += static_cast<int>(Input::GamepadButtonDown(gamepadID, key));
		return (keysHeld != 0);
	case InputType::MOUSE_KB:
		if (button == Buttons::SHOOT)
		{
			keysHeld += static_cast<int>(Input::MouseButtonDown(SDL_BUTTON_LEFT));

		}
		for (const auto& key : mappingKB[button])
			keysHeld += static_cast<int>(Input::KeyDown(key));
		return (keysHeld != 0);
	}
	return false;
}


bool InputDevice::is(Buttons button, Action action) {
	switch (action) {
	case Action::HELD:    return isButtonHeld(button);
	case Action::PRESSED: return isButtonPressed(button);
	}
	return false;
}

int InputDevice::getGamepadID() const
{
	return gamepadID;
}

InputDevice::InputDevice(InputType _type) {
	type = _type;
	gamepadID = -1;
}

InputDevice::InputDevice(InputType _type, int _gamepadID)
{
	type = _type;
	gamepadID = _gamepadID;
}

InputType InputDevice::getType()
{
	return type;
}

void InputDevice::updateTimings()
{
    SDL_UpdateGamepads();

	for (auto& timing : pressedTimings)
	{
		if (timing.second > 0.0f)
		{
			timing.second -= Window::GetFrameTime();
		}
		else
		{
			timing.second = 0.0f;
		}
	}
}



void Input::_init()
{

    SDL_Init(SDL_INIT_GAMEPAD);

	inputs.push_back(new InputDevice(InputType::MOUSE_KB));

    auto gamePadCount = 0;
    joystickIds = SDL_GetGamepads(&gamePadCount);

	for (int i = 0; i < gamePadCount; i++) {
        gamepadInfo temp;
        temp.previousButtonState.resize(SDL_GAMEPAD_BUTTON_MAX);
        temp.currentButtonState.resize(SDL_GAMEPAD_BUTTON_MAX);
        temp.gamepad = SDL_OpenGamepad(joystickIds[i]);
        gamepads.push_back(temp);
    	inputs.push_back(new InputDevice(InputType::GAMEPAD, i));
	}


}

InputDevice* Input::_getController(int index)
{

	return inputs[index];
}

void Input::_clear()
{
	inputs.clear();
}

//*/
