#pragma once

#include <unordered_map>

#include "SDL3/SDL.h"
#include "SDL3_image/SDL_image.h"

#include "configuration/configuration.h"

#include "utils/rectangles.h"
#include "utils/shader.h"

#include "window/windowhandler.h"

struct Texture
{
    int width;
    int height;

    SDL_Surface* surface;
};

struct Image
{
    int width;
    int height;

    SDL_Surface* surface;
};

class Textures {
public:
    static Texture GetTexture(const char* fileName)
    {
        return get()._getTexture(fileName);
    }

    static Rectangle getRectangle(int x, int y)
    {
        return get()._getRectangle(x, y);
    }

    static Rectangle getTile(int tileId)
    {
        return get()._getTile(tileId);
    }
    static Rectangle getTile(int tileId, bool doubleHeight)
    {
        return get()._getTile(tileId, doubleHeight);
    }

    static void Unload(Texture texture) {

    }

//    static void setTexture(const char* fileName, const Texture& _tex)
//    {
//        get()._setTexture(fileName, _tex);
//    }

    static void LoadTexture(const char* fileName)
    {
        get()._loadTexture(fileName);
    }

    static void DrawTexture(Texture texture, Rectangle dest)
    {
        get()._drawTexture(texture, dest);
    }

    static void DrawTexture(Texture texture, Rectangle source, Rectangle dest)
    {
        get()._drawTexture(texture, source, dest);
    }

    static void LoadEmptyTexture()
    {
        get()._loadEmptyTexture();
    }

private:
    std::unordered_map<const char*, Texture> _textures;

    Texture _getTexture(const char* fileName);

    Texture _loadTexture(const char* fileName);
    void _drawTexture(Texture texture, Rectangle dest);
    void _drawTexture(Texture texture, Rectangle source, Rectangle dest);
    Rectangle _getRectangle(int x, int y);

    Rectangle _getTile(int tileId);
    Rectangle _getTile(int tileId, bool doubleHeight);

    void _setTexture(int textureId);
//    void _setTextureFromImage(const char* fileName, const Image& _image);



    void _loadEmptyTexture();

public:
    Textures(const Textures&) = delete;
    static Textures& get() { static Textures instance; return instance; }
private:
    Textures() = default;
};

//*/