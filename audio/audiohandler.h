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
     * @brief Retrieves a music asset.
     *
     * @param fileName The filename of the music asset.
     * @return The music asset.
     */
    static Music GetMusic(const char *fileName) {
        return get()._getMusic(fileName);
    }

    /**
     * @brief Retrieves a sound asset.
     *
     * @param fileName The filename of the sound asset.
     * @return The sound asset.
     */
    static Sound GetSound(const char *fileName) {
        return get()._getSound(fileName);
    }

    /**
     * @brief Plays music from the specified file.
     *
     * @param fileName The filename of the music to play.
     */
    static void PlayMusic(const char *fileName) {
        get()._playMusic(fileName);
    }

    /**
     * @brief Plays a sound effect from the specified file.
     *
     * @param fileName The filename of the sound effect to play.
     */
    static void PlaySound(const char *fileName) {
        get()._playSound(fileName);
    }

    /**
     * @brief Checks if music is currently playing.
     *
     * @return True if music is playing, false otherwise.
     */
    static bool IsMusicPlaying() {
        return get()._isMusicPlaying();
    }

private:
    std::unordered_map<const char *, Sound> _sounds;
    std::unordered_map<const char *, Music> _musics;

    Sound _getSound(const char *fileName);

    Music _getMusic(const char *fileName);

    void _playSound(const char *fileName);

    void _updateMusicStreams();

    void _stopMusic();

    bool _isMusicPlaying();

    void _playMusic(const char *fileName);

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