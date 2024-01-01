#pragma once

#include <unordered_map>

#include "SDL3/SDL.h"
#include "SDL3_image/SDL_image.h"

#include "configuration/configuration.h"

#include "utils/vectors.h"
#include "utils/rectangles.h"
#include "utils/shader.h"

#include "assettypes/texture.h"


class Textures {
public:
    static Texture GetTexture(const char* fileName) { return get()._getTexture(fileName); }

    static Rectangle GetRectangle(int x, int y) { return get()._getRectangle(x, y); }
    static Rectangle GetTile(int tileId) { return get()._getTile(tileId); }
    static Rectangle GetTile(int tileId, bool doubleHeight) { return get()._getTile(tileId, doubleHeight); }

    static void LoadTexture(const char* fileName) { get()._loadTexture(fileName); }
    static void SaveTextureAsPNG(Texture texture, const char* fileName) { get()._saveTextureAsPNG(texture, fileName); }

    static Texture CreateEmptyTexture(vf2d size) { return get()._createEmptyTexture(size); }

    static std::unordered_map<const char*, Texture> GetTextures() { return get()._textures; }
private:
    std::unordered_map<const char*, Texture> _textures;

    Texture _getTexture(const char* fileName);

    Texture _loadTexture(const char* fileName);

    Rectangle _getRectangle(int x, int y);
    Rectangle _getTile(int tileId);
    Rectangle _getTile(int tileId, bool doubleHeight);

    Texture _createEmptyTexture(const vf2d& size);

    void _saveTextureAsPNG(Texture texture, const char* fileName);

public:
    Textures(const Textures&) = delete;
    static Textures& get() { static Textures instance; return instance; }
private:
    Textures() = default;
};

//*/