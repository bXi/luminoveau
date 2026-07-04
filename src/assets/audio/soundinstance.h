#pragma once

#include "miniaudio.h"

// ── Controllable sound instance handle (user-owned) ──

/**
 * @brief A single playing instance of a decoded Sound, with live control.
 *
 * Created with Audio::PlaySoundInstance() from a Sound previously loaded via
 * AssetHandler::GetSound (which registers + caches the decoded data). Unlike the
 * fire-and-forget Audio::PlaySound, an instance can be looped, re-panned, have its
 * volume changed while playing, queried for completion, and stopped.
 *
 * Lightweight handle (pointer wrapper) — safe to copy, move, and return by value.
 * The ma_sound lives on the heap so internal pointers stay stable. The caller owns
 * the instance and must call Audio::StopSoundInstance() to release it.
 */
struct SoundInstanceAsset {
    struct Internal {
        ma_sound sound;
    };
    Internal* impl        = nullptr;  ///< Heap-allocated internal state (ma_sound).
    bool      initialized = false;    ///< True once the instance has been successfully created.
};

using SoundInstance = SoundInstanceAsset;
