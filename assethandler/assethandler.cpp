#include "assethandler.h"
#include "window/windowhandler.h"
#include "spirv_cross.hpp"
#include "log/loghandler.h"

#include <iostream>
#include <vector>

#include "renderer/rendererhandler.h"
#include "renderer/shaderhandler.h"

#include <msdf-atlas-gen/msdf-atlas-gen.h>
#include <msdfgen/msdfgen.h>
#include <SDL3_image/SDL_image.h>

#include "picosha2.h"


AssetHandler::AssetHandler() {
    // Reserve space to prevent map reallocation (important since we return references!)
    _textures.reserve(1000);
    _sounds.reserve(100);
    _musics.reserve(50);
    _fonts.reserve(50);
    _shaders.reserve(50);

    // Initialize font cache
    _initFontCache();
    
    // Load default font using MSDF from embedded data
    LOG_INFO("Loading default MSDF font");
    
    // Compute hash of embedded font data for cache validation
    std::string embeddedHash = _computeFontCacheKeyFromData(DroidSansMono_ttf, DroidSansMono_ttf_len);
    
    // Try loading from cache first
    if (!_loadFontFromCache("__default_font__", 16, defaultFont, embeddedHash)) {
        // Cache miss — generate from scratch
        LOG_INFO("Default font not in cache, generating MSDF atlas");
        
        msdfgen::FreetypeHandle *ft = msdfgen::initializeFreetype();
        if (!ft) {
            LOG_CRITICAL("Failed to initialize FreeType for default font");
        }
        
        defaultFont.fontHandle = msdfgen::loadFontData(ft, 
            (const unsigned char*)DroidSansMono_ttf, DroidSansMono_ttf_len);
        
        if (!defaultFont.fontHandle) {
            msdfgen::deinitializeFreetype(ft);
            LOG_CRITICAL("Failed to load default font");
        }
        
        // Use temporary msdf vector for generation, then convert to CachedGlyph
        std::vector<msdf_atlas::GlyphGeometry> msdfGlyphs;
        
        msdf_atlas::FontGeometry fontGeometry(&msdfGlyphs);
        msdf_atlas::Charset charset;
        for (uint32_t cp = 0x20; cp <= 0x17F; ++cp) charset.add(cp);
        fontGeometry.loadCharset(defaultFont.fontHandle, 1.0, charset);
        
        defaultFont.ascender = fontGeometry.getMetrics().ascenderY;
        defaultFont.descender = fontGeometry.getMetrics().descenderY;
        defaultFont.lineHeight = fontGeometry.getMetrics().lineHeight;
        
        const double maxCornerAngle = 3.0;
        for (msdf_atlas::GlyphGeometry &glyph : msdfGlyphs) {
            glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, maxCornerAngle, 0);
        }
        
        msdf_atlas::TightAtlasPacker packer;
        packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::SQUARE);
        packer.setMinimumScale(64.0);
        packer.setPixelRange(4.0);
        packer.setMiterLimit(1.0);
        packer.pack(msdfGlyphs.data(), msdfGlyphs.size());
        
        packer.getDimensions(defaultFont.atlasWidth, defaultFont.atlasHeight);
        
        LOG_INFO("Default font MSDF atlas: {}x{}", defaultFont.atlasWidth, defaultFont.atlasHeight);
        
        msdf_atlas::ImmediateAtlasGenerator<
            float, 3,
            msdf_atlas::msdfGenerator,
            msdf_atlas::BitmapAtlasStorage<unsigned char, 3>
        > generator(defaultFont.atlasWidth, defaultFont.atlasHeight);
        
        generator.setThreadCount(8);
        generator.generate(msdfGlyphs.data(), msdfGlyphs.size());
        
        msdfgen::BitmapConstRef<unsigned char, 3> bitmap = generator.atlasStorage();
        
        std::vector<unsigned char> rgbaData(defaultFont.atlasWidth * defaultFont.atlasHeight * 4);
        for (int y = 0; y < defaultFont.atlasHeight; ++y) {
            for (int x = 0; x < defaultFont.atlasWidth; ++x) {
                int srcY = defaultFont.atlasHeight - 1 - y;
                int idx = (y * defaultFont.atlasWidth + x);
                const unsigned char *pixel = bitmap(x, srcY);
                rgbaData[idx * 4 + 0] = pixel[0];
                rgbaData[idx * 4 + 1] = pixel[1];
                rgbaData[idx * 4 + 2] = pixel[2];
                rgbaData[idx * 4 + 3] = 255;
            }
        }
        
        SDL_GPUTextureCreateInfo textureInfo{
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = static_cast<Uint32>(defaultFont.atlasWidth),
            .height = static_cast<Uint32>(defaultFont.atlasHeight),
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
        };
        
        defaultFont.atlasTexture = SDL_CreateGPUTexture(Renderer::GetDevice(), &textureInfo);
        
        if (!_copy_to_texture(Renderer::GetDevice(), rgbaData.data(),
                             rgbaData.size(), defaultFont.atlasTexture,
                             defaultFont.atlasWidth, defaultFont.atlasHeight)) {
            LOG_CRITICAL("Failed to upload default font MSDF atlas to GPU");
        }
        
        // Convert msdf_atlas::GlyphGeometry -> CachedGlyph
        defaultFont.glyphs = new std::vector<CachedGlyph>();
        defaultFont.glyphMap = new std::unordered_map<uint32_t, size_t>();
        for (size_t i = 0; i < msdfGlyphs.size(); ++i) {
            CachedGlyph cached;
            cached.codepoint = msdfGlyphs[i].getCodepoint();
            cached.advance = msdfGlyphs[i].getAdvance();
            msdfGlyphs[i].getQuadPlaneBounds(cached.pl, cached.pb, cached.pr, cached.pt);
            msdfGlyphs[i].getQuadAtlasBounds(cached.al, cached.ab, cached.ar, cached.at);
            defaultFont.glyphs->push_back(cached);
            if (cached.codepoint > 0) {
                (*defaultFont.glyphMap)[cached.codepoint] = i;
            }
        }
        
        defaultFont.generatedSize = 64;
        defaultFont.defaultRenderSize = 16;
        
        // Save to cache for next startup
        _saveFontToCache("__default_font__", defaultFont, rgbaData, embeddedHash);
    }
    
    LOG_INFO("Default MSDF font loaded ({} glyphs)", defaultFont.glyphs->size());
};

void AssetHandler::_cleanup() {

    std::lock_guard<std::mutex> lock(assetMutex);
    auto device = Renderer::GetDevice();
    
    // Cleanup textures
    for (auto& [name, tex] : _textures) {
        if (tex.gpuTexture) {
            SDL_ReleaseGPUTexture(device, tex.gpuTexture);
            tex.gpuTexture = nullptr;
        }
    }
    _textures.clear();
    
    // Cleanup shaders
    for (auto& [name, shader] : _shaders) {
        if (shader.shader) {
            SDL_ReleaseGPUShader(device, shader.shader);
            shader.shader = nullptr;
        }
    }
    _shaders.clear();
    
    // Cleanup fonts
    for (auto& [name, font] : _fonts) {
        if (font.fontHandle) {
            msdfgen::destroyFont(font.fontHandle);
            font.fontHandle = nullptr;
        }
        if (font.glyphs) {
            delete font.glyphs;
            font.glyphs = nullptr;
        }
        if (font.glyphMap) {
            delete font.glyphMap;
            font.glyphMap = nullptr;
        }
        if (font.atlasTexture) {
            SDL_ReleaseGPUTexture(device, font.atlasTexture);
            font.atlasTexture = nullptr;
        }
        if (font.fontData) {
            free(font.fontData);
            font.fontData = nullptr;
        }
    }
    _fonts.clear();
    
    // Cleanup sounds
    for (auto& [name, sound] : _sounds) {
        if (sound.sound) {
            ma_sound_uninit(sound.sound);
            delete sound.sound;
            sound.sound = nullptr;
        }
        if (sound.fileData) {
            free(sound.fileData);
            sound.fileData = nullptr;
        }
    }
    _sounds.clear();
    
    // Cleanup music
    for (auto& [name, music] : _musics) {
        if (music.music) {
            ma_sound_uninit(music.music);
            delete music.music;
            music.music = nullptr;
        }
        if (music.fileData) {
            free(music.fileData);
            music.fileData = nullptr;
        }
    }
    _musics.clear();
    
    // Cleanup default font
    if (defaultFont.fontHandle) {
        msdfgen::destroyFont(defaultFont.fontHandle);
        defaultFont.fontHandle = nullptr;
    }
    if (defaultFont.glyphs) {
        delete defaultFont.glyphs;
        defaultFont.glyphs = nullptr;
    }
    if (defaultFont.glyphMap) {
        delete defaultFont.glyphMap;
        defaultFont.glyphMap = nullptr;
    }
    if (defaultFont.atlasTexture) {
        SDL_ReleaseGPUTexture(device, defaultFont.atlasTexture);
        defaultFont.atlasTexture = nullptr;
    }
    // Note: defaultFont.fontData is NOT allocated (uses embedded data), so no need to free
    
    // Cleanup font cache
    if (_fontCache) {
        delete _fontCache;
        _fontCache = nullptr;
    }
    
    LOG_INFO("asset cleanup complete");
}

Texture AssetHandler::_getTexture(const std::string &fileName) {
    std::lock_guard<std::mutex> lock(assetMutex);
    
    if (_textures.find(fileName) == _textures.end()) {
        _loadTexture(fileName);
        return _textures[fileName];
    } else {
        return _textures[fileName];
    }
}

TextureAsset AssetHandler::_loadTexture(const std::string &fileName) {

    if (!Renderer::GetDevice()) {
        LOG_WARNING("Skipping texture load after shutdown: {}", fileName);
        return {};
    }

    TextureAsset texture;

    auto filedata = FileHandler::ReadFile(fileName);
    SDL_IOStream* io = SDL_IOFromMem(filedata.data, filedata.fileSize);
    SDL_Surface* surface = IMG_Load_IO(io, true); // SDL_TRUE = close IO after reading

    if (!surface) {
        LOG_CRITICAL("IMG_Load failed: {}", SDL_GetError());
    }

    if (surface->format != SDL_PIXELFORMAT_RGBA32) {
        SDL_Surface* convertedSurface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface); // Free the original surface

        surface = convertedSurface;
    }

    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);

    texture.filename = fileName.c_str();
    texture.width  = surface->w;
    texture.height = surface->h;

    SDL_GPUTextureCreateInfo texture_create_info{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = static_cast<Uint32>(texture.width),
        .height = static_cast<Uint32>(texture.height),
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    SDL_GPUTexture *gpuTexture = SDL_CreateGPUTexture(Renderer::GetDevice(), &texture_create_info);
    if (!gpuTexture)
    {

        LOG_CRITICAL("failed to create texture: {} ({})", fileName.c_str(), SDL_GetError());
    }

    if (!_copy_to_texture(
            Renderer::GetDevice(),
            surface->pixels,
            texture.width * texture.height * 4,
            gpuTexture,
            texture.width,
            texture.height
        ))
    {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), gpuTexture);
        LOG_CRITICAL("failed to copy image data to texture");
    }

    texture.gpuSampler = Renderer::GetSampler(defaultMode);
    texture.gpuTexture = gpuTexture;

    if(texture.filename && texture.filename[0]) {
        SDL_SetGPUTextureName(Renderer::GetDevice(), gpuTexture, texture.filename);
    }

    SDL_DestroySurface(surface);
    free(filedata.data);

    LOG_INFO("loaded texture {} ({}x{})", fileName.c_str(), texture.width, texture.height);

    _textures[std::string(fileName)] = texture;
    
    // FIX: Point filename to the stable string in the map, not the local variable!
    _textures[fileName].filename = _textures.find(fileName)->first.c_str();

    return texture;
}

TextureAsset AssetHandler::_createEmptyTexture(const vf2d &size) {
    TextureAsset texture;

    texture.width  = size.x;
    texture.height = size.y;

    SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(Renderer::GetDevice(), Window::GetWindow());

    SDL_GPUTextureCreateInfo sdlGpuTextureCreateInfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = swapchainFormat,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = static_cast<Uint32>(size.x),
        .height = static_cast<Uint32>(size.y),
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };

    texture.gpuSampler = Renderer::GetSampler(defaultMode);
    texture.gpuTexture = SDL_CreateGPUTexture(Renderer::GetDevice(), &sdlGpuTextureCreateInfo);

    return texture;
}

void AssetHandler::_saveTextureAsPNG(Texture texture, const char *fileName) {
    LUMI_UNUSED(texture, fileName);
}

Sound AssetHandler::_getSound(const std::string &fileName) {
    std::lock_guard<std::mutex> lock(assetMutex);
    
    if (_sounds.find(fileName) == _sounds.end()) {
        SoundAsset _sound;
        _sound.sound    = new ma_sound();
        _sound.fileName = fileName;

        auto filedata = FileHandler::ReadFile(fileName);
        
        // Store fileData so we can free it in cleanup
        _sound.fileData = filedata.data;
        
        ma_resource_manager_register_encoded_data(Audio::GetAudioEngine()->pResourceManager, fileName.c_str(), filedata.data, filedata.fileSize);

        ma_result result = ma_sound_init_from_file(Audio::GetAudioEngine(), fileName.c_str(),
                                                   MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC,
                                                   Audio::GetChannelGroup(AudioChannel::SFX),
                                                   nullptr, _sound.sound);

        if (result != MA_SUCCESS) {
            free(filedata.data);  // Free on error
            delete _sound.sound;

            LOG_CRITICAL("GetSound failed: {}", fileName.c_str());
        }

        _sounds[fileName] = _sound;

        return _sounds[fileName];
    } else {
        return _sounds[fileName];
    }
}

Music AssetHandler::_getMusic(const std::string &fileName) {
    std::lock_guard<std::mutex> lock(assetMutex);
    
    if (_musics.find(fileName) == _musics.end()) {
        MusicAsset _music;

        _music.music = new ma_sound();

        auto filedata = FileHandler::ReadFile(fileName);
        
        // Store fileData so we can free it in cleanup
        _music.fileData = filedata.data;
        
        ma_resource_manager_register_encoded_data(Audio::GetAudioEngine()->pResourceManager, fileName.c_str(), filedata.data, filedata.fileSize);

        ma_result result = ma_sound_init_from_file(Audio::GetAudioEngine(), fileName.c_str(),
                                                   MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC,
                                                   Audio::GetChannelGroup(AudioChannel::Music),
                                                   nullptr, _music.music);

        if (result != MA_SUCCESS) {
            free(filedata.data);  // Free on error
            delete _music.music;
            LOG_CRITICAL("GetMusic failed: {}", fileName.c_str());
        }

        _musics[fileName] = _music;

        return _musics[fileName];
    } else {
        return _musics[fileName];
    }
}

Font AssetHandler::_getFont(const std::string &fileName, const int fontSize) {
    std::lock_guard<std::mutex> lock(assetMutex);
    
    // Check if this font file is already loaded (ignore size, we'll set defaultRenderSize)
    auto it = _fonts.find(fileName);
    if (it != _fonts.end()) {
        _fonts[fileName].defaultRenderSize = fontSize;
        return _fonts[fileName];
    }

    // Try loading from font cache
    FontAsset _font;
    if (_loadFontFromCache(fileName, fontSize, _font)) {
        _fonts[fileName] = _font;
        return _fonts[fileName];
    }

    // Cache miss - generate MSDF atlas from scratch
    LOG_INFO("Generating MSDF font {} (atlas size: 64, default render: {})", fileName.c_str(), fontSize);

    auto filedata = FileHandler::ReadFile(fileName);
    _font.fontData = filedata.data;
    
    msdfgen::FreetypeHandle *ft = msdfgen::initializeFreetype();
    if (!ft) {
        free(filedata.data);
        LOG_CRITICAL("Failed to initialize FreeType for MSDF: {}", fileName.c_str());
    }
    
    _font.fontHandle = msdfgen::loadFontData(ft, 
        (const unsigned char*)filedata.data, filedata.fileSize);
    
    if (!_font.fontHandle) {
        free(filedata.data);
        msdfgen::deinitializeFreetype(ft);
        LOG_CRITICAL("Failed to load font for MSDF: {}", fileName.c_str());
    }
    
    std::vector<msdf_atlas::GlyphGeometry> msdfGlyphs;
    
    msdf_atlas::FontGeometry fontGeometry(&msdfGlyphs);
    msdf_atlas::Charset charset;
    for (uint32_t cp = 0x20; cp <= 0x17F; ++cp) charset.add(cp);
    fontGeometry.loadCharset(_font.fontHandle, 1.0, charset);
    
    _font.ascender = fontGeometry.getMetrics().ascenderY;
    _font.descender = fontGeometry.getMetrics().descenderY;
    _font.lineHeight = fontGeometry.getMetrics().lineHeight;
    
    const double maxCornerAngle = 3.0;
    for (msdf_atlas::GlyphGeometry &glyph : msdfGlyphs) {
        glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, maxCornerAngle, 0);
    }
    
    const int ATLAS_GENERATION_SIZE = 64;
    
    msdf_atlas::TightAtlasPacker packer;
    packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::SQUARE);
    packer.setMinimumScale(ATLAS_GENERATION_SIZE);
    packer.setPixelRange(4.0);
    packer.setMiterLimit(1.0);
    packer.pack(msdfGlyphs.data(), msdfGlyphs.size());
    
    packer.getDimensions(_font.atlasWidth, _font.atlasHeight);
    
    LOG_INFO("MSDF atlas for {}: {}x{}", fileName.c_str(), _font.atlasWidth, _font.atlasHeight);
    
    msdf_atlas::ImmediateAtlasGenerator<
        float, 3,
        msdf_atlas::msdfGenerator,
        msdf_atlas::BitmapAtlasStorage<unsigned char, 3>
    > generator(_font.atlasWidth, _font.atlasHeight);
    
    generator.setThreadCount(8);
    generator.generate(msdfGlyphs.data(), msdfGlyphs.size());
    
    msdfgen::BitmapConstRef<unsigned char, 3> bitmap = generator.atlasStorage();
    
    std::vector<unsigned char> rgbaData(_font.atlasWidth * _font.atlasHeight * 4);
    for (int y = 0; y < _font.atlasHeight; ++y) {
        for (int x = 0; x < _font.atlasWidth; ++x) {
            int srcY = _font.atlasHeight - 1 - y;
            int idx = (y * _font.atlasWidth + x);
            const unsigned char *pixel = bitmap(x, srcY);
            rgbaData[idx * 4 + 0] = pixel[0];
            rgbaData[idx * 4 + 1] = pixel[1];
            rgbaData[idx * 4 + 2] = pixel[2];
            rgbaData[idx * 4 + 3] = 255;
        }
    }
    
    SDL_GPUTextureCreateInfo textureInfo{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = static_cast<Uint32>(_font.atlasWidth),
        .height = static_cast<Uint32>(_font.atlasHeight),
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };
    
    _font.atlasTexture = SDL_CreateGPUTexture(Renderer::GetDevice(), &textureInfo);
    
    if (!_copy_to_texture(Renderer::GetDevice(), rgbaData.data(),
                         rgbaData.size(), _font.atlasTexture,
                         _font.atlasWidth, _font.atlasHeight)) {
        LOG_CRITICAL("Failed to upload MSDF atlas to GPU: {}", fileName.c_str());
    }
    
    // Convert msdf_atlas::GlyphGeometry -> CachedGlyph
    _font.glyphs = new std::vector<CachedGlyph>();
    _font.glyphMap = new std::unordered_map<uint32_t, size_t>();
    for (size_t i = 0; i < msdfGlyphs.size(); ++i) {
        CachedGlyph cached;
        cached.codepoint = msdfGlyphs[i].getCodepoint();
        cached.advance = msdfGlyphs[i].getAdvance();
        msdfGlyphs[i].getQuadPlaneBounds(cached.pl, cached.pb, cached.pr, cached.pt);
        msdfGlyphs[i].getQuadAtlasBounds(cached.al, cached.ab, cached.ar, cached.at);
        _font.glyphs->push_back(cached);
        if (cached.codepoint > 0) {
            (*_font.glyphMap)[cached.codepoint] = i;
        }
    }
    
    _font.generatedSize = ATLAS_GENERATION_SIZE;
    _font.defaultRenderSize = fontSize;
    
    // Save to font cache
    _saveFontToCache(fileName, _font, rgbaData);
    
    LOG_INFO("Loaded MSDF font {} ({} glyphs, default render size: {})", fileName.c_str(), _font.glyphs->size(), fontSize);

    _fonts[fileName] = _font;
    return _fonts[fileName];
}

void AssetHandler::_setDefaultTextureScaleMode(ScaleMode mode) {
    defaultMode = mode;
}

ScaleMode AssetHandler::_getDefaultTextureScaleMode() {
    return defaultMode;
}

Shader AssetHandler::_getShader(const std::string &fileName) {
    std::lock_guard<std::mutex> lock(assetMutex);

    if (_shaders.find(fileName) == _shaders.end()) {

        LOG_INFO("loading shader: {}", fileName.c_str());

        // Auto-detect the shader stage from the file name
        SDL_GPUShaderStage stage;
        if (SDL_strstr(fileName.c_str(), ".vert")) {
            stage = SDL_GPU_SHADERSTAGE_VERTEX;
        } else if (SDL_strstr(fileName.c_str(), ".frag")) {
            stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        } else {
            LOG_CRITICAL("invalid shader stage");
        }

        // Use Shaders::CreateShaderAsset which handles format correctly
        ShaderAsset _shader = Shaders::CreateShaderAsset(
            Renderer::GetDevice(), 
            fileName, 
            stage
        );

        _shaders[std::string(fileName)] = _shader;

        return _shaders[fileName];
    } else {
        return _shaders[fileName];
    }
}

bool AssetHandler::_copy_to_texture(
    SDL_GPUDevice *device, void *src_data, uint32_t src_data_len, SDL_GPUTexture *dst_texture,
    uint32_t dst_texture_width, uint32_t dst_texture_height
)
{
    SDL_GPUTransferBufferCreateInfo transfer_buf_create_info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = src_data_len,
        .props = 0,
    };
    SDL_GPUTransferBuffer *transfer_buf =
        SDL_CreateGPUTransferBuffer(device, &transfer_buf_create_info);
    if (!transfer_buf)
    {
//        spdlog::error("copy_to_texture: failed to create transfer buffer: {}", SDL_GetError());
        return false;
    }
    void *transfer_buf_ptr = SDL_MapGPUTransferBuffer(device, transfer_buf, false);
    if (!transfer_buf_ptr)
    {
//        spdlog::error("copy_to_texture: failed to map transfer buffer: {}", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer_buf);
        return false;
    }
    std::memcpy(transfer_buf_ptr, src_data, src_data_len);
    SDL_UnmapGPUTransferBuffer(device, transfer_buf);

    SDL_GPUCommandBuffer *copy_cmd_buf = SDL_AcquireGPUCommandBuffer(device);
    if (!copy_cmd_buf)
    {
//        spdlog::error("copy_to_texture: failed to create init command buffer: {}", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer_buf);
        return false;
    }
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(copy_cmd_buf);
    {
        SDL_GPUTextureTransferInfo transfer_info{
            .transfer_buffer = transfer_buf,
            .offset = 0,
            .pixels_per_row = 0,
            .rows_per_layer = 0,
        };
        SDL_GPUTextureRegion destination_info{
            .texture = dst_texture,
            .mip_level = 0,
            .layer = 0,
            .x = 0,
            .y = 0,
            .z = 0,
            .w = dst_texture_width,
            .h = dst_texture_height,
            .d = 1,
        };
        SDL_UploadToGPUTexture(copy_pass, &transfer_info, &destination_info, false);
    }
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(copy_cmd_buf);

    SDL_ReleaseGPUTransferBuffer(device, transfer_buf);

    return true;
}

TextureAsset AssetHandler::_createDepthTarget(SDL_GPUDevice *device, uint32_t width, uint32_t height) {

    SDL_GPUTextureCreateInfo texture_create_info{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &texture_create_info);
    if (!texture)
    {
        LOG_CRITICAL("failed to create depth texture");
    }

    TextureAsset tex;

    tex.gpuSampler = Renderer::GetSampler(defaultMode);
    tex.gpuTexture = texture;


    return tex;
}

TextureAsset AssetHandler::_createWhitePixel() {
    // Create a 1x1 white texture
    TextureAsset whitePixel;

    auto device = Renderer::GetDevice();

    Uint32 white_pixel_data           = 0xFFFFFFFF;// RGBA: 255, 255, 255, 255
    SDL_GPUTextureCreateInfo tex_info = {
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width                = 1,
        .height               = 1,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
        .sample_count         = SDL_GPU_SAMPLECOUNT_1,
    };
    
    whitePixel.gpuSampler = Renderer::GetSampler(defaultMode);
    whitePixel.gpuTexture = SDL_CreateGPUTexture(device, &tex_info);
    whitePixel.filename = "[Lumi]WhitePixel";

    SDL_GPUTransferBufferCreateInfo transfer_buf_create_info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = sizeof(Uint32),
        .props = 0,
    };
    SDL_GPUTransferBuffer *transfer_buf =
        SDL_CreateGPUTransferBuffer(device, &transfer_buf_create_info);

    void *transfer_buf_ptr = SDL_MapGPUTransferBuffer(device, transfer_buf, false);

    std::memcpy(transfer_buf_ptr, &white_pixel_data, sizeof(Uint32));
    SDL_UnmapGPUTransferBuffer(device, transfer_buf);

    SDL_GPUCommandBuffer *copy_cmd_buf = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(copy_cmd_buf);
    {
        SDL_GPUTextureTransferInfo transfer_info{
            .transfer_buffer = transfer_buf,
            .offset          = 0,
            .pixels_per_row  = 0,
            .rows_per_layer  = 0,
        };
        SDL_GPUTextureRegion destination_info{
            .texture   = whitePixel.gpuTexture,
            .mip_level = 0,
            .layer     = 0,
            .x         = 0,
            .y         = 0,
            .z         = 0,
            .w         = 1,
            .h         = 1,
            .d         = 1,
        };
        SDL_UploadToGPUTexture(copy_pass, &transfer_info, &destination_info, false);
    }
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(copy_cmd_buf);

    SDL_ReleaseGPUTransferBuffer(device, transfer_buf);

    return whitePixel;
}

TextureAsset AssetHandler::_loadFromPixelData(const vf2d& size, void *pixelData, std::string fileName) {
    LUMI_UNUSED(size, pixelData, fileName);

        TextureAsset texture;
#if 0

#endif

    return texture;
}



ModelAsset AssetHandler::_createCube(float size, CubeUVLayout layout) {
    ModelAsset cube;
    cube.name = "cube";
    
    float s = size / 2.0f;  // Half size for centering
    
    // UV inset to avoid sampling at exact atlas boundaries
    // This prevents texture bleeding between atlas regions
    constexpr float UV_INSET = 0.00005f;  // ~0.25 pixel on 512x512 texture
    
    // Helper lambda to apply inset to UV coordinates
    auto insetUV = [](float uMin, float vMin, float uMax, float vMax) {
        return FaceUV(
            uMin + UV_INSET,
            vMin + UV_INSET,
            uMax - UV_INSET,
            vMax - UV_INSET
        );
    };
    
    // Create cube with default UVs (will be overridden based on layout)
    // 6 faces x 4 vertices = 24 vertices
    // Order: Front, Back, Top, Bottom, Right, Left
    
    // Front face (+Z) - CubeFace::Front
    cube.vertices.push_back({-s, -s,  s,  0, 0, 1,  0, 0,  1, 1, 1, 1});
    cube.vertices.push_back({ s, -s,  s,  0, 0, 1,  1, 0,  1, 1, 1, 1});
    cube.vertices.push_back({ s,  s,  s,  0, 0, 1,  1, 1,  1, 1, 1, 1});
    cube.vertices.push_back({-s,  s,  s,  0, 0, 1,  0, 1,  1, 1, 1, 1});
    
    // Back face (-Z) - CubeFace::Back
    cube.vertices.push_back({ s, -s, -s,  0, 0, -1,  0, 0,  1, 1, 1, 1});
    cube.vertices.push_back({-s, -s, -s,  0, 0, -1,  1, 0,  1, 1, 1, 1});
    cube.vertices.push_back({-s,  s, -s,  0, 0, -1,  1, 1,  1, 1, 1, 1});
    cube.vertices.push_back({ s,  s, -s,  0, 0, -1,  0, 1,  1, 1, 1, 1});
    
    // Top face (+Y) - CubeFace::Top
    cube.vertices.push_back({-s,  s,  s,  0, 1, 0,  0, 0,  1, 1, 1, 1});
    cube.vertices.push_back({ s,  s,  s,  0, 1, 0,  1, 0,  1, 1, 1, 1});
    cube.vertices.push_back({ s,  s, -s,  0, 1, 0,  1, 1,  1, 1, 1, 1});
    cube.vertices.push_back({-s,  s, -s,  0, 1, 0,  0, 1,  1, 1, 1, 1});
    
    // Bottom face (-Y) - CubeFace::Bottom
    cube.vertices.push_back({-s, -s, -s,  0, -1, 0,  0, 0,  1, 1, 1, 1});
    cube.vertices.push_back({ s, -s, -s,  0, -1, 0,  1, 0,  1, 1, 1, 1});
    cube.vertices.push_back({ s, -s,  s,  0, -1, 0,  1, 1,  1, 1, 1, 1});
    cube.vertices.push_back({-s, -s,  s,  0, -1, 0,  0, 1,  1, 1, 1, 1});
    
    // Right face (+X) - CubeFace::Right
    cube.vertices.push_back({ s, -s,  s,  1, 0, 0,  0, 0,  1, 1, 1, 1});
    cube.vertices.push_back({ s, -s, -s,  1, 0, 0,  1, 0,  1, 1, 1, 1});
    cube.vertices.push_back({ s,  s, -s,  1, 0, 0,  1, 1,  1, 1, 1, 1});
    cube.vertices.push_back({ s,  s,  s,  1, 0, 0,  0, 1,  1, 1, 1, 1});
    
    // Left face (-X) - CubeFace::Left
    cube.vertices.push_back({-s, -s, -s,  -1, 0, 0,  0, 0,  1, 1, 1, 1});
    cube.vertices.push_back({-s, -s,  s,  -1, 0, 0,  1, 0,  1, 1, 1, 1});
    cube.vertices.push_back({-s,  s,  s,  -1, 0, 0,  1, 1,  1, 1, 1, 1});
    cube.vertices.push_back({-s,  s, -s,  -1, 0, 0,  0, 1,  1, 1, 1, 1});
    
    // Indices for all 6 faces (2 triangles per face)
    for (uint32_t i = 0; i < 6; i++) {
        uint32_t base = i * 4;
        cube.indices.push_back(base + 0);
        cube.indices.push_back(base + 1);
        cube.indices.push_back(base + 2);
        cube.indices.push_back(base + 2);
        cube.indices.push_back(base + 3);
        cube.indices.push_back(base + 0);
    }
    
    // Apply UV layout
    switch (layout) {
        case CubeUVLayout::SingleTexture:
            // Default UVs (0,0 to 1,1) per face - already set!
            break;
            
        case CubeUVLayout::Atlas4x4:
            // 4x4 grid layout with UV inset to prevent texture bleeding:
            // Row 0: X, Top, X, X
            // Row 1: West, South, East, North
            // Row 2: X, Bottom, X, X
            // Row 3: Random (not used)
            cube.SetCubeFaceUVs(CubeFace::Front,  insetUV(0.25f, 0.25f, 0.5f,  0.5f));   // South
            cube.SetCubeFaceUVs(CubeFace::Back,   insetUV(0.75f, 0.25f, 1.0f,  0.5f));   // North
            cube.SetCubeFaceUVs(CubeFace::Top,    insetUV(0.25f, 0.0f,  0.5f,  0.25f));  // Top
            cube.SetCubeFaceUVs(CubeFace::Bottom, insetUV(0.25f, 0.5f,  0.5f,  0.75f));  // Bottom
            cube.SetCubeFaceUVs(CubeFace::Right,  insetUV(0.5f,  0.25f, 0.75f, 0.5f));   // East
            cube.SetCubeFaceUVs(CubeFace::Left,   insetUV(0.0f,  0.25f, 0.25f, 0.5f));   // West
            break;
            
        case CubeUVLayout::Atlas3x2:
            // 3x2 horizontal cross layout with UV inset:
            // Row 0: Left, Front, Right
            // Row 1: Bottom, Back, Top
            cube.SetCubeFaceUVs(CubeFace::Front,  insetUV(0.333f, 0.5f,   0.667f, 1.0f));   // Front
            cube.SetCubeFaceUVs(CubeFace::Back,   insetUV(0.333f, 0.0f,   0.667f, 0.5f));   // Back
            cube.SetCubeFaceUVs(CubeFace::Top,    insetUV(0.667f, 0.0f,   1.0f,   0.5f));   // Top
            cube.SetCubeFaceUVs(CubeFace::Bottom, insetUV(0.0f,   0.0f,   0.333f, 0.5f));   // Bottom
            cube.SetCubeFaceUVs(CubeFace::Right,  insetUV(0.667f, 0.5f,   1.0f,   1.0f));   // Right
            cube.SetCubeFaceUVs(CubeFace::Left,   insetUV(0.0f,   0.5f,   0.333f, 1.0f));   // Left
            break;
            
        case CubeUVLayout::Skybox:
            // 6 textures stitched horizontally (1/6th width each) with UV inset:
            // Order: Right, Left, Top, Bottom, Front, Back
            cube.SetCubeFaceUVs(CubeFace::Right,  insetUV(0.0f,    0.0f, 0.1667f, 1.0f));
            cube.SetCubeFaceUVs(CubeFace::Left,   insetUV(0.1667f, 0.0f, 0.3333f, 1.0f));
            cube.SetCubeFaceUVs(CubeFace::Top,    insetUV(0.3333f, 0.0f, 0.5f,    1.0f));
            cube.SetCubeFaceUVs(CubeFace::Bottom, insetUV(0.5f,    0.0f, 0.6667f, 1.0f));
            cube.SetCubeFaceUVs(CubeFace::Front,  insetUV(0.6667f, 0.0f, 0.8333f, 1.0f));
            cube.SetCubeFaceUVs(CubeFace::Back,   insetUV(0.8333f, 0.0f, 1.0f,    1.0f));
            break;
            
        case CubeUVLayout::Custom:
            // User will call SetCubeFaceUVs() manually
            break;
    }
    
    // Set default texture to white pixel
    cube.texture = _createWhitePixel();
    
    return cube;
}

// ============================================================
// Font Cache
// ============================================================

static constexpr uint32_t FONT_CACHE_VERSION = 1;

void AssetHandler::_initFontCache() {
    _fontCache = new ResourcePack("font.cache", "luminoveau_fonts");
    if (_fontCache->Loaded()) {
        LOG_INFO("Loaded existing font cache from font.cache");
    } else {
        LOG_INFO("No existing font cache found, will create on first font load");
    }
}

void AssetHandler::_saveFontCache() {
    if (_fontCache) {
        _fontCache->SavePack();
    }
}

std::string AssetHandler::_computeFontCacheKey(const std::string& fileName) {
    auto filedata = FileHandler::ReadFile(fileName);
    std::string data(static_cast<char*>(filedata.data), filedata.fileSize);
    std::string hash = picosha2::hash256_hex_string(data);
    free(filedata.data);
    return hash;
}

std::string AssetHandler::_computeFontCacheKeyFromData(const void* data, size_t size) {
    std::string str(static_cast<const char*>(data), size);
    return picosha2::hash256_hex_string(str);
}

bool AssetHandler::_loadFontFromCache(const std::string& fileName, int fontSize, FontAsset& outFont, const std::string& precomputedHash) {
    if (!_fontCache) return false;
    
    // Build cache keys
    std::string safeName = fileName;
    std::replace(safeName.begin(), safeName.end(), '/', '_');
    std::replace(safeName.begin(), safeName.end(), '\\', '_');
    std::string metaKey = safeName + ".fontmeta";
    std::string atlasKey = safeName + ".fontatlas";
    std::string hashKey = safeName + ".fonthash";
    
    if (!_fontCache->HasFile(metaKey) || !_fontCache->HasFile(atlasKey) || !_fontCache->HasFile(hashKey)) {
        return false;
    }
    
    // Verify hash
    std::string currentHash = precomputedHash.empty() ? _computeFontCacheKey(fileName) : precomputedHash;
    auto hashBuf = _fontCache->GetFileBuffer(hashKey);
    std::string cachedHash(hashBuf.vMemory.begin(), hashBuf.vMemory.end());
    if (currentHash != cachedHash) {
        LOG_INFO("Font cache invalid for {} (file changed), regenerating", fileName.c_str());
        return false;
    }
    
    // Load metadata
    auto metaBuf = _fontCache->GetFileBuffer(metaKey);
    const uint8_t* ptr = metaBuf.vMemory.data();
    size_t remaining = metaBuf.vMemory.size();
    
    auto readVal = [&](auto& val) {
        if (remaining < sizeof(val)) return false;
        std::memcpy(&val, ptr, sizeof(val));
        ptr += sizeof(val);
        remaining -= sizeof(val);
        return true;
    };
    
    uint32_t version = 0;
    if (!readVal(version) || version != FONT_CACHE_VERSION) {
        LOG_INFO("Font cache version mismatch for {}, regenerating", fileName.c_str());
        return false;
    }
    
    uint32_t atlasWidth = 0, atlasHeight = 0, generatedSize = 0, glyphCount = 0;
    if (!readVal(atlasWidth) || !readVal(atlasHeight) || !readVal(generatedSize) || !readVal(glyphCount)) return false;
    
    double ascender = 0, descender = 0, lineHeight = 0;
    if (!readVal(ascender) || !readVal(descender) || !readVal(lineHeight)) return false;
    
    // Read glyphs
    auto* glyphs = new std::vector<CachedGlyph>();
    auto* glyphMap = new std::unordered_map<uint32_t, size_t>();
    glyphs->reserve(glyphCount);
    
    for (uint32_t i = 0; i < glyphCount; ++i) {
        CachedGlyph g;
        if (!readVal(g.codepoint) || !readVal(g.advance)
            || !readVal(g.pl) || !readVal(g.pb) || !readVal(g.pr) || !readVal(g.pt)
            || !readVal(g.al) || !readVal(g.ab) || !readVal(g.ar) || !readVal(g.at)) {
            delete glyphs;
            delete glyphMap;
            return false;
        }
        glyphs->push_back(g);
        if (g.codepoint > 0) {
            (*glyphMap)[g.codepoint] = i;
        }
    }
    
    // Load atlas RGBA data
    auto atlasBuf = _fontCache->GetFileBuffer(atlasKey);
    size_t expectedSize = (size_t)atlasWidth * atlasHeight * 4;
    if (atlasBuf.vMemory.size() != expectedSize) {
        LOG_WARNING("Font cache atlas size mismatch for {}", fileName.c_str());
        delete glyphs;
        delete glyphMap;
        return false;
    }
    
    // Upload atlas to GPU
    SDL_GPUTextureCreateInfo textureInfo{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = atlasWidth,
        .height = atlasHeight,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };
    
    SDL_GPUTexture* gpuTex = SDL_CreateGPUTexture(Renderer::GetDevice(), &textureInfo);
    if (!gpuTex || !_copy_to_texture(Renderer::GetDevice(), atlasBuf.vMemory.data(),
                                      atlasBuf.vMemory.size(), gpuTex, atlasWidth, atlasHeight)) {
        LOG_WARNING("Font cache GPU upload failed for {}", fileName.c_str());
        delete glyphs;
        delete glyphMap;
        return false;
    }
    
    // Fill FontAsset
    outFont.atlasTexture = gpuTex;
    outFont.atlasWidth = atlasWidth;
    outFont.atlasHeight = atlasHeight;
    outFont.generatedSize = generatedSize;
    outFont.defaultRenderSize = fontSize;
    outFont.ascender = ascender;
    outFont.descender = descender;
    outFont.lineHeight = lineHeight;
    outFont.glyphs = glyphs;
    outFont.glyphMap = glyphMap;
    outFont.fontHandle = nullptr;  // No FreeType needed from cache
    outFont.fontData = nullptr;
    
    LOG_INFO("Loaded font {} from cache ({} glyphs, {}x{} atlas)", fileName.c_str(), glyphCount, atlasWidth, atlasHeight);
    return true;
}

void AssetHandler::_saveFontToCache(const std::string& fileName, const FontAsset& font, const std::vector<unsigned char>& rgbaData, const std::string& precomputedHash) {
    if (!_fontCache) return;
    
    std::string safeName = fileName;
    std::replace(safeName.begin(), safeName.end(), '/', '_');
    std::replace(safeName.begin(), safeName.end(), '\\', '_');
    std::string metaKey = safeName + ".fontmeta";
    std::string atlasKey = safeName + ".fontatlas";
    std::string hashKey = safeName + ".fonthash";
    
    // Save source hash
    std::string sourceHash = precomputedHash.empty() ? _computeFontCacheKey(fileName) : precomputedHash;
    std::vector<unsigned char> hashBytes(sourceHash.begin(), sourceHash.end());
    _fontCache->AddFile(hashKey, hashBytes);
    
    // Build metadata blob
    std::vector<unsigned char> metaBlob;
    auto writeVal = [&](const auto& val) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&val);
        metaBlob.insert(metaBlob.end(), p, p + sizeof(val));
    };
    
    writeVal(FONT_CACHE_VERSION);
    writeVal(static_cast<uint32_t>(font.atlasWidth));
    writeVal(static_cast<uint32_t>(font.atlasHeight));
    writeVal(static_cast<uint32_t>(font.generatedSize));
    writeVal(static_cast<uint32_t>(font.glyphs->size()));
    writeVal(font.ascender);
    writeVal(font.descender);
    writeVal(font.lineHeight);
    
    for (const auto& g : *font.glyphs) {
        writeVal(g.codepoint);
        writeVal(g.advance);
        writeVal(g.pl); writeVal(g.pb); writeVal(g.pr); writeVal(g.pt);
        writeVal(g.al); writeVal(g.ab); writeVal(g.ar); writeVal(g.at);
    }
    
    _fontCache->AddFile(metaKey, metaBlob);
    
    // Save atlas RGBA data
    std::vector<unsigned char> atlasData(rgbaData.begin(), rgbaData.end());
    _fontCache->AddFile(atlasKey, atlasData);
    
    // Persist to disk
    if (_fontCache->SavePack()) {
        LOG_INFO("Font cache saved for {}", fileName.c_str());
    } else {
        LOG_WARNING("Failed to save font cache!");
    }
}
