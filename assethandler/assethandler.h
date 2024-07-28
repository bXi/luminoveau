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
    static void SaveTextureAsPNG(Texture texture, const char *fileName) { get()._saveTextureAsPNG(texture, fileName); }

    /**
     * @brief Creates an empty texture with the specified size.
     *
     * @param size The size of the empty texture.
     * @return The created empty texture.
     */
    static TextureAsset CreateEmptyTexture(const vf2d& size) { return get()._createEmptyTexture(size); }

    /**
     * @brief Retrieves a map of all loaded textures.
     *
     * @return The map of loaded textures.
     */
    static std::unordered_map<std::string, TextureAsset> GetTextures() { return get()._textures; }

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


    template<typename T>
    static void Delete(T& asset) {
        get()._delete(asset);
    }





    static std::unordered_map<std::string, MusicAsset> &GetLoadedMusics() {
        return get()._musics;
    }


private:
    // Textures

    Texture _getTexture(const std::string &fileName);

    TextureAsset _loadTexture(const std::string &fileName);

    void _setDefaultTextureScaleMode(ScaleMode mode);

    ScaleMode _getDefaultTextureScaleMode();

    TextureAsset _createEmptyTexture(const vf2d &size);

    void _saveTextureAsPNG(Texture texture, const char *fileName);

    // Fonts

    Font _getFont(const std::string &fileName, int fontSize);

    // Audio

    Sound _getSound(const std::string &fileName);

    Music _getMusic(const std::string &fileName);

    //Containers

    std::unordered_map<std::string, FontAsset> _fonts;
    std::unordered_map<std::string, MusicAsset> _musics;
    std::unordered_map<std::string, SoundAsset> _sounds;
    std::unordered_map<std::string, TextureAsset> _textures;
    int _createTextureId = 0;

    ScaleMode defaultMode = ScaleMode::NEAREST;

    // default font asset Droid Sans Mono.ttf
    static const unsigned char DroidSansMono_ttf[];
    unsigned int DroidSansMono_ttf_len = 119380;

    FontAsset defaultFont;


    template<typename T>
    void _delete(T& asset) {

        int a =1;




        if constexpr (std::is_same_v<T, FontAsset>) {
            std::cout << "Deleting Font..." << std::endl;
            auto it = std::find_if(_fonts.begin(), _fonts.end(), [&](const auto& pair) {
                return &pair.second == &asset;
            });

            if (it != _fonts.end()) {
                TTF_CloseFont(static_cast<FontAsset>(asset).font);
                _fonts.erase(it);
            } else {
                throw std::runtime_error("Font not found in the map");
            }
        } else if constexpr (std::is_same_v<T, MusicAsset>) {
            std::cout << "Deleting Music..." << std::endl;
            auto it = std::find_if(_musics.begin(), _musics.end(), [&](const auto& pair) {
                return &pair.second == &asset;
            });

            if (it != _musics.end()) {
                ma_sound_uninit(static_cast<MusicAsset>(asset).music);
                _musics.erase(it);
            } else {
                throw std::runtime_error("MusicAsset not found in the map");
            }
        } else if constexpr (std::is_same_v<T, SoundAsset>) {
            std::cout << "Deleting Sound..." << std::endl;
            auto it = std::find_if(_sounds.begin(), _sounds.end(), [&](const auto& pair) {
                return &pair.second == &asset;
            });

            if (it != _sounds.end()) {
                ma_sound_uninit(static_cast<SoundAsset>(asset).sound);
                _sounds.erase(it);
            } else {
                throw std::runtime_error("Sound not found in the map");
            }
        } else if constexpr (std::is_same_v<T, TextureAsset>) {
            std::cout << "Deleting Texture..." << std::endl;
            auto it = std::find_if(_textures.begin(), _textures.end(), [&](const auto& pair) {
                return &pair.second == &asset;
            });

            if (it != _textures.end()) {
                if (static_cast<TextureAsset>(asset).surface) {
                    SDL_DestroySurface(static_cast<Texture>(asset).surface);
                }
                if (static_cast<Texture>(asset).texture) {
                    SDL_DestroyTexture(static_cast<Texture>(asset).texture);
                }
                _textures.erase(it);
            } else {
                throw std::runtime_error("Texture not found in the map");
            }
        } else {
            throw std::runtime_error("Trying to delete invalid asset");
        }


    };


public:
    AssetHandler(const AssetHandler &) = delete;

    static AssetHandler &get() {
        static AssetHandler instance;
        return instance;
    }

private:
    AssetHandler() {

        if (!TTF_WasInit()) {
            TTF_Init();
        }

        SDL_IOStream* rwops = SDL_IOFromConstMem(DroidSansMono_ttf, DroidSansMono_ttf_len);
        if (rwops == NULL) {
            // Handle error
            TTF_Quit();
            SDL_Quit();
            throw std::runtime_error("Could not load the default font.");
        }

        // Load the font from the memory stream
        TTF_Font* font = TTF_OpenFontIO(rwops, 1, 16); // Replace "24" with your desired font size
        if (font == NULL) {
            // Handle error
            SDL_CloseIO(rwops);
            TTF_Quit();
            SDL_Quit();
            throw std::runtime_error("Could not load the default font.");
        }
        defaultFont.font = font;
    };
};

//*/
