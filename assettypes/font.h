#pragma once

#include "blend2d.h"

/**
 * @brief Represents a font asset for rendering text using SDL_ttf.
 */
struct FontAsset {
    BLFont *font = nullptr; /**< Pointer to the TrueType font loaded with Blend2D. */
};

using Font = FontAsset&;
