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
     * @brief Plays a sound effect from the specified file with specified volume and panning.
     *
     * @param fileName The filename of the sound effect to play.
     * @param volume The volume of the sound from 0.0f to 1.0f
     * @param panning The panning of the sound from -1.0f to 1.0f
     */
    static void PlaySound(Sound sound, float volume, float panning) {
        get()._playSound(sound, volume, panning);
    }

    /**
     * @brief Checks if music is currently playing.
     *
     * @return True if music is playing, false otherwise.
     */
    static bool IsMusicPlaying() {
        return get()._isMusicPlaying();
    }

    /**
     * @brief Sets the number of channels to be used. Defaults to 2 for normal stereo.
     *
     * @param newNumberOfChannels Number of channels to initialize audio engine with.
     */
    static void SetNumberOfChannels(int newNumberOfChannels) {
        get()._setNumberOfChannels(newNumberOfChannels);
    }

    static ma_engine* GetAudioEngine() {
        return &get().engine;
    }



private:

    void _playSound(Sound sound);

    void _playSound(Sound sound, float volume, float panning);

    void _updateMusicStreams();

    void _stopMusic();

    bool _isMusicPlaying();

    void _playMusic(Music music);

    void _rewindMusic(Music music);

    void _init();

    void _close();

    void _setNumberOfChannels(int newNumberOfChannels);

    int _numberChannels = 2;

    bool audioInit = false;
    ma_device device;
    ma_engine engine;
    ma_resource_manager resourceManager;

    std::array<ma_sound*, 128> _sounds;

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