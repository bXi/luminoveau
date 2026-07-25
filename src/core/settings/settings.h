#pragma once

#include <random>
#include <iostream>

#include <filesystem>

#include "platform/window/window.h"
#include "util/helpers.h"
// #include "mini.h"

/// @brief Application settings: resolution, fullscreen, vsync and audio volumes, with save/load.
class Settings {
public:
    /// @brief Sets the window resolution.
    /// @param width Width in pixels.
    /// @param height Height in pixels.
    static void SetRes(int width, int height) { Get()._setRes(width, height); }
    /// @brief Toggles fullscreen mode.
    static void ToggleFullscreen() { Get()._toggleFullscreen(); }
    /// @brief Toggles vertical sync.
    static void ToggleVsync() { Get()._toggleVsync(); }
    /// @brief Returns whether vsync is currently enabled.
    static bool GetVsync() { return Get()._getVsync(); }
    /// @brief Persists the current settings to disk.
    static void SaveSettings() { Get()._saveSettings(); }
    /// @brief Loads settings from disk (or defaults) and applies them.
    static void Init() { Get()._init(); }
    /// @brief Returns the list of supported (width, height) resolutions.
    static std::vector<std::pair<int, int>> Resolutions() { return Get()._resolutions(); }
    /// @brief Sets the music volume (0.0–1.0).
    static void SetMusicVolume(float volume) { Get()._setMusicVolume(volume); }
    /// @brief Sets the sound-effects volume (0.0–1.0).
    static void SetSoundVolume(float volume) { Get()._setSoundVolume(volume); }
    /// @brief Sets the master volume (0.0–1.0).
    static void SetMasterVolume(float volume) { Get()._setMasterVolume(volume); }
    /// @brief Returns the music volume (0.0–1.0).
    static float GetMusicVolume() { return Get()._getMusicVolume(); }
    /// @brief Returns the sound-effects volume (0.0–1.0).
    static float GetSoundVolume() { return Get()._getSoundVolume(); }
    /// @brief Returns the master volume (0.0–1.0).
    static float GetMasterVolume() { return Get()._getMasterVolume(); }
    /// @brief Returns the monitor's refresh rate in Hz.
    static int GetMonitorRefreshRate() { return Get()._getMonitorRefreshRate(); }

private:
    bool _vsync      = true;
    bool _fullscreen = false;
    int  _resWidth   = 1280;
    int  _resHeight  = 720;

    // audio
    float _masterVolume  = 1.0f;
    float _effectsVolume = 1.0f;
    float _musicVolume   = 1.0f;

    void                                    _setRes(int width, int height);
    void                                    _toggleFullscreen();
    void                                    _toggleVsync();
    bool                                    _getVsync() const;
    void                                    _saveSettings() const;
    void                                    _init();
    static std::vector<std::pair<int, int>> _resolutions(); // NOLINT(readability-identifier-naming) — private static; clang-tidy files statics as ClassMethod

    void _setMusicVolume(float volume);
    void _setSoundVolume(float volume);
    void _setMasterVolume(float volume);

    float _getMusicVolume() const;
    float _getSoundVolume() const;
    float _getMasterVolume() const;

    int _getMonitorRefreshRate() const;

public:
    /// @cond INTERNAL
    Settings(const Settings &) = delete;
    static Settings &Get() {
        static Settings instance;
        return instance;
    }
    /// @endcond
private:
    Settings() {};
};
