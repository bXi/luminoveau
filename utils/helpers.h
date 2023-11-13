#pragma once

#include <configuration/configuration.h>
#include <utils/vectors.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#define MAX_TEXT_BUFFER_LENGTH              1024

class Helpers {
public:
	static int clamp(int input, int min, int max);
	static float mapValues(float x, float in_min, float in_max, float out_min, float out_max);
	static void DrawMainMenu();
	static float getDifficultyModifier(float mod);

	static bool lineIntersectsRectangle(vf2d linestart, vf2d lineend, Rectangle rect);

	static std::vector<std::pair<vf2d, vf2d>> getLinesFromRectangle(Rectangle rect);

	static bool randomChance(const float required);
	static const char* TextFormat(const char *text, ...);

    static int GetRandomValue(int min, int max);

};
