#pragma once

#include <unordered_map>
#include <string>

#include "SDL3/SDL.h"
#include "SDL3_image/SDL_image.h"

#include "configuration/configuration.h"

#include "utils/vectors.h"
#include "utils/rectangles.h"
#include "utils/shader.h"

#include "assettypes/texture.h"

class Textures {
public:
    static Texture GetTexture(const char *fileName) { return get()._getTexture(fileName); }

    static rectf GetRectangle(int x, int y) { return get()._getRectangle(x, y); }

    static rectf GetRectangle(int x, int y, int spriteWidth, int spriteHeight) { return get()._getRectangle(x, y, spriteWidth, spriteHeight); }

    static rectf GetTile(int tileId) { return get()._getTile(tileId); }

    static rectf GetTile(int tileId, bool doubleHeight) { return get()._getTile(tileId, doubleHeight); }

    static rectf GetTile(int tileId, bool doubleHeight, int spriteWidth, int spriteHeight) {
        return get()._getTile(tileId, doubleHeight, spriteWidth, spriteHeight);
    }

    static void LoadTexture(const char *fileName) { get()._loadTexture(fileName); }

    static void SaveTextureAsPNG(Texture texture, const char *fileName) { get()._saveTextureAsPNG(texture, fileName); }

    static Texture CreateEmptyTexture(vf2d size) { return get()._createEmptyTexture(size); }

    static std::unordered_map<std::string, Texture> GetTextures() { return get()._textures; }

private:
    std::unordered_map<std::string, Texture> _textures;

    Texture _getTexture(const std::string &fileName);

    Texture _loadTexture(const std::string &fileName);

    rectf _getRectangle(int x, int y);

    rectf _getRectangle(int x, int y, int spriteWidth, int spriteHeight);

    rectf _getTile(int tileId);

    rectf _getTile(int tileId, bool doubleHeight);

    rectf _getTile(int tileId, bool doubleHeight, int spriteWidth, int spriteHeight);

    Texture _createEmptyTexture(const vf2d &size);

    void _saveTextureAsPNG(Texture texture, const char *fileName);

public:
    Textures(const Textures &) = delete;

    static Textures &get() {
        static Textures instance;
        return instance;
    }

private:
    Textures() = default;
};

//*/