#include "audiohandler.h"
/*
Sound Audio::_getSound(const char* fileName)
{
	if (_sounds.find(fileName) == _sounds.end()) {
		const Sound _sound = LoadSound(fileName);
		_sounds[fileName] = _sound;

		return _sounds[fileName];
	}
	else {
		return _sounds[fileName];
	}
}

Music Audio::_getMusic(const char* fileName)
{
	if (_musics.find(fileName) == _musics.end()) {
		const Music _music = LoadMusicStream(fileName);
		_musics[fileName].music = _music;

		return _musics[fileName].music;
	}
	else {
		return _musics[fileName].music;
	}
}

void Audio::_playSound(const char* fileName)
{
	if (_sounds.find(fileName) == _sounds.end()) {
		static_assert(true, "File not loaded yet.");
	}
	else {
		SetSoundVolume(_sounds[fileName], Settings::getSoundVolume());
		PlaySoundMulti(_sounds[fileName]);
	}
}

void Audio::_updateMusicStreams()
{
	for (auto& music : _musics) {
		if (music.second.shouldPlay)
		{
			SetMusicVolume(music.second.music, Settings::getMusicVolume());
			UpdateMusicStream(music.second.music);
		}

	}
}

void Audio::_stopMusic()
{
	for (auto& music : _musics) {
		music.second.shouldPlay = false;
		StopMusicStream(music.second.music);
	}
}

bool Audio::_isMusicPlaying()
{
	for (const auto& music : _musics) {
		if (music.second.shouldPlay) return true;
	}
	return false;
}

void Audio::_playMusic(const char* fileName)
{
	if (_musics.find(fileName) == _musics.end()) {
		static_assert(true, "File not loaded yet.");
	}
	else {
		_musics[fileName].shouldPlay = true;
		if (!_musics[fileName].started)
		{
			PlayMusicStream(_musics[fileName].music);

		}


	}
}
//*/