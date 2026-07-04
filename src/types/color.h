#pragma once

#if __has_include("SDL3/SDL.h")
#include "SDL3/SDL.h"
#endif

#if __has_include("glm/vec4.hpp")
#include "glm/vec4.hpp"
#endif

/**
 * @brief An 8-bit-per-channel RGBA color.
 *
 * Channels are stored as 0-255 integer components. Helpers convert to the 0.0-1.0
 * float range (and to glm::vec4 / SDL color types) that the GPU and SDL expect.
 */
struct Color {
    unsigned int r; ///< Red channel, 0-255.
    unsigned int g; ///< Green channel, 0-255.
    unsigned int b; ///< Blue channel, 0-255.
    unsigned int a; ///< Alpha channel, 0-255 (0 = transparent, 255 = opaque).

    /// @brief Constructs a fully transparent black (all channels 0).
    constexpr Color()
        : r(0), g(0), b(0), a(0) {}

    /**
     * @brief Constructs a color from individual 0-255 channel values.
     *
     * @param red   Red channel, 0-255.
     * @param green Green channel, 0-255.
     * @param blue  Blue channel, 0-255.
     * @param alpha Alpha channel, 0-255.
     */
    constexpr Color(unsigned int red, unsigned int green, unsigned int blue, unsigned int alpha)
        : r(red), g(green), b(blue), a(alpha) {}

    /**
     * @brief Constructs a color from a packed 0xRRGGBBAA hex code.
     *
     * @param colorCode Packed color, alpha in the low byte, e.g. 0xFF0000FF is opaque red.
     */
    constexpr Color(uint32_t colorCode)
        : r((colorCode >> 24) & 0xFF)
        , g((colorCode >> 16) & 0xFF)
        , b((colorCode >>  8) & 0xFF)
        , a( colorCode        & 0xFF) {}

    /**
     * @brief Sets all channels from 0.0-1.0 float values.
     *
     * @param red   Red channel, 0.0-1.0.
     * @param green Green channel, 0.0-1.0.
     * @param blue  Blue channel, 0.0-1.0.
     * @param alpha Alpha channel, 0.0-1.0.
     */
    void CreateFromFloats(float red, float green, float blue, float alpha) {
        r = static_cast<unsigned int>(red   * 255.0f);
        g = static_cast<unsigned int>(green * 255.0f);
        b = static_cast<unsigned int>(blue  * 255.0f);
        a = static_cast<unsigned int>(alpha * 255.0f);
    }

    /// @brief Returns the red channel as a 0.0-1.0 float.
    [[nodiscard]] float getRFloat() const { return static_cast<float>(r) / 255.0f; }
    /// @brief Returns the green channel as a 0.0-1.0 float.
    [[nodiscard]] float getGFloat() const { return static_cast<float>(g) / 255.0f; }
    /// @brief Returns the blue channel as a 0.0-1.0 float.
    [[nodiscard]] float getBFloat() const { return static_cast<float>(b) / 255.0f; }
    /// @brief Returns the alpha channel as a 0.0-1.0 float.
    [[nodiscard]] float getAFloat() const { return static_cast<float>(a) / 255.0f; }

#if __has_include("glm/vec4.hpp")
    /// @brief Returns the color as a glm::vec4 of 0.0-1.0 components (RGBA).
    glm::vec4 asVec4() {
        return { getRFloat(), getGFloat(), getBFloat(), getAFloat() };
    }
#endif

#if __has_include("SDL3/SDL.h")
    /// @brief Converts to an SDL_Color (8-bit 0-255 channels).
    explicit operator SDL_Color()  const { return SDL_Color ({(Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a}); }
    /// @brief Converts to an SDL_FColor (0.0-1.0 float channels).
    explicit operator SDL_FColor() const { return SDL_FColor({getRFloat(), getGFloat(), getBFloat(), getAFloat()}); }
#endif
};
