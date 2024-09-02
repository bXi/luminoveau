#include "texthandler.h"

void Text::_drawText(Font font, vf2d pos, std::string textToDraw, Color color) {

    if (textToDraw.empty()) return;

    SDL_Surface *textSurface;

    textSurface = TTF_RenderUTF8_Blended(font.font, textToDraw.c_str(), color);

    SDL_FRect dstRect = {pos.x, pos.y, (float) textSurface->w, (float) textSurface->h};
    SDL_FRect srcRect = {0.f, 0.f, (float) textSurface->w, (float) textSurface->h};

    auto tex = SDL_CreateTextureFromSurface(Window::GetRenderer(), textSurface);
    SDL_RenderTexture(Window::GetRenderer(), tex, &srcRect, &dstRect);

    SDL_DestroyTexture(tex);
    SDL_DestroySurface(textSurface);

}

int Text::_measureText(Font font, std::string textToDraw) {

    if (textToDraw.empty()) return 0;

    SDL_Surface *textSurface;

    textSurface = TTF_RenderUTF8_Blended(font.font, textToDraw.c_str(), {255, 255, 255, 255});

    if (!textSurface) return 0;

    int width = textSurface->w;

    SDL_DestroySurface(textSurface);

    return width;
}

TextureAsset Text::_drawTextToTexture(Font font, std::string textToDraw, Color color) {
    if (textToDraw.empty()) {
        // In this case we return space so we still render something for when the user uses
        // the height of the returned texture to position multiple lines of text.
        textToDraw = " ";
    };

    TextureAsset tex;
    tex.surface = TTF_RenderUTF8_Blended(font.font, textToDraw.c_str(), color);

    tex.width = tex.surface->w;
    tex.height = tex.surface->h;

    tex.texture = SDL_CreateTextureFromSurface(Window::GetRenderer(), tex.surface);

    return tex;
}
