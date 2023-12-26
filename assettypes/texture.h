#pragma once

#include "SDL3/SDL.h"

struct Texture
{
    int width;
    int height;

    SDL_Surface* surface;
    SDL_Texture* texture;
};