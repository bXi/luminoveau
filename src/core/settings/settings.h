#pragma once

#include <random>
#include <iostream>

#include <filesystem>


#include "platform/window/window.h"
#include "util/helpers.h"
//#include "mini.h"

/// @brief Application settings: resolution, fullscreen, vsync and audio volumes, with save/load.
class Settings {
public:

    /// @brief Sets the window resolution.
    /// @param width Width in pixels.
    /// @param height Height in pixels.
    static void setRes(int width, int height) { get()._setRes(width, height); }
    /// @brief Toggles fullscreen mode.
    static void ToggleFullscreen() { get()._toggleFullscreen(); }
    /// @brief Toggles vertical sync.
    static void toggleVsync() { get()._toggleVsync(); }
    /// @brief Returns whether vsync is currently enabled.
    static bool getVsync() { return get()._getVsync(); }
    /// @brief Persists the current settings to disk.
    static void saveSettings() { get()._saveSettings(); }
    /// @brief Loads settings from disk (or defaults) and applies them.
    static void Init() { get()._init(); }
    /// @brief Returns the list of supported (width, height) resolutions.
    static std::vector<std::pair<int, int>> resolutions() { return get()._resolutions(); }
    /// @brief Sets the music volume (0.0–1.0).
    static void setMusicVolume(float volume) { get()._setMusicVolume(volume); }
    /// @brief Sets the sound-effects volume (0.0–1.0).
    static void setSoundVolume(float volume) { get()._setSoundVolume(volume); }
    /// @brief Sets the master volume (0.0–1.0).
    static void setMasterVolume(float volume) { get()._setMasterVolume(volume); }
    /// @brief Returns the music volume (0.0–1.0).
    static float getMusicVolume() { return get()._getMusicVolume(); }
    /// @brief Returns the sound-effects volume (0.0–1.0).
    static float getSoundVolume() { return get()._getSoundVolume(); }
    /// @brief Returns the master volume (0.0–1.0).
    static float getMasterVolume() { return get()._getMasterVolume(); }
    /// @brief Returns the monitor's refresh rate in Hz.
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
    /// @cond INTERNAL
    Settings(const Settings&) = delete;
    static Settings& get() { static Settings instance; return instance; }
    /// @endcond
private:
    Settings() {}
    ;
};
