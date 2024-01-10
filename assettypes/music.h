#pragma once

#include "audio/miniaudio.h"


struct Music
{
    ma_sound* music = nullptr;

	bool shouldPlay = false;
	bool started = false;
};
