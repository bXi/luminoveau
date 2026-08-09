#pragma once

#include "miniaudio.h"

/**
 * @brief Represents a music asset for playing audio using miniaudio.
 * @typedef Music Music
 */
struct MusicAsset {
    ma_sound *music = nullptr; /**< Pointer to the audio data loaded with miniaudio. */

    bool        shouldPlay = false;   /**< Flag indicating whether the music should play. */
    bool        started    = false;   /**< Flag indicating whether the music playback has started. */
    void       *fileData   = nullptr; /**< Internal: encoded file bytes kept alive for cleanup. */
    ma_decoder *decoder    = nullptr; /**< Internal: memory decoder (used for absolute-path sources). */
    ma_uint64   lengthFrames = 0;     /**< Internal: cached total length; the length query scans the file, so compute it once. */
};

using Music = MusicAsset &;