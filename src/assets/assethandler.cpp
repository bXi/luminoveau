#include "assethandler.h"
#include "platform/window/window.h"
#include "core/log/log.h"

#include <iostream>
#include <vector>
#include <regex>

#include "renderer/renderer.h"

#include <SDL3_image/SDL_image.h>

#ifndef LUMINOVEAU_WEBGPU_BACKEND
#include "gpu/backends/sdl/sdlgpu.h"
#include "spirv_cross.hpp"
#include "renderer/shaders.h"
#else
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_cross.hpp>
#include "gpu/types.h"
#include "renderer/renderer.h"
#include "file/filehandler.h"
#endif

#include <msdf-atlas-gen/msdf-atlas-gen.h>
#include <msdfgen/msdfgen.h>

#include "picosha2.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif


AssetHandler::AssetHandler() {
    // Reserve space to prevent map reallocation (important since we return references!)
    _textures.reserve(1000);
    _sounds.reserve(100);
    _musics.reserve(50);
    _fonts.reserve(50);
    _shaders.reserve(50);
    _computePipelines.reserve(20);

    // Initialize font cache
    _initFontCache();
    
    // Load default font using MSDF from embedded data
    
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
        
#ifdef __EMSCRIPTEN__
        generator.setThreadCount(1);
#else
        generator.setThreadCount(8);
#endif
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
        
        GpuTextureCreateInfo textureInfo{
            .width        = static_cast<uint32_t>(defaultFont.atlasWidth),
            .height       = static_cast<uint32_t>(defaultFont.atlasHeight),
            .depthOrLayers = 1,
            .numLevels    = 1,
            .format       = GpuTextureFormat::R8G8B8A8_Unorm,
            .sampleCount  = GpuSampleCount::x1,
            .usage        = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer,
        };
        defaultFont.atlasTexture = Renderer::GetGpu().createTexture(textureInfo);

        if (!_copy_to_texture(rgbaData.data(), (uint32_t)rgbaData.size(),
                              defaultFont.atlasTexture,
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
    
};

void AssetHandler::_cleanup() {

    std::lock_guard<std::mutex> lock(assetMutex);
    auto& gpu = Renderer::GetGpu();

    // Cleanup textures
    for (auto& [name, tex] : _textures) {
        if (tex.gpuTexture) {
            gpu.releaseTexture(tex.gpuTexture);
            tex.gpuTexture = 0;
        }
    }
    _textures.clear();

    // Cleanup shaders
    for (auto& [name, shader] : _shaders) {
        if (shader.gpuShader) {
            gpu.releaseShader(shader.gpuShader);
            shader.gpuShader = 0;
        }
    }
    _shaders.clear();

    // Cleanup compute pipelines
    for (auto& [name, cp] : _computePipelines) {
        if (cp.pipeline) {
            gpu.releaseComputePipeline(cp.pipeline);
            cp.pipeline = 0;
        }
    }
    _computePipelines.clear();
    
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
            gpu.releaseTexture(font.atlasTexture);
            font.atlasTexture = 0;
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
        gpu.releaseTexture(defaultFont.atlasTexture);
        defaultFont.atlasTexture = 0;
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

    if (!Renderer::IsReady()) {
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

    auto& gpu = Renderer::GetGpu();
    GpuTextureCreateInfo texInfo{
        .width        = static_cast<uint32_t>(texture.width),
        .height       = static_cast<uint32_t>(texture.height),
        .depthOrLayers = 1,
        .numLevels    = 1,
        .format       = GpuTextureFormat::R8G8B8A8_Unorm,
        .sampleCount  = GpuSampleCount::x1,
        .usage        = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer,
    };
    GpuTextureHandle gpuTexture = gpu.createTexture(texInfo);
    if (!gpuTexture) {
        LOG_CRITICAL("failed to create texture: {}", fileName.c_str());
    }

    if (!_copy_to_texture(surface->pixels,
                          static_cast<uint32_t>(texture.width * texture.height * 4),
                          gpuTexture, texture.width, texture.height)) {
        gpu.releaseTexture(gpuTexture);
        LOG_CRITICAL("failed to copy image data to texture");
    }

    texture.gpuSampler = Renderer::GetSampler(defaultMode);
    texture.gpuTexture = gpuTexture;

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
    texture.width  = (int)size.x;
    texture.height = (int)size.y;

    // Swapchain-format default framebuffer: no storage usage (BGRA8Unorm on WebGPU is incompatible
    // with StorageBinding unless the BGRA8UnormStorage feature is enabled).
    GpuTextureCreateInfo info{
        .width         = static_cast<uint32_t>(size.x),
        .height        = static_cast<uint32_t>(size.y),
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = Renderer::GetGpu().getSwapchainFormat(),
        .sampleCount   = GpuSampleCount::x1,
#ifdef LUMINOVEAU_WEBGPU_BACKEND
        .usage         = GpuTextureUsage::ColorTarget | GpuTextureUsage::Sampler,
#else
        .usage         = GpuTextureUsage::ColorTarget | GpuTextureUsage::Sampler
                       | GpuTextureUsage::StorageRead  | GpuTextureUsage::StorageWrite,
#endif
    };
    texture.gpuSampler = Renderer::GetSampler(defaultMode);
    texture.gpuTexture = Renderer::GetGpu().createTexture(info);
    return texture;
}

TextureAsset AssetHandler::_createEmptyTexture(const vf2d &size, GpuTextureFormat format) {
    TextureAsset texture;
    texture.width  = (int)size.x;
    texture.height = (int)size.y;

    GpuTextureCreateInfo info{
        .width         = static_cast<uint32_t>(size.x),
        .height        = static_cast<uint32_t>(size.y),
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = format,
        .sampleCount   = GpuSampleCount::x1,
        .usage         = GpuTextureUsage::ColorTarget | GpuTextureUsage::Sampler
                       | GpuTextureUsage::StorageRead  | GpuTextureUsage::StorageWrite,
    };
    texture.gpuSampler = Renderer::GetSampler(defaultMode);
    texture.gpuTexture = Renderer::GetGpu().createTexture(info);
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
    
#ifdef __EMSCRIPTEN__
    generator.setThreadCount(1);
#else
    generator.setThreadCount(8);
#endif
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
    
    GpuTextureCreateInfo textureInfo{
        .width        = static_cast<uint32_t>(_font.atlasWidth),
        .height       = static_cast<uint32_t>(_font.atlasHeight),
        .depthOrLayers = 1,
        .numLevels    = 1,
        .format       = GpuTextureFormat::R8G8B8A8_Unorm,
        .sampleCount  = GpuSampleCount::x1,
        .usage        = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer,
    };
    _font.atlasTexture = Renderer::GetGpu().createTexture(textureInfo);

    if (!_copy_to_texture(rgbaData.data(), (uint32_t)rgbaData.size(),
                          _font.atlasTexture, _font.atlasWidth, _font.atlasHeight)) {
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

#ifndef LUMINOVEAU_WEBGPU_BACKEND
        SDL_GPUShaderStage stage;
        if (SDL_strstr(fileName.c_str(), ".vert")) {
            stage = SDL_GPU_SHADERSTAGE_VERTEX;
        } else if (SDL_strstr(fileName.c_str(), ".frag")) {
            stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        } else {
            LOG_CRITICAL("invalid shader stage");
        }

        ShaderAsset _shader = Shaders::CreateShaderAsset(
            Renderer::GetDevice(),
            fileName,
            stage
        );
        _shaders[std::string(fileName)] = _shader;
#else
        // ── WebGPU path: GLSL → SPIR-V (glslang) → reflect (SPIRV-Cross) → WGSL (naga) ──

        // Determine stage from filename
        EShLanguage glslStage;
        GpuShaderStage gpuStage;
        const char* nagaStage;
        if (fileName.find(".vert") != std::string::npos) {
            glslStage = EShLangVertex;
            gpuStage  = GpuShaderStage::Vertex;
            nagaStage = "vertex";
        } else if (fileName.find(".frag") != std::string::npos) {
            glslStage = EShLangFragment;
            gpuStage  = GpuShaderStage::Fragment;
            nagaStage = "fragment";
        } else {
            LOG_CRITICAL("AssetHandler::_getShader: cannot determine stage from filename '{}'", fileName.c_str());
        }

        // Load GLSL source
        auto sourceFile = FileHandler::GetFileFromPhysFS(fileName);
        std::string glslSource(static_cast<const char*>(sourceFile.data), sourceFile.fileSize);

        // Compile GLSL → SPIR-V via glslang
        glslang::InitializeProcess();
        glslang::TShader glslShader(glslStage);
        const char* srcPtr = glslSource.c_str();
        glslShader.setStrings(&srcPtr, 1);
        glslShader.setEnvInput(glslang::EShSourceGlsl, glslStage, glslang::EShClientVulkan, 450);
        glslShader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
        glslShader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_1);

        TBuiltInResource glslRes = {};
        glslRes.maxLights = 32; glslRes.maxClipPlanes = 6; glslRes.maxTextureUnits = 32;
        glslRes.maxTextureCoords = 32; glslRes.maxVertexAttribs = 64;
        glslRes.maxVertexUniformComponents = 4096; glslRes.maxVaryingFloats = 64;
        glslRes.maxVertexTextureImageUnits = 32; glslRes.maxCombinedTextureImageUnits = 80;
        glslRes.maxTextureImageUnits = 32; glslRes.maxFragmentUniformComponents = 4096;
        glslRes.maxDrawBuffers = 32; glslRes.maxVertexUniformVectors = 128;
        glslRes.maxVaryingVectors = 8; glslRes.maxFragmentUniformVectors = 16;
        glslRes.maxVertexOutputVectors = 16; glslRes.maxFragmentInputVectors = 15;
        glslRes.minProgramTexelOffset = -8; glslRes.maxProgramTexelOffset = 7;
        glslRes.maxClipDistances = 8;
        glslRes.maxComputeWorkGroupCountX = 65535; glslRes.maxComputeWorkGroupCountY = 65535;
        glslRes.maxComputeWorkGroupCountZ = 65535; glslRes.maxComputeWorkGroupSizeX = 1024;
        glslRes.maxComputeWorkGroupSizeY = 1024; glslRes.maxComputeWorkGroupSizeZ = 64;
        glslRes.maxComputeUniformComponents = 1024; glslRes.maxComputeTextureImageUnits = 16;
        glslRes.maxComputeImageUniforms = 8; glslRes.maxComputeAtomicCounters = 8;
        glslRes.maxComputeAtomicCounterBuffers = 1; glslRes.maxVaryingComponents = 60;
        glslRes.maxVertexOutputComponents = 64; glslRes.maxGeometryInputComponents = 64;
        glslRes.maxGeometryOutputComponents = 128; glslRes.maxFragmentInputComponents = 128;
        glslRes.maxImageUnits = 8; glslRes.maxCombinedImageUnitsAndFragmentOutputs = 8;
        glslRes.maxCombinedShaderOutputResources = 8; glslRes.maxImageSamples = 0;
        glslRes.maxVertexImageUniforms = 0; glslRes.maxTessControlImageUniforms = 0;
        glslRes.maxTessEvaluationImageUniforms = 0; glslRes.maxGeometryImageUniforms = 0;
        glslRes.maxFragmentImageUniforms = 8; glslRes.maxCombinedImageUniforms = 8;
        glslRes.maxGeometryTextureImageUnits = 16; glslRes.maxGeometryOutputVertices = 256;
        glslRes.maxGeometryTotalOutputComponents = 1024; glslRes.maxGeometryUniformComponents = 1024;
        glslRes.maxGeometryVaryingComponents = 64; glslRes.maxTessControlInputComponents = 128;
        glslRes.maxTessControlOutputComponents = 128; glslRes.maxTessControlTextureImageUnits = 16;
        glslRes.maxTessControlUniformComponents = 1024; glslRes.maxTessControlTotalOutputComponents = 4096;
        glslRes.maxTessEvaluationInputComponents = 128; glslRes.maxTessEvaluationOutputComponents = 128;
        glslRes.maxTessEvaluationTextureImageUnits = 16; glslRes.maxTessEvaluationUniformComponents = 1024;
        glslRes.maxTessPatchComponents = 120; glslRes.maxPatchVertices = 32; glslRes.maxTessGenLevel = 64;
        glslRes.maxViewports = 16; glslRes.maxVertexAtomicCounters = 0;
        glslRes.maxTessControlAtomicCounters = 0; glslRes.maxTessEvaluationAtomicCounters = 0;
        glslRes.maxGeometryAtomicCounters = 0; glslRes.maxFragmentAtomicCounters = 8;
        glslRes.maxCombinedAtomicCounters = 8; glslRes.maxAtomicCounterBindings = 1;
        glslRes.maxVertexAtomicCounterBuffers = 0; glslRes.maxTessControlAtomicCounterBuffers = 0;
        glslRes.maxTessEvaluationAtomicCounterBuffers = 0; glslRes.maxGeometryAtomicCounterBuffers = 0;
        glslRes.maxFragmentAtomicCounterBuffers = 1; glslRes.maxCombinedAtomicCounterBuffers = 1;
        glslRes.maxAtomicCounterBufferSize = 16384; glslRes.maxTransformFeedbackBuffers = 4;
        glslRes.maxTransformFeedbackInterleavedComponents = 64; glslRes.maxCullDistances = 8;
        glslRes.maxCombinedClipAndCullDistances = 8; glslRes.maxSamples = 4;
        glslRes.limits.nonInductiveForLoops = 1; glslRes.limits.whileLoops = 1;
        glslRes.limits.doWhileLoops = 1; glslRes.limits.generalUniformIndexing = 1;
        glslRes.limits.generalAttributeMatrixVectorIndexing = 1;
        glslRes.limits.generalVaryingIndexing = 1; glslRes.limits.generalSamplerIndexing = 1;
        glslRes.limits.generalVariableIndexing = 1;
        glslRes.limits.generalConstantMatrixVectorIndexing = 1;

        std::vector<uint32_t> spirv;
        if (!glslShader.parse(&glslRes, 450, EProfile::ECoreProfile, false, true, EShMsgDefault)) {
            LOG_ERROR("WebGPU shader GLSL parse failed ({}): {}", fileName.c_str(), glslShader.getInfoLog());
            glslang::FinalizeProcess();
            _shaders[std::string(fileName)] = {};
            return _shaders[fileName];
        }
        glslang::TProgram glslProg;
        glslProg.addShader(&glslShader);
        if (!glslProg.link(EShMsgDefault)) {
            LOG_ERROR("WebGPU shader link failed ({}): {}", fileName.c_str(), glslProg.getInfoLog());
            glslang::FinalizeProcess();
            _shaders[std::string(fileName)] = {};
            return _shaders[fileName];
        }
        glslang::GlslangToSpv(*glslProg.getIntermediate(glslStage), spirv);
        glslang::FinalizeProcess();

        // Reflect SPIR-V with SPIRV-Cross → uniform offsets/sizes, resource counts
        ShaderAsset _shader;
        _shader.shaderFilename = fileName;
        try {
            spirv_cross::Compiler spvComp(spirv);
            auto spvRes = spvComp.get_shader_resources();

            _shader.samplerCount = static_cast<uint32_t>(spvRes.sampled_images.size());
            _shader.uniformBufferCount = static_cast<uint32_t>(spvRes.uniform_buffers.size());
            _shader.storageBufferCount = static_cast<uint32_t>(spvRes.storage_buffers.size());
            _shader.storageTextureCount = static_cast<uint32_t>(spvRes.storage_images.size());

            for (const auto& ub : spvRes.uniform_buffers) {
                auto& bufType = spvComp.get_type(ub.base_type_id);
                for (size_t i = 0; i < bufType.member_types.size(); ++i) {
                    const std::string& name = spvComp.get_member_name(ub.base_type_id, i);
                    _shader.uniformOffsets[name] = spvComp.type_struct_member_offset(bufType, i);
                    _shader.uniformSizes[name]   = spvComp.get_declared_struct_member_size(bufType, i);
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("WebGPU SPIRV reflection failed ({}): {}", fileName.c_str(), e.what());
        }

        // Load pre-transpiled WGSL file (generated at build time by Tint).
        // Path: /_transpiled/<relative-shader-path>.wgsl
        std::string wgslPath = "/_transpiled/" + std::string(fileName) + ".wgsl";
        std::string wgsl = FileHandler::ReadTextFile(wgslPath);

        if (wgsl.empty()) {
            LOG_ERROR("WebGPU: pre-transpiled WGSL not found for '{}' (looked at '{}')", fileName.c_str(), wgslPath.c_str());
            _shaders[std::string(fileName)] = {};
            return _shaders[fileName];
        }

        LOG_INFO("Loaded WGSL ({} bytes) for '{}'", wgsl.size(), fileName.c_str());

        // Post-process: fix Tint's group(2) binding pair order.
        // Tint emits each GLSL combined sampler2D as binding(N*2)=texture, binding(N*2+1)=sampler.
        // IGpu expects binding(N*2)=sampler, binding(N*2+1)=texture. Swap within every pair.
        {
            std::regex re(R"(@group\(2u?\)\s*@binding\((\d+)u?\))");
            std::string out;
            out.reserve(wgsl.size());
            auto it  = std::sregex_iterator(wgsl.begin(), wgsl.end(), re);
            auto end = std::sregex_iterator();
            size_t last = 0;
            for (; it != end; ++it) {
                auto& m = *it;
                out.append(wgsl, last, m.position() - last);
                uint32_t b = (uint32_t)std::stoul(m[1].str());
                uint32_t swapped = (b ^ 1u);   // flip low bit: 0<->1, 2<->3, 4<->5, ...
                out += "@group(2u) @binding(" + std::to_string(swapped) + "u)";
                last = m.position() + m.length();
            }
            out.append(wgsl, last, std::string::npos);
            wgsl = std::move(out);
        }

        // Fragment shaders authored against SDL_GPU place uniforms at set=3. The WebGPU
        // backend's graphics pipeline layout expects fragment uniforms at group(1). Remap
        // @group(3u) → @group(1u) so the same GLSL works in both backends without
        // backend-specific source forks.
        if (gpuStage == GpuShaderStage::Fragment) {
            std::regex re(R"(@group\(3u?\))");
            wgsl = std::regex_replace(wgsl, re, "@group(1u)");
        }

        // Create GpuShader from WGSL
        GpuShaderCreateInfo shaderCI;
        shaderCI.code            = reinterpret_cast<const uint8_t*>(wgsl.c_str());
        shaderCI.codeSize        = wgsl.size();
        shaderCI.entrypoint      = "main";
        shaderCI.stage           = gpuStage;
        shaderCI.samplerCount    = _shader.samplerCount;
        shaderCI.uniformBufferCount = _shader.uniformBufferCount;
        shaderCI.storageBufferCount = _shader.storageBufferCount;
        shaderCI.storageTextureCount = _shader.storageTextureCount;

        _shader.gpuShader = Renderer::GetGpu().createShader(shaderCI);
        _shaders[std::string(fileName)] = _shader;
#endif

        return _shaders[fileName];
    } else {
        return _shaders[fileName];
    }
}

ComputePipelineAsset& AssetHandler::_getComputePipeline(const std::string& fileName) {
    std::lock_guard<std::mutex> lock(assetMutex);

    auto it = _computePipelines.find(fileName);
    if (it != _computePipelines.end()) {
        return it->second;
    }

    LOG_INFO("loading compute pipeline: {}", fileName.c_str());
#ifndef LUMINOVEAU_WEBGPU_BACKEND
    _computePipelines[fileName] = Shaders::CreateComputePipeline(Renderer::GetDevice(), fileName);
#else
    _computePipelines[fileName] = Renderer::CreateComputePipelineAsset(fileName);
#endif
    return _computePipelines[fileName];
}

bool AssetHandler::_copy_to_texture(void* src_data, uint32_t src_data_len,
                                     GpuTextureHandle dst_texture,
                                     uint32_t dst_texture_width, uint32_t dst_texture_height) {
    auto& gpu = Renderer::GetGpu();

    GpuTransferBufferCreateInfo tbInfo{ .size = src_data_len, .usage = GpuTransferUsage::Upload };
    GpuTransferBufferHandle tb = gpu.createTransferBuffer(tbInfo);
    if (!tb) return false;

    void* ptr = gpu.mapTransferBuffer(tb, false);
    if (!ptr) { gpu.releaseTransferBuffer(tb); return false; }
    std::memcpy(ptr, src_data, src_data_len);
    gpu.unmapTransferBuffer(tb);

    GpuCmdBufferHandle cmd = gpu.acquireCommandBuffer();
    if (!cmd) { gpu.releaseTransferBuffer(tb); return false; }

    GpuTransferBufferRegion src{ .transferBuffer = tb, .offset = 0 };
    GpuTextureRegion dst{
        .texture  = dst_texture,
        .mipLevel = 0, .layer = 0,
        .x = 0, .y = 0, .z = 0,
        .width = dst_texture_width, .height = dst_texture_height, .depth = 1,
    };
    gpu.uploadToTexture(cmd, src, dst, false);
    gpu.submitCommandBuffer(cmd);
    gpu.releaseTransferBuffer(tb);
    return true;
}

TextureAsset AssetHandler::_createDepthTarget(uint32_t width, uint32_t height) {
    GpuTextureCreateInfo info{
        .width         = width,
        .height        = height,
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = GpuTextureFormat::D32_Float_S8_Uint,
        .sampleCount   = GpuSampleCount::x1,
        .usage         = GpuTextureUsage::DepthStencilTarget,
    };
    GpuTextureHandle handle = Renderer::GetGpu().createTexture(info);
    if (!handle) LOG_CRITICAL("failed to create depth texture");

    TextureAsset tex;
    tex.gpuSampler = Renderer::GetSampler(defaultMode);
    tex.gpuTexture = handle;
    return tex;
}

TextureAsset AssetHandler::_createWhitePixel() {
    TextureAsset whitePixel;
    whitePixel.filename = "[Lumi]WhitePixel";

    GpuTextureCreateInfo info{
        .width = 1, .height = 1, .depthOrLayers = 1, .numLevels = 1,
        .format      = GpuTextureFormat::R8G8B8A8_Unorm,
        .sampleCount = GpuSampleCount::x1,
        .usage       = GpuTextureUsage::ColorTarget | GpuTextureUsage::Sampler
                     | GpuTextureUsage::Transfer,
    };
    whitePixel.gpuSampler = Renderer::GetSampler(defaultMode);
    whitePixel.gpuTexture = Renderer::GetGpu().createTexture(info);

    uint32_t white = 0xFFFFFFFF;
    _copy_to_texture(&white, sizeof(white), whitePixel.gpuTexture, 1, 1);

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
#ifdef __EMSCRIPTEN__
    // Mount IDBFS so font.cache persists across page reloads.
    // Asyncify.handleSleep pauses C execution until the async syncfs completes.
    static bool s_idbfsMounted = false;
    if (!s_idbfsMounted) {
        s_idbfsMounted = true;
        EM_ASM(
            try { FS.mkdir('/cache'); } catch(e) {}
            try { FS.mount(IDBFS, {}, '/cache'); } catch(e) { console.warn('[Lumi] IDBFS mount failed:', e); }
            Asyncify.handleSleep(function(wakeUp) {
                FS.syncfs(true, function(err) {
                    if (err) console.warn('[Lumi] IDBFS load error:', err);
                    wakeUp();
                });
            });
        );
    }
    _fontCache = new ResourcePack("/cache/font.cache", "luminoveau_fonts");
#else
    _fontCache = new ResourcePack("font.cache", "luminoveau_fonts");
#endif
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
    GpuTextureCreateInfo textureInfo{
        .width        = atlasWidth,
        .height       = atlasHeight,
        .depthOrLayers = 1,
        .numLevels    = 1,
        .format       = GpuTextureFormat::R8G8B8A8_Unorm,
        .sampleCount  = GpuSampleCount::x1,
        .usage        = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer,
    };
    GpuTextureHandle gpuTex = Renderer::GetGpu().createTexture(textureInfo);
    if (!gpuTex || !_copy_to_texture(atlasBuf.vMemory.data(),
                                     (uint32_t)atlasBuf.vMemory.size(),
                                     gpuTex, atlasWidth, atlasHeight)) {
        LOG_WARNING("Font cache GPU upload failed for {}", fileName.c_str());
        if (gpuTex) Renderer::GetGpu().releaseTexture(gpuTex);
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
#ifdef __EMSCRIPTEN__
        // Flush MEMFS → IndexedDB so the cache survives page reload.
        EM_ASM(
            Asyncify.handleSleep(function(wakeUp) {
                FS.syncfs(false, function(err) {
                    if (err) console.warn('[Lumi] IDBFS flush error:', err);
                    wakeUp();
                });
            });
        );
#endif
    } else {
        LOG_WARNING("Failed to save font cache!");
    }
}
