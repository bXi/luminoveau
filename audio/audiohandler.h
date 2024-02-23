#pragma once

#include <unordered_map>

#include "settings/settingshandler.h"

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

	static Music GetMusic(const char* fileName)
	{
		return get()._getMusic(fileName);
	}

	static Sound GetSound(const char* fileName)
	{
		return get()._getSound(fileName);
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

    ma_device device;
    ma_engine engine;
    ma_resource_manager resourceManager;

    static void ma_data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

public:
	Audio(const Audio&) = delete;
	static Audio& get() { static Audio instance; return instance; }

private:
	Audio() {}
};
//*/