#pragma once

#include "miniaudio.h"

/**
 * @brief Represents a music asset for playing audio using miniaudio.
 * @typedef Music Music
 */
struct MusicAsset {
    ma_sound *music = nullptr; /**< Pointer to the audio data loaded with miniaudio (unused on 3DS). */

    bool  shouldPlay = false;   /**< Flag indicating whether the music should play. */
    bool  started    = false;   /**< Flag indicating whether the music playback has started. */
    void *fileData   = nullptr; /**< Internal: PhysFS file data for cleanup. */
#ifdef __3DS__
    // 3DS streams music at play time (full songs are too large to decode to PCM). Holds the
    // compressed Ogg bytes; the audio backend opens a stb_vorbis stream over them. Owned; freed
    // on unload.
    unsigned char *encoded     = nullptr;
    unsigned long  encodedSize = 0;
    int            channels    = 0;
    int            sampleRate  = 0;
#endif
};

using Music = MusicAsset &;