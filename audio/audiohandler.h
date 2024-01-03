#pragma once

#include <unordered_map>

#include "settings/settingshandler.h"

#include "SDL3/SDL.h"
#include "SDL3_mixer/SDL_mixer.h"

#include "assettypes/sound.h"
#include "assettypes/music.h"

class Audio {
public:
    static void Init()
    {
        get()._init();
    }

    static void Close()
    {
        get()._close();
    }

	static void UpdateMusicStreams()
	{
		get()._updateMusicStreams();
	}

	static void StopMusic()
	{
		get()._stopMusic();
	}

	static void GetMusic(const char* fileName)
	{
		get()._getMusic(fileName);
	}

	static void GetSound(const char* fileName)
	{
		get()._getSound(fileName);
	}

	static void PlayMusic(const char* fileName)
	{
		get()._playMusic(fileName);
	}

	static void PlaySound(const char* fileName)
	{
		get()._playSound(fileName);
	}

	static bool IsMusicPlaying()
	{
		return get()._isMusicPlaying();
	}

private:
	std::unordered_map<const char*, Sound> _sounds;
	std::unordered_map<const char*, Music> _musics;

	Sound _getSound(const char* fileName);
	Music _getMusic(const char* fileName);
	void _playSound(const char* fileName);
	void _updateMusicStreams();
	void _stopMusic();
	bool _isMusicPlaying();
	void _playMusic(const char* fileName);
    void _init();
    void _close();

    SDL_AudioSpec _spec;


public:
	Audio(const Audio&) = delete;
	static Audio& get() { static Audio instance; return instance; }

private:
	Audio() {}
};
//*/