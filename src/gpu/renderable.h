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

    // Scissor (clip rect) active when this renderable was submitted. Batches split on any change,
    // so a clipped region cuts pixels off cleanly. Disabled = draw to the full viewport.
    bool     scissorEnabled = false;
    int32_t  scissorX       = 0;
    int32_t  scissorY       = 0;
    uint32_t scissorW       = 0;
    uint32_t scissorH       = 0;
};
