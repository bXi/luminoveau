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

#ifdef ADD_IMGUI

#include "imgui.h"

#endif

#define MAX_TEXT_BUFFER_LENGTH              1024
#ifndef _MSC_VER
#define CURRENT_METHOD() ({ \
    static thread_local char _log_current_method_buffer[256]; \
    int _log_current_method_status; \
    char *_log_current_method_class_name = abi::__cxa_demangle(typeid(*this).name(), 0, 0, &_log_current_method_status); \
    snprintf(_log_current_method_buffer, sizeof(_log_current_method_buffer), "[Lumi] %s::%s", _log_current_method_class_name, __func__); \
    free(_log_current_method_class_name); \
    _log_current_method_buffer; \
})
#else
#define CURRENT_METHOD() __FUNCTION__
#endif

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



};
