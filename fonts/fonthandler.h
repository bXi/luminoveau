#pragma once

#include <unordered_map>
#include "configuration/configuration.h"
#include "window/windowhandler.h"

#include "utils/helpers.h"
#include "utils/colors.h"

#include "SDL3_ttf/SDL_ttf.h"
#include "assettypes/font.h"

/**
 * @brief Provides functionality for managing fonts and rendering text.
 */
class Fonts {
public:
    /**
     * @brief Retrieves a font asset with the specified filename and font size.
     *
     * @param fileName The filename of the font asset.
     * @param fontSize The size of the font.
     * @return The font asset.
     */
    static Font GetFont(const char *fileName, const int fontSize) {
        return get()._getFont(fileName, fontSize);
    }

    /**
     * @brief Draws text using the specified font, position, text to draw, and color.
     *
     * @param font The font to use for rendering.
     * @param pos The position where the text will be drawn.
     * @param textToDraw The text to draw.
     * @param color The color of the text.
     */
    static void DrawText(Font font, vf2d pos, std::string textToDraw, Color color) {
        get()._drawText(font, pos, textToDraw, color);
    }

    /**
     * @brief Measures the width of the specified text when rendered with the given font.
     *
     * @param font The font used for rendering the text.
     * @param text The text to measure.
     * @return The width of the text in pixels.
     */
    static int MeasureText(Font font, std::string text) {
        return get()._measureText(font, text);
    }

    /**
     * @brief Draws text onto a texture using the specified font.
     *
     * @param font The font to use for rendering the text.
     * @param textToDraw The text string to render.
     * @param color The color of the rendered text (default is WHITE).
     * @return A Texture object representing the rendered text, or an empty texture if rendering fails.
     */
    static Texture DrawTextToTexture(Font font, std::string textToDraw, Color color) {
        return get()._drawTextToTexture(font, textToDraw, color);
    }

private:
    std::unordered_map<std::string, Font> _fonts;

    Font _getFont(const char *fileName, int fontSize);

    void _drawText(Font font, vf2d pos, std::string textToDraw, Color color);

    int _measureText(Font font, std::string text);

    Texture _drawTextToTexture(Font font, std::string textToDraw, Color color);

public:
    Fonts(const Fonts &) = delete;

    static Fonts &get() {
        static Fonts instance;
        return instance;
    }

private:
    Fonts() {
        TTF_Init();
    };
};