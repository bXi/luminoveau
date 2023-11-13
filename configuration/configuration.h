#pragma once

#include <random>
#include <iostream>

class Configuration {
public:

	inline static bool showGameStats = false;
	inline static bool showFPS = false;
	inline static bool showTargetBeam = false;
	inline static bool showDebugMenu = false;

	inline static int tileWidth = 128;
	inline static int tileHeight = 128;

	inline static double gameTime = 0.0;
	inline static float slowMotionFactor = 1.0f;


private:

public:
	Configuration(const Configuration&) = delete;
	static Configuration& get() { static Configuration instance; return instance; }

private:
	Configuration() {}
};
