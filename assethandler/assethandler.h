#pragma once

#include <unordered_map>
#include <string>

#include "SDL3/SDL.h"
#include "SDL3_image/SDL_image.h"

#include "utils/vectors.h"
#include "utils/rectangles.h"

#include "audio/audiohandler.h"

#include "assettypes/font.h"
#include "assettypes/music.h"
#include "assettypes/sound.h"
#include "assettypes/texture.h"

/**
 * @brief Manages assets and provides utility functions for working with assets.
 */
class AssetHandler {
public:
    /**
     * @brief Retrieves a texture by its file name.
     *
     * @param fileName The name of the texture file.
     * @return The texture corresponding to the file name.
     */
    static Texture GetTexture(const char *fileName) { return get()._getTexture(fileName); }

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

    /**
     * @brief Retrieves a font asset with the specified filename and font size.
     *
     * @param fileName The filename of the font asset.
     * @param fontSize The size of the font.
     * @return The font asset.
     */
    static Font GetFont(const char *fileName, const int fontSize) {
        return get()._getFont(fileName, fontSize);
    }

    /**
     * @brief Retrieves a music asset.
     *
     * @param fileName The filename of the music asset.
     * @return The music asset.
     */
    static Music& GetMusic(const char *fileName) {
        return get()._getMusic(fileName);
    }

    /**
     * @brief Retrieves a sound asset.
     *
     * @param fileName The filename of the sound asset.
     * @return The sound asset.
     */
    static Sound GetSound(const char *fileName) {
        return get()._getSound(fileName);
    }


    static std::unordered_map<std::string, MusicAsset> &GetLoadedMusics() {
        return get()._musics;
    }


private:
    // Textures

    Texture _getTexture(const std::string &fileName);

    Texture _loadTexture(const std::string &fileName);

    Texture _createEmptyTexture(const vf2d &size);

    void _saveTextureAsPNG(Texture texture, const char *fileName);

    // Fonts

    Font _getFont(const std::string &fileName, int fontSize);

    // Audio

    Sound _getSound(const std::string &fileName);

    Music _getMusic(const std::string &fileName);

    //Containers

    std::unordered_map<std::string, Font> _fonts;
    std::unordered_map<std::string, MusicAsset> _musics;
    std::unordered_map<std::string, Sound> _sounds;
    std::unordered_map<std::string, Texture> _textures;

public:
    AssetHandler(const AssetHandler &) = delete;

    static AssetHandler &get() {
        static AssetHandler instance;
        return instance;
    }

private:
    AssetHandler() = default;
};

//*/