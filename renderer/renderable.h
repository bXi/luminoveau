#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "assettypes/texture.h"
#include "utils/colors.h"

struct Renderable {
    TextureAsset texture;
    glm::ivec2   size;
    glm::vec2 uv[6] = {
        glm::vec2(1.0, 1.0),   // top-right
        glm::vec2(0.0, 1.0),  // top-left
        glm::vec2(1.0, 0.0),  // bottom-right
        glm::vec2(0.0, 1.0),  // top-left
        glm::vec2(0.0, 0.0), // bottom-left
        glm::vec2(1.0, 0.0)   // bottom-right
    };
    Color tintColor = 0xFFFFFFFF;
    bool flipped_horizontally{false};
    bool flipped_vertically{false};
    int  z_index{0};

    struct Transform {
        glm::vec2 position{0.0f};
        glm::vec2 scale{1.0f};

        [[nodiscard]] glm::mat4 to_matrix() const {
            return glm::mat4(
                this->scale.x, 0.0f, 0.0f, 0.0f,
                0.0f, this->scale.y, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                this->position.x, this->position.y, 0.0f, 1.0f
            );
        }
    } transform;


};