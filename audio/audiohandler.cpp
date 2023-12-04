#include "audiohandler.h"

Sound Audio::_getSound(const char* fileName)
{
	if (_sounds.find(fileName) == _sounds.end()) {
		Sound _sound;
        _sound.sound = Mix_LoadWAV(fileName);

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
		Music _music;
        _music.music = Mix_LoadMUS(fileName);

		_musics[fileName] = _music;

		return _musics[fileName];
	}
	else {
		return _musics[fileName];
	}
}

void Audio::_playSound(const char* fileName)
{
	if (_sounds.find(fileName) == _sounds.end()) {
		static_assert(true, "File not loaded yet.");
	}
	else {
        Mix_VolumeChunk(_sounds[fileName].sound, (int)(Settings::getSoundVolume() * 128.f));
        Mix_PlayChannel(-1, _sounds[fileName].sound, 0);
	}
}

void Audio::_updateMusicStreams()
{
	for (auto& music : _musics) {
		if (music.second.shouldPlay)
		{
			Mix_VolumeMusic((int)(Settings::getMusicVolume() * 128.f));
			Mix_PlayMusic(music.second.music, 0);
		}

	}
}

void Audio::_stopMusic()
{
	for (auto& music : _musics) {
        if (music.second.shouldPlay) {
            music.second.shouldPlay = false;
            //Mix_PauseMusic();
        }
    }
	Mix_HaltMusic();
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

            Mix_PlayMusic(_musics[fileName].music, 0);
		}


	}
}

void Audio::_init() {
    Mix_Init(MIX_INIT_MP3 | MIX_INIT_WAVPACK);

    _spec.freq = MIX_DEFAULT_FREQUENCY;
    _spec.format = MIX_DEFAULT_FORMAT;
    _spec.channels = MIX_DEFAULT_CHANNELS;


    if (Mix_OpenAudio(0, &_spec) < 0) {
#ifndef __EMSCRIPTEN__
        SDL_Log("Couldn't open audio: %s\n", SDL_GetError());
#endif
    } else {
        Mix_QuerySpec(&_spec.freq, &_spec.format, &_spec.channels);
#ifndef __EMSCRIPTEN__
        SDL_Log("Opened audio at %d Hz %d bit%s %s", _spec.freq,
            (_spec.format&0xFF),
            (SDL_AUDIO_ISFLOAT(_spec.format) ? " (float)" : ""),
            (_spec.channels > 2) ? "surround" :
            (_spec.channels > 1) ? "stereo" : "mono");

        putchar('\n');
#endif

    }


}

void Audio::_close() {
    _stopMusic();

    Mix_CloseAudio();

}
//*/