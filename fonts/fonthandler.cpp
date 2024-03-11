#include "fonthandler.h"


Font Fonts::_getFont(const char *fileName, const int fontSize) {
    std::string index = std::string(Helpers::TextFormat("%s%d", fileName, fontSize));

    auto it = _fonts.find(index);

    if (it == _fonts.end()) {

        Font _font;
        _font.font = TTF_OpenFont(fileName, fontSize);

        if (_font.font == nullptr) {
            std::string error = Helpers::TextFormat("Couldn't load %d pt font from %s: %s\n",
                                                    fontSize, fileName, SDL_GetError());

            SDL_Log(error.c_str());
            throw std::runtime_error(error.c_str());
        }

        _fonts[index] = _font;

        return _fonts[index];
    } else {
        return _fonts[index];
    }
}

void Fonts::_drawText(Font font, vf2d pos, std::string textToDraw, Color color) {

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

int Fonts::_measureText(Font font, std::string textToDraw) {

    if (textToDraw.empty()) return 0;

    SDL_Surface *textSurface;

    textSurface = TTF_RenderUTF8_Blended(font.font, textToDraw.c_str(), {255, 255, 255, 255});
    int width = textSurface->w;

    SDL_DestroySurface(textSurface);

    return width;
}

Texture Fonts::_drawTextToTexture(Font font, std::string textToDraw, Color color) {
    if (textToDraw.empty()) {
        textToDraw = " ";
    };

    Texture tex;
    tex.surface = TTF_RenderUTF8_Blended(font.font, textToDraw.c_str(), color);

    tex.width = tex.surface->w;
    tex.height = tex.surface->h;

    tex.texture = SDL_CreateTextureFromSurface(Window::GetRenderer(), tex.surface);

    return tex;
}
