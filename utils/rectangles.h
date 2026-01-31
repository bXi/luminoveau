#pragma once

#if __has_include("SDL3/SDL.h")

#include "SDL3/SDL.h"

#endif

#include "vectors.h"

template<class T>
union rect_generic {
    struct {
        v2d_generic<T> pos;
        v2d_generic<T> size;
    };

    struct {
        T x, y;
        T width, height;
    };

    struct {
        T _x, _y;
        T w, h;
    };

    rect_generic() {}

    rect_generic(const rect_generic &other) : pos(other.pos), size(other.size) {}

    rect_generic(v2d_generic<T> _pos, v2d_generic<T> _size) : pos(_pos), size(_size) {}

    rect_generic(T _x, T _y, T _width, T _height) : x(_x), y(_y), width(_width), height(_height) {}


    bool contains(const v2d_generic<T>& _point) {
		return !(_point.x < this->pos.x ||
                 _point.y < this->pos.y ||
			     _point.x > (this->pos.x + this->size.x) ||
                 _point.y > (this->pos.y + this->size.y));
    }

    bool intersects(const rect_generic<T>& other) const {
        return !(this->pos.x + this->size.x < other.pos.x ||
                 other.pos.x + other.size.x < this->pos.x ||
                 this->pos.y + this->size.y < other.pos.y ||
                 other.pos.y + other.size.y < this->pos.y);
    }

#if __has_include("SDL3/SDL.h")

    operator SDL_FRect() {
        SDL_FRect frect;
        frect.x = (float)x;
        frect.y = (float)y;
        frect.w = (float)width;
        frect.h = (float)height;

        return frect;
    }

    operator SDL_Rect() {
        SDL_Rect rect;
        rect.x = (int)x;
        rect.y = (int)y;
        rect.w = (int)width;
        rect.h = (int)height;

        return rect;
    }

#endif
};

typedef rect_generic<int32_t> recti;
typedef rect_generic<uint32_t> rectu;
typedef rect_generic<float> rectf;
typedef rect_generic<double> rectd;
