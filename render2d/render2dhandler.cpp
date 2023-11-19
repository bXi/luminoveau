#include "render2dhandler.h"

void Render2D::_drawRectangle(vf2d pos, vf2d size, Color color) {
    SDL_FRect rect = {pos.x, pos.y, size.x, size.y};

    SDL_SetRenderDrawColor(renderer, color.r, color.g,color.b,color.a);
    SDL_RenderRect(renderer, &rect);
}

void Render2D::_drawTexture(Texture texture, vf2d pos, vf2d size, Color color) {

    SDL_FRect dstRect = {pos.x, pos.y, size.x, size.y};
    SDL_FRect srcRect = {0.f, 0.f, (float)texture.width, (float)texture.height};

    auto tex = SDL_CreateTextureFromSurface(renderer, texture.surface);

    SDL_RenderTexture(renderer, tex, &srcRect, &dstRect);

    SDL_DestroyTexture(tex);
}

void Render2D::_drawTexturePart(Texture texture, vf2d pos, vf2d size, Rectangle src, Color color) {
    SDL_FRect dstRect = {pos.x, pos.y, size.x, size.y};
    SDL_FRect srcRect = src;

    auto tex = SDL_CreateTextureFromSurface(renderer, texture.surface);


    if (color.r < 255 || color.g < 255 || color.b < 255) {
        SDL_SetTextureColorMod(tex, color.r, color.g, color.b < 255);
    }

    if (color.a < 255) {
        SDL_SetTextureAlphaMod(tex, color.a);
    }


    SDL_RenderTexture(renderer, tex, &srcRect, &dstRect);

    SDL_DestroyTexture(tex);
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
