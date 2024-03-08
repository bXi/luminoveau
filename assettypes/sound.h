#pragma once

#include "audio/miniaudio.h"

/**
 * @brief Represents a sound asset for playing short audio clips using miniaudio.
 */
struct Sound {
    ma_sound *sound = nullptr; /**< Pointer to the audio data loaded with miniaudio. */
};
