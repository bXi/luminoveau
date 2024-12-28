#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "assettypes/texture.h"
#include "utils/colors.h"


struct RenderableVertex {
    glm::vec3 pos;
    SDL_FColor color;
    glm::vec2 uv;
};

struct Renderable {
    TextureAsset texture;
    glm::ivec2   size;

    struct GeometryData {
        std::vector<RenderableVertex> vertices;
        std::vector<int> indices;
    } geometryData;

    int  z_index{0};
    bool flipped_horizontally{false};
    bool flipped_vertically{false};

    struct Transform {
        glm::vec2 position{0.0f};
        glm::vec2 scale{1.0f};

        [[nodiscard]] glm::mat4 to_matrix() const {
            glm::mat4 mat(1.0f);
            mat = glm::translate(mat, glm::vec3(this->position, 0.0f));
            mat = glm::scale(mat, glm::vec3(this->scale, 1.0f));
            return mat;
        }
    } transform;

    glm::vec2 uv[6] = {
        glm::vec2(1.0, 1.0),   // top-right
        glm::vec2(0.0, 1.0),  // top-left
        glm::vec2(1.0, 0.0),  // bottom-right
        glm::vec2(0.0, 1.0),  // top-left
        glm::vec2(0.0, 0.0), // bottom-left
        glm::vec2(1.0, 0.0)   // bottom-right
    };

    Color tintColor = 0xFFFFFFFF;

    bool temporary = false;
};