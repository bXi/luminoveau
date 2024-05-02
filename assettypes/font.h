#pragma once

#include "SDL3_ttf/SDL_ttf.h"

/**
 * @brief Represents a font asset for rendering text using SDL_ttf.
 */
struct FontAsset {
    TTF_Font *font = nullptr; /**< Pointer to the TrueType font loaded with SDL_ttf. */
};

using Font = FontAsset&;
