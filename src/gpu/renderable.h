#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "assets/texture/texture.h"
#include "gpu/geometry/geometry2d.h"
#include "types/color.h"

struct Renderable {
    TextureAsset texture;
    Geometry2D*  geometry  = nullptr;

    float x, y, z;
    float rotation;
    float tex_u, tex_v, tex_w, tex_h;
    float r, g, b, a;
    float w, h;
    float pivot_x, pivot_y;
    bool  isSDF       = false;
    int32_t effectIndex = -1;
};
