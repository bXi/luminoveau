#pragma once

#include <unordered_map>
#include <string>
#include <utility>

#include "SDL3/SDL.h"
#include "SDL3_image/SDL_image.h"

#include "utils/vectors.h"
#include "utils/rectangles.h"

#include "audio/audiohandler.h"

#include "assettypes/font.h"
#include "assettypes/music.h"
#include "assettypes/sound.h"
#include "assettypes/texture.h"

enum class ScaleMode {
    NEAREST,
    LINEAR,
    BEST,
};

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
     * @brief Sets a scaling mode for all textures loaded after this call.
     *
     * @param mode The new ScaleMode to set as default.
     */
    static void SetDefaultTextureScaleMode(ScaleMode mode) { get()._setDefaultTextureScaleMode(mode); }

    /**
     * @brief Gets the current scaling mode.
     *
     * @return The current ScaleMode.
     */
    static ScaleMode GetDefaultTextureScaleMode() { return get()._getDefaultTextureScaleMode(); }


    /**
     * @brief Saves the given texture as a PNG file.
     *
     * @param texture The texture to save.
     * @param fileName The name of the PNG file to save.
     */
    static void SaveTextureAsPNG(Texture texture, const char *fileName) { get()._saveTextureAsPNG(std::move(texture), fileName); }

    /**
     * @brief Creates an empty texture with the specified size.
     *
     * @param size The size of the empty texture.
     * @return The created empty texture.
     */
    static Texture CreateEmptyTexture(const vf2d& size) { return get()._createEmptyTexture(size); }

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

    /**
     * @brief Retrieves the default font asset.
     *
     * @return The default font.
     */
    static Font GetDefaultFont() {
        return get().defaultFont;
    }



    static std::unordered_map<std::string, MusicAsset> &GetLoadedMusics() {
        return get()._musics;
    }


private:
    // Textures

    Texture _getTexture(const std::string &fileName);

    Texture _loadTexture(const std::string &fileName);

    void _setDefaultTextureScaleMode(ScaleMode mode);

    ScaleMode _getDefaultTextureScaleMode();

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

    ScaleMode defaultMode = ScaleMode::NEAREST;

    // default font asset Droid Sans Mono.ttf
    static const unsigned char DroidSansMono_ttf[];
    unsigned int DroidSansMono_ttf_len = 119380;

    Font defaultFont;

public:
    AssetHandler(const AssetHandler &) = delete;

    static AssetHandler &get() {
        static AssetHandler instance;
        return instance;
    }

private:
    AssetHandler() {
        SDL_RWops* rwops = SDL_RWFromConstMem(DroidSansMono_ttf, DroidSansMono_ttf_len);
        if (rwops == NULL) {
            // Handle error
            TTF_Quit();
            SDL_Quit();
            throw std::exception("Could not load the default font.");
        }

        // Load the font from the memory stream
        TTF_Font* font = TTF_OpenFontRW(rwops, 1, 16); // Replace "24" with your desired font size
        if (font == NULL) {
            // Handle error
            SDL_RWclose(rwops);
            TTF_Quit();
            SDL_Quit();
            throw std::exception("Could not load the default font.");
        }
        defaultFont.font = font;
    };
};

//*/