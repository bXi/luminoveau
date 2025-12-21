#pragma once

#include <SDL3_ttf/SDL_ttf.h>

/**
 * @brief Represents a font asset for rendering text using SDL_ttf.
 */
struct FontAsset {
    TTF_Font *ttfFont = nullptr; /**< Pointer to the TrueType font loaded with SDL_ttf. */
    TTF_TextEngine *textEngine = nullptr; /**< Pointer to the GPU text engine. */
    void *fontData = nullptr; /**< Internal: Font file data for cleanup. */
};

using Font = FontAsset &;
