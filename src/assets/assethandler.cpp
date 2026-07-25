#include "assethandler.h"
#include "platform/window/window.h"
#include "core/log/log.h"
#include "file/filehandler.h"
#include "util/helpers.h"

#include <algorithm>
#include <iostream>
#include <vector>
#include <regex>

#include "renderer/renderer.h"

#include <SDL3_image/SDL_image.h>

#if defined(LUMINOVEAU_WITH_KTX2)
#include "basisu_transcoder.h"
#endif

#include <cstring>

// 3DS builds have no freetype/msdfgen toolchain; fonts come exclusively from the
// baked atlas blob (and the on-disk cache format, which needs no msdf code to read).
#ifndef LUMINOVEAU_NO_RUNTIME_MSDF
#include <msdf-atlas-gen/msdf-atlas-gen.h>
#include <msdfgen/msdfgen.h>
#endif

#include "picosha2.h"

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
    std::string embeddedHash = _computeFontCacheKeyFromData(DROID_SANS_MONO_TTF, DROID_SANS_MONO_TTF_LEN);

    bool loaded = false;
#if defined(LUMINOVEAU_HAVE_FONT_ATLAS_BLOB)
    // Prefer the baked atlas blob: no MSDF generation, and no dependence on a persistent cache
    // (Emscripten MEMFS is wiped each reload, so the runtime font cache never survives on the web).
    loaded = _loadDefaultFontFromBlob(_defaultFont);
#endif
    // Otherwise the on-disk font cache; generate from scratch only as a last resort.
    if (!loaded && !_loadFontFromCache("__default_font__", 16, _defaultFont, embeddedHash)) {
#ifdef LUMINOVEAU_NO_RUNTIME_MSDF
        LOG_WARNING("Default font unavailable: no baked atlas blob and runtime MSDF generation is compiled out");
#else
        // Cache miss — generate from scratch
        LOG_INFO("Default font not in cache, generating MSDF atlas");

        msdfgen::FreetypeHandle *ft = msdfgen::initializeFreetype();
        if (!ft) {
            LOG_CRITICAL("Failed to initialize FreeType for default font");
        }

        _defaultFont.fontHandle = msdfgen::loadFontData(ft,
            (const unsigned char *)DROID_SANS_MONO_TTF, DROID_SANS_MONO_TTF_LEN);

        if (!_defaultFont.fontHandle) {
            msdfgen::deinitializeFreetype(ft);
            LOG_CRITICAL("Failed to load default font");
        }

        // Use temporary msdf vector for generation, then convert to CachedGlyph
        std::vector<msdf_atlas::GlyphGeometry> msdfGlyphs;

        msdf_atlas::FontGeometry fontGeometry(&msdfGlyphs);
        msdf_atlas::Charset      charset;
        for (uint32_t cp = 0x20; cp <= 0x17F; ++cp)
            charset.add(cp);
        fontGeometry.loadCharset(_defaultFont.fontHandle, 1.0, charset);

        _defaultFont.ascender   = fontGeometry.getMetrics().ascenderY;
        _defaultFont.descender  = fontGeometry.getMetrics().descenderY;
        _defaultFont.lineHeight = fontGeometry.getMetrics().lineHeight;

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

        packer.getDimensions(_defaultFont.atlasWidth, _defaultFont.atlasHeight);

        LOG_INFO("Default font MSDF atlas: {}x{}", _defaultFont.atlasWidth, _defaultFont.atlasHeight);

        msdf_atlas::ImmediateAtlasGenerator<
            float, 3,
            msdf_atlas::msdfGenerator,
            msdf_atlas::BitmapAtlasStorage<unsigned char, 3>>
            generator(_defaultFont.atlasWidth, _defaultFont.atlasHeight);

        generator.setThreadCount(Platform::DefaultThreadCount());
        generator.generate(msdfGlyphs.data(), msdfGlyphs.size());

        msdfgen::BitmapConstRef<unsigned char, 3> bitmap = generator.atlasStorage();

        std::vector<unsigned char> rgbaData(_defaultFont.atlasWidth * _defaultFont.atlasHeight * 4);
        for (int y = 0; y < _defaultFont.atlasHeight; ++y) {
            for (int x = 0; x < _defaultFont.atlasWidth; ++x) {
                int                  srcY  = _defaultFont.atlasHeight - 1 - y;
                int                  idx   = (y * _defaultFont.atlasWidth + x);
                const unsigned char *pixel = bitmap(x, srcY);
                rgbaData[idx * 4 + 0]      = pixel[0];
                rgbaData[idx * 4 + 1]      = pixel[1];
                rgbaData[idx * 4 + 2]      = pixel[2];
                rgbaData[idx * 4 + 3]      = 255;
            }
        }

        GpuTextureCreateInfo textureInfo {
            .width         = static_cast<uint32_t>(_defaultFont.atlasWidth),
            .height        = static_cast<uint32_t>(_defaultFont.atlasHeight),
            .depthOrLayers = 1,
            .numLevels     = 1,
            .format        = GpuTextureFormat::R8G8B8A8_Unorm,
            .sampleCount   = GpuSampleCount::X1,
            .usage         = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer,
        };
        _defaultFont.atlasTexture = Renderer::GetGpu().CreateTexture(textureInfo);

        if (!_copyToTexture(rgbaData.data(), (uint32_t)rgbaData.size(),
                _defaultFont.atlasTexture,
                _defaultFont.atlasWidth, _defaultFont.atlasHeight)) {
            LOG_CRITICAL("Failed to upload default font MSDF atlas to GPU");
        }

        // Convert msdf_atlas::GlyphGeometry -> CachedGlyph
        _defaultFont.glyphs   = new std::vector<CachedGlyph>();
        _defaultFont.glyphMap = new std::unordered_map<uint32_t, size_t>();
        for (size_t i = 0; i < msdfGlyphs.size(); ++i) {
            CachedGlyph cached;
            cached.codepoint = msdfGlyphs[i].getCodepoint();
            cached.advance   = msdfGlyphs[i].getAdvance();
            msdfGlyphs[i].getQuadPlaneBounds(cached.pl, cached.pb, cached.pr, cached.pt);
            msdfGlyphs[i].getQuadAtlasBounds(cached.al, cached.ab, cached.ar, cached.at);
            _defaultFont.glyphs->push_back(cached);
            if (cached.codepoint > 0) {
                (*_defaultFont.glyphMap)[cached.codepoint] = i;
            }
        }

        _defaultFont.generatedSize     = 64;
        _defaultFont.defaultRenderSize = 16;

        // Save to cache for next startup
        _saveFontToCache("__default_font__", _defaultFont, rgbaData, embeddedHash);
#endif // LUMINOVEAU_NO_RUNTIME_MSDF
    }
};

void AssetHandler::_cleanup() {

    std::lock_guard<std::mutex> lock(_assetMutex);
    auto                       &gpu = Renderer::GetGpu();

    // Cleanup textures
    for (auto &[name, tex] : _textures) {
        if (tex.gpuTexture) {
            gpu.ReleaseTexture(tex.gpuTexture);
            tex.gpuTexture = 0;
        }
    }
    _textures.clear();

    // Cleanup shaders
    for (auto &[name, shader] : _shaders) {
        if (shader.gpuShader) {
            gpu.ReleaseShader(shader.gpuShader);
            shader.gpuShader = 0;
        }
    }
    _shaders.clear();

    // Cleanup compute pipelines
    for (auto &[name, cp] : _computePipelines) {
        if (cp.pipeline) {
            gpu.ReleaseComputePipeline(cp.pipeline);
            cp.pipeline = 0;
        }
    }
    _computePipelines.clear();

    // Cleanup fonts
    for (auto &[name, font] : _fonts) {
#ifndef LUMINOVEAU_NO_RUNTIME_MSDF
        if (font.fontHandle) {
            msdfgen::destroyFont(font.fontHandle);
            font.fontHandle = nullptr;
        }
#endif
        if (font.glyphs) {
            delete font.glyphs;
            font.glyphs = nullptr;
        }
        if (font.glyphMap) {
            delete font.glyphMap;
            font.glyphMap = nullptr;
        }
        if (font.atlasTexture) {
            gpu.ReleaseTexture(font.atlasTexture);
            font.atlasTexture = 0;
        }
        if (font.fontData) {
            free(font.fontData);
            font.fontData = nullptr;
        }
    }
    _fonts.clear();

    // Cleanup sounds
    for (auto &[name, sound] : _sounds) {
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
    for (auto &[name, music] : _musics) {
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
#ifndef LUMINOVEAU_NO_RUNTIME_MSDF
    if (_defaultFont.fontHandle) {
        msdfgen::destroyFont(_defaultFont.fontHandle);
        _defaultFont.fontHandle = nullptr;
    }
#endif
    if (_defaultFont.glyphs) {
        delete _defaultFont.glyphs;
        _defaultFont.glyphs = nullptr;
    }
    if (_defaultFont.glyphMap) {
        delete _defaultFont.glyphMap;
        _defaultFont.glyphMap = nullptr;
    }
    if (_defaultFont.atlasTexture) {
        gpu.ReleaseTexture(_defaultFont.atlasTexture);
        _defaultFont.atlasTexture = 0;
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
    std::lock_guard<std::mutex> lock(_assetMutex);

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

    // KTX2/Basis containers can't go through SDL_image — transcode them (UASTC -> BC7).
    // Same TextureAsset out, so callers/shaders are oblivious to the format.
    static const uint8_t KTX2_MAGIC[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A }; // NOLINT(readability-identifier-naming)
    if (filedata.data && filedata.fileSize >= 12 && memcmp(filedata.data, KTX2_MAGIC, 12) == 0) {
        texture = _loadKtx2(reinterpret_cast<const uint8_t *>(filedata.data), filedata.fileSize);
        free(filedata.data);
        _textures[std::string(fileName)] = texture;
        _textures[fileName].filename     = _textures.find(fileName)->first.c_str();
        return _textures[fileName];
    }

    SDL_IOStream *io      = SDL_IOFromMem(filedata.data, filedata.fileSize);
    SDL_Surface  *surface = IMG_Load_IO(io, true); // SDL_TRUE = close IO after reading

    if (!surface) {
        LOG_CRITICAL("IMG_Load failed: {}", SDL_GetError());
    }

    if (surface->format != SDL_PIXELFORMAT_RGBA32) {
        SDL_Surface *convertedSurface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface); // Free the original surface

        surface = convertedSurface;
    }

    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);

    texture.filename = fileName.c_str();
    texture.width    = surface->w;
    texture.height   = surface->h;

    auto                &gpu = Renderer::GetGpu();
    GpuTextureCreateInfo texInfo {
        .width         = static_cast<uint32_t>(texture.width),
        .height        = static_cast<uint32_t>(texture.height),
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = GpuTextureFormat::R8G8B8A8_Unorm,
        .sampleCount   = GpuSampleCount::X1,
        .usage         = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer,
    };
    GpuTextureHandle gpuTexture = gpu.CreateTexture(texInfo);
    if (!gpuTexture) {
        LOG_CRITICAL("failed to create texture: {}", fileName.c_str());
    }

    if (!_copyToTexture(surface->pixels,
            static_cast<uint32_t>(texture.width * texture.height * 4),
            gpuTexture, texture.width, texture.height)) {
        gpu.ReleaseTexture(gpuTexture);
        LOG_CRITICAL("failed to copy image data to texture");
    }

    texture.gpuSampler = Renderer::GetSampler(_defaultMode);
    texture.gpuTexture = gpuTexture;

    SDL_DestroySurface(surface);
    free(filedata.data);

    LOG_INFO("loaded texture {} ({}x{})", fileName.c_str(), texture.width, texture.height);

    _textures[std::string(fileName)] = texture;

    // FIX: Point filename to the stable string in the map, not the local variable!
    _textures[fileName].filename = _textures.find(fileName)->first.c_str();

    return texture;
}

// Uncached load to a GPU asset; dispatches KTX2/Basis (transcode -> BC) vs SDL_image (RGBA8).
TextureAsset AssetHandler::_loadTextureFile(const std::string &path) {
    TextureAsset out;
    if (!Renderer::IsReady())
        return out;

    auto fd = FileHandler::ReadFile(path);
    if (!fd.data || fd.fileSize < 12) {
        if (fd.data)
            free(fd.data);
        return out;
    }

    static const uint8_t KTX2_MAGIC[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A }; // NOLINT(readability-identifier-naming) «KTX 20»\r\n\x1A\n
    bool                 isKtx2         = (memcmp(fd.data, KTX2_MAGIC, 12) == 0);

    if (isKtx2) {
        out = _loadKtx2(reinterpret_cast<const uint8_t *>(fd.data), fd.fileSize);
        free(fd.data);
        return out;
    }

    // SDL_image path (RGBA8), uncached.
    SDL_IOStream *io      = SDL_IOFromMem(fd.data, fd.fileSize);
    SDL_Surface  *surface = IMG_Load_IO(io, true);
    if (!surface) {
        LOG_WARNING("LoadTextureFile: decode failed: {}", path.c_str());
        free(fd.data);
        return out;
    }
    if (surface->format != SDL_PIXELFORMAT_RGBA32) {
        SDL_Surface *conv = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        surface = conv;
    }

    auto                &gpu = Renderer::GetGpu();
    GpuTextureCreateInfo tci {
        .width         = (uint32_t)surface->w,
        .height        = (uint32_t)surface->h,
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = GpuTextureFormat::R8G8B8A8_Unorm,
        .sampleCount   = GpuSampleCount::X1,
        .usage         = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer,
    };
    GpuTextureHandle tex = gpu.CreateTexture(tci);
    _copyToTexture(surface->pixels, (uint32_t)(surface->w * surface->h * 4), tex, surface->w, surface->h);
    out.gpuTexture = tex;
    out.gpuSampler = Renderer::GetSampler(ScaleMode::Linear);
    out.width      = surface->w;
    out.height     = surface->h;
    SDL_DestroySurface(surface);
    free(fd.data);
    return out;
}

// Transcode a KTX2/Basis container to a GPU-compressed BC7 texture (all mip levels).
TextureAsset AssetHandler::_loadKtx2(const uint8_t *data, size_t size) {
    TextureAsset out;
#if defined(LUMINOVEAU_WITH_KTX2)
    static bool basisInit = false;
    if (!basisInit) {
        basist::basisu_transcoder_init();
        basisInit = true;
    }

    basist::ktx2_transcoder t;
    if (!t.init(data, (uint32_t)size)) {
        LOG_WARNING("KTX2: init failed");
        return out;
    }
    if (!t.start_transcoding()) {
        LOG_WARNING("KTX2: start_transcoding failed");
        return out;
    }

    uint32_t levels = t.get_levels();
    if (levels < 1)
        levels = 1;
    auto &gpu = Renderer::GetGpu();

    // Only attempt BC7 if the backend reports support — on WebGPU CreateTexture(BC7) THROWS
    // (aborting wasm) when texture-compression-bc is off, so we can't rely on a null return
    // there. SDL/Metal report true and still null-return on a genuinely unsupported format,
    // caught below.
#if defined(__APPLE__)
    // Apple: force RGBA8 (uncompressed). The UASTC->BC7 *pack* is ~5s per map of SIMD-less scalar
    // work even at -O2 (the UASTC->RGBA *unpack* is ~1ms), and Apple GPUs don't sample BC natively.
    // RGBA8 costs 4x VRAM but loads near-instantly. TODO: revisit with a threaded transcode or an
    // on-disk cache of pre-transcoded blocks if VRAM becomes a concern.
    bool useBC7 = false;
#else
    bool useBC7 = gpu.SupportsBCTextures();
#endif

    // BC works in 4x4 blocks, and WebGPU's writeTexture rejects compressed copies for mips
    // smaller than the block (the 2x2 / 1x1 tail: "copySize.width is not a multiple of 4").
    // Cap the BC mip chain at the smallest level still >= 4px in both dims; sampling just
    // clamps LOD there. (RGBA8 has no block constraint but a slightly shorter chain is fine.)
    if (useBC7) {
        uint32_t n = 0;
        while (n < levels && (t.get_width() >> n) >= 4 && (t.get_height() >> n) >= 4)
            n++;
        levels = (n < 1) ? 1 : n;
    }

    // Prefer BC7 (4x smaller in VRAM). If the device/backend can't create a BC7 texture
    // (some drivers/backends don't expose BC), fall back to an RGBA8 transcode so HD textures
    // still show (at 4x the VRAM) instead of silently dropping to the 8-bit base.
    GpuTextureCreateInfo tci {
        .width         = t.get_width(),
        .height        = t.get_height(),
        .depthOrLayers = 1,
        .numLevels     = levels,
        .format        = GpuTextureFormat::BC7_Unorm,
        .sampleCount   = GpuSampleCount::X1,
        .usage         = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer,
    };
    GpuTextureHandle tex = 0;
    if (useBC7) {
        tex = gpu.CreateTexture(tci);
        if (!tex) {
            LOG_WARNING("KTX2: BC7 texture create failed ({}x{}) -> RGBA8 fallback",
                t.get_width(), t.get_height());
            useBC7 = false;
        }
    }
    if (!useBC7) {
        tci.format = GpuTextureFormat::R8G8B8A8_Unorm;
        tex        = gpu.CreateTexture(tci);
        if (!tex) {
            LOG_WARNING("KTX2: RGBA8 fallback create also failed");
            return out;
        }
    }

    const basist::transcoder_texture_format tfmt = useBC7 ? basist::transcoder_texture_format::cTFBC7_RGBA
                                                          : basist::transcoder_texture_format::cTFRGBA32;

    // Upload ALL mip levels through a single command buffer + one submit. The previous
    // per-level acquire/submit cost ~one command-buffer commit per mip — thousands per map
    // load — which is brutal on backends with high submit overhead (Metal). Transfer buffers
    // stay alive until after the submit; SDL defers their actual free until the GPU is done.
    // _batchAcquire/_batchFinishUpload additionally fold this whole texture's upload into a
    // map-wide batch (see Begin/EndUploadBatch) when one is active, so the per-TEXTURE submit
    // also collapses — one commit per flush instead of one per HD texture.
    GpuCmdBufferHandle cmd = _batchAcquire();
    for (uint32_t lvl = 0; lvl < levels; lvl++) {
        basist::ktx2_image_level_info li {};
        if (!t.get_image_level_info(li, lvl, 0, 0))
            continue;
        // BC7 works in 4x4 blocks (16 B each); RGBA32 works per pixel (4 B each).
        uint32_t count    = useBC7 ? li.m_total_blocks : (li.m_orig_width * li.m_orig_height);
        uint32_t dstBytes = useBC7 ? li.m_total_blocks * 16u : count * 4u;
        uint32_t rowPx    = useBC7 ? 0u : li.m_orig_width; // BC7: infer block-aligned; RGBA: explicit

        GpuTransferBufferCreateInfo tbci { dstBytes, GpuTransferUsage::Upload };
        GpuTransferBufferHandle     tb  = gpu.CreateTransferBuffer(tbci);
        void                       *dst = gpu.MapTransferBuffer(tb, false);
        bool                        ok  = t.transcode_image_level(lvl, 0, 0, dst, count, tfmt);
        gpu.UnmapTransferBuffer(tb);
        if (!ok) {
            gpu.ReleaseTransferBuffer(tb);
            LOG_WARNING("KTX2: transcode level {} failed", lvl);
            continue;
        }

        GpuTransferBufferRegion src { tb, 0, rowPx, 0 };
        GpuTextureRegion        dr { tex, lvl, 0, 0, 0, 0, li.m_orig_width, li.m_orig_height, 1 };
        gpu.UploadToTexture(cmd, src, dr, false);
        _batchTrack(tb, dstBytes);
    }
    _batchFinishUpload(cmd);

    out.gpuTexture = tex;
    out.gpuSampler = Renderer::GetSampler(ScaleMode::Linear);
    out.width      = (int)t.get_width();
    out.height     = (int)t.get_height();
#else
    (void)data;
    (void)size;
    LOG_WARNING("KTX2 requested but engine built without LUMINOVEAU_WITH_KTX2");
#endif
    return out;
}

TextureAsset AssetHandler::_createEmptyTexture(const vf2d &size) {
    TextureAsset texture;
    texture.width  = (int)size.x;
    texture.height = (int)size.y;

    // Swapchain-format default framebuffer: no storage usage (BGRA8Unorm on WebGPU is incompatible
    // with StorageBinding unless the BGRA8UnormStorage feature is enabled).
    GpuTextureCreateInfo info {
        .width         = static_cast<uint32_t>(size.x),
        .height        = static_cast<uint32_t>(size.y),
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = Renderer::GetGpu().GetSwapchainFormat(),
        .sampleCount   = GpuSampleCount::X1,
        .usage         = GpuTextureUsage::ColorTarget | GpuTextureUsage::Sampler,
    };
    texture.gpuSampler = Renderer::GetSampler(_defaultMode);
    texture.gpuTexture = Renderer::GetGpu().CreateTexture(info);
    return texture;
}

TextureAsset AssetHandler::_createEmptyTexture(const vf2d &size, GpuTextureFormat format) {
    TextureAsset texture;
    texture.width  = (int)size.x;
    texture.height = (int)size.y;

    GpuTextureCreateInfo info {
        .width         = static_cast<uint32_t>(size.x),
        .height        = static_cast<uint32_t>(size.y),
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = format,
        .sampleCount   = GpuSampleCount::X1,
        .usage         = GpuTextureUsage::ColorTarget | GpuTextureUsage::Sampler
            | GpuTextureUsage::StorageRead | GpuTextureUsage::StorageWrite,
    };
    texture.gpuSampler = Renderer::GetSampler(_defaultMode);
    texture.gpuTexture = Renderer::GetGpu().CreateTexture(info);
    return texture;
}

void AssetHandler::_saveTextureAsPNG(Texture texture, const char *fileName) {
    LUMI_UNUSED(texture, fileName);
}

Sound AssetHandler::_getSound(const std::string &fileName) {
    std::lock_guard<std::mutex> lock(_assetMutex);

    if (_sounds.find(fileName) == _sounds.end()) {
        SoundAsset soundAsset;
        soundAsset.sound    = new ma_sound();
        soundAsset.fileName = fileName;

        auto filedata = FileHandler::ReadFile(fileName);

        // Store fileData so we can free it in cleanup
        soundAsset.fileData = filedata.data;

        ma_resource_manager_register_encoded_data(Audio::GetAudioEngine()->pResourceManager, fileName.c_str(), filedata.data, filedata.fileSize);

        ma_result result = ma_sound_init_from_file(Audio::GetAudioEngine(), fileName.c_str(),
            MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC,
            Audio::GetChannelGroup(AudioChannel::SFX),
            nullptr, soundAsset.sound);

        if (result != MA_SUCCESS) {
            // Non-fatal: a single undecodable sound must not kill the app (LOG_CRITICAL
            // exits). Fully clean up so no half-registered data lingers in the resource
            // manager (which crashes later): unregister the encoded data, then free it.
            ma_resource_manager_unregister_data(Audio::GetAudioEngine()->pResourceManager,
                fileName.c_str());
            free(filedata.data);
            delete soundAsset.sound;
            soundAsset.sound    = nullptr;
            soundAsset.fileData = nullptr;
            // LOG_WARNING, not LOG_ERROR/LOG_CRITICAL — both of those are fatal here
            // (Error throws [[noreturn]], Critical exits). A bad sound must not abort.
            LOG_WARNING("GetSound failed (sound will be silent): {}", fileName.c_str());
        }

        _sounds[fileName] = soundAsset;

        return _sounds[fileName];
    } else {
        return _sounds[fileName];
    }
}

Music AssetHandler::_getMusic(const std::string &fileName) {
    std::lock_guard<std::mutex> lock(_assetMutex);

    if (_musics.find(fileName) == _musics.end()) {
        MusicAsset musicAsset;

        musicAsset.music = new ma_sound();

        auto filedata = FileHandler::ReadFile(fileName);

        // Store fileData so we can free it in cleanup
        musicAsset.fileData = filedata.data;

        ma_resource_manager_register_encoded_data(Audio::GetAudioEngine()->pResourceManager, fileName.c_str(), filedata.data, filedata.fileSize);

        ma_result result = ma_sound_init_from_file(Audio::GetAudioEngine(), fileName.c_str(),
            MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC,
            Audio::GetChannelGroup(AudioChannel::Music),
            nullptr, musicAsset.music);

        if (result != MA_SUCCESS) {
            // Non-fatal (LOG_CRITICAL exits). Keep filedata (registered with the
            // resource manager; tracked in musicAsset.fileData for cleanup).
            delete musicAsset.music;
            musicAsset.music = nullptr;
            LOG_WARNING("GetMusic failed (music will be silent): {}", fileName.c_str());
        }

        _musics[fileName] = musicAsset;

        return _musics[fileName];
    } else {
        return _musics[fileName];
    }
}

Font AssetHandler::_getFont(const std::string &fileName, const int fontSize) {
    std::lock_guard<std::mutex> lock(_assetMutex);

    // Check if this font file is already loaded (ignore size, we'll set defaultRenderSize)
    auto it = _fonts.find(fileName);
    if (it != _fonts.end()) {
        _fonts[fileName].defaultRenderSize = fontSize;
        return _fonts[fileName];
    }

    // Try loading from font cache
    FontAsset fontAsset;
    if (_loadFontFromCache(fileName, fontSize, fontAsset)) {
        _fonts[fileName] = fontAsset;
        return _fonts[fileName];
    }

#ifdef LUMINOVEAU_NO_RUNTIME_MSDF
    // No runtime generation on this platform — fall back to the default font so
    // text keeps rendering (with a warning) instead of crashing.
    LOG_WARNING("Cannot generate MSDF font '{}' (runtime MSDF compiled out); using default font", fileName.c_str());
    return _defaultFont;
#else
    // Cache miss - generate MSDF atlas from scratch
    LOG_INFO("Generating MSDF font {} (atlas size: 64, default render: {})", fileName.c_str(), fontSize);

    auto filedata      = FileHandler::ReadFile(fileName);
    fontAsset.fontData = filedata.data;

    msdfgen::FreetypeHandle *ft = msdfgen::initializeFreetype();
    if (!ft) {
        free(filedata.data);
        LOG_CRITICAL("Failed to initialize FreeType for MSDF: {}", fileName.c_str());
    }

    fontAsset.fontHandle = msdfgen::loadFontData(ft,
        (const unsigned char *)filedata.data, filedata.fileSize);

    if (!fontAsset.fontHandle) {
        free(filedata.data);
        msdfgen::deinitializeFreetype(ft);
        LOG_CRITICAL("Failed to load font for MSDF: {}", fileName.c_str());
    }

    std::vector<msdf_atlas::GlyphGeometry> msdfGlyphs;

    msdf_atlas::FontGeometry fontGeometry(&msdfGlyphs);
    msdf_atlas::Charset      charset;
    for (uint32_t cp = 0x20; cp <= 0x17F; ++cp)
        charset.add(cp);
    fontGeometry.loadCharset(fontAsset.fontHandle, 1.0, charset);

    fontAsset.ascender   = fontGeometry.getMetrics().ascenderY;
    fontAsset.descender  = fontGeometry.getMetrics().descenderY;
    fontAsset.lineHeight = fontGeometry.getMetrics().lineHeight;

    const double maxCornerAngle = 3.0;
    for (msdf_atlas::GlyphGeometry &glyph : msdfGlyphs) {
        glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, maxCornerAngle, 0);
    }

    const int ATLAS_GENERATION_SIZE = 64; // NOLINT(readability-identifier-naming)

    msdf_atlas::TightAtlasPacker packer;
    packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::SQUARE);
    packer.setMinimumScale(ATLAS_GENERATION_SIZE);
    packer.setPixelRange(4.0);
    packer.setMiterLimit(1.0);
    packer.pack(msdfGlyphs.data(), msdfGlyphs.size());

    packer.getDimensions(fontAsset.atlasWidth, fontAsset.atlasHeight);

    LOG_INFO("MSDF atlas for {}: {}x{}", fileName.c_str(), fontAsset.atlasWidth, fontAsset.atlasHeight);

    msdf_atlas::ImmediateAtlasGenerator<
        float, 3,
        msdf_atlas::msdfGenerator,
        msdf_atlas::BitmapAtlasStorage<unsigned char, 3>>
        generator(fontAsset.atlasWidth, fontAsset.atlasHeight);

    generator.setThreadCount(Platform::DefaultThreadCount());
    generator.generate(msdfGlyphs.data(), msdfGlyphs.size());

    msdfgen::BitmapConstRef<unsigned char, 3> bitmap = generator.atlasStorage();

    std::vector<unsigned char> rgbaData(fontAsset.atlasWidth * fontAsset.atlasHeight * 4);
    for (int y = 0; y < fontAsset.atlasHeight; ++y) {
        for (int x = 0; x < fontAsset.atlasWidth; ++x) {
            int                  srcY  = fontAsset.atlasHeight - 1 - y;
            int                  idx   = (y * fontAsset.atlasWidth + x);
            const unsigned char *pixel = bitmap(x, srcY);
            rgbaData[idx * 4 + 0]      = pixel[0];
            rgbaData[idx * 4 + 1]      = pixel[1];
            rgbaData[idx * 4 + 2]      = pixel[2];
            rgbaData[idx * 4 + 3]      = 255;
        }
    }

    GpuTextureCreateInfo textureInfo {
        .width         = static_cast<uint32_t>(fontAsset.atlasWidth),
        .height        = static_cast<uint32_t>(fontAsset.atlasHeight),
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = GpuTextureFormat::R8G8B8A8_Unorm,
        .sampleCount   = GpuSampleCount::X1,
        .usage         = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer,
    };
    fontAsset.atlasTexture = Renderer::GetGpu().CreateTexture(textureInfo);

    if (!_copyToTexture(rgbaData.data(), (uint32_t)rgbaData.size(),
            fontAsset.atlasTexture, fontAsset.atlasWidth, fontAsset.atlasHeight)) {
        LOG_CRITICAL("Failed to upload MSDF atlas to GPU: {}", fileName.c_str());
    }

    // Convert msdf_atlas::GlyphGeometry -> CachedGlyph
    fontAsset.glyphs   = new std::vector<CachedGlyph>();
    fontAsset.glyphMap = new std::unordered_map<uint32_t, size_t>();
    for (size_t i = 0; i < msdfGlyphs.size(); ++i) {
        CachedGlyph cached;
        cached.codepoint = msdfGlyphs[i].getCodepoint();
        cached.advance   = msdfGlyphs[i].getAdvance();
        msdfGlyphs[i].getQuadPlaneBounds(cached.pl, cached.pb, cached.pr, cached.pt);
        msdfGlyphs[i].getQuadAtlasBounds(cached.al, cached.ab, cached.ar, cached.at);
        fontAsset.glyphs->push_back(cached);
        if (cached.codepoint > 0) {
            (*fontAsset.glyphMap)[cached.codepoint] = i;
        }
    }

    fontAsset.generatedSize     = ATLAS_GENERATION_SIZE;
    fontAsset.defaultRenderSize = fontSize;

    // Save to font cache
    _saveFontToCache(fileName, fontAsset, rgbaData);

    LOG_INFO("Loaded MSDF font {} ({} glyphs, default render size: {})", fileName.c_str(), fontAsset.glyphs->size(), fontSize);

    _fonts[fileName] = fontAsset;
    return _fonts[fileName];
#endif // LUMINOVEAU_NO_RUNTIME_MSDF
}

void AssetHandler::_setDefaultTextureScaleMode(ScaleMode mode) {
    _defaultMode = mode;
}

ScaleMode AssetHandler::_getDefaultTextureScaleMode() {
    return _defaultMode;
}

Shader AssetHandler::_getShader(const std::string &fileName) {
    std::lock_guard<std::mutex> lock(_assetMutex);

    if (_shaders.find(fileName) == _shaders.end()) {
        LOG_INFO("loading shader: {}", fileName.c_str());
        _shaders[std::string(fileName)] = _loadShaderFromDisk(fileName);
        return _shaders[fileName];
    }
    return _shaders[fileName];
}

ComputePipelineAsset &AssetHandler::_getComputePipeline(const std::string &fileName) {
    std::lock_guard<std::mutex> lock(_assetMutex);

    auto it = _computePipelines.find(fileName);
    if (it != _computePipelines.end()) {
        return it->second;
    }

    LOG_INFO("loading compute pipeline: {}", fileName.c_str());
    _computePipelines[fileName] = Renderer::CreateComputePipelineAsset(fileName);
    return _computePipelines[fileName];
}

bool AssetHandler::_copyToTexture(void *srcData, uint32_t srcDataLen,
    GpuTextureHandle dstTexture,
    uint32_t dstTextureWidth, uint32_t dstTextureHeight) {
    auto &gpu = Renderer::GetGpu();

    GpuTransferBufferCreateInfo tbInfo { .size = srcDataLen, .usage = GpuTransferUsage::Upload };
    GpuTransferBufferHandle     tb = gpu.CreateTransferBuffer(tbInfo);
    if (!tb)
        return false;

    void *ptr = gpu.MapTransferBuffer(tb, false);
    if (!ptr) {
        gpu.ReleaseTransferBuffer(tb);
        return false;
    }
    std::memcpy(ptr, srcData, srcDataLen);
    gpu.UnmapTransferBuffer(tb);

    GpuCmdBufferHandle cmd = _batchAcquire();
    if (!cmd) {
        gpu.ReleaseTransferBuffer(tb);
        return false;
    }

    GpuTransferBufferRegion src { .transferBuffer = tb, .offset = 0 };
    GpuTextureRegion        dst {
               .texture  = dstTexture,
               .mipLevel = 0,
               .layer    = 0,
               .x        = 0,
               .y        = 0,
               .z        = 0,
               .width    = dstTextureWidth,
               .height   = dstTextureHeight,
               .depth    = 1,
    };
    gpu.UploadToTexture(cmd, src, dst, false);
    _batchTrack(tb, srcDataLen);
    _batchFinishUpload(cmd);
    return true;
}

// ── Upload batching ───────────────────────────────────────────────────────────
// Outside a batch these behave exactly like the original acquire→upload→submit→release per
// texture. Inside a batch (Begin/EndUploadBatch) uploads share one command buffer and defer
// transfer-buffer release to a flush, so a map's hundreds of HD-texture uploads collapse from
// one GPU commit each to one per flush — the win on Metal's relatively costly per-submit path.

void AssetHandler::_beginUploadBatch() {
    if (_uploadBatching) {
        LOG_WARNING("BeginUploadBatch: already batching (nesting unsupported)");
        return;
    }
    _uploadBatching = true;
    _batchCmd       = 0;
    _batchStaging.clear();
    _batchBytes = 0;
}

void AssetHandler::_endUploadBatch() {
    if (!_uploadBatching)
        return;
    _batchFlush(); // submit whatever's pending + release its transfer buffers
    _uploadBatching = false;
}

GpuCmdBufferHandle AssetHandler::_batchAcquire() {
    auto &gpu = Renderer::GetGpu();
    if (!_uploadBatching)
        return gpu.AcquireCommandBuffer();
    if (!_batchCmd)
        _batchCmd = gpu.AcquireCommandBuffer();
    return _batchCmd;
}

void AssetHandler::_batchTrack(GpuTransferBufferHandle tb, uint32_t bytes) {
    // Used in both modes: while batching this is the deferred-release list (freed at flush);
    // otherwise it's this single upload's transient list, freed by _batchFinishUpload below.
    _batchStaging.push_back(tb);
    _batchBytes += bytes;
}

void AssetHandler::_batchFinishUpload(GpuCmdBufferHandle cmd) {
    auto &gpu = Renderer::GetGpu();
    if (!_uploadBatching) {
        gpu.SubmitCommandBuffer(cmd);
        for (GpuTransferBufferHandle tb : _batchStaging)
            gpu.ReleaseTransferBuffer(tb);
        _batchStaging.clear();
        _batchBytes = 0;
        return;
    }
    // Batching: keep recording into the shared command buffer. Flush opportunistically once the
    // pending transfer buffers exceed a budget so a large map doesn't pin hundreds of MB of
    // host-visible staging at once. (BC7 512^2 + mips ≈ 0.35 MB; x4 maps x hundreds of surfaces.)
    static constexpr size_t kBatchFlushBytes = 64u * 1024u * 1024u;
    if (_batchBytes >= kBatchFlushBytes)
        _batchFlush();
}

void AssetHandler::_batchFlush() {
    auto &gpu = Renderer::GetGpu();
    if (_batchCmd) {
        gpu.SubmitCommandBuffer(_batchCmd);
        _batchCmd = 0;
    }
    for (GpuTransferBufferHandle tb : _batchStaging)
        gpu.ReleaseTransferBuffer(tb);
    _batchStaging.clear();
    _batchBytes = 0;
}

TextureAsset AssetHandler::_createDepthTarget(uint32_t width, uint32_t height) {
    GpuTextureCreateInfo info {
        .width         = width,
        .height        = height,
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = GpuTextureFormat::D32_Float_S8_Uint,
        .sampleCount   = GpuSampleCount::X1,
        .usage         = GpuTextureUsage::DepthStencilTarget,
    };
    GpuTextureHandle handle = Renderer::GetGpu().CreateTexture(info);
    if (!handle)
        LOG_CRITICAL("failed to create depth texture");

    TextureAsset tex;
    tex.gpuSampler = Renderer::GetSampler(_defaultMode);
    tex.gpuTexture = handle;
    return tex;
}

TextureAsset AssetHandler::_createWhitePixel() {
    TextureAsset whitePixel;
    whitePixel.filename = "[Lumi]WhitePixel";

    GpuTextureCreateInfo info {
        .width         = 1,
        .height        = 1,
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = GpuTextureFormat::R8G8B8A8_Unorm,
        .sampleCount   = GpuSampleCount::X1,
        .usage         = GpuTextureUsage::ColorTarget | GpuTextureUsage::Sampler
            | GpuTextureUsage::Transfer,
    };
    whitePixel.gpuSampler = Renderer::GetSampler(_defaultMode);
    whitePixel.gpuTexture = Renderer::GetGpu().CreateTexture(info);

    uint32_t white = 0xFFFFFFFF;
    _copyToTexture(&white, sizeof(white), whitePixel.gpuTexture, 1, 1);

    return whitePixel;
}

TextureAsset AssetHandler::_loadFromPixelData(const vf2d &size, void *pixelData, std::string fileName) {
    LUMI_UNUSED(size, pixelData, fileName);

    TextureAsset texture;
#if 0

#endif

    return texture;
}

ModelAsset AssetHandler::_createCube(float size, CubeUVLayout layout) {
    ModelAsset cube;
    cube.name = "cube";

    float s = size / 2.0f; // Half size for centering

    // UV inset to avoid sampling at exact atlas boundaries
    // This prevents texture bleeding between atlas regions
    constexpr float UV_INSET = 0.00005f; // NOLINT(readability-identifier-naming) ~0.25 px on a 512x512 texture

    // Helper lambda to apply inset to UV coordinates
    auto insetUV = [](float uMin, float vMin, float uMax, float vMax) {
        return FaceUV(
            uMin + UV_INSET,
            vMin + UV_INSET,
            uMax - UV_INSET,
            vMax - UV_INSET);
    };

    // Create cube with default UVs (will be overridden based on layout)
    // 6 faces x 4 vertices = 24 vertices
    // Order: Front, Back, Top, Bottom, Right, Left

    // Front face (+Z) - CubeFace::Front
    cube.vertices.push_back({ -s, -s, s, 0, 0, 1, 0, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ s, -s, s, 0, 0, 1, 1, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ s, s, s, 0, 0, 1, 1, 1, 1, 1, 1, 1 });
    cube.vertices.push_back({ -s, s, s, 0, 0, 1, 0, 1, 1, 1, 1, 1 });

    // Back face (-Z) - CubeFace::Back
    cube.vertices.push_back({ s, -s, -s, 0, 0, -1, 0, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ -s, -s, -s, 0, 0, -1, 1, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ -s, s, -s, 0, 0, -1, 1, 1, 1, 1, 1, 1 });
    cube.vertices.push_back({ s, s, -s, 0, 0, -1, 0, 1, 1, 1, 1, 1 });

    // Top face (+Y) - CubeFace::Top
    cube.vertices.push_back({ -s, s, s, 0, 1, 0, 0, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ s, s, s, 0, 1, 0, 1, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ s, s, -s, 0, 1, 0, 1, 1, 1, 1, 1, 1 });
    cube.vertices.push_back({ -s, s, -s, 0, 1, 0, 0, 1, 1, 1, 1, 1 });

    // Bottom face (-Y) - CubeFace::Bottom
    cube.vertices.push_back({ -s, -s, -s, 0, -1, 0, 0, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ s, -s, -s, 0, -1, 0, 1, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ s, -s, s, 0, -1, 0, 1, 1, 1, 1, 1, 1 });
    cube.vertices.push_back({ -s, -s, s, 0, -1, 0, 0, 1, 1, 1, 1, 1 });

    // Right face (+X) - CubeFace::Right
    cube.vertices.push_back({ s, -s, s, 1, 0, 0, 0, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ s, -s, -s, 1, 0, 0, 1, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ s, s, -s, 1, 0, 0, 1, 1, 1, 1, 1, 1 });
    cube.vertices.push_back({ s, s, s, 1, 0, 0, 0, 1, 1, 1, 1, 1 });

    // Left face (-X) - CubeFace::Left
    cube.vertices.push_back({ -s, -s, -s, -1, 0, 0, 0, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ -s, -s, s, -1, 0, 0, 1, 0, 1, 1, 1, 1 });
    cube.vertices.push_back({ -s, s, s, -1, 0, 0, 1, 1, 1, 1, 1, 1 });
    cube.vertices.push_back({ -s, s, -s, -1, 0, 0, 0, 1, 1, 1, 1, 1 });

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
        // Left-handed camera mirrors the scene horizontally, so face textures render flipped
        // left-to-right — flipU (swap uMin/uMax) un-mirrors each face. Front/Back are also
        // swapped so North lands on +Z and South on -Z (matching +Z = North).
        {
            auto flipU  = [](const FaceUV &f) { return FaceUV(f.uMax, f.vMin, f.uMin, f.vMax); };
            auto rot180 = [](const FaceUV &f) { return FaceUV(f.uMax, f.vMax, f.uMin, f.vMin); };
            cube.SetCubeFaceUVs(CubeFace::Front, flipU(insetUV(0.75f, 0.25f, 1.0f, 0.5f)));       // North (+Z)
            cube.SetCubeFaceUVs(CubeFace::Back, flipU(insetUV(0.25f, 0.25f, 0.5f, 0.5f)));        // South (-Z)
            cube.SetCubeFaceUVs(CubeFace::Top, rot180(flipU(insetUV(0.25f, 0.0f, 0.5f, 0.25f)))); // Top   (+Y), rotated 180°
            cube.SetCubeFaceUVs(CubeFace::Bottom, flipU(insetUV(0.25f, 0.5f, 0.5f, 0.75f)));      // Bottom(-Y)
            cube.SetCubeFaceUVs(CubeFace::Right, flipU(insetUV(0.5f, 0.25f, 0.75f, 0.5f)));       // East  (+X)
            cube.SetCubeFaceUVs(CubeFace::Left, flipU(insetUV(0.0f, 0.25f, 0.25f, 0.5f)));        // West  (-X)
        }
        break;

    case CubeUVLayout::Atlas3x2:
        // 3x2 horizontal cross layout with UV inset:
        // Row 0: Left, Front, Right
        // Row 1: Bottom, Back, Top
        cube.SetCubeFaceUVs(CubeFace::Front, insetUV(0.333f, 0.5f, 0.667f, 1.0f)); // Front
        cube.SetCubeFaceUVs(CubeFace::Back, insetUV(0.333f, 0.0f, 0.667f, 0.5f));  // Back
        cube.SetCubeFaceUVs(CubeFace::Top, insetUV(0.667f, 0.0f, 1.0f, 0.5f));     // Top
        cube.SetCubeFaceUVs(CubeFace::Bottom, insetUV(0.0f, 0.0f, 0.333f, 0.5f));  // Bottom
        cube.SetCubeFaceUVs(CubeFace::Right, insetUV(0.667f, 0.5f, 1.0f, 1.0f));   // Right
        cube.SetCubeFaceUVs(CubeFace::Left, insetUV(0.0f, 0.5f, 0.333f, 1.0f));    // Left
        break;

    case CubeUVLayout::Skybox:
        // 6 textures stitched horizontally (1/6th width each) with UV inset:
        // Order: Right, Left, Top, Bottom, Front, Back
        cube.SetCubeFaceUVs(CubeFace::Right, insetUV(0.0f, 0.0f, 0.1667f, 1.0f));
        cube.SetCubeFaceUVs(CubeFace::Left, insetUV(0.1667f, 0.0f, 0.3333f, 1.0f));
        cube.SetCubeFaceUVs(CubeFace::Top, insetUV(0.3333f, 0.0f, 0.5f, 1.0f));
        cube.SetCubeFaceUVs(CubeFace::Bottom, insetUV(0.5f, 0.0f, 0.6667f, 1.0f));
        cube.SetCubeFaceUVs(CubeFace::Front, insetUV(0.6667f, 0.0f, 0.8333f, 1.0f));
        cube.SetCubeFaceUVs(CubeFace::Back, insetUV(0.8333f, 0.0f, 1.0f, 1.0f));
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

#if defined(LUMINOVEAU_HAVE_FONT_ATLAS_BLOB)
#include "font_atlas_generated.h"
// The decode-only zstd bundled by basis_universal (zstddeclib.c) — declared here so we don't need
// its header. Used to inflate the baked atlas.
// NOLINTBEGIN(readability-identifier-naming) — these declare zstd's own C API; the names
// are upstream's and must match exactly for the linker to resolve them.
extern "C" {
size_t   ZSTD_decompress(void *dst, size_t dstCap, const void *src, size_t srcSize);
unsigned ZSTD_isError(size_t code);
}
// NOLINTEND(readability-identifier-naming)

// Load the default font from the baked blob (tools/font_baker output): parse the layout table (same
// format as .fontmeta) + inflate the zstd RGBA atlas + upload it. No MSDF generation, no font cache.
bool AssetHandler::_loadDefaultFontFromBlob(FontAsset &font) {
    const uint8_t *p   = LUMI_FONT_ATLAS_META;
    size_t         rem = LUMI_FONT_ATLAS_META_LEN;
    auto           rd  = [&](auto &v) {
        if (rem < sizeof(v))
            return false;
        std::memcpy(&v, p, sizeof(v));
        p += sizeof(v);
        rem -= sizeof(v);
        return true;
    };

    uint32_t version = 0, atlasW = 0, atlasH = 0, genSize = 0, glyphCount = 0;
    if (!rd(version) || version != FONT_CACHE_VERSION)
        return false;
    if (!rd(atlasW) || !rd(atlasH) || !rd(genSize) || !rd(glyphCount))
        return false;
    double asc = 0, desc = 0, lh = 0;
    if (!rd(asc) || !rd(desc) || !rd(lh))
        return false;

    font.atlasWidth        = (int)atlasW;
    font.atlasHeight       = (int)atlasH;
    font.generatedSize     = (int)genSize;
    font.defaultRenderSize = 16;
    font.ascender          = asc;
    font.descender         = desc;
    font.lineHeight        = lh;
    font.glyphs            = new std::vector<CachedGlyph>();
    font.glyphMap          = new std::unordered_map<uint32_t, size_t>();
    for (uint32_t i = 0; i < glyphCount; ++i) {
        CachedGlyph g;
        if (!rd(g.codepoint) || !rd(g.advance) || !rd(g.pl) || !rd(g.pb) || !rd(g.pr) || !rd(g.pt) || !rd(g.al) || !rd(g.ab) || !rd(g.ar) || !rd(g.at))
            return false;
        font.glyphs->push_back(g);
        if (g.codepoint > 0)
            (*font.glyphMap)[g.codepoint] = i;
    }

    std::vector<unsigned char> rgba(LUMI_FONT_ATLAS_RGBA_LEN);
    size_t                     got = ZSTD_decompress(rgba.data(), rgba.size(),
                            LUMI_FONT_ATLAS_RGBA_ZSTD, LUMI_FONT_ATLAS_RGBA_ZSTD_LEN);
    if (ZSTD_isError(got) || got != rgba.size()) {
        LOG_WARNING("Font atlas blob: zstd inflate failed");
        return false;
    }

#ifdef __3DS__
    // The PICA200 has no fragment shaders, so the MSDF decode (median of rgb vs 0.5)
    // can't run on the GPU. Bake it into the atlas instead: alpha = smoothstepped
    // median distance, rgb = white, so glyphs render through the plain modulate TEV
    // (texture × vertex color) like any other sprite. Slightly softer edges than
    // true per-pixel MSDF; acceptable at 400×240.
    for (size_t i = 0; i < rgba.size(); i += 4) {
        const unsigned char r = rgba[i], g = rgba[i + 1], b = rgba[i + 2];
        unsigned char       med = std::max(std::min(r, g), std::min(std::max(r, g), b));
        // smoothstep around the 0.5 (=127) iso-line over a ~±16 band
        float t = ((float)med - 111.0f) / 32.0f;
        t       = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        t       = t * t * (3.0f - 2.0f * t);
        rgba[i] = rgba[i + 1] = rgba[i + 2] = 255;
        rgba[i + 3]                         = (unsigned char)(t * 255.0f + 0.5f);
    }
#endif

    GpuTextureCreateInfo textureInfo {
        .width         = atlasW,
        .height        = atlasH,
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = GpuTextureFormat::R8G8B8A8_Unorm,
        .sampleCount   = GpuSampleCount::X1,
        .usage         = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer,
    };
    font.atlasTexture = Renderer::GetGpu().CreateTexture(textureInfo);
    if (!font.atlasTexture || !_copyToTexture(rgba.data(), (uint32_t)rgba.size(), font.atlasTexture, atlasW, atlasH)) {
        LOG_WARNING("Font atlas blob: GPU upload failed");
        return false;
    }

    LOG_INFO("Default font loaded from baked atlas blob ({}x{}, {} glyphs)", atlasW, atlasH, glyphCount);
    return true;
}
#endif // LUMINOVEAU_HAVE_FONT_ATLAS_BLOB

void AssetHandler::_initFontCache() {
    FileHandler::InitPersistentStorage();
    std::string path = FileHandler::GetCacheDirectory() + "font.cache";
    _fontCache       = new ResourcePack(path, "luminoveau_fonts");
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

std::string AssetHandler::_computeFontCacheKey(const std::string &fileName) {
    auto        filedata = FileHandler::ReadFile(fileName);
    std::string data(static_cast<char *>(filedata.data), filedata.fileSize);
    std::string hash = picosha2::hash256_hex_string(data);
    free(filedata.data);
    return hash;
}

std::string AssetHandler::_computeFontCacheKeyFromData(const void *data, size_t size) {
    std::string str(static_cast<const char *>(data), size);
    return picosha2::hash256_hex_string(str);
}

bool AssetHandler::_loadFontFromCache(const std::string &fileName, int fontSize, FontAsset &outFont, const std::string &precomputedHash) {
    if (!_fontCache)
        return false;

    // Build cache keys
    std::string safeName = fileName;
    std::replace(safeName.begin(), safeName.end(), '/', '_');
    std::replace(safeName.begin(), safeName.end(), '\\', '_');
    std::string metaKey  = safeName + ".fontmeta";
    std::string atlasKey = safeName + ".fontatlas";
    std::string hashKey  = safeName + ".fonthash";

    if (!_fontCache->HasFile(metaKey) || !_fontCache->HasFile(atlasKey) || !_fontCache->HasFile(hashKey)) {
        return false;
    }

    // Verify hash
    std::string currentHash = precomputedHash.empty() ? _computeFontCacheKey(fileName) : precomputedHash;
    auto        hashBuf     = _fontCache->GetFileBuffer(hashKey);
    std::string cachedHash(hashBuf.memory.begin(), hashBuf.memory.end());
    if (currentHash != cachedHash) {
        LOG_INFO("Font cache invalid for {} (file changed), regenerating", fileName.c_str());
        return false;
    }

    // Load metadata
    auto           metaBuf   = _fontCache->GetFileBuffer(metaKey);
    const uint8_t *ptr       = metaBuf.memory.data();
    size_t         remaining = metaBuf.memory.size();

    auto readVal = [&](auto &val) {
        if (remaining < sizeof(val))
            return false;
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
    if (!readVal(atlasWidth) || !readVal(atlasHeight) || !readVal(generatedSize) || !readVal(glyphCount))
        return false;

    double ascender = 0, descender = 0, lineHeight = 0;
    if (!readVal(ascender) || !readVal(descender) || !readVal(lineHeight))
        return false;

    // Read glyphs
    auto *glyphs   = new std::vector<CachedGlyph>();
    auto *glyphMap = new std::unordered_map<uint32_t, size_t>();
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
    auto   atlasBuf     = _fontCache->GetFileBuffer(atlasKey);
    size_t expectedSize = (size_t)atlasWidth * atlasHeight * 4;
    if (atlasBuf.memory.size() != expectedSize) {
        LOG_WARNING("Font cache atlas size mismatch for {}", fileName.c_str());
        delete glyphs;
        delete glyphMap;
        return false;
    }

    // Upload atlas to GPU
    GpuTextureCreateInfo textureInfo {
        .width         = atlasWidth,
        .height        = atlasHeight,
        .depthOrLayers = 1,
        .numLevels     = 1,
        .format        = GpuTextureFormat::R8G8B8A8_Unorm,
        .sampleCount   = GpuSampleCount::X1,
        .usage         = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer,
    };
    GpuTextureHandle gpuTex = Renderer::GetGpu().CreateTexture(textureInfo);
    if (!gpuTex || !_copyToTexture(atlasBuf.memory.data(), (uint32_t)atlasBuf.memory.size(), gpuTex, atlasWidth, atlasHeight)) {
        LOG_WARNING("Font cache GPU upload failed for {}", fileName.c_str());
        if (gpuTex)
            Renderer::GetGpu().ReleaseTexture(gpuTex);
        delete glyphs;
        delete glyphMap;
        return false;
    }

    // Fill FontAsset
    outFont.atlasTexture      = gpuTex;
    outFont.atlasWidth        = atlasWidth;
    outFont.atlasHeight       = atlasHeight;
    outFont.generatedSize     = generatedSize;
    outFont.defaultRenderSize = fontSize;
    outFont.ascender          = ascender;
    outFont.descender         = descender;
    outFont.lineHeight        = lineHeight;
    outFont.glyphs            = glyphs;
    outFont.glyphMap          = glyphMap;
    outFont.fontHandle        = nullptr; // No FreeType needed from cache
    outFont.fontData          = nullptr;

    LOG_INFO("Loaded font {} from cache ({} glyphs, {}x{} atlas)", fileName.c_str(), glyphCount, atlasWidth, atlasHeight);
    return true;
}

void AssetHandler::_saveFontToCache(const std::string &fileName, const FontAsset &font, const std::vector<unsigned char> &rgbaData, const std::string &precomputedHash) {
    if (!_fontCache)
        return;

    std::string safeName = fileName;
    std::replace(safeName.begin(), safeName.end(), '/', '_');
    std::replace(safeName.begin(), safeName.end(), '\\', '_');
    std::string metaKey  = safeName + ".fontmeta";
    std::string atlasKey = safeName + ".fontatlas";
    std::string hashKey  = safeName + ".fonthash";

    // Save source hash
    std::string                sourceHash = precomputedHash.empty() ? _computeFontCacheKey(fileName) : precomputedHash;
    std::vector<unsigned char> hashBytes(sourceHash.begin(), sourceHash.end());
    _fontCache->AddFile(hashKey, hashBytes);

    // Build metadata blob
    std::vector<unsigned char> metaBlob;
    auto                       writeVal = [&](const auto &val) {
        const uint8_t *p = reinterpret_cast<const uint8_t *>(&val);
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

    for (const auto &g : *font.glyphs) {
        writeVal(g.codepoint);
        writeVal(g.advance);
        writeVal(g.pl);
        writeVal(g.pb);
        writeVal(g.pr);
        writeVal(g.pt);
        writeVal(g.al);
        writeVal(g.ab);
        writeVal(g.ar);
        writeVal(g.at);
    }

    _fontCache->AddFile(metaKey, metaBlob);

    // Save atlas RGBA data
    std::vector<unsigned char> atlasData(rgbaData.begin(), rgbaData.end());
    _fontCache->AddFile(atlasKey, atlasData);

    // Persist to disk (and on web, push MEMFS → IndexedDB so it survives reload).
    if (_fontCache->SavePack()) {
        LOG_INFO("Font cache saved for {}", fileName.c_str());
        FileHandler::FlushPersistentStorage();
    } else {
        LOG_WARNING("Failed to save font cache!");
    }
}
