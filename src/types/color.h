#pragma once

#if __has_include("SDL3/SDL.h")
#include "SDL3/SDL.h"
#endif

#if __has_include("glm/vec4.hpp")
#include "glm/vec4.hpp"
#endif

struct Color {
    unsigned int r;
    unsigned int g;
    unsigned int b;
    unsigned int a;

    constexpr Color()
        : r(0), g(0), b(0), a(0) {}

    constexpr Color(unsigned int red, unsigned int green, unsigned int blue, unsigned int alpha)
        : r(red), g(green), b(blue), a(alpha) {}

    constexpr Color(uint32_t colorCode)
        : r((colorCode >> 24) & 0xFF)
        , g((colorCode >> 16) & 0xFF)
        , b((colorCode >>  8) & 0xFF)
        , a( colorCode        & 0xFF) {}

    void CreateFromFloats(float red, float green, float blue, float alpha) {
        r = static_cast<unsigned int>(red   * 255.0f);
        g = static_cast<unsigned int>(green * 255.0f);
        b = static_cast<unsigned int>(blue  * 255.0f);
        a = static_cast<unsigned int>(alpha * 255.0f);
    }

    [[nodiscard]] float getRFloat() const { return static_cast<float>(r) / 255.0f; }
    [[nodiscard]] float getGFloat() const { return static_cast<float>(g) / 255.0f; }
    [[nodiscard]] float getBFloat() const { return static_cast<float>(b) / 255.0f; }
    [[nodiscard]] float getAFloat() const { return static_cast<float>(a) / 255.0f; }

#if __has_include("glm/vec4.hpp")
    glm::vec4 asVec4() {
        return { getRFloat(), getGFloat(), getBFloat(), getAFloat() };
    }
#endif

#if __has_include("SDL3/SDL.h")
    explicit operator SDL_Color()  const { return SDL_Color ({(Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a}); }
    explicit operator SDL_FColor() const { return SDL_FColor({getRFloat(), getGFloat(), getBFloat(), getAFloat()}); }
#endif
};
