#include "render2dhandler.h"

void Render2D::_drawRectangle(vf2d pos, vf2d size, Color color) {
    SDL_FRect rect = {pos.x, pos.y, size.x, size.y};

    SDL_SetRenderDrawColor(renderer, color.r, color.g,color.b,color.a);
    SDL_RenderRect(renderer, &rect);
}

void Render2D::_drawTexture(Texture texture, vf2d pos, vf2d size) {

    SDL_FRect dstRect = {pos.x, pos.y, size.x, size.y};
    SDL_FRect srcRect = {0.f, 0.f, (float)texture.width, (float)texture.height};

    auto tex = SDL_CreateTextureFromSurface(renderer, texture.surface);

    SDL_RenderTexture(renderer, tex, &srcRect, &dstRect);

    SDL_DestroyTexture(tex);
}
