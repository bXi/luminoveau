

#include "texturehandler.h"


Texture Textures::_getTexture(const char *fileName) {
    if (_textures.find(fileName) == _textures.end()) {
        Texture _tex = _loadTexture(fileName);
        _textures[fileName] = _tex;

        return _textures[fileName];
    } else {
        return _textures[fileName];
    }
}

void Textures::_drawTexture(Texture texture, Rectangle dest) {
    Rectangle source = {
            0,
            0,
            (float) texture.width,
            (float) texture.height

    };
    _drawTexture(texture, source, dest);
}

void Textures::_drawTexture(Texture texture, Rectangle source, Rectangle dest) {





}

Rectangle Textures::_getRectangle(int x, int y) {

    const Rectangle rect = {(float) (x * Configuration::tileWidth), (float) (y * Configuration::tileHeight),
                            (float) (Configuration::tileWidth),
                            (float) (Configuration::tileHeight)};
    return rect;
};

Rectangle Textures::_getTile(int tileId) {
    return getRectangle(tileId % 16, (int) tileId / 16);
};

Rectangle Textures::_getTile(int tileId, bool doubleHeight) {
    Rectangle r = getRectangle(tileId % 16, (int) tileId / 16);
    if (doubleHeight) {
        r.y -= r.height;
        r.height *= 2;
    }
    return r;
}


Texture Textures::_loadTexture(const char *fileName) {

    Texture texture;

    auto surface = IMG_Load(fileName);

    texture.width = surface->w;
    texture.height = surface->h;

    texture.surface = surface;

    texture.texture = SDL_CreateTextureFromSurface(Window::GetRenderer(), surface);

    SDL_SetTextureBlendMode(texture.texture, SDL_BLENDMODE_BLEND);
    return texture;

}

void Textures::_setTexture(int textureId) {


}
