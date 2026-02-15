#include "audiohandler.h"

#include "miniaudio.h"

#include "assethandler/assethandler.h"

// ═══════════════════════════════════════════════════════════════════
// miniaudio vtable callbacks (C-style, called on the audio thread)
// ═══════════════════════════════════════════════════════════════════

// ── Custom data source for PCM generators ──

static ma_result pcmDataSourceRead(ma_data_source* pDataSource, void* pFramesOut,
                                    ma_uint64 frameCount, ma_uint64* pFramesRead) {
    auto* ds = reinterpret_cast<LumiPCMDataSource*>(pDataSource);

    if (ds->callback) {
        ds->callback(static_cast<float*>(pFramesOut),
                     static_cast<uint32_t>(frameCount),
                     ds->channels, ds->userData);
    } else {
        // No callback — output silence
        memset(pFramesOut, 0, static_cast<size_t>(frameCount) * ds->channels * sizeof(float));
    }

    if (pFramesRead) *pFramesRead = frameCount;
    return MA_SUCCESS;
}

static ma_result pcmDataSourceSeek(ma_data_source* /*pDataSource*/, ma_uint64 /*frameIndex*/) {
    return MA_SUCCESS; // Generators are infinite streams, seek is a no-op
}

static ma_result pcmDataSourceGetDataFormat(ma_data_source* pDataSource, ma_format* pFormat,
                                             ma_uint32* pChannels, ma_uint32* pSampleRate,
                                             ma_channel* /*pChannelMap*/, size_t /*channelMapCap*/) {
    auto* ds = reinterpret_cast<LumiPCMDataSource*>(pDataSource);
    if (pFormat)     *pFormat     = ma_format_f32;
    if (pChannels)   *pChannels   = ds->channels;
    if (pSampleRate) *pSampleRate = ds->sampleRate;
    return MA_SUCCESS;
}

static ma_result pcmDataSourceGetCursor(ma_data_source* /*pDataSource*/, ma_uint64* pCursor) {
    if (pCursor) *pCursor = 0;
    return MA_SUCCESS;
}

static ma_result pcmDataSourceGetLength(ma_data_source* /*pDataSource*/, ma_uint64* pLength) {
    if (pLength) *pLength = 0; // Unknown / infinite
    return MA_SUCCESS;
}

ma_data_source_vtable Audio::pcmDataSourceVtable = {
    pcmDataSourceRead,
    pcmDataSourceSeek,
    pcmDataSourceGetDataFormat,
    pcmDataSourceGetCursor,
    pcmDataSourceGetLength
};

// ── Custom node for channel effects ──

static void effectNodeProcess(ma_node* pNode, const float** ppFramesIn,
                               ma_uint32* pFrameCountIn,
                               float** ppFramesOut, ma_uint32* pFrameCountOut) {
    auto* effect = reinterpret_cast<LumiEffectNode*>(pNode);

    ma_uint32 frameCount = *pFrameCountOut;
    if (pFrameCountIn != nullptr) {
        frameCount = (frameCount < pFrameCountIn[0]) ? frameCount : pFrameCountIn[0];
    }

    uint32_t sampleCount = frameCount * effect->channels;

    // Copy input → output
    if (ppFramesOut[0] != ppFramesIn[0]) {
        memcpy(ppFramesOut[0], ppFramesIn[0], sampleCount * sizeof(float));
    }

    // Apply user effect in-place on the output
    if (effect->callback) {
        effect->callback(ppFramesOut[0], frameCount, effect->channels, effect->userData);
    }

    *pFrameCountOut = frameCount;
    if (pFrameCountIn != nullptr) {
        pFrameCountIn[0] = frameCount;
    }
}

ma_node_vtable Audio::effectNodeVtable = {
    effectNodeProcess,
    nullptr,  // onGetRequiredInputFrameCount (use default)
    1,        // input bus count
    1,        // output bus count
    0         // flags
};

// ═══════════════════════════════════════════════════════════════════
// Helper
// ═══════════════════════════════════════════════════════════════════

static int channelIndex(AudioChannel channel) {
    return static_cast<int>(channel) - 1;
}

// ═══════════════════════════════════════════════════════════════════
// Sound playback
// ═══════════════════════════════════════════════════════════════════

void Audio::_playSound(Sound sound, AudioChannel channel) {
    if (ma_sound_is_playing(sound.sound)) {
        ma_sound_seek_to_pcm_frame(sound.sound, 0);
        return;
    }

    if (channel != AudioChannel::Master) {
        int idx = channelIndex(channel);
        if (idx >= 0 && idx < NUM_GROUPS && _channels[idx].initialized) {
            ma_node_attach_output_bus(sound.sound, 0, &_channels[idx].group, 0);
        }
    }

    ma_sound_set_looping(sound.sound, false);
    ma_sound_start(sound.sound);
}

void Audio::_playSound(Sound sound, float volume, float panning, AudioChannel channel) {
    int index = -1;

    for (unsigned int i = 0; i < _soundPool.size(); i++) {
        auto s = _soundPool[i];
        if (!s || !ma_sound_is_playing(s)) {
            index = i;
            break;
        }
    }

    if (index == -1) return;

    volume  = std::clamp(volume, 0.0f, 1.0f);
    panning = std::clamp(panning, -1.0f, 1.0f);

    if (_soundPool[index]) {
        ma_sound_uninit(_soundPool[index]);
        delete _soundPool[index];
    }

    ma_sound_group* group = nullptr;
    if (channel != AudioChannel::Master) {
        int idx = channelIndex(channel);
        if (idx >= 0 && idx < NUM_GROUPS && _channels[idx].initialized) {
            group = &_channels[idx].group;
        }
    }

    _soundPool[index] = new ma_sound;
    ma_sound_init_from_file(&engine, sound.fileName.c_str(),
                            MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC,
                            group, nullptr, _soundPool[index]);
    ma_sound_set_volume(_soundPool[index], volume);
    ma_sound_set_pan(_soundPool[index], panning);
    ma_sound_start(_soundPool[index]);
}

// ═══════════════════════════════════════════════════════════════════
// Music playback
// ═══════════════════════════════════════════════════════════════════

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

void Audio::_setMusicVolume(Music &music, float volume) {
    volume = std::clamp(volume, 0.0f, 1.0f);
    ma_sound_set_volume(music.music, volume);
}

void Audio::_rewindMusic(Music &music) {
    ma_sound_seek_to_pcm_frame(music.music, 0);
}

// ═══════════════════════════════════════════════════════════════════
// Channel control
// ═══════════════════════════════════════════════════════════════════

void Audio::_setChannelVolume(AudioChannel channel, float volume) {
    volume = std::clamp(volume, 0.0f, 1.0f);

    if (channel == AudioChannel::Master) {
        _masterVolume = volume;
        if (!_masterMuted) {
            ma_engine_set_volume(&engine, volume);
        }
        return;
    }

    int idx = channelIndex(channel);
    if (idx < 0 || idx >= NUM_GROUPS || !_channels[idx].initialized) return;

    _channels[idx].volume = volume;
    if (!_channels[idx].muted) {
        ma_sound_group_set_volume(&_channels[idx].group, volume);
    }
}

float Audio::_getChannelVolume(AudioChannel channel) {
    if (channel == AudioChannel::Master) return _masterVolume;

    int idx = channelIndex(channel);
    if (idx < 0 || idx >= NUM_GROUPS) return 0.0f;
    return _channels[idx].volume;
}

void Audio::_setChannelPanning(AudioChannel channel, float panning) {
    if (channel == AudioChannel::Master) return;

    panning = std::clamp(panning, -1.0f, 1.0f);

    int idx = channelIndex(channel);
    if (idx < 0 || idx >= NUM_GROUPS || !_channels[idx].initialized) return;

    _channels[idx].panning = panning;
    ma_sound_group_set_pan(&_channels[idx].group, panning);
}

float Audio::_getChannelPanning(AudioChannel channel) {
    if (channel == AudioChannel::Master) return 0.0f;

    int idx = channelIndex(channel);
    if (idx < 0 || idx >= NUM_GROUPS) return 0.0f;
    return _channels[idx].panning;
}

void Audio::_muteChannel(AudioChannel channel, bool muted) {
    if (channel == AudioChannel::Master) {
        _masterMuted = muted;
        ma_engine_set_volume(&engine, muted ? 0.0f : _masterVolume);
        return;
    }

    int idx = channelIndex(channel);
    if (idx < 0 || idx >= NUM_GROUPS || !_channels[idx].initialized) return;

    _channels[idx].muted = muted;
    ma_sound_group_set_volume(&_channels[idx].group, muted ? 0.0f : _channels[idx].volume);
}

bool Audio::_isChannelMuted(AudioChannel channel) {
    if (channel == AudioChannel::Master) return _masterMuted;

    int idx = channelIndex(channel);
    if (idx < 0 || idx >= NUM_GROUPS) return false;
    return _channels[idx].muted;
}

ma_sound_group* Audio::_getChannelGroup(AudioChannel channel) {
    if (channel == AudioChannel::Master) return nullptr;

    int idx = channelIndex(channel);
    if (idx < 0 || idx >= NUM_GROUPS || !_channels[idx].initialized) return nullptr;
    return &_channels[idx].group;
}

// ═══════════════════════════════════════════════════════════════════
// PCM generators
// ═══════════════════════════════════════════════════════════════════

PCMSound Audio::_createPCMGenerator(const PCMFormat& format, PCMGenerateCallback callback, void* userData) {
    PCMSound pcm{};

    // Set up the custom data source
    pcm.dataSource.callback   = callback;
    pcm.dataSource.userData   = userData;
    pcm.dataSource.channels   = format.channels;
    pcm.dataSource.sampleRate = format.sampleRate;

    ma_data_source_config dsConfig = ma_data_source_config_init();
    dsConfig.vtable = &pcmDataSourceVtable;

    ma_result result = ma_data_source_init(&dsConfig, &pcm.dataSource.base);
    if (result != MA_SUCCESS) {
        LOG_CRITICAL("Failed to init PCM data source");
        return pcm;
    }

    // Create a ma_sound from the data source (don't start yet)
    result = ma_sound_init_from_data_source(&engine, &pcm.dataSource.base,
                                            MA_SOUND_FLAG_NO_SPATIALIZATION,
                                            nullptr, &pcm.sound);
    if (result != MA_SUCCESS) {
        ma_data_source_uninit(&pcm.dataSource.base);
        LOG_CRITICAL("Failed to init PCM sound from data source");
        return pcm;
    }

    pcm.initialized = true;
    return pcm;
}

void Audio::_playPCMSound(PCMSound& sound, AudioChannel channel) {
    if (!sound.initialized) return;

    // Route through the requested channel group
    if (channel != AudioChannel::Master) {
        int idx = channelIndex(channel);
        if (idx >= 0 && idx < NUM_GROUPS && _channels[idx].initialized) {
            ma_node_attach_output_bus(&sound.sound, 0, &_channels[idx].group, 0);
        }
    }

    ma_sound_set_looping(&sound.sound, true); // Generators run continuously
    ma_sound_start(&sound.sound);
}

void Audio::_stopPCMSound(PCMSound& sound) {
    if (!sound.initialized) return;
    ma_sound_stop(&sound.sound);
}

void Audio::_destroyPCMSound(PCMSound& sound) {
    if (!sound.initialized) return;

    if (ma_sound_is_playing(&sound.sound)) {
        ma_sound_stop(&sound.sound);
    }

    ma_sound_uninit(&sound.sound);
    ma_data_source_uninit(&sound.dataSource.base);
    sound.initialized = false;
}

// ═══════════════════════════════════════════════════════════════════
// Channel effects
// ═══════════════════════════════════════════════════════════════════

void Audio::_setChannelEffect(AudioChannel channel, PCMEffectCallback callback, void* userData) {
    // Master effect — applied in the device data callback, no node graph needed
    if (channel == AudioChannel::Master) {
        _masterEffectCallback = callback;
        _masterEffectUserData = userData;
        return;
    }

    int idx = channelIndex(channel);
    if (idx < 0 || idx >= NUM_GROUPS || !_channels[idx].initialized) return;

    auto& ch = _channels[idx];

    // If there's already an effect node, tear it down first
    if (ch.effectNode.initialized) {
        _removeChannelEffect(channel);
    }

    // Set up the effect node
    ch.effectNode.callback = callback;
    ch.effectNode.userData = userData;
    ch.effectNode.channels = static_cast<uint32_t>(_numberChannels);

    uint32_t channelCount = ch.effectNode.channels;

    ma_node_config nodeConfig = ma_node_config_init();
    nodeConfig.vtable          = &effectNodeVtable;
    nodeConfig.inputBusCount   = 1;
    nodeConfig.outputBusCount  = 1;
    nodeConfig.pInputChannels  = &channelCount;
    nodeConfig.pOutputChannels = &channelCount;

    ma_result result = ma_node_init(ma_engine_get_node_graph(&engine),
                                    &nodeConfig, nullptr, &ch.effectNode.base);
    if (result != MA_SUCCESS) {
        LOG_WARNING("Failed to init effect node for channel {}", idx);
        return;
    }

    ch.effectNode.initialized = true;

    // Re-route: group → effect node → engine endpoint
    ma_node* endpoint = ma_engine_get_endpoint(&engine);
    ma_node_detach_output_bus(&ch.group, 0);
    ma_node_attach_output_bus(&ch.group, 0, &ch.effectNode.base, 0);
    ma_node_attach_output_bus(&ch.effectNode.base, 0, endpoint, 0);
}

void Audio::_removeChannelEffect(AudioChannel channel) {
    if (channel == AudioChannel::Master) {
        _masterEffectCallback = nullptr;
        _masterEffectUserData = nullptr;
        return;
    }

    int idx = channelIndex(channel);
    if (idx < 0 || idx >= NUM_GROUPS || !_channels[idx].initialized) return;

    auto& ch = _channels[idx];
    if (!ch.effectNode.initialized) return;

    // Re-route: group → engine endpoint (bypass effect)
    ma_node* endpoint = ma_engine_get_endpoint(&engine);
    ma_node_detach_output_bus(&ch.group, 0);
    ma_node_detach_output_bus(&ch.effectNode.base, 0);
    ma_node_attach_output_bus(&ch.group, 0, endpoint, 0);

    ma_node_uninit(&ch.effectNode.base, nullptr);
    ch.effectNode.initialized = false;
    ch.effectNode.callback = nullptr;
    ch.effectNode.userData = nullptr;
}

// ═══════════════════════════════════════════════════════════════════
// Engine lifecycle
// ═══════════════════════════════════════════════════════════════════

void Audio::ma_data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount) {
    LUMI_UNUSED(pInput);

    auto& audio = Audio::get();
    ma_engine_read_pcm_frames(&audio.engine, pOutput, frameCount, nullptr);

    // Apply master effect if one is set
    if (audio._masterEffectCallback) {
        audio._masterEffectCallback(static_cast<float*>(pOutput), frameCount,
                                    static_cast<uint32_t>(audio._numberChannels),
                                    audio._masterEffectUserData);
    }
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

    // Initialize mix channel groups (SFX, Voice, Music)
    for (int i = 0; i < NUM_GROUPS; i++) {
        ma_result groupResult = ma_sound_group_init(&engine, 0, nullptr, &_channels[i].group);
        if (groupResult == MA_SUCCESS) {
            _channels[i].initialized = true;
        } else {
            LOG_WARNING("Failed to initialize audio channel group {}", i);
        }
    }

    // Initialize polyphonic sound pool
    for (size_t i = 0; i < _soundPool.size(); ++i) {
        _soundPool[i] = nullptr;
    }
}

void Audio::_close() {
    if (!audioInit) {
        return;
    }

    _stopMusic();

    // Remove all channel effects
    for (int i = 0; i < NUM_GROUPS; i++) {
        if (_channels[i].effectNode.initialized) {
            AudioChannel ch = static_cast<AudioChannel>(i + 1);
            _removeChannelEffect(ch);
        }
    }
    _masterEffectCallback = nullptr;
    _masterEffectUserData = nullptr;

    // Clean up polyphonic sound pool
    for (size_t i = 0; i < _soundPool.size(); ++i) {
        if (_soundPool[i]) {
            if (ma_sound_is_playing(_soundPool[i])) {
                ma_sound_stop(_soundPool[i]);
            }
            ma_sound_uninit(_soundPool[i]);
            delete _soundPool[i];
            _soundPool[i] = nullptr;
        }
    }

    // Uninitialize channel groups
    for (int i = 0; i < NUM_GROUPS; i++) {
        if (_channels[i].initialized) {
            ma_sound_group_uninit(&_channels[i].group);
            _channels[i].initialized = false;
        }
    }

    ma_resource_manager_uninit(&resourceManager);
    ma_engine_uninit(&engine);

    audioInit = false;
}

void Audio::_setNumberOfChannels(int newNumberOfChannels) {
    if (get().audioInit)
        LOG_CRITICAL("can't run SetNumberOfChannels() after audio has been initialized");

    get()._numberChannels = std::clamp(newNumberOfChannels, 1, 8);
}
//*/
