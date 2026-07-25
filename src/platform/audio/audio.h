#pragma once

#include <unordered_map>
#include <array>
#include <vector>

#include "core/settings/settings.h"

#include "assets/audio/sound.h"
#include "assets/audio/music.h"
#include "assets/audio/pcmsound.h"
#include "assets/audio/soundinstance.h"

/**
 * @brief Audio mix channels for routing sounds through volume/panning groups.
 */
enum class AudioChannel : uint8_t {
    Master, ///< Controls the engine master volume (not a real group)
    SFX,    ///< Sound effects channel
    Voice,  ///< Voice/dialogue channel
    Music,  ///< Music channel
    Count   ///< Number of entries (used for array sizing)
};

/**
 * @brief Provides functionality for managing audio assets and playback.
 */
class Audio {
public:
    /**
     * @brief Initializes the audio system.
     */
    static void Init() {
        Get()._init();
    }

    /**
     * @brief Closes the audio system and releases resources.
     */
    static void Close() {
        Get()._close();
    }

    /**
     * @brief Updates music streams.
     */
    static void UpdateMusicStreams() {
        Get()._updateMusicStreams();
    }

    /**
     * @brief Stops music playback.
     */
    static void StopMusic() {
        Get()._stopMusic();
    }

    /**
     * @brief Plays music from the specified file. Always routes through the Music channel.
     *
     * @param music The Music asset to play.
     */
    static void PlayMusic(Music &music) {
        Get()._playMusic(music);
    }

    /**
     * @brief Sets the volume on the supplied music.
     *
     * @param music The Music asset to change the volume on.
     * @param volume The new volume for the Music asset.
     */
    static void SetMusicVolume(Music &music, float volume) {
        Get()._setMusicVolume(music, volume);
    }

    /**
     * @brief Rewinds the music file from the specified file.
     *
     * @param music The Music asset to rewind.
     */
    static void RewindMusic(Music &music) {
        Get()._rewindMusic(music);
    }

    /**
     * @brief Pauses music playback, preserving the playback position.
     */
    static void PauseMusic(Music &music) {
        Get()._pauseMusic(music);
    }

    /**
     * @brief Resumes music playback from where it was paused.
     */
    static void ResumeMusic(Music &music) {
        Get()._resumeMusic(music);
    }

    /**
     * @brief Sets whether the music loops when it reaches the end.
     */
    static void SetMusicLooping(Music &music, bool looping) {
        Get()._setMusicLooping(music, looping);
    }

    /**
     * @brief Plays a sound effect (non-polyphonic, reuses the pre-loaded ma_sound).
     *
     * @param sound The Sound asset to play.
     * @param channel The audio channel to route through (default: SFX).
     */
    static void PlaySound(Sound sound, AudioChannel channel = AudioChannel::SFX) {
        Get()._playSound(sound, channel);
    }

    /**
     * @brief Plays a sound effect with specified volume and panning (polyphonic).
     *
     * @param sound The Sound asset to play.
     * @param volume The volume of the sound from 0.0f to 1.0f.
     * @param panning The panning of the sound from -1.0f (left) to 1.0f (right).
     * @param channel The audio channel to route through (default: SFX).
     */
    static void PlaySound(Sound sound, float volume, float panning, AudioChannel channel = AudioChannel::SFX) {
        Get()._playSound(sound, volume, panning, channel);
    }

    /**
     * @brief Plays a controllable instance of a sound.
     *
     * Unlike PlaySound, the returned handle can be looped, re-panned, volume-adjusted,
     * queried, and stopped while playing. The Sound must have been loaded via
     * AssetHandler::GetSound first (registers + caches the decoded data).
     *
     * @param sound   The Sound asset to play.
     * @param volume  Volume from 0.0f to 1.0f.
     * @param panning Panning from -1.0f (left) to 1.0f (right).
     * @param looping Whether the instance loops until stopped.
     * @param channel The audio channel to route through (default: SFX).
     * @return A SoundInstance handle. Caller owns it and must call StopSoundInstance().
     */
    static SoundInstance PlaySoundInstance(Sound sound, float volume, float panning,
        bool looping, AudioChannel channel = AudioChannel::SFX) {
        return Get()._playSoundInstance(sound, volume, panning, looping, channel);
    }

    /**
     * @brief Sets the volume of a playing sound instance.
     */
    static void SetSoundInstanceVolume(SoundInstance &instance, float volume) {
        Get()._setSoundInstanceVolume(instance, volume);
    }

    /**
     * @brief Sets the panning of a playing sound instance.
     */
    static void SetSoundInstancePanning(SoundInstance &instance, float panning) {
        Get()._setSoundInstancePanning(instance, panning);
    }

    /**
     * @brief Returns whether a sound instance is currently playing.
     */
    static bool IsSoundInstancePlaying(const SoundInstance &instance) {
        return Get()._isSoundInstancePlaying(instance);
    }

    /**
     * @brief Stops and releases a sound instance. Do not use the handle afterwards.
     */
    static void StopSoundInstance(SoundInstance &instance) {
        Get()._stopSoundInstance(instance);
    }

    /**
     * @brief Checks if music is currently playing.
     *
     * @return True if music is playing, false otherwise.
     */
    static bool IsMusicPlaying() {
        return Get()._isMusicPlaying();
    }

    /**
     * @brief Sets the number of output channels to be used. Defaults to 2 for normal stereo.
     *
     * @param newNumberOfChannels Number of channels to initialize audio engine with.
     *
     * @note This needs to be called before Audio::Init().
     */
    static void SetNumberOfChannels(int newNumberOfChannels) {
        Get()._setNumberOfChannels(newNumberOfChannels);
    }

    // ── Channel control ──

    /**
     * @brief Sets the volume for an audio channel.
     *
     * @param channel The channel to set volume on.
     * @param volume Volume from 0.0f to 1.0f.
     */
    static void SetChannelVolume(AudioChannel channel, float volume) {
        Get()._setChannelVolume(channel, volume);
    }

    /**
     * @brief Gets the current volume for an audio channel.
     *
     * @param channel The channel to query.
     * @return Current volume (0.0f to 1.0f).
     */
    static float GetChannelVolume(AudioChannel channel) {
        return Get()._getChannelVolume(channel);
    }

    /**
     * @brief Sets the panning for an audio channel.
     *
     * @param channel The channel to set panning on. Has no effect on Master.
     * @param panning Panning from -1.0f (left) to 1.0f (right).
     */
    static void SetChannelPanning(AudioChannel channel, float panning) {
        Get()._setChannelPanning(channel, panning);
    }

    /**
     * @brief Gets the current panning for an audio channel.
     *
     * @param channel The channel to query.
     * @return Current panning (-1.0f to 1.0f). Always 0.0f for Master.
     */
    static float GetChannelPanning(AudioChannel channel) {
        return Get()._getChannelPanning(channel);
    }

    /**
     * @brief Mutes or unmutes an audio channel. Preserves the volume setting.
     *
     * @param channel The channel to mute/unmute.
     * @param muted True to mute, false to unmute.
     */
    static void MuteChannel(AudioChannel channel, bool muted) {
        Get()._muteChannel(channel, muted);
    }

    /**
     * @brief Checks if an audio channel is currently muted.
     *
     * @param channel The channel to query.
     * @return True if muted.
     */
    static bool IsChannelMuted(AudioChannel channel) {
        return Get()._isChannelMuted(channel);
    }

    // ── PCM generators ──

    /**
     * @brief Creates a PCM sound driven by a user callback.
     *
     * The callback runs on the audio thread whenever the engine needs more samples.
     * It must be lock-free: no allocations, no mutexes, no file I/O.
     * Use std::atomic in your userData struct to control parameters from the game thread.
     *
     * @param format   Sample rate and channel count for the generator.
     * @param callback Function that fills the output buffer with samples.
     * @param userData Pointer passed to the callback. You own the lifetime.
     * @return A PCMSound handle. Caller owns it and must call DestroyPCMSound when done.
     */
    static PCMSound CreatePCMGenerator(const PCMFormat &format, PCMGenerateCallback callback, void *userData = nullptr) {
        return Get()._createPCMGenerator(format, callback, userData);
    }

    /**
     * @brief Starts playback of a PCM generator sound.
     *
     * The generator callback will be invoked continuously on the audio thread.
     * Output silence in your callback when you don't want to produce sound.
     *
     * @param sound   The PCMSound to start.
     * @param channel The audio channel to route through (default: SFX).
     */
    static void PlayPCMSound(PCMSound &sound, AudioChannel channel = AudioChannel::SFX) {
        Get()._playPCMSound(sound, channel);
    }

    /**
     * @brief Stops playback of a PCM generator sound.
     *
     * @param sound The PCMSound to stop.
     */
    static void StopPCMSound(PCMSound &sound) {
        Get()._stopPCMSound(sound);
    }

    /**
     * @brief Destroys a PCM generator sound and releases its resources.
     *
     * @param sound The PCMSound to destroy. Do not use after this call.
     */
    static void DestroyPCMSound(PCMSound &sound) {
        Get()._destroyPCMSound(sound);
    }

    // ── Channel effects ──

    /**
     * @brief Sets an insert effect on an audio channel.
     *
     * The callback receives the mixed channel output and modifies samples in-place.
     * Runs on the audio thread — must be lock-free.
     * One effect per channel. Calling again replaces the previous effect.
     * For Master, the effect runs on the final mixed output before the device.
     *
     * @param channel  The channel to apply the effect to.
     * @param callback Function that processes samples in-place.
     * @param userData Pointer passed to the callback. You own the lifetime.
     */
    static void SetChannelEffect(AudioChannel channel, PCMEffectCallback callback, void *userData = nullptr) {
        Get()._setChannelEffect(channel, callback, userData);
    }

    /**
     * @brief Removes the insert effect from an audio channel.
     *
     * @param channel The channel to remove the effect from.
     */
    static void RemoveChannelEffect(AudioChannel channel) {
        Get()._removeChannelEffect(channel);
    }

    /// @brief Returns the underlying miniaudio engine for advanced/direct access.
    static ma_engine *GetAudioEngine() {
        return &Get()._engine;
    }

    /**
     * @brief Gets the ma_sound_group for a given channel.
     *
     * @param channel The channel whose group to retrieve. Must not be Master.
     * @return Pointer to the channel's ma_sound_group, or nullptr if Master or not initialized.
     */
    static ma_sound_group *GetChannelGroup(AudioChannel channel) {
        return Get()._getChannelGroup(channel);
    }

private:
    // ── Sound playback ──

    void _playSound(Sound sound, AudioChannel channel);

    void _playSound(Sound sound, float volume, float panning, AudioChannel channel);

    // ── Controllable sound instances ──

    SoundInstance _playSoundInstance(Sound sound, float volume, float panning,
        bool looping, AudioChannel channel);

    void _setSoundInstanceVolume(SoundInstance &instance, float volume);

    void _setSoundInstancePanning(SoundInstance &instance, float panning);

    bool _isSoundInstancePlaying(const SoundInstance &instance);

    void _stopSoundInstance(SoundInstance &instance);

    // ── Music playback ──

    void _updateMusicStreams();

    void _stopMusic();

    bool _isMusicPlaying();

    void _playMusic(Music music);

    void _setMusicVolume(Music &music, float volume);

    void _rewindMusic(Music music);

    void _pauseMusic(Music &music);

    void _resumeMusic(Music &music);

    void _setMusicLooping(Music &music, bool looping);

    // ── Channel control ──

    void _setChannelVolume(AudioChannel channel, float volume);

    float _getChannelVolume(AudioChannel channel);

    void _setChannelPanning(AudioChannel channel, float panning);

    float _getChannelPanning(AudioChannel channel);

    void _muteChannel(AudioChannel channel, bool muted);

    bool _isChannelMuted(AudioChannel channel);

    ma_sound_group *_getChannelGroup(AudioChannel channel);

    // ── PCM generators ──

    PCMSound _createPCMGenerator(const PCMFormat &format, PCMGenerateCallback callback, void *userData);

    void _playPCMSound(PCMSound &sound, AudioChannel channel);

    void _stopPCMSound(PCMSound &sound);

    void _destroyPCMSound(PCMSound &sound);

    // ── Channel effects ──

    void _setChannelEffect(AudioChannel channel, PCMEffectCallback callback, void *userData);

    void _removeChannelEffect(AudioChannel channel);

    // ── Lifecycle ──

    void _init();

    void _close();

    void _setNumberOfChannels(int newNumberOfChannels);

    // ── Channel state ──

    /// Number of real mix groups (everything except Master)
    static constexpr int NUM_GROUPS = static_cast<int>(AudioChannel::Count) - 1;

    /// @cond INTERNAL
    struct ChannelState {
        ma_sound_group group;
        float          volume      = 1.0f;
        float          panning     = 0.0f;
        bool           muted       = false;
        bool           initialized = false;

        LumiEffectNode effectNode;
    };
    /// @endcond

    std::array<ChannelState, NUM_GROUPS> _channels;
    float                                _masterVolume = 1.0f;
    bool                                 _masterMuted  = false;

    // Master effect (applied in the device data callback)
    PCMEffectCallback _masterEffectCallback = nullptr;
    void             *_masterEffectUserData = nullptr;

    // ── Engine state ──

    int _numberChannels = 2;

    bool                _audioInit = false;
    ma_device           _device;
    ma_engine           _engine;
    ma_resource_manager _resourceManager;

    /// Pool of polyphonic sound instances
    std::array<ma_sound *, 128> _soundPool;

    // ── miniaudio vtables for custom data source / node (defined in .cpp) ──

    static ma_data_source_vtable pcmDataSourceVtable;
    static ma_node_vtable        effectNodeVtable;

    static void ma_data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount); // NOLINT(readability-identifier-naming) — miniaudio callback signature

public:
    /// @cond INTERNAL
    Audio(const Audio &) = delete;

    static Audio &Get() {
        static Audio instance;
        return instance;
    }
    /// @endcond

private:
    Audio() { }
};
//*/
