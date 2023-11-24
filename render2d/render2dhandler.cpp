#include "render2dhandler.h"


void Render2D::_drawRectangle(vf2d pos, vf2d size, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);;

    SDL_SetRenderDrawColor(renderer, color.r, color.g,color.b,color.a);
    SDL_RenderRect(renderer, &dstRect);
}

void Render2D::_drawTexture(Texture texture, vf2d pos, vf2d size, Color color) {

    SDL_FRect dstRect = _doCamera(pos, size);;
    SDL_FRect srcRect = {0.f, 0.f, (float)texture.width, (float)texture.height};

    SDL_SetTextureColorMod(texture.texture, color.r, color.g, color.b < 255);
    SDL_SetTextureAlphaMod(texture.texture, color.a);

    int flipFlags = SDL_FLIP_NONE;
    SDL_RenderTextureRotated(renderer, texture.texture, &srcRect, &dstRect, 0.0, nullptr, (SDL_RendererFlip)flipFlags);
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

void Render2D::_drawCircle(vf2d pos, float radius, Color color) {

    //TODO verify
    SDL_SetRenderDrawColor(renderer, color.r, color.g,color.b,color.a);

    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {

        // Draw points in all eight octants
        SDL_RenderPoint(renderer, pos.x + x, pos.y - y);
        SDL_RenderPoint(renderer, pos.x + y, pos.y - x);
        SDL_RenderPoint(renderer, pos.x - y, pos.y - x);
        SDL_RenderPoint(renderer, pos.x - x, pos.y - y);
        SDL_RenderPoint(renderer, pos.x - x, pos.y + y);
        SDL_RenderPoint(renderer, pos.x - y, pos.y + x);
        SDL_RenderPoint(renderer, pos.x + y, pos.y + x);
        SDL_RenderPoint(renderer, pos.x + x, pos.y + y);

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }

}

void Render2D::_drawRectangleRounded(vf2d pos, vf2d size, float radius, Color color) {
    //TODO: implement this

    _drawRectangle(pos,size,color);
}

void Render2D::_drawLine(vf2d start, vf2d end, float width, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g,color.b,color.a);
    SDL_RenderLine(renderer, start.x, start.y, end.x, end.y);
}

void Render2D::_drawCircleSector(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {

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
    //TODO: implement this

    _drawRectangleFilled(pos,size,color);
}

void Render2D::_drawCircleFilled(vf2d pos, float radius, Color color) {

}

void Render2D::_drawCircleSectorFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {

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


