#pragma once

#include "SDL3/SDL.h"

struct Texture
{
    int width;
    int height;

    std::string filename;

    SDL_Surface* surface = nullptr;
    SDL_Texture* texture = nullptr;
};