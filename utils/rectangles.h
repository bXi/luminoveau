#pragma once

#if __has_include("SDL3/SDL.h")
#include "SDL3/SDL.h"
#endif


#include "vectors.h"


template <class T>
union rect_generic {
    struct {
        v2d_generic<T> pos;
        v2d_generic<T> size;
    };

    struct {
        T x, y;
        T width, height;
    };

    rect_generic() {}

    rect_generic(const rect_generic& other) : pos(other.pos), size(other.size) {}

    rect_generic(v2d_generic<T> _pos, v2d_generic<T> _size) : pos(_pos), size(_size) {}
    rect_generic(T _x, T _y, T _width, T _height) : x(_x), y(_y), width(_width), height(_height) {}



#if __has_include("SDL3/SDL.h")
    operator SDL_FRect() { return SDL_FRect((float)x,(float)y,(float)width,(float)height); }
    operator SDL_Rect() { return SDL_Rect((int)x,(int)y,(int)width,(int)height); }
#endif
};

typedef rect_generic<int32_t> recti;
typedef rect_generic<uint32_t> rectu;
typedef rect_generic<float> rectf;
typedef rect_generic<double> rectd;


struct RectangleOld
{
    float x;
    float y;
    float width;
    float height;

    #if __has_include("SDL3/SDL.h")
    operator SDL_FRect() { return SDL_FRect({ x,y,width,height }); }
    operator SDL_Rect() { return SDL_Rect({ (int)x,(int)y,(int)width,(int)height }); }
    #endif
};