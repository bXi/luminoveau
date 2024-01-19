#include "texturehandler.h"
#include "window/windowhandler.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"


Texture Textures::_getTexture(const char *fileName) {
    if (_textures.find(fileName) == _textures.end()) {
        Texture _tex = _loadTexture(fileName);
        _textures[fileName] = _tex;

        return _textures[fileName];
    } else {
        return _textures[fileName];
    }
}

Rectangle Textures::_getRectangle(int x, int y) {

    const Rectangle rect = {(float) (x * Configuration::tileWidth), (float) (y * Configuration::tileHeight),
                            (float) (Configuration::tileWidth),
                            (float) (Configuration::tileHeight)};
    return rect;
};

Rectangle Textures::_getTile(int tileId) {
    return GetRectangle(tileId % 16, (int) tileId / 16);
};

Rectangle Textures::_getTile(int tileId, bool doubleHeight) {
    Rectangle r = GetRectangle(tileId % 16, (int) tileId / 16);
    if (doubleHeight) {
        r.y -= r.height;
        r.height *= 2;
    }
    return r;
}

Texture Textures::_loadTexture(const char *fileName) {

    Texture texture;

    auto surface = IMG_Load(fileName);

    if (!surface)
        SDL_Log("IMG_Load failed: %s", SDL_GetError());

    texture.width = surface->w;
    texture.height = surface->h;

    texture.filename = fileName;

    texture.surface = surface;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(Window::GetRenderer(), surface);
    texture.texture = tex;

    if (!tex)
        SDL_Log("Texture failed: %s", SDL_GetError());

    auto error = SDL_SetTextureBlendMode(texture.texture, SDL_BLENDMODE_BLEND);

    if (error)
        SDL_Log("Blendmode failed: %s", SDL_GetError());

    SDL_Log("Loaded texture: %s\n"
            "\tX: %i - Y: %i", fileName, texture.width, texture.height);

    return texture;

}

Texture Textures::_createEmptyTexture(const vf2d& size) {
    Texture texture;

    texture.width = size.x;
    texture.height = size.y;

    texture.surface = nullptr;

    texture.texture = SDL_CreateTexture(Window::GetRenderer(), SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, (int)size.x, (int)size.y);

    SDL_SetTextureBlendMode(texture.texture, SDL_BLENDMODE_BLEND);
    return texture;
}

void Textures::_saveTextureAsPNG(Texture texture, const char *fileName) {

    SDL_Surface* surface = texture.surface;

    if (!surface) {
        int texWidth, texHeight;
        SDL_QueryTexture(texture.texture, NULL, NULL, &texWidth, &texHeight);
        surface = SDL_CreateSurface(texWidth, texHeight, 32);
        SDL_RenderReadPixels(Window::GetRenderer(), NULL, SDL_PIXELFORMAT_RGBA32, surface->pixels, surface->pitch);
    }

    SDL_Surface* rgbaSurface = surface;

    if (surface->format->format != SDL_PIXELFORMAT_RGBA32) {
        rgbaSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32);
    }

    unsigned char* pixels = new unsigned char[rgbaSurface->w * rgbaSurface->h * 4];

    SDL_LockSurface(rgbaSurface);
    memcpy(pixels, rgbaSurface->pixels, rgbaSurface->w * rgbaSurface->h * 4);
    SDL_UnlockSurface(rgbaSurface);

    stbi_write_png(fileName, rgbaSurface->w, rgbaSurface->h, 4, pixels, rgbaSurface->w * 4);

    delete[] pixels;

    if (surface->format->format != SDL_PIXELFORMAT_RGBA32) {
        SDL_DestroySurface(rgbaSurface);
    }
}
