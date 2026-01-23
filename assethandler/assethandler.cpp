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


AssetHandler::AssetHandler() {
    // Reserve space to prevent map reallocation (important since we return references!)
    _textures.reserve(1000);
    _sounds.reserve(100);
    _musics.reserve(50);
    _fonts.reserve(50);
    _shaders.reserve(50);

    // Load default font using MSDF from embedded data
    LOG_INFO("Loading default MSDF font");
    
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
    
    defaultFont.glyphs = new std::vector<msdf_atlas::GlyphGeometry>();
    
    msdf_atlas::FontGeometry fontGeometry(defaultFont.glyphs);
    fontGeometry.loadCharset(defaultFont.fontHandle, 1.0, msdf_atlas::Charset::ASCII);
    
    // Get font metrics
    defaultFont.ascender = fontGeometry.getMetrics().ascenderY;
    defaultFont.descender = fontGeometry.getMetrics().descenderY;
    defaultFont.lineHeight = fontGeometry.getMetrics().lineHeight;
    
    const double maxCornerAngle = 3.0;
    for (msdf_atlas::GlyphGeometry &glyph : *defaultFont.glyphs) {
        glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, maxCornerAngle, 0);
    }
    
    msdf_atlas::TightAtlasPacker packer;
    packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::SQUARE);
    packer.setMinimumScale(96.0);  // High-res atlas
    packer.setPixelRange(4.0);
    packer.setMiterLimit(1.0);
    packer.pack(defaultFont.glyphs->data(), defaultFont.glyphs->size());
    
    packer.getDimensions(defaultFont.atlasWidth, defaultFont.atlasHeight);
    
    LOG_INFO("Default font MSDF atlas: {}x{}", defaultFont.atlasWidth, defaultFont.atlasHeight);
    
    msdf_atlas::ImmediateAtlasGenerator<
        float, 3,
        msdf_atlas::msdfGenerator,
        msdf_atlas::BitmapAtlasStorage<unsigned char, 3>
    > generator(defaultFont.atlasWidth, defaultFont.atlasHeight);
    
    generator.setThreadCount(4);
    generator.generate(defaultFont.glyphs->data(), defaultFont.glyphs->size());
    
    msdfgen::BitmapConstRef<unsigned char, 3> bitmap = generator.atlasStorage();
    
    std::vector<unsigned char> rgbaData(defaultFont.atlasWidth * defaultFont.atlasHeight * 4);
    // Read bitmap bottom-to-top to flip it for GPU (which expects top-left origin)
    for (int y = 0; y < defaultFont.atlasHeight; ++y) {
        for (int x = 0; x < defaultFont.atlasWidth; ++x) {
            // Flip Y: read from bottom row first
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
    
    defaultFont.glyphMap = new std::unordered_map<uint32_t, size_t>();
    for (size_t i = 0; i < defaultFont.glyphs->size(); ++i) {
        int codepoint = (*defaultFont.glyphs)[i].getCodepoint();
        if (codepoint >= 0) {
            (*defaultFont.glyphMap)[codepoint] = i;
        }
    }
    
    defaultFont.generatedSize = 64;
    defaultFont.defaultRenderSize = 16;
    
    // NOTE: Don't call msdfgen::deinitializeFreetype(ft) here!
    // The font handle needs the FreeType instance to remain alive.
    // We'll destroy the font handle in cleanup, which will clean up FreeType resources.
    
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
                                                   MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr,
                                                   _sound.sound);

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
                                                   MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr,
                                                   _music.music);

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
        // Font atlas already exists, just update the default render size
        _fonts[fileName].defaultRenderSize = fontSize;
        return _fonts[fileName];
    }

    LOG_INFO("Loading MSDF font {} (atlas size: 32, default render: {})", fileName.c_str(), fontSize);

    FontAsset _font;
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
    
    _font.glyphs = new std::vector<msdf_atlas::GlyphGeometry>();
    
    msdf_atlas::FontGeometry fontGeometry(_font.glyphs);
    fontGeometry.loadCharset(_font.fontHandle, 1.0, msdf_atlas::Charset::ASCII);
    
    // Get font metrics
    _font.ascender = fontGeometry.getMetrics().ascenderY;
    _font.descender = fontGeometry.getMetrics().descenderY;
    _font.lineHeight = fontGeometry.getMetrics().lineHeight;
    
    const double maxCornerAngle = 3.0;
    for (msdf_atlas::GlyphGeometry &glyph : *_font.glyphs) {
        glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, maxCornerAngle, 0);
    }
    
    // Always generate MSDF atlas at size 64 for quality and memory efficiency
    // MSDF scales well both up and down, so this provides good quality for most use cases
    const int ATLAS_GENERATION_SIZE = 64;
    
    msdf_atlas::TightAtlasPacker packer;
    packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::SQUARE);
    packer.setMinimumScale(ATLAS_GENERATION_SIZE);
    packer.setPixelRange(4.0);
    packer.setMiterLimit(1.0);
    packer.pack(_font.glyphs->data(), _font.glyphs->size());
    
    packer.getDimensions(_font.atlasWidth, _font.atlasHeight);
    
    LOG_INFO("MSDF atlas for {}: {}x{}", fileName.c_str(), _font.atlasWidth, _font.atlasHeight);
    
    msdf_atlas::ImmediateAtlasGenerator<
        float, 3,
        msdf_atlas::msdfGenerator,
        msdf_atlas::BitmapAtlasStorage<unsigned char, 3>
    > generator(_font.atlasWidth, _font.atlasHeight);
    
    generator.setThreadCount(4);
    generator.generate(_font.glyphs->data(), _font.glyphs->size());
    
    msdfgen::BitmapConstRef<unsigned char, 3> bitmap = generator.atlasStorage();
    
    std::vector<unsigned char> rgbaData(_font.atlasWidth * _font.atlasHeight * 4);
    // Read bitmap bottom-to-top to flip it for GPU (which expects top-left origin)
    for (int y = 0; y < _font.atlasHeight; ++y) {
        for (int x = 0; x < _font.atlasWidth; ++x) {
            // Flip Y: read from bottom row first
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
    
    _font.glyphMap = new std::unordered_map<uint32_t, size_t>();
    for (size_t i = 0; i < _font.glyphs->size(); ++i) {
        int codepoint = (*_font.glyphs)[i].getCodepoint();
        if (codepoint >= 0) {
            (*_font.glyphMap)[codepoint] = i;
        }
    }
    
    _font.generatedSize = ATLAS_GENERATION_SIZE;
    _font.defaultRenderSize = fontSize;
    
    // NOTE: Don't call msdfgen::deinitializeFreetype(ft) here!
    // The font handle needs the FreeType instance to remain alive.
    // We'll destroy the font handle in cleanup, which will clean up FreeType resources.
    
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
