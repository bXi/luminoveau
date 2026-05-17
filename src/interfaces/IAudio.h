#pragma once

#include <cstdint>

#include "assets/audio/sound.h"
#include "assets/audio/music.h"
#include "assets/audio/pcmsound.h"

enum class AudioChannel : uint8_t;

class IAudio {
public:
    virtual ~IAudio() = default;

    virtual void init() = 0;
    virtual void close() = 0;
    virtual void updateMusicStreams() = 0;

    virtual void playMusic(Music& music) = 0;
    virtual void stopMusic() = 0;
    virtual void rewindMusic(Music& music) = 0;
    virtual void setMusicVolume(Music& music, float volume) = 0;
    virtual bool isMusicPlaying() = 0;

    virtual void playSound(Sound sound, AudioChannel channel) = 0;
    virtual void playSound(Sound sound, float volume, float panning, AudioChannel channel) = 0;

    virtual void setNumberOfChannels(int n) = 0;

    virtual void  setChannelVolume(AudioChannel channel, float volume) = 0;
    virtual float getChannelVolume(AudioChannel channel) = 0;
    virtual void  setChannelPanning(AudioChannel channel, float panning) = 0;
    virtual float getChannelPanning(AudioChannel channel) = 0;
    virtual void  muteChannel(AudioChannel channel, bool muted) = 0;
    virtual bool  isChannelMuted(AudioChannel channel) = 0;

    virtual PCMSound createPCMGenerator(const PCMFormat& format,
                                         PCMGenerateCallback callback,
                                         void* userData = nullptr) = 0;
    virtual void playPCMSound(PCMSound& sound, AudioChannel channel) = 0;
    virtual void stopPCMSound(PCMSound& sound) = 0;
    virtual void destroyPCMSound(PCMSound& sound) = 0;

    virtual void setChannelEffect(AudioChannel channel, PCMEffectCallback callback,
                                   void* userData = nullptr) = 0;
    virtual void removeChannelEffect(AudioChannel channel) = 0;
};
