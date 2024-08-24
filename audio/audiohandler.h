#pragma once

#include <unordered_map>

#include "settings/settingshandler.h"

#include "assettypes/sound.h"
#include "assettypes/music.h"

/**
 * @brief Provides functionality for managing audio assets and playback.
 */
class Audio {
public:
    /**
     * @brief Initializes the audio system.
     */
    static void Init() {
        get()._init();
    }

    /**
     * @brief Closes the audio system and releases resources.
     */
    static void Close() {
        get()._close();
    }

    /**
     * @brief Updates music streams.
     */
    static void UpdateMusicStreams() {
        get()._updateMusicStreams();
    }

    /**
     * @brief Stops music playback.
     */
    static void StopMusic() {
        get()._stopMusic();
    }

    /**
     * @brief Plays music from the specified file.
     *
     * @param fileName The filename of the music to play.
     */
    static void PlayMusic(Music &music) {
        get()._playMusic(music);
    }

    /**
     * @brief Rewinds the music file from the specified file.
     *
     * @param fileName The filename of the music to play.
     */
    static void RewindMusic(Music &music) {
        get()._rewindMusic(music);
    }

    /**
     * @brief Plays a sound effect from the specified file.
     *
     * @param fileName The filename of the sound effect to play.
     */
    static void PlaySound(Sound sound) {
        get()._playSound(sound);
    }

    /**
     * @brief Checks if music is currently playing.
     *
     * @return True if music is playing, false otherwise.
     */
    static bool IsMusicPlaying() {
        return get()._isMusicPlaying();
    }

    static ma_engine* GetAudioEngine() {
        return &get().engine;
    }



private:

    void _playSound(Sound sound);

    void _updateMusicStreams();

    void _stopMusic();

    bool _isMusicPlaying();

    void _playMusic(Music music);

    void _rewindMusic(Music music);

    void _init();

    void _close();

    ma_device device;
    ma_engine engine;
    ma_resource_manager resourceManager;

    static void ma_data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount);

public:
    Audio(const Audio &) = delete;

    static Audio &get() {
        static Audio instance;
        return instance;
    }

private:
    Audio() {}
};
//*/