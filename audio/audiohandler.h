#pragma once

#include <unordered_map>
/*
#include "settings/settingshandler.h"

struct MusicWrapper
{
	Music music;
	bool shouldPlay = false;
	bool started = false;
};

class Audio {
public:
	enum class gamestate {
		INTRO,
		MENU,
		GAME,
		PAUSE,
		CREDITS,
		QUIT,
		TEST,
	};

	static void updateMusicStreams()
	{
		get()._updateMusicStreams();
	}

	static void stopMusic()
	{
		get()._stopMusic();
	}

	static void getMusic(const char* fileName)
	{
		get()._getMusic(fileName);
	}

	static void getSound(const char* fileName)
	{
		get()._getSound(fileName);
	}

	static void playMusic(const char* fileName)
	{
		get()._playMusic(fileName);
	}

	static void playSound(const char* fileName)
	{
		get()._playSound(fileName);
	}

	static bool isMusicPlaying()
	{
		return get()._isMusicPlaying();
	}

private:
	std::unordered_map<const char*, Sound> _sounds;
	std::unordered_map<const char*, MusicWrapper> _musics;

	Sound _getSound(const char* fileName);
	Music _getMusic(const char* fileName);
	void _playSound(const char* fileName);
	void _updateMusicStreams();
	void _stopMusic();
	bool _isMusicPlaying();
	void _playMusic(const char* fileName);

public:
	Audio(const Audio&) = delete;
	static Audio& get() { static Audio instance; return instance; }

private:
	Audio() {}
};



//*/