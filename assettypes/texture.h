#pragma once

#include <string>

#include "SDL3/SDL.h"

/**
 * @brief Represents a texture asset for rendering images using SDL.
 */
struct TextureAsset {
    int32_t id;
    int width = -1; /**< Width of the texture. */
    int height = -1; /**< Height of the texture. */
    std::string filename; /**< Filename of the texture image file. */



    SDL_Surface *surface = nullptr; /**< Pointer to the SDL surface representing the texture. */
    SDL_Texture *texture = nullptr; /**< Pointer to the SDL texture. */
};

using Texture = TextureAsset&;

