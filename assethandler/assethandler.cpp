#include "assethandler.h"
#include "window/windowhandler.h"

Texture AssetHandler::_getTexture(const std::string &fileName) {
    if (_textures.find(fileName) == _textures.end()) {
        _loadTexture(fileName);

        return _textures[fileName];
    } else {
        return _textures[fileName];
    }
}

TextureAsset AssetHandler::_loadTexture(const std::string &fileName) {

    TextureAsset texture;

    auto surface = STBIMG_Load(fileName.c_str());

    if (!surface) {
        std::string error = Helpers::TextFormat("IMG_Load failed: %s", SDL_GetError());

        SDL_Log("%s", error.c_str());
        throw std::runtime_error(error.c_str());
    }

    texture.width  = surface->w;
    texture.height = surface->h;

    texture.filename = fileName;

    texture.surface = surface;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(Window::GetRenderer(), surface);
    texture.texture = tex;

    SDL_PropertiesID props = SDL_GetTextureProperties(tex);

    texture.id = props[SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER];

    SDL_SetTextureScaleMode(tex, (SDL_ScaleMode) defaultMode);

    if (!tex)
        SDL_Log("Texture failed: %s", SDL_GetError());

    auto error = SDL_SetTextureBlendMode(texture.texture, SDL_BLENDMODE_BLEND);

    if (error == SDL_FALSE)
        SDL_Log("Blendmode failed: %s", SDL_GetError());

    SDL_Log("Loaded texture: %s\n"
            "\tX: %i - Y: %i", fileName.c_str(), texture.width, texture.height);

    _textures[std::string(fileName)] = texture;

    return texture;
}

TextureAsset AssetHandler::_createEmptyTexture(const vf2d &size) {
    TextureAsset texture;

    texture.width  = size.x;
    texture.height = size.y;

    texture.surface = nullptr;

    texture.texture = SDL_CreateTexture(Window::GetRenderer(), SDL_PixelFormat::SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_TARGET, (int) size.x, (int) size.y);

    //TODO: fix for other renderers

    get()._createTextureId++;
    texture.id = get()._createTextureId;

    SDL_SetTextureScaleMode(texture.texture, (SDL_ScaleMode) defaultMode);

    return texture;
}

void AssetHandler::_saveTextureAsPNG(Texture texture, const char *fileName) {

    SDL_Surface *surface = texture.surface;

    if (!surface) {
        float texWidth, texHeight;

        SDL_PropertiesID props = SDL_GetTextureProperties(texture.texture);
        SDL_GetTextureSize(texture.texture, &texWidth, &texHeight);
        surface = SDL_CreateSurface((int) texWidth, (int) texHeight, SDL_PixelFormat::SDL_PIXELFORMAT_ARGB8888);

        if (!surface) {
            SDL_Log("%s", SDL_GetError());
        }

        SDL_RenderReadPixels(Window::GetRenderer(), nullptr);
    }

    SDL_Surface *rgbaSurface = surface;

    if (surface->format != SDL_PIXELFORMAT_ARGB8888) {
        rgbaSurface = SDL_ConvertSurface(surface, SDL_PixelFormat::SDL_PIXELFORMAT_ARGB8888);
    }

    unsigned char *pixels = new unsigned char[rgbaSurface->w * rgbaSurface->h * 4];

    SDL_LockSurface(rgbaSurface);
    memcpy(pixels, rgbaSurface->pixels, rgbaSurface->w * rgbaSurface->h * 4);
    SDL_UnlockSurface(rgbaSurface);

    stbi_write_png(fileName, rgbaSurface->w, rgbaSurface->h, 4, pixels, rgbaSurface->w * 4);

    delete[] pixels;

    if (surface->format != SDL_PixelFormat::SDL_PIXELFORMAT_ARGB8888) {
        SDL_DestroySurface(rgbaSurface);
    }
}

Sound AssetHandler::_getSound(const std::string &fileName) {
    if (_sounds.find(fileName) == _sounds.end()) {
        SoundAsset _sound;
        _sound.sound    = new ma_sound();
        _sound.fileName = fileName;
        ma_result result = ma_sound_init_from_file(Audio::GetAudioEngine(), fileName.c_str(),
                                                   MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr,
                                                   _sound.sound);

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
        ma_result result = ma_sound_init_from_file(Audio::GetAudioEngine(), fileName.c_str(),
                                                   MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr,
                                                   _music.music);

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

        // Load the font from the memory stream
        BLFontFace fontFace;
        BLResult   result = fontFace.createFromFile(fileName.c_str());

        if (result != BL_SUCCESS) {
            throw std::runtime_error("Can't load font.");
        }

        BLFont font;
        font.createFromFace(fontFace, (float) fontSize);
        _font.font = new BLFont(font);

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
