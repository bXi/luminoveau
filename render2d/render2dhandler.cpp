#include "render2dhandler.h"

#include "SDL2_gfxPrimitives.h"

void Render2D::_drawRectangle(vf2d pos, vf2d size, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);;

    SDL_SetRenderDrawColor(renderer, color.r, color.g,color.b,color.a);
    SDL_RenderRect(renderer, &dstRect);
}

void Render2D::_drawCircle(vf2d pos, float radius, Color color) {
    circleRGBA(Window::GetRenderer(), pos.x, pos.y, radius, color.r, color.g, color.b, color.a);
}

void Render2D::_drawRectangleRounded(vf2d pos, vf2d size, float radius, Color color) {
    roundedRectangleRGBA(Window::GetRenderer(),
                         pos.x, pos.y,
                         pos.x + size.x, pos.y + size.y,
                         radius,
                         color.r, color.g, color.b, color.a);

}

void Render2D::_drawLine(vf2d start, vf2d end, Color color) {
    lineRGBA(Window::GetRenderer(), start.x, start.y, end.x, end.y, color.r, color.r, color.g, color.a);
}

void Render2D::_drawArc(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {

}

void Render2D::_drawTexture(Texture texture, vf2d pos, vf2d size, Color color) {

    SDL_FRect dstRect = _doCamera(pos, size);;


    SDL_SetTextureColorMod(texture.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture.texture, color.a);

    SDL_RenderTextureRotated(renderer, texture.texture, nullptr, &dstRect, 0.0, nullptr, SDL_FLIP_NONE);
}

void Render2D::_drawTexturePart(Texture texture, vf2d pos, vf2d size, Rectangle src, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);
    SDL_FRect srcRect = { src.x, src.y, std::abs(src.width), std::abs(src.height) };

    SDL_SetTextureColorMod(texture.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture.texture, color.a);

    // Set the flip flags
    int flipFlags = SDL_FLIP_NONE;
    if (src.width < 0.f) { flipFlags |= SDL_FLIP_HORIZONTAL; }
    if (src.height < 0.f) { flipFlags |= SDL_FLIP_VERTICAL; }

    SDL_RenderTextureRotated(renderer, texture.texture, &srcRect, &dstRect, 0.0, nullptr, (SDL_RendererFlip)flipFlags);
}

void Render2D::_beginScissorMode(Rectangle area) {
    const SDL_Rect cliprect = area;
    SDL_SetRenderClipRect(renderer, &cliprect);
}

void Render2D::_endScissorMode() {
    SDL_SetRenderClipRect(renderer, nullptr);
}

void Render2D::_drawRectangleFilled(vf2d pos, vf2d size, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);

    SDL_SetRenderDrawColor(renderer, color.r, color.g,color.b,color.a);
    SDL_RenderFillRect(renderer, &dstRect);
}

void Render2D::_drawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color) {

    if (Camera::IsActive()) {
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();
    }
    roundedBoxRGBA(Window::GetRenderer(),
                         pos.x, pos.y,
                         pos.x + size.x, pos.y + size.y,
                         radius,
                         color.r, color.g, color.b, color.a);

    _drawRectangleFilled({pos.x, pos.y + radius}, {size.x, size.y - (radius * 2.f) + 1 }, color);
}

void Render2D::_drawCircleFilled(vf2d pos, float radius, Color color) {

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);

    }

    filledCircleRGBA(Window::GetRenderer(), pos.x, pos.y, radius, color.r, color.g, color.b, color.a);
}

void Render2D::_drawArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {

}

void Render2D::_beginMode2D() {
    // Clear the screen
    Camera::Activate();
}

void Render2D::_endMode2D() {
    Camera::Deactivate();
}

Rectangle Render2D::_doCamera(vf2d pos, vf2d size) {

    Rectangle dstRect;

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        vf2d screenPos = Camera::ToScreenSpace(pos);
        vf2d screenSize = Camera::ToScreenSpace(pos + size) - screenPos;

        dstRect = { screenPos.x, screenPos.y, screenSize.x, screenSize.y };
    } else {
        // Camera is active, render directly in world space
        dstRect = { pos.x, pos.y, size.x, size.y };
    }

    return dstRect;
}

void Render2D::_drawThickLine(vf2d start, vf2d end, Color color, float width) {

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        start = Camera::ToScreenSpace(start);
        end = Camera::ToScreenSpace(end);

        width *= Camera::GetScale();
    }

    thickLineRGBA(
            Window::GetRenderer(),
            start.x, start.y,
            end.x, end.y,
            (int)width,
            color.r, color.g, color.b, color.a);
}

void Render2D::_drawTriangle(vf2d v1, vf2d v2, vf2d v3, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }
    _drawLine(v1, v2, color);
    _drawLine(v2, v3, color);
    _drawLine(v3, v1, color);
//TODO: figure out why this doesn't draw
//    trigonRGBA(Window::GetRenderer(),
//               v1.x, v1.y,
//               v2.x, v2.y,
//               v3.x, v3.y,
//                color.r, color.g, color.b, color.a);
}

void Render2D::_drawEllipse(vf2d center, float radiusX, float radiusY, Color color) {

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        center = Camera::ToScreenSpace(center);
    }


    ellipseRGBA(Window::GetRenderer(),
               center.x, center.y,
               radiusX,
               radiusY,
                color.r, color.g, color.b, color.a);
}

void Render2D::_drawTriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }

    filledTrigonRGBA(Window::GetRenderer(),
               v1.x, v1.y,
               v2.x, v2.y,
               v3.x, v3.y,
                color.r, color.g, color.b, color.a);
}

void Render2D::_drawEllipseFilled(vf2d center, float radiusX, float radiusY, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        center = Camera::ToScreenSpace(center);
    }

    filledEllipseRGBA(Window::GetRenderer(),
               center.x, center.y,
               radiusX,
               radiusY,
                color.r, color.g, color.b, color.a);
}

void Render2D::_drawPixel(vi2d pos, Color color) {
        SDL_SetRenderDrawColor(Window::GetRenderer(), color.r, color.g,color.b,color.a);
    SDL_RenderPoint(Window::GetRenderer(), pos.x, pos.y);

}


