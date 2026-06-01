#pragma once

#include "core/enginestate/enginestate.h"
#include "math/vectors.h"
#include "math/rectangles.h"

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

namespace Platform {
    // Sensible default thread count for parallelizable work (e.g., MSDF atlas generation,
    // texture decompression). Emscripten without -pthread can't spawn std::thread, so
    // callers must use 1 there; everywhere else, scale to roughly hardware concurrency
    // capped at a reasonable upper bound so we don't drown a many-core box in oversubscription.
    inline unsigned int DefaultThreadCount() {
#ifdef __EMSCRIPTEN__
        return 1u;
#else
        return 8u;
#endif
    }
}

class Helpers {
public:
    static int clamp(int input, int min, int max);

    static float mapValues(float x, float in_min, float in_max, float out_min, float out_max);

    static float getDifficultyModifier(float mod);

    static bool lineIntersectsRectangle(vf2d lineStart, vf2d lineEnd, rectf rect);

    static std::vector<std::pair<vf2d, vf2d>> getLinesFromRectangle(rectf rect);

    static bool randomChance(const float required);

    static const char *TextFormat(const char *text, ...);

    static int GetRandomValue(int min, int max);

    static uint64_t GetTotalSystemMemory();

    static std::string Slugify(std::string input);

    static time_t GetFileModificationTime(const std::string& filepath);



};
