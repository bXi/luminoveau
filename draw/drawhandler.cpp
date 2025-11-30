#include "drawhandler.h"

#include <utility>

void Draw::_drawRectangle(const vf2d& pos, const vf2d& size, Color color) {
    rectf dstRect = _doCamera(pos, size);

    Texture(Renderer::WhitePixel(), dstRect.pos, dstRect.size, color);
}

void Draw::_drawCircle(vf2d pos, float radius, Color color, int segments = 32) {

    float angleStep = 2.0f * PI / segments;

vf2d first = {
    pos.x + std::cos(0.0f) * radius,
    pos.y + std::sin(0.0f) * radius - 0.5f  // Offset by half pixel
};

    vf2d prev = first;

    for (int i = 1; i < segments; i++) {  // Changed: i < segments (not <=)
        float a = i * angleStep;
        vf2d curr = {
            pos.x + std::cos(a) * radius,
            pos.y + std::sin(a) * radius
        };

        _drawLine(prev, curr, color);
        prev = curr;
    }

    // Explicitly close the circle
    _drawLine(prev, first, color);
}



void Draw::_drawRectangleRounded(vf2d pos, const vf2d &size, float radius, Color color) {
    LUMI_UNUSED(size, radius, color);
    if (Camera::IsActive()) {
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();
    }
}

void Draw::_drawLine(vf2d start, vf2d end, Color color) {
    if (Camera::IsActive()) {
        start = Camera::ToScreenSpace(start);
        end   = Camera::ToScreenSpace(end);
    }

    vf2d line = end - start;

    // Early exit for zero-length lines
    if (line.x == 0.0f && line.y == 0.0f) return;

    float length = std::hypot(line.x, line.y);
    float angle = std::atan2(line.y, line.x) * 180.0f / PI; // Convert to degrees

    vf2d size = {length, 1.0f};
    vf2d pivot = {0.0f, 0.5f};

    _drawRotatedTexture(Renderer::WhitePixel(), start, size, angle, pivot, color);
}

void Draw::_drawArc(const vf2d& center, float radius, float startAngle, float endAngle, int segments, Color color) {
    LUMI_UNUSED(center, radius, startAngle, endAngle, segments, color);
}

void Draw::_drawTexture(TextureType texture, const vf2d& pos, const vf2d &size, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);

    Renderable renderable = {
        .texture = texture,

        .x = dstRect.x,
        .y = dstRect.y,
        .z = (float)Renderer::GetZIndex(),

        .rotation = 0.f,


        .tex_u = 0.f,
        .tex_v = 0.f,
        .tex_w = 1.f,
        .tex_h = 1.f,

        .r = (float) color.r / 255.f,
        .g = (float) color.g / 255.f,
        .b = (float) color.b / 255.f,
        .a = (float) color.a / 255.f,

        .w = size.x,
        .h = size.y,

        .pivot_x = 0.5f,
        .pivot_y = 0.5f,
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_drawTexturePart(TextureType texture, const vf2d &pos, const vf2d &size, const rectf &src, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);
    SDL_FRect srcRect = {src.x, src.y, std::abs(src.width), std::abs(src.height)};

    float u0 = (srcRect.x / (float) texture.getSize().x);
    float v0 = (srcRect.y / (float) texture.getSize().y);
    float u1 = ((srcRect.x + srcRect.w) / (float) texture.getSize().x);
    float v1 = ((srcRect.y + srcRect.h) / (float) texture.getSize().y);

    Renderable renderable = {
        .texture = texture,

        .x = dstRect.x,
        .y = dstRect.y,
        .z = (float)Renderer::GetZIndex() / INT_MAX,

        .rotation = 0.f,

        .tex_u = u0,
        .tex_v = v0,
        .tex_w = u1 - u0,
        .tex_h = v1 - v0,

        .r = (float) color.r / 255.f,
        .g = (float) color.g / 255.f,
        .b = (float) color.b / 255.f,
        .a = (float) color.a / 255.f,

        .w = dstRect.w,
        .h = dstRect.h,

        .pivot_x = 0.5f,
        .pivot_y = 0.5f,
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_drawRotatedTexture(Draw::TextureType texture, const vf2d& pos, const vf2d &size, float angle, const vf2d& pivot, Color color) {

    Renderable renderable = {
        .texture = texture,

        .x = pos.x,
        .y = pos.y,
        .z = (float)Renderer::GetZIndex(),

        .rotation = glm::radians(fmod(angle, 360.f)),

        .tex_u = 0.f,
        .tex_v = 0.f,
        .tex_w = 1.f,
        .tex_h = 1.f,

        .r = (float) color.r / 255.f,
        .g = (float) color.g / 255.f,
        .b = (float) color.b / 255.f,
        .a = (float) color.a / 255.f,

        .w = size.x,
        .h = size.y,

        .pivot_x = pivot.x,
        .pivot_y = pivot.y,
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_drawRotatedTexturePart(Draw::TextureType texture, const vf2d &pos, const vf2d &size, const rectf &src, float angle, const vf2d& pivot,
                                   Color color) {
    SDL_FRect srcRect = {src.x, src.y, std::abs(src.width), std::abs(src.height)};

    float u0 = (srcRect.x / (float) texture.getSize().x);
    float v0 = (srcRect.y / (float) texture.getSize().y);
    float u1 = ((srcRect.x + srcRect.w) / (float) texture.getSize().x);
    float v1 = ((srcRect.y + srcRect.h) / (float) texture.getSize().y);

    Renderable renderable = {
        .texture = texture,

        .x = pos.x,
        .y = pos.y,
        .z = (float)Renderer::GetZIndex(),

        .rotation = glm::radians(fmod(angle, 360.f)),

        .tex_u = u0,
        .tex_v = v0,
        .tex_w = u1,
        .tex_h = v1,

        .r = (float) color.r / 255.f,
        .g = (float) color.g / 255.f,
        .b = (float) color.b / 255.f,
        .a = (float) color.a / 255.f,

        .w = size.x,
        .h = size.y,

        .pivot_x = pivot.x,
        .pivot_y = pivot.x,
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_setScissorMode(const rectf& area) {
    Renderer::SetScissorMode(_targetRenderPass, area);
}

void Draw::_drawRectangleFilled(vf2d pos, vf2d size, Color color) {
    //TODO: fix with sdl_GPU
    SDL_FRect dstRect = _doCamera(pos, size);

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &dstRect);
}

void Draw::_drawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color) {
    LUMI_UNUSED(radius, color);
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();  // Scale the radius with the camera's zoom
        size *= Camera::GetScale();    // Scale the size with the camera's zoom
    }
}

void Draw::_drawCircleFilled(vf2d pos, float radius, Color color) {
    LUMI_UNUSED(radius, color);
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
    }
}

void Draw::_drawArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
    LUMI_UNUSED(center, radius, startAngle, endAngle, segments, color);

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
        dstRect.pos  = Camera::ToScreenSpace(pos);
        dstRect.size = Camera::ToScreenSpace(pos + size) - dstRect.pos;

    } else {
        // Camera is not active, render directly in screen space

        dstRect.pos = pos;
        dstRect.size = size;
    }

    return dstRect;
}

void Draw::_drawThickLine(vf2d start, vf2d end, Color color, float width) {
    if (Camera::IsActive()) {
        start = Camera::ToScreenSpace(start);
        end   = Camera::ToScreenSpace(end);
        width *= Camera::GetScale();
    }

    vf2d line = end - start;

    // Early exit for (nearly) zero-length lines
    const float EPS = 1e-6f;
    if (std::fabs(line.x) < EPS && std::fabs(line.y) < EPS) return;

    float length = std::hypot(line.x, line.y);
    float angle = std::atan2(line.y, line.x) * 180.0f / PI; // degrees

    // draw rectangle with pivot at left-center so we can place it exactly at `start`
    vf2d size = { length, width };
    vf2d pivot = { 0.0f, 0.5f };
    vf2d offsetStart = start - vf2d(0.f, width/2.f); // left-center of the rotated rectangle should be at `start`

    _drawRotatedTexture(Renderer::WhitePixel(), offsetStart, size, angle, pivot, color);
}


void Draw::_drawTriangle(vf2d v1, vf2d v2, vf2d v3, Color color) {
    LUMI_UNUSED(color);

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }
}

void Draw::_drawEllipse(vf2d center, float radiusX, float radiusY, Color color) {
    LUMI_UNUSED(radiusX, radiusY, color);

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        center = Camera::ToScreenSpace(center);
        radiusX *= Camera::GetScale();  // Scale radiusX with camera zoom
        radiusY *= Camera::GetScale();  // Scale radiusY with camera zoom
    }
}

void Draw::_drawTriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color) {
    LUMI_UNUSED(color);
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }
}

void Draw::_drawEllipseFilled(vf2d center, float radiusX, float radiusY, Color color) {
    LUMI_UNUSED(radiusX, radiusY, color);
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        center = Camera::ToScreenSpace(center);
        radiusX *= Camera::GetScale();  // Scale radiusX with camera zoom
        radiusY *= Camera::GetScale();  // Scale radiusY with camera zoom
    }
}

void Draw::_drawPixel(const vi2d& pos, Color color) {
    LUMI_UNUSED(pos, color);

    //TODO: fix with sdl_GPU

}