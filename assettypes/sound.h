#pragma once

#include <string>
#include "miniaudio.h"

/**
 * @brief Represents a sound asset for playing short audio clips using miniaudio.
 */
struct SoundAsset {
    ma_sound *sound = nullptr; /**< Pointer to the audio data loaded with miniaudio. */
    std::string fileName; /**< Holds the fileName that was used to load the sound. */
};

using Sound = SoundAsset&;
