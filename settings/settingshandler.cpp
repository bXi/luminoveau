#include "settingshandler.h"

void Settings::_setRes(int width, int height)
{
	resWidth = width;
	resHeight = height;

	saveSettings();

	Window::SetSize(width, height);
}

void Settings::_toggleFullscreen()
{
	fullscreen = !fullscreen;
	saveSettings();
	Window::ToggleFullscreen();

    SDL_SetRenderVSync(Window::GetRenderer(), vsync);


}

void Settings::_toggleVsync()
{
	vsync = !vsync;
	saveSettings();

    SDL_SetRenderVSync(Window::GetRenderer(), vsync);

}

bool Settings::_getVsync() const
{
	return vsync;
}

int Settings::_getMonitorRefreshRate() const
{
#ifdef __EMSCRIPTEN__
    return 60;
#endif
    auto currentDisplayMode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());

	return (int)currentDisplayMode->refresh_rate;
}

void Settings::_saveSettings() const
{
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

void Settings::_init()
{
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
//		const std::string& _vsync = ini["Video"]["Vsync"];
//		const std::string& _fullscreen = ini["Video"]["Fullscreen"];
//		const std::string& _resWidth = ini["Video"]["Width"];
//		const std::string& _resHeight = ini["Video"]["Height"];
//
//		const std::string& _mastervolume = ini["Audio"]["Mastervolume"];
//		const std::string& _effectsvolume = ini["Audio"]["Effectsvolume"];
//		const std::string& _musicvolume = ini["Audio"]["Musicvolume"];
//
//
//		vsync = (_vsync == "true");
//		fullscreen = (_fullscreen == "true");
//
//		resWidth = std::stoi(_resWidth);
//		resHeight = std::stoi(_resHeight);
//
//		masterVolume = std::stof(_mastervolume) / 100.f;
//		effectsVolume = std::stof(_effectsvolume) / 100.f;
//		musicVolume = std::stof(_musicvolume) / 100.f;
//	}

	//SetWindowSize(resWidth, resHeight);
	//SetWindowPosition(100, 100);
	//BeginDrawing();
	//ClearBackground(BLACK);
	//EndDrawing();

//	file.write(ini);



	if (fullscreen) {
//		ToggleFullscreen();
		//BeginDrawing();
		//ClearBackground(BLACK);
		//EndDrawing();
	}

    SDL_SetRenderVSync(Window::GetRenderer(), vsync);



}

std::vector<std::pair<int, int>> Settings::_resolutions()
{
	std::vector<std::pair<int, int>> list;



    list.push_back({1280,720});
    list.push_back({1920,1080});
    list.push_back({2560,1440});

	return list;

}

void Settings::_setMusicVolume(float volume)
{

	musicVolume = volume;
	musicVolume = std::clamp(musicVolume, 0.0f, 1.0f);

	saveSettings();
}

void Settings::_setSoundVolume(float volume)
{

	effectsVolume = volume;
	effectsVolume = std::clamp(effectsVolume, 0.0f, 1.0f);

	saveSettings();
}

void Settings::_setMasterVolume(float volume)
{

	masterVolume = volume;
	masterVolume = std::clamp(masterVolume, 0.0f, 1.0f);

	saveSettings();
}

float Settings::_getMusicVolume() const
{
	return musicVolume;
}

float Settings::_getSoundVolume() const
{
	return effectsVolume;
}

float Settings::_getMasterVolume() const
{
	return masterVolume;
}