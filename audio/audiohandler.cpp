#include "audiohandler.h"

#include "miniaudio.h"

#include "assethandler/assethandler.h"

void Audio::_playSound(Sound sound) {
    if (ma_sound_is_playing(sound.sound)) {
        ma_sound_seek_to_pcm_frame(sound.sound, 0);
        return;
    }

    ma_sound_set_looping(sound.sound, false);
    ma_sound_start(sound.sound);
}

void Audio::_playSound(Sound sound, float volume, float panning) {
    int index = -1;

    for (int i = 0; i < _sounds.size(); i++) {
        auto s = _sounds[i];
        if (!s || !ma_sound_is_playing(s)) {
            index = i;
            break;
        }
    }

    if (index == -1) return;

    volume = std::clamp(volume, 0.0f, 1.0f);
    panning = std::clamp(panning, -1.0f, 1.0f);

    _sounds[index] = new ma_sound;
    ma_sound_init_from_file(&engine, sound.fileName.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr, _sounds[index]);
    ma_sound_set_volume(_sounds[index], volume);
    ma_sound_set_pan(_sounds[index], panning);
    ma_sound_start(_sounds[index]);
}

void Audio::_updateMusicStreams() {
#ifdef __EMSCRIPTEN__
    ma_resource_manager_process_next_job(&resourceManager);
#endif
    for (auto &music: AssetHandler::GetLoadedMusics()) {
        if (music.second.shouldPlay) {
            ma_sound_start(music.second.music);
        }
    }
}

void Audio::_stopMusic() {
    for (auto& music: AssetHandler::GetLoadedMusics()) {
        if (music.second.shouldPlay) {
            music.second.shouldPlay = false;
            ma_sound_stop(music.second.music);
        }
    }
    //Mix_HaltMusic();
}

bool Audio::_isMusicPlaying() {
    for (const auto &music: AssetHandler::GetLoadedMusics()) {
        if (music.second.shouldPlay) return true;
    }
    return false;
}

void Audio::_playMusic(Music &music) {
    music.shouldPlay = true;
    if (!music.started) {
        if (ma_sound_is_playing(music.music)) {
            ma_sound_seek_to_pcm_frame(music.music, 0);
            return;
        }

        ma_sound_set_looping(music.music, true);
        ma_sound_start(music.music);
    }
}


void Audio::_rewindMusic(Music &music) {
    ma_sound_seek_to_pcm_frame(music.music, 0);
    return;
}
void Audio::ma_data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount) {
    ma_engine_read_pcm_frames((ma_engine *) (pDevice->pUserData), pOutput, frameCount, nullptr);
}

void Audio::_init() {
    int sampleRate = 48000;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = _numberChannels;
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

    ma_result result = ma_engine_init(&engineConfig, &engine);
    if (result == MA_SUCCESS) {
        get().audioInit = true;
    }

    for (size_t i = 0; i < get()._sounds.size(); ++i) {
        _sounds[i] = nullptr;
    }

}

void Audio::_close() {
    _stopMusic();

    for (size_t i = 0; i < get()._sounds.size(); ++i) {
        if (_sounds[i] && ma_sound_is_playing(_sounds[i])) {
            ma_sound_stop(_sounds[i]);
            ma_sound_uninit(_sounds[i]);
        }
    }

    ma_resource_manager_uninit(&resourceManager);
    ma_engine_uninit(&engine);
}

void Audio::_setNumberOfChannels(int newNumberOfChannels) {
    if (get().audioInit) throw std::runtime_error("Can't run SetNumberOfChannels() after audio has been initialized.");

    get()._numberChannels = std::clamp(newNumberOfChannels, 1, 8);
}
//*/