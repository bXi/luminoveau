#include "audiohandler.h"

#define MINIAUDIO_IMPLEMENTATION

#include "miniaudio.h"

Sound Audio::_getSound(const char *fileName) {
    if (_sounds.find(fileName) == _sounds.end()) {
        Sound _sound;
        _sound.sound = new ma_sound();
        ma_result result = ma_sound_init_from_file(&engine, fileName, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr, _sound.sound);

        if (result != MA_SUCCESS) {
            std::string error = Helpers::TextFormat("GetSound failed: %s", fileName);

            SDL_Log(error.c_str());
            throw std::runtime_error(error.c_str());
        }

        _sounds[fileName] = _sound;

        return _sounds[fileName];
    } else {
        return _sounds[fileName];
    }
}

Music Audio::_getMusic(const char *fileName) {
    if (_musics.find(fileName) == _musics.end()) {
        Music _music;

        _music.music = new ma_sound();
        ma_result result = ma_sound_init_from_file(&engine, fileName, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr, _music.music);

        if (result != MA_SUCCESS) {
            std::string error = Helpers::TextFormat("GetMusic failed: %s", fileName);

            SDL_Log(error.c_str());
            throw std::runtime_error(error.c_str());
        }

        _musics[fileName] = _music;

        return _musics[fileName];
    } else {
        return _musics[fileName];
    }
}

void Audio::_playSound(const char *fileName) {
    if (_sounds.find(fileName) == _sounds.end()) {
        static_assert(true, "File not loaded yet.");
    } else {

        if (ma_sound_is_playing(_sounds[fileName].sound)) {
            ma_sound_seek_to_pcm_frame(_sounds[fileName].sound, 0);
            return;
        }

        ma_sound_set_looping(_sounds[fileName].sound, false);
        ma_sound_start(_sounds[fileName].sound);

    }
}

void Audio::_updateMusicStreams() {
#ifdef __EMSCRIPTEN__
    ma_resource_manager_process_next_job(&resourceManager);
#endif

    for (auto &music: _musics) {
        if (music.second.shouldPlay) {

//            if(ma_sound_is_playing(music.second.music))
//            {
//                ma_sound_seek_to_pcm_frame(music.second.music, 0);
//                return;
//            }
//
//            ma_sound_set_looping(music.second.music, true);
            ma_sound_start(music.second.music);

        }

    }
}

void Audio::_stopMusic() {
    for (auto &music: _musics) {
        if (music.second.shouldPlay) {
            music.second.shouldPlay = false;
            ma_sound_stop(music.second.music);
        }
    }
    //Mix_HaltMusic();
}

bool Audio::_isMusicPlaying() {
    for (const auto &music: _musics) {
        if (music.second.shouldPlay) return true;
    }
    return false;
}

void Audio::_playMusic(const char *fileName) {
    if (_musics.find(fileName) == _musics.end()) {
        static_assert(true, "File not loaded yet.");
    } else {
        _musics[fileName].shouldPlay = true;
        if (!_musics[fileName].started) {
            if (ma_sound_is_playing(_musics[fileName].music)) {
                ma_sound_seek_to_pcm_frame(_musics[fileName].music, 0);
                return;
            }

            ma_sound_set_looping(_musics[fileName].music, true);
            ma_sound_start(_musics[fileName].music);
        }


    }
}

void Audio::ma_data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount) {
    ma_engine_read_pcm_frames((ma_engine *) (pDevice->pUserData), pOutput, frameCount, nullptr);
}

void Audio::_init() {
    int sampleRate = 48000;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate = sampleRate;
    deviceConfig.dataCallback = Audio::ma_data_callback;
    deviceConfig.pUserData = &engine;

    ma_device_init(nullptr, &deviceConfig, &device);


    ma_resource_manager_config resourceManagerConfig = ma_resource_manager_config_init();
    resourceManagerConfig.decodedFormat = ma_format_f32;
    resourceManagerConfig.decodedChannels = 0;
    resourceManagerConfig.decodedSampleRate = sampleRate;

#ifdef __EMSCRIPTEN__
    resourceManagerConfig.jobThreadCount = 0;
    resourceManagerConfig.flags |= MA_RESOURCE_MANAGER_FLAG_NON_BLOCKING;
    resourceManagerConfig.flags |= MA_RESOURCE_MANAGER_FLAG_NO_THREADING;
#endif

    ma_resource_manager_init(&resourceManagerConfig, &resourceManager);

    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.pDevice = &device;
    engineConfig.pResourceManager = &resourceManager;

    ma_engine_init(&engineConfig, &engine);

}

void Audio::_close() {
    _stopMusic();

    ma_resource_manager_uninit(&resourceManager);
    ma_engine_uninit(&engine);
}
//*/