#pragma once

#if __has_include("SDL3/SDL.h")
#include "SDL3/SDL.h"
#endif

struct Rectangle
{
    float x;
    float y;
    float width;
    float height;

    #if __has_include("SDL3/SDL.h")
    operator SDL_FRect() { return SDL_FRect(x,y,width,height); }
    #endif
};