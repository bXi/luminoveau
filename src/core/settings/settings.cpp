#include "settings.h"

void Settings::_setRes(int width, int height) {
    _resWidth  = width;
    _resHeight = height;

    SaveSettings();

    Window::SetSize(width, height);
}

void Settings::_toggleFullscreen() {
    _fullscreen = !_fullscreen;
    SaveSettings();
    Window::ToggleFullscreen();
    // TODO: fix vsync
}

void Settings::_toggleVsync() {
    _vsync = !_vsync;
    SaveSettings();
    // TODO: fix vsync
}

bool Settings::_getVsync() const {
    return _vsync;
}

int Settings::_getMonitorRefreshRate() const {
#ifdef __EMSCRIPTEN__
    return 60;
#endif
    auto currentDisplayMode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());

    return (int)currentDisplayMode->refresh_rate;
}

void Settings::_saveSettings() const {
    //	const mINI::INIFile file("settings.ini");
    //	mINI::INIStructure ini;
    //
    //	ini["Video"]["Vsync"] = (vsync) ? "true" : "false";
    //	ini["Video"]["Fullscreen"] = (fullscreen) ? "true" : "false";
    //	ini["Video"]["Width"] = Helpers::TextFormat("%i", resWidth);
    //	ini["Video"]["Height"] = Helpers::TextFormat("%i", resHeight);
    //
    //	ini["Audio"]["Mastervolume"] = Helpers::TextFormat("%i", static_cast<int>(masterVolume * 100));
    //	ini["Audio"]["Effectsvolume"] = Helpers::TextFormat("%i", static_cast<int>(effectsVolume * 100));
    //	ini["Audio"]["Musicvolume"] = Helpers::TextFormat("%i", static_cast<int>(musicVolume * 100));
    //
    //	file.write(ini);
}

void Settings::_init() {
    // first, create a file instance
    //	const mINI::INIFile file("settings.ini");
    //	mINI::INIStructure ini;
    //
    //	file.read(ini);

    //	if (!std::filesystem::exists("settings.ini")) {
    //		ini["Video"]["Vsync"] = (vsync) ? "true" : "false";
    //		ini["Video"]["Fullscreen"] = (fullscreen) ? "true" : "false";
    //		ini["Video"]["Width"] = Helpers::TextFormat("%i", resWidth);
    //		ini["Video"]["Height"] = Helpers::TextFormat("%i", resHeight);
    //
    //		ini["Audio"]["Mastervolume"] = Helpers::TextFormat("%i", static_cast<int>(masterVolume) * 100);
    //		ini["Audio"]["Effectsvolume"] = Helpers::TextFormat("%i", static_cast<int>(effectsVolume) * 100);
    //		ini["Audio"]["Musicvolume"] = Helpers::TextFormat("%i", static_cast<int>(musicVolume) * 100);
    //
    //	}
    //	else {
    //
    //		const std::string& vsync = ini["Video"]["Vsync"];
    //		const std::string& fullscreen = ini["Video"]["Fullscreen"];
    //		const std::string& resWidth = ini["Video"]["Width"];
    //		const std::string& resHeight = ini["Video"]["Height"];
    //
    //		const std::string& _mastervolume = ini["Audio"]["Mastervolume"];
    //		const std::string& _effectsvolume = ini["Audio"]["Effectsvolume"];
    //		const std::string& _musicvolume = ini["Audio"]["Musicvolume"];
    //
    //
    //		vsync = (vsync == "true");
    //		fullscreen = (fullscreen == "true");
    //
    //		resWidth = std::stoi(resWidth);
    //		resHeight = std::stoi(resHeight);
    //
    //		masterVolume = std::stof(_mastervolume) / 100.f;
    //		effectsVolume = std::stof(_effectsvolume) / 100.f;
    //		musicVolume = std::stof(_musicvolume) / 100.f;
    //	}

    // SetWindowSize(resWidth, resHeight);
    // SetWindowPosition(100, 100);
    // BeginDrawing();
    // ClearBackground(BLACK);
    // EndDrawing();

    //	file.write(ini);

    if (_fullscreen) {
        //		ToggleFullscreen();
        // BeginDrawing();
        // ClearBackground(BLACK);
        // EndDrawing();
    }
    // TODO: fix vsync
}

std::vector<std::pair<int, int>> Settings::_resolutions() {
    std::vector<std::pair<int, int>> list;

    list.push_back({ 1280, 720 });
    list.push_back({ 1920, 1080 });
    list.push_back({ 2560, 1440 });

    return list;
}

void Settings::_setMusicVolume(float volume) {

    _musicVolume = volume;
    _musicVolume = std::clamp(_musicVolume, 0.0f, 1.0f);

    SaveSettings();
}

void Settings::_setSoundVolume(float volume) {

    _effectsVolume = volume;
    _effectsVolume = std::clamp(_effectsVolume, 0.0f, 1.0f);

    SaveSettings();
}

void Settings::_setMasterVolume(float volume) {

    _masterVolume = volume;
    _masterVolume = std::clamp(_masterVolume, 0.0f, 1.0f);

    SaveSettings();
}

float Settings::_getMusicVolume() const {
    return _musicVolume;
}

float Settings::_getSoundVolume() const {
    return _effectsVolume;
}

float Settings::_getMasterVolume() const {
    return _masterVolume;
}
