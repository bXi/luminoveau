#include "drawhandler.h"

#include <utility>

void Draw::_drawRectangle(vf2d pos, vf2d size, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderRect(renderer, &dstRect);
}

void Draw::_drawCircle(vf2d pos, float radius, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
    }
}

void Draw::_drawRectangleRounded(vf2d pos, const vf2d &size, float radius, Color color) {
    if (Camera::IsActive()) {
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();
    }
}

void Draw::_drawLine(vf2d start, vf2d end, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        start = Camera::ToScreenSpace(start);
        end   = Camera::ToScreenSpace(end);
    }
}

void Draw::_drawArc(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
}

void Draw::_drawTexture(TextureType texture, vf2d pos, const vf2d &size, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);

    vf2d scale = size / texture.getSize();

    // Precompute flipped vectors
    glm::vec2 flipped{1.0f, 1.0f};

    // Given parameters
    glm::vec2 rPosition = {dstRect.x, dstRect.y};    // World position
    glm::vec2 rSize     = texture.getSize(); // Original texture size in pixels
    glm::vec2 rScale    = scale;             // Scale factors (can be non-uniform)
    float     rZ_index  = (float) Renderer::GetZIndex() / (float) INT32_MAX;

    glm::mat4 scale_matrix       = glm::scale(glm::mat4(1.0f), glm::vec3(rSize * rScale, 1.0f));
    glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(rPosition, rZ_index));

    glm::mat4 model_matrix = translation_matrix * scale_matrix;

    Renderable renderable = {
        .texture = texture,
        .size = {texture.width, texture.height},
        .model = model_matrix,
        .flipped = flipped,
        .tintColor = color,
        .transform = {
            .position = {dstRect.x, dstRect.y},
            .scale = {scale.x, scale.y}
        },
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_drawTexturePart(TextureType texture, const vf2d &pos, const vf2d &size, const rectf &src, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);
    SDL_FRect srcRect = {src.x, src.y, std::abs(src.width), std::abs(src.height)};

    vf2d scale = size / texture.getSize();

    float u0 = 1.0f - (srcRect.x / (float) texture.getSize().x);
    float v0 = (srcRect.y / (float) texture.getSize().y);
    float u1 = 1.0f - ((srcRect.x + srcRect.w) / (float) texture.getSize().x);
    float v1 = ((srcRect.y + srcRect.h) / (float) texture.getSize().y);

    // Precompute flipped vectors
    glm::vec2 flipped{
        (src.width >= 0.f) ? -1.0f : 1.0f,
        (src.height < 0.f) ? -1.0f : 1.0f
    };

    // Given parameters
    glm::vec2 rPosition          = {dstRect.x, dstRect.y}; // World position
    glm::vec2 rSize              = texture.getSize();      // Original texture size in pixels
    glm::vec2 rScale             = scale;                  // Scale factors (can be non-uniform)
    float     rZ_index           = (float) Renderer::GetZIndex() / (float) INT32_MAX;
    glm::mat4 scale_matrix       = glm::scale(glm::mat4(1.0f), glm::vec3(rSize * rScale, 1.0f));
    glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(rPosition, rZ_index));

    glm::mat4 model_matrix = translation_matrix * scale_matrix;

    Renderable renderable = {
        .texture = texture,
        .size = {texture.width, texture.height},
        .uv = {
            glm::vec2(u1, v1),  // Top-right
            glm::vec2(u0, v1),  // Top-left
            glm::vec2(u1, v0),  // Bottom-right
            glm::vec2(u0, v1),  // Top-left (repeated for triangle)
            glm::vec2(u0, v0),  // Bottom-left
            glm::vec2(u1, v0)   // Bottom-right
        },
        .model = model_matrix,
        .flipped = flipped,
        .tintColor = color,
        .transform = {
            .position = {dstRect.x, dstRect.y},
            .scale = {scale.x, scale.y}
        },
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_drawRotatedTexture(Draw::TextureType texture, vf2d pos, const vf2d &size, float angle, vf2d pivot, Color color) {

    vf2d scale = size / texture.getSize();

    // Precompute flipped vectors
    glm::vec2 flipped{1.0f, 1.0f};

    // Given parameters
    glm::vec2 rPosition = {pos.x, pos.y};     // World position
    glm::vec2 rSize     = texture.getSize();  // Original texture size in pixels
    glm::vec2 rScale    = scale;              // Scale factors (can be non-uniform)
    float     rAngle    = fmod(angle, 360.f); // Rotation angle in degrees
    glm::vec2 rPivot    = pivot;              // Normalized pivot (center)
    float     rZ_index  = (float) Renderer::GetZIndex() / (float) INT32_MAX;

    glm::vec2 pivot_offset         = (size * scale) * rPivot;
    glm::mat4 scale_matrix         = glm::scale(glm::mat4(1.0f), glm::vec3(rSize * rScale, 1.0f));
    glm::mat4 pivot_translate_back = glm::translate(glm::mat4(1.0f), glm::vec3(-pivot_offset, 0.0f));
    glm::mat4 rotation_matrix      = glm::rotate(glm::mat4(1.0f), glm::radians(rAngle), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 pivot_translate      = glm::translate(glm::mat4(1.0f), glm::vec3(pivot_offset, 0.0f));
    glm::mat4 translation_matrix   = glm::translate(glm::mat4(1.0f), glm::vec3(rPosition, rZ_index));

    glm::mat4 model_matrix = translation_matrix * pivot_translate * rotation_matrix * pivot_translate_back * scale_matrix;

    Renderable renderable = {
        .texture = texture,
        .size = {texture.width, texture.height},
        .model = model_matrix,
        .flipped = flipped,
        .tintColor = color,
        .transform = {
            .position = {pos.x, pos.y},
            .scale = {scale.x, scale.y}
        },
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_drawRotatedTexturePart(Draw::TextureType texture, const vf2d &pos, const vf2d &size, const rectf &src, float angle, vf2d pivot,
                                   Color color) {
    SDL_FRect srcRect = {src.x, src.y, std::abs(src.width), std::abs(src.height)};

    vf2d scale = size / texture.getSize();

    float u0 = 1.0f - (srcRect.x / (float) texture.getSize().x);
    float v0 = (srcRect.y / (float) texture.getSize().y);
    float u1 = 1.0f - ((srcRect.x + srcRect.w) / (float) texture.getSize().x);
    float v1 = ((srcRect.y + srcRect.h) / (float) texture.getSize().y);

    // Precompute flipped vectors
    glm::vec2 flipped{1.0f, 1.0f};

    // Given parameters
    glm::vec2 rPosition = {pos.x, pos.y};     // World position
    glm::vec2 rSize     = texture.getSize();  // Original texture size in pixels
    glm::vec2 rScale    = scale;              // Scale factors (can be non-uniform)
    float     rAngle    = fmod(angle, 360.f); // Rotation angle in degrees
    glm::vec2 rPivot    = pivot;              // Normalized pivot (center)
    float     rZ_index  = (float) Renderer::GetZIndex() / (float) INT32_MAX;

    glm::vec2 pivot_offset         = (size * scale) * rPivot;
    glm::mat4 scale_matrix         = glm::scale(glm::mat4(1.0f), glm::vec3(rSize * rScale, 1.0f));
    glm::mat4 pivot_translate_back = glm::translate(glm::mat4(1.0f), glm::vec3(-pivot_offset, 0.0f));
    glm::mat4 rotation_matrix      = glm::rotate(glm::mat4(1.0f), glm::radians(rAngle), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 pivot_translate      = glm::translate(glm::mat4(1.0f), glm::vec3(pivot_offset, 0.0f));
    glm::mat4 translation_matrix   = glm::translate(glm::mat4(1.0f), glm::vec3(rPosition, rZ_index));

    glm::mat4 model_matrix = translation_matrix * pivot_translate * rotation_matrix * pivot_translate_back * scale_matrix;

    Renderable renderable = {
        .texture = texture,
        .size = {texture.width, texture.height},
        .uv = {
            glm::vec2(u1, v1),  // Top-right
            glm::vec2(u0, v1),  // Top-left
            glm::vec2(u1, v0),  // Bottom-right
            glm::vec2(u0, v1),  // Top-left (repeated for triangle)
            glm::vec2(u0, v0),  // Bottom-left
            glm::vec2(u1, v0)   // Bottom-right
        },
        .model = model_matrix,
        .flipped = flipped,
        .tintColor = color,
        .transform = {
            .position = {pos.x, pos.y},
            .scale = {scale.x, scale.y}
        },
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_beginScissorMode(rectf area) {
    //TODO: fix with sdl_GPU
    const SDL_Rect cliprect = area;
    SDL_SetRenderClipRect(renderer, &cliprect);
}

void Draw::_endScissorMode() {
    //TODO: fix with sdl_GPU
    SDL_SetRenderClipRect(renderer, nullptr);
}

void Draw::_drawRectangleFilled(vf2d pos, vf2d size, Color color) {
    //TODO: fix with sdl_GPU
    SDL_FRect dstRect = _doCamera(pos, size);

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &dstRect);
}

void Draw::_drawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();  // Scale the radius with the camera's zoom
        size *= Camera::GetScale();    // Scale the size with the camera's zoom
    }
}

void Draw::_drawCircleFilled(vf2d pos, float radius, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
    }
}

void Draw::_drawArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
    //TODO: create this function
}

void Draw::_beginMode2D() {
    // Clear the screen
    Camera::Activate();
}

void Draw::_endMode2D() {
    Camera::Deactivate();
}

rectf Draw::_doCamera(const vf2d &pos, const vf2d &size) {

    rectf dstRect;

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        vf2d screenPos  = Camera::ToScreenSpace(pos);
        vf2d screenSize = Camera::ToScreenSpace(pos + size) - screenPos;

        dstRect = {screenPos.x, screenPos.y, screenSize.x, screenSize.y};
    } else {
        // Camera is not active, render directly in screen space
        dstRect = {pos.x, pos.y, size.x, size.y};
    }

    return dstRect;
}

void Draw::_drawThickLine(vf2d start, vf2d end, Color color, float width) {

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        start = Camera::ToScreenSpace(start);
        end   = Camera::ToScreenSpace(end);

        // Scale the line width with the camera's zoom
        width *= Camera::GetScale();
    }
}

void Draw::_drawTriangle(vf2d v1, vf2d v2, vf2d v3, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }
}

void Draw::_drawEllipse(vf2d center, float radiusX, float radiusY, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        center = Camera::ToScreenSpace(center);
        radiusX *= Camera::GetScale();  // Scale radiusX with camera zoom
        radiusY *= Camera::GetScale();  // Scale radiusY with camera zoom
    }
}

void Draw::_drawTriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }
}

void Draw::_drawEllipseFilled(vf2d center, float radiusX, float radiusY, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        center = Camera::ToScreenSpace(center);
        radiusX *= Camera::GetScale();  // Scale radiusX with camera zoom
        radiusY *= Camera::GetScale();  // Scale radiusY with camera zoom
    }
}

void Draw::_drawPixel(vi2d pos, Color color) {
    //TODO: fix with sdl_GPU

}