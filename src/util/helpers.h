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
    /**
     * @brief Returns a sensible default thread count for parallelizable work.
     *
     * Used for e.g. MSDF atlas generation and texture decompression. Returns 1 on
     * Emscripten without -pthread (can't spawn std::thread); elsewhere scales toward
     * hardware concurrency, capped to avoid oversubscribing many-core machines.
     */
    inline unsigned int DefaultThreadCount() {
#ifdef __EMSCRIPTEN__
        return 1u;
#else
        return 8u;
#endif
    }
}

/// @brief Assorted math, random, geometry and string utility helpers.
class Helpers {
public:
    /// @brief Clamps an integer to the inclusive [min, max] range.
    static int clamp(int input, int min, int max);

    /// @brief Linearly remaps x from the [in_min, in_max] range to [out_min, out_max].
    static float mapValues(float x, float in_min, float in_max, float out_min, float out_max);

    /// @brief Returns a scaled difficulty modifier from a base value.
    static float getDifficultyModifier(float mod);

    /// @brief Returns true if the line segment intersects the rectangle.
    static bool lineIntersectsRectangle(vf2d lineStart, vf2d lineEnd, rectf rect);

    /// @brief Returns the four edges of a rectangle as start/end point pairs.
    static std::vector<std::pair<vf2d, vf2d>> getLinesFromRectangle(rectf rect);

    /// @brief Returns true with probability `required` (0.0–1.0).
    static bool randomChance(const float required);

    /// @brief printf-style formats text into a rotating static buffer (raylib-style).
    static const char *TextFormat(const char *text, ...);

    /// @brief Returns a random integer in the inclusive [min, max] range.
    static int GetRandomValue(int min, int max);

    /// @brief Returns total system RAM in bytes.
    static uint64_t GetTotalSystemMemory();

    /// @brief Converts a string to a lowercase, hyphenated slug.
    static std::string Slugify(std::string input);

    /// @brief Returns a file's last-modification time (0 if it doesn't exist).
    static time_t GetFileModificationTime(const std::string& filepath);
};
