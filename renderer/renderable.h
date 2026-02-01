#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <vector>

#include "assettypes/texture.h"
#include "assettypes/effect.h"
#include "renderer/geometry2d.h"
#include "utils/colors.h"
struct SpriteInstance {

};

struct Renderable {
    TextureAsset texture;
    Geometry2D* geometry = nullptr;  // Which geometry to use for this renderable

    float x, y, z;
    float rotation;
    float tex_u, tex_v, tex_w, tex_h;
    float r, g, b, a;
    float w, h;
    float pivot_x, pivot_y;
    bool isSDF = false;  // True for SDF text, false for regular sprites
    
    // Effects to apply to this sprite (captured when sprite is drawn)
    std::vector<EffectAsset> effects;
};