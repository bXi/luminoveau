#pragma once

#include "enginestate/enginestate.h"
#include <utils/vectors.h>
#include <utils/rectangles.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <regex>

#ifndef _MSC_VER
#include <cxxabi.h>
#endif

#include <sstream>

#ifdef LUMINOVEAU_WITH_IMGUI

#include "imgui.h"

#endif

#define MAX_TEXT_BUFFER_LENGTH              1024

template<typename... T>
inline void LUMI_UNUSED(T&&...) {}

class Helpers {
public:
    static int clamp(int input, int min, int max);

    static float mapValues(float x, float in_min, float in_max, float out_min, float out_max);

    static void DrawMainMenu();

    static float getDifficultyModifier(float mod);

    static bool lineIntersectsRectangle(vf2d lineStart, vf2d lineEnd, rectf rect);

    static std::vector<std::pair<vf2d, vf2d>> getLinesFromRectangle(rectf rect);

    static bool randomChance(const float required);

    static const char *TextFormat(const char *text, ...);

    static int GetRandomValue(int min, int max);

    static uint64_t GetTotalSystemMemory();

    static bool imguiTexturesVisible;
    static bool imguiAudioVisible;
    static bool imguiInputVisible;
    static bool imguiDemoVisible;

    static std::string Slugify(std::string input);

    static time_t GetFileModificationTime(const std::string& filepath);



};
