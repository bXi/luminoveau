#pragma once

#include "SDL3_mixer/SDL_mixer.h"

struct Music
{
	Mix_Music* music = nullptr;
	bool shouldPlay = false;
	bool started = false;
};
