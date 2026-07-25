#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "assets/texture/texture.h"
#include "gpu/geometry/geometry2d.h"
#include "types/color.h"

struct Renderable {
    TextureAsset texture;
    Geometry2D  *geometry = nullptr;

    float   x, y, z;
    float   rotation;
    float   texU, texV, texW, texH;
    float   r, g, b, a;
    float   w, h;
    float   pivotX, pivotY;
    bool    isSDF       = false;
    int32_t effectIndex = -1;
};
