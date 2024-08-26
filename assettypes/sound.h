#pragma once

#include "miniaudio.h"

/**
 * @brief Represents a sound asset for playing short audio clips using miniaudio.
 */
struct SoundAsset {
    ma_sound *sound = nullptr; /**< Pointer to the audio data loaded with miniaudio. */
};

using Sound = SoundAsset&;
