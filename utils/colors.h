#pragma once

#if __has_include("SDL3/SDL.h")

#include "SDL3/SDL.h"

#endif

/**
 * @brief Represents a color with red, green, blue, and alpha components.
 */
struct Color {
    unsigned int r; /**< The red component of the color. */
    unsigned int g; /**< The green component of the color. */
    unsigned int b; /**< The blue component of the color. */
    unsigned int a; /**< The alpha component of the color. */

    /**
     * @brief Constructs a color from integer values.
     *
     * @param red The red component (0-255).
     * @param green The green component (0-255).
     * @param blue The blue component (0-255).
     * @param alpha The alpha component (0-255).
     */
    Color(unsigned int red, unsigned int green, unsigned int blue, unsigned int alpha)
        : r(red), g(green), b(blue), a(alpha) {}

    /**
     * @brief Constructs a color from a 32-bit color code.
     *
     * @param colorCode The color code in the format 0xAARRGGBB.
     */
    Color(uint32_t colorCode) {
        r = (colorCode >> 24) & 0xFF; // Extract Red component
        g = (colorCode >> 16) & 0xFF; // Extract Green component
        b = (colorCode >> 8) & 0xFF;  // Extract Blue component
        a = colorCode & 0xFF;         // Extract Alpha component
    }

    /**
     * @brief Creates a color from floating-point values.
     *
     * @param red The red component (0.0 - 1.0).
     * @param green The green component (0.0 - 1.0).
     * @param blue The blue component (0.0 - 1.0).
     * @param alpha The alpha component (0.0 - 1.0).
     */
    void CreateFromFloats(float red, float green, float blue, float alpha) {
        r = static_cast<unsigned int>(red * 255.0f);
        g = static_cast<unsigned int>(green * 255.0f);
        b = static_cast<unsigned int>(blue * 255.0f);
        a = static_cast<unsigned int>(alpha * 255.0f);
    }

    /**
     * @brief Gets the red component as a floating-point value.
     *
     * @return The red component (0.0 - 1.0).
     */
    float getRFloat() const { return static_cast<float>(r) / 255.0f; }

    /**
     * @brief Gets the green component as a floating-point value.
     *
     * @return The green component (0.0 - 1.0).
     */
    float getGFloat() const { return static_cast<float>(g) / 255.0f; }

    /**
     * @brief Gets the blue component as a floating-point value.
     *
     * @return The blue component (0.0 - 1.0).
     */
    float getBFloat() const { return static_cast<float>(b) / 255.0f; }

    /**
     * @brief Gets the alpha component as a floating-point value.
     *
     * @return The alpha component (0.0 - 1.0).
     */
    float getAFloat() const { return static_cast<float>(a) / 255.0f; }
};
#if __has_include("SDL3/SDL.h")

    operator SDL_Color() { return SDL_Color({ (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a }); }

#endif
};

static inline Color RED = {255, 0, 0, 255};
static inline Color BLACK = {0, 0, 0, 255};
static inline Color WHITE = {255, 255, 255, 255};
static inline Color BLUE = {0, 0, 255, 255};
static inline Color GREEN = {0, 255, 0, 255};
static inline Color YELLOW = {255, 255, 0, 255};
static inline Color PURPLE = {255, 0, 255, 255};
static inline Color PINK = {255, 0, 127, 255};

static inline Color DARKGREEN = {0, 128, 0, 255};
static inline Color DARKRED = {128, 0, 0, 255};

static inline Color GRAY = {128, 128, 128, 255};
static inline Color DARKGRAY = {64, 64, 64, 255};
static inline Color LIME = {50, 205, 50, 255};

static inline Color BROWN = {73, 54, 28, 255};
