#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "assettypes/texture.h"
#include "utils/colors.h"
struct SpriteInstance {

};

struct Renderable {
    TextureAsset texture;

    float x, y, z;
    float rotation;
    float tex_u, tex_v, tex_w, tex_h;
    float r, g, b, a;
    float w, h;

};