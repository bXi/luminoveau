#pragma once

#include <random>
#include <iostream>

#include <filesystem>


#include "window/windowhandler.h"
#include "utils/helpers.h"
//#include "mini.h"

class Settings {
public:

    static void setRes(int width, int height) { get()._setRes(width, height); }
    static void toggleFullscreen() { get()._toggleFullscreen(); }
    static void toggleVsync() { get()._toggleVsync(); }
    static bool getVsync() { return get()._getVsync(); }
    static void saveSettings() { get()._saveSettings(); }
    static void Init() { get()._init(); }
    static std::vector<std::pair<int, int>> resolutions() { return get()._resolutions(); }
    static void setMusicVolume(float volume) { get()._setMusicVolume(volume); }
    static void setSoundVolume(float volume) { get()._setSoundVolume(volume); }
    static void setMasterVolume(float volume) { get()._setMasterVolume(volume); }
    static float getMusicVolume() { return get()._getMusicVolume(); }
    static float getSoundVolume() { return get()._getSoundVolume(); }
    static float getMasterVolume() { return get()._getMasterVolume(); }
    static int getMonitorRefreshRate() { return get()._getMonitorRefreshRate(); }

private:
    bool vsync = true;
    bool fullscreen = false;
    int resWidth = 1280;
    int resHeight = 720;

    //audio
    float masterVolume = 1.0f;
    float effectsVolume = 1.0f;
    float musicVolume = 1.0f;

    void _setRes(int width, int height);
    void _toggleFullscreen();
    void _toggleVsync();
    bool _getVsync() const;
    void _saveSettings() const;
    void _init();
    static std::vector<std::pair<int, int>> _resolutions();

    void _setMusicVolume(float volume);
    void _setSoundVolume(float volume);
    void _setMasterVolume(float volume);

    float _getMusicVolume() const;
    float _getSoundVolume() const;
    float _getMasterVolume() const;

    int _getMonitorRefreshRate() const;

public:
    Settings(const Settings&) = delete;
    static Settings& get() { static Settings instance; return instance; }
private:
    Settings() {}
    ;
};

