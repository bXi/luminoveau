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

/**
 * @brief Manages textures and provides utility functions for working with textures.
 */
class Textures {
public:
    /**
     * @brief Retrieves a texture by its file name.
     *
     * @param fileName The name of the texture file.
     * @return The texture corresponding to the file name.
     */
    static Texture GetTexture(const char *fileName) { return get()._getTexture(fileName); }

    /**
     * @brief Retrieves a rectangle at the specified position.
     *
     * @param x The x-coordinate of the rectangle.
     * @param y The y-coordinate of the rectangle.
     * @return The rectangle at the specified position.
     */
    static rectf GetRectangle(int x, int y) { return get()._getRectangle(x, y); }

    /**
     * @brief Retrieves a rectangle at the specified position with the given sprite width and height.
     *
     * @param x The x-coordinate of the rectangle.
     * @param y The y-coordinate of the rectangle.
     * @param spriteWidth The width of the sprite.
     * @param spriteHeight The height of the sprite.
     * @return The rectangle at the specified position with the given sprite dimensions.
     */
    static rectf GetRectangle(int x, int y, int spriteWidth, int spriteHeight) { return get()._getRectangle(x, y, spriteWidth, spriteHeight); }

    /**
     * @brief Retrieves a tile rectangle by its ID.
     *
     * @param tileId The ID of the tile.
     * @return The rectangle representing the tile.
     */
    static rectf GetTile(int tileId) { return get()._getTile(tileId); }

    /**
     * @brief Retrieves a tile rectangle by its ID, with optional double height.
     *
     * @param tileId The ID of the tile.
     * @param doubleHeight Indicates if the tile should be double height.
     * @return The rectangle representing the tile.
     */
    static rectf GetTile(int tileId, bool doubleHeight) { return get()._getTile(tileId, doubleHeight); }

    /**
     * @brief Retrieves a tile rectangle by its ID, with optional double height and sprite dimensions.
     *
     * @param tileId The ID of the tile.
     * @param doubleHeight Indicates if the tile should be double height.
     * @param spriteWidth The width of the sprite.
     * @param spriteHeight The height of the sprite.
     * @return The rectangle representing the tile.
     */
    static rectf GetTile(int tileId, bool doubleHeight, int spriteWidth, int spriteHeight) {
        return get()._getTile(tileId, doubleHeight, spriteWidth, spriteHeight);
    }

    /**
     * @brief Loads a texture from the specified file.
     *
     * @param fileName The name of the texture file to load.
     */
    static void LoadTexture(const char *fileName) { get()._loadTexture(fileName); }

    /**
     * @brief Saves the given texture as a PNG file.
     *
     * @param texture The texture to save.
     * @param fileName The name of the PNG file to save.
     */
    static void SaveTextureAsPNG(Texture texture, const char *fileName) { get()._saveTextureAsPNG(texture, fileName); }

    /**
     * @brief Creates an empty texture with the specified size.
     *
     * @param size The size of the empty texture.
     * @return The created empty texture.
     */
    static Texture CreateEmptyTexture(vf2d size) { return get()._createEmptyTexture(size); }

    /**
     * @brief Retrieves a map of all loaded textures.
     *
     * @return The map of loaded textures.
     */
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