#pragma once

#include <cstdint>
#include "miniaudio.h"

// ── PCM format descriptor ──

struct PCMFormat {
    uint32_t sampleRate = 48000;
    uint32_t channels   = 2;
};

// ── Callback types (raw function pointers — safe for audio thread) ──

/**
 * @brief Callback for generating PCM audio data.
 *
 * Called on the audio thread. Must be lock-free: no allocations, no mutexes,
 * no file I/O. Use std::atomic for parameter control from the game thread.
 *
 * @param output     Buffer to fill with interleaved float samples.
 * @param frameCount Number of frames to generate.
 * @param channels   Number of channels (matches PCMFormat::channels).
 * @param userData   User-provided context pointer.
 */
using PCMGenerateCallback = void(*)(float* output, uint32_t frameCount,
                                     uint32_t channels, void* userData);

/**
 * @brief Callback for processing audio data as a channel insert effect.
 *
 * Called on the audio thread. Must be lock-free. Modifies samples in-place.
 *
 * @param samples    Interleaved float samples to process in-place.
 * @param frameCount Number of frames in the buffer.
 * @param channels   Number of channels.
 * @param userData   User-provided context pointer.
 */
using PCMEffectCallback = void(*)(float* samples, uint32_t frameCount,
                                   uint32_t channels, void* userData);

// ── Custom miniaudio data source for PCM generators ──

struct LumiPCMDataSource {
    ma_data_source_base base; ///< Must be first member
    PCMGenerateCallback callback;
    void*    userData;
    uint32_t channels;
    uint32_t sampleRate;
};

// ── Custom miniaudio node for channel effects ──

struct LumiEffectNode {
    ma_node_base base; ///< Must be first member
    PCMEffectCallback callback;
    void*    userData;
    uint32_t channels;
    bool     initialized = false;
};

// ── PCM sound handle (user-owned) ──

/**
 * @brief A procedurally generated sound driven by a user callback.
 *
 * Created with Audio::CreatePCMGenerator(). The callback runs continuously
 * on the audio thread once started with Audio::PlayPCMSound(). Use atomics
 * in your userData struct to control parameters from the game thread.
 *
 * This is a lightweight handle (pointer wrapper) that is safe to copy, move,
 * and return by value. The ma_sound + data source live on the heap so internal
 * pointers remain stable.
 */
struct PCMSoundAsset {
    struct Internal {
        ma_sound          sound;
        LumiPCMDataSource dataSource;
    };
    Internal* impl = nullptr;
    bool initialized = false;
};

using PCMSound = PCMSoundAsset;
