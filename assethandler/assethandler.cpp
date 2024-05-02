#include "assethandler.h"
#include "window/windowhandler.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image_write.h"

Texture AssetHandler::_getTexture(const std::string &fileName) {
    if (_textures.find(fileName) == _textures.end()) {
        auto _tex = _loadTexture(fileName);
        _textures[std::string(fileName)] = _tex;

        return _textures[fileName];
    } else {
        return _textures[fileName];
    }
}

TextureAsset AssetHandler::_loadTexture(const std::string &fileName) {

    TextureAsset texture;

    auto surface = IMG_Load(fileName.c_str());

    if (!surface) {
        std::string error = Helpers::TextFormat("IMG_Load failed: %s", SDL_GetError());

        SDL_Log(error.c_str());
        throw std::runtime_error(error.c_str());
    }

    texture.width = surface->w;
    texture.height = surface->h;

    texture.filename = fileName;

    texture.surface = surface;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(Window::GetRenderer(), surface);
    texture.texture = tex;


    SDL_SetTextureScaleMode(tex, (SDL_ScaleMode)defaultMode);

    if (!tex)
        SDL_Log("Texture failed: %s", SDL_GetError());

    auto error = SDL_SetTextureBlendMode(texture.texture, SDL_BLENDMODE_BLEND);

    if (error)
        SDL_Log("Blendmode failed: %s", SDL_GetError());

    SDL_Log("Loaded texture: %s\n"
            "\tX: %i - Y: %i", fileName.c_str(), texture.width, texture.height);

    return texture;

}

TextureAsset AssetHandler::_createEmptyTexture(const vf2d &size) {
    TextureAsset texture;

    texture.width = size.x;
    texture.height = size.y;

    texture.surface = nullptr;

    texture.texture = SDL_CreateTexture(Window::GetRenderer(), SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, (int) size.x, (int) size.y);

    SDL_SetTextureBlendMode(texture.texture, SDL_BLENDMODE_BLEND);
    return texture;
}

void AssetHandler::_saveTextureAsPNG(Texture texture, const char *fileName) {

    SDL_Surface *surface = texture.surface;

    if (!surface) {
        int texWidth, texHeight;
        SDL_QueryTexture(texture.texture, NULL, NULL, &texWidth, &texHeight);
        surface = SDL_CreateSurface(texWidth, texHeight,SDL_PixelFormatEnum::SDL_PIXELFORMAT_RGBA32);

        if (!surface) {
            SDL_Log("%s", SDL_GetError());
        }

        SDL_RenderReadPixels(Window::GetRenderer(), nullptr);

    }

    SDL_Surface *rgbaSurface = surface;

    if (surface->format->format != SDL_PIXELFORMAT_RGBA32) {
        rgbaSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32);
    }

    unsigned char *pixels = new unsigned char[rgbaSurface->w * rgbaSurface->h * 4];

    SDL_LockSurface(rgbaSurface);
    memcpy(pixels, rgbaSurface->pixels, rgbaSurface->w * rgbaSurface->h * 4);
    SDL_UnlockSurface(rgbaSurface);

    stbi_write_png(fileName, rgbaSurface->w, rgbaSurface->h, 4, pixels, rgbaSurface->w * 4);

    delete[] pixels;

    if (surface->format->format != SDL_PIXELFORMAT_RGBA32) {
        SDL_DestroySurface(rgbaSurface);
    }
}

Sound AssetHandler::_getSound(const std::string &fileName) {
    if (_sounds.find(fileName) == _sounds.end()) {
        SoundAsset _sound;
        _sound.sound = new ma_sound();
        ma_result result = ma_sound_init_from_file(Audio::GetAudioEngine(), fileName.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr, _sound.sound);

        if (result != MA_SUCCESS) {
            std::string error = Helpers::TextFormat("GetSound failed: %s", fileName.c_str());

            SDL_Log("%s", error.c_str());
            throw std::runtime_error(error.c_str());
        }

        _sounds[fileName] = _sound;

        return _sounds[fileName];
    } else {
        return _sounds[fileName];
    }
}

Music AssetHandler::_getMusic(const std::string &fileName) {
    if (_musics.find(fileName) == _musics.end()) {
        MusicAsset _music;

        _music.music = new ma_sound();
        ma_result result = ma_sound_init_from_file(Audio::GetAudioEngine(), fileName.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr, _music.music);

        if (result != MA_SUCCESS) {
            std::string error = Helpers::TextFormat("GetMusic failed: %s", fileName.c_str());

            SDL_Log("%s", error.c_str());
            throw std::runtime_error(error.c_str());
        }

        _musics[fileName] = _music;

        return _musics[fileName];
    } else {
        return _musics[fileName];
    }
}


Font AssetHandler::_getFont(const std::string &fileName, const int fontSize) {
    std::string index = std::string(Helpers::TextFormat("%s%d", fileName.c_str(), fontSize));

    auto it = _fonts.find(index);

    if (it == _fonts.end()) {

        FontAsset _font;
        _font.font = TTF_OpenFont(fileName.c_str(), fontSize);

        if (_font.font == nullptr) {
            std::string error = Helpers::TextFormat("Couldn't load %d pt font from %s: %s\n",
                                                    fontSize, fileName.c_str(), SDL_GetError());

            SDL_Log("%s", error.c_str());
            throw std::runtime_error(error.c_str());
        }

        _fonts[index] = _font;

        return _fonts[index];
    } else {
        return _fonts[index];
    }
}

void AssetHandler::_setDefaultTextureScaleMode(ScaleMode mode) {
    defaultMode = mode;
}

ScaleMode AssetHandler::_getDefaultTextureScaleMode() {
    return defaultMode;
}
