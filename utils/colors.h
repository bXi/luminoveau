#pragma once

#if __has_include("SDL3/SDL.h")
#include "SDL3/SDL.h"
#endif


struct Color {
    unsigned int r;
    unsigned int g;
    unsigned int b;
    unsigned int a;

    // Constructor that takes integer values
    Color(unsigned int red, unsigned int green, unsigned int blue, unsigned int alpha)
        : r(red), g(green), b(blue), a(alpha) {}

    // Constructor that takes floating-point values
    void CreateFromFloats(float red, float green, float blue, float alpha) {
        r = static_cast<unsigned int>(red * 255.0f);
                g = static_cast<unsigned int>(green * 255.0f);
                b = static_cast<unsigned int>(blue * 255.0f);
                a = static_cast<unsigned int>(alpha * 255.0f);
    }

    // Conversion methods from integer to float
    float getRFloat() const { return static_cast<float>(r) / 255.0f; }
    float getGFloat() const { return static_cast<float>(g) / 255.0f; }
    float getBFloat() const { return static_cast<float>(b) / 255.0f; }
    float getAFloat() const { return static_cast<float>(a) / 255.0f; }

    #if __has_include("SDL3/SDL.h")
    operator SDL_Color() { return SDL_Color(r,g,b,a); }
    #endif
};
static inline Color RED = {255, 0, 0, 255};
static inline Color BLACK = {0, 0, 0, 255};
static inline Color WHITE = {255, 255, 255, 255};
static inline Color BLUE  = {0,0,255,255};
static inline Color GREEN = {0,255,0,255};
static inline Color YELLOW = {255,255,0,255};
static inline Color PURPLE = {255, 0, 255, 255};
static inline Color PINK = {255, 0, 127, 255};

static inline Color DARKGREEN = {0, 128, 0, 255};

static inline Color GRAY = {128,128,128,255};
static inline Color LIME = {50, 205, 50,255};

static inline Color BROWN = {73, 54, 28,255};
