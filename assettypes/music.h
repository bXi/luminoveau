#pragma once

#include "audio/miniaudio.h"

/**
 * @brief Represents a music asset for playing audio using miniaudio.
 * @typedef Music Music
 */
struct MusicAsset {
    ma_sound *music = nullptr; /**< Pointer to the audio data loaded with miniaudio. */

    bool shouldPlay = false; /**< Flag indicating whether the music should play. */
    bool started = false; /**< Flag indicating whether the music playback has started. */
};

using Music = MusicAsset&;