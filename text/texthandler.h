#pragma once

#include <unordered_map>
#include "configuration/configuration.h"
#include "window/windowhandler.h"

#include "utils/helpers.h"
#include "utils/colors.h"

#include "assettypes/font.h"

#include <SDL3_ttf/SDL_ttf.h>


#define MAX_VERTEX_COUNT 4000
#define MAX_INDEX_COUNT 6000

typedef struct Vertex {
    glm::vec3  pos;
    SDL_FColor color;
    glm::vec2  uv;
} Vertex;

typedef struct GeometryData {
    Vertex *vertices;
    int    vertex_count;
    int    *indices;
    int    index_count;
} GeometryData;

/**
 * @brief Provides functionality for managing fonts and rendering text.
 */
class Text {
public:

    /**
     * @brief Draws text using the specified font, position, text to draw, and color.
     *
     * @param font The font to use for rendering.
     * @param pos The position where the text will be drawn.
     * @param textToDraw The text to draw.
     * @param color The color of the text.
     */
    static void DrawText(Font font, const vf2d& pos, const std::string& textToDraw, Color color) {
        get()._drawText(font, pos, textToDraw, color);
    }

    /**
     * @brief Renders text that is wider than the given width by splitting it at space boundaries.
     *        The text will be drawn across multiple lines to ensure it fits within the maximum width.
     *
     * @param font The font to use for rendering the text.
     * @param pos The starting position (top-left) to begin rendering the text.
     * @param textToDraw The full text string to render, potentially split across multiple lines.
     * @param maxWidth The maximum width allowed for each line of text. If a line exceeds this width, it is wrapped.
     * @param color The color to use for rendering the text.
     */
    static void DrawWrappedText(Font font, vf2d pos, std::string textToDraw, float maxWidth, Color color) {
        get()._drawWrappedText(font, pos, textToDraw, maxWidth, color);
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
     * @brief Measures the width of the specified text when rendered with the given font.
     *
     * @param font The font used for rendering the text.
     * @param text The text to measure.
     * @return A 2d vector containing the size of the text in pixels.
     */

    static vf2d GetRenderedTextSize(Font font, std::string text) {
        return get()._getRenderedTextSize(font, text);
    }

    /**
     * @brief Draws text onto a texture using the specified font.
     *
     * @param font The font to use for rendering the text.
     * @param textToDraw The text string to render.
     * @param color The color of the rendered text (default is WHITE).
     * @return A Texture object representing the rendered text, or an empty texture if rendering fails.
     */
    static TextureAsset DrawTextToTexture(Font font, std::string textToDraw, Color color) {
        return get()._drawTextToTexture(font, textToDraw, color);
    }

private:

    void _drawText(Font font, const vf2d& pos, const std::string &textToDraw, Color color);

    void _drawWrappedText(Font font, vf2d pos, std::string textToDraw, float maxWidth, Color color);

    int _measureText(Font font, std::string text);

    vf2d _getRenderedTextSize(Font font, const std::string &textToDraw);

    TextureAsset _drawTextToTexture(Font font, std::string textToDraw, Color color);

    SDL_GPUSampler *textSampler;


public:
    Text(const Text &) = delete;

    static Text &get() {
        static Text instance;
        return instance;
    }

private:
    Text() {
        TTF_Init();

        SDL_GPUSamplerCreateInfo sampler_info = {
            .min_filter = SDL_GPU_FILTER_LINEAR,
            .mag_filter = SDL_GPU_FILTER_LINEAR,
            .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
        };
        textSampler = SDL_CreateGPUSampler(Window::GetDevice(), &sampler_info);
    };
};