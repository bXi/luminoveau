#pragma once

#include <unordered_map>
#include <string>
#include <utility>
#include <mutex>
#include <vector>

#include "SDL3/SDL.h"

#include "stb_image_write.h"

#include "math/vectors.h"
#include "math/rectangles.h"

#include "platform/audio/audio.h"

#include "assets/font/font.h"
#include "assets/audio/music.h"
#include "assets/shader/shader.h"
#include "assets/audio/sound.h"
#include "assets/texture/texture.h"
#include "assets/model/model.h"
#include "assets/compute/computepipeline.h"

#include "file/filehandler.h"
#include "file/resourcepack.h"

// Forward declarations for cleanup
namespace msdfgen {
    class FontHandle;
    void destroyFont(FontHandle*);
}

#include "renderer/renderer.h"

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
     * @brief Load a texture file to a GPU asset WITHOUT caching (caller owns it).
     *
     * Dispatches by format: KTX2/Basis is transcoded to a GPU-compressed BC format
     * (BC7) and uploaded directly; everything else goes through SDL_image (RGBA8).
     * Returns an empty TextureAsset (gpuTexture == 0) on failure.
     */
    static TextureAsset LoadTextureFile(const std::string &path) {
        return get()._loadTextureFile(path);
    }

    /**
     * @brief Coalesce many texture uploads into shared command buffers.
     *
     * Between Begin/EndUploadBatch, the texture loaders record their copy passes into one shared
     * command buffer and defer transfer-buffer release, replacing the per-texture
     * acquire+submit (a GPU commit each) with one submit per flush. The batch auto-flushes once
     * its pending transfer buffers exceed an internal byte budget, so peak host memory stays
     * bounded. Used during map load, where hundreds of HD/KTX2 textures upload back-to-back —
     * on Metal the per-texture commit overhead is otherwise significant. Calls must be paired;
     * nesting is not supported (Begin while already batching is a no-op-with-warning).
     */
    static void BeginUploadBatch() { get()._beginUploadBatch(); }
    /// @brief Ends an upload batch started with BeginUploadBatch(), flushing pending uploads.
    static void EndUploadBatch()   { get()._endUploadBatch(); }

    /// @brief Creates a texture from raw RGBA pixel data.
    /// @param size Texture dimensions.
    /// @param pixelData Pointer to tightly-packed RGBA8 pixels.
    /// @param fileName Name to register the resulting texture under.
    /// @return The created texture asset.
    static TextureAsset LoadFromPixelData(const vf2d& size, void *pixelData, std::string fileName) { return get()._loadFromPixelData(size, pixelData, std::move(fileName)); }

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
    static TextureAsset CreateEmptyTexture(const vf2d &size) { return get()._createEmptyTexture(size); }
    /// @brief Creates an empty texture with an explicit pixel format.
    /// @param size The size of the empty texture.
    /// @param format The GPU pixel format to allocate.
    /// @return The created empty texture.
    static TextureAsset CreateEmptyTexture(const vf2d &size, GpuTextureFormat format) { return get()._createEmptyTexture(size, format); }

    /**
     * @brief Retrieves a map of all loaded textures.
     *
     * @return The map of loaded textures.
     */
    static const std::unordered_map<std::string, TextureAsset>& GetTextures() { return get()._textures; }

    /**
     * @brief Retrieves a font asset with the specified filename and font size.
     *
     * @param fileName The filename of the font asset.
     * @param fontSize The size to generate the MSDF atlas at (recommended: use large size like 64-128 for best quality).
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
    static Music &GetMusic(const char *fileName) {
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

    /**
     * @brief Retrieves a shader by its file name.
     *
     * @param fileName The name of the texture file.
     * @return The texture corresponding to the file name.
     */
    static Shader GetShader(const char *fileName) {
        return get()._getShader(fileName);
    }

    /**
     * @brief Loads and caches a compute pipeline from a .comp GLSL file.
     *
     * The pipeline is compiled and reflected on first call; subsequent calls
     * return the cached asset by reference.
     *
     * @param fileName Path to the .comp shader file.
     * @return Reference to the cached ComputePipelineAsset.
     */
    static ComputePipelineAsset& GetComputePipeline(const char *fileName) {
        return get()._getComputePipeline(fileName);
    }

    /// @brief Creates a depth texture usable as a depth render target.
    /// @param width Target width in pixels.
    /// @param height Target height in pixels.
    /// @return The created depth-target texture.
    static TextureAsset CreateDepthTarget(uint32_t width, uint32_t height) {
        return get()._createDepthTarget(width, height);
    }

    /// @brief Creates a 1×1 opaque white texture, handy as a tint/solid-fill source.
    static TextureAsset CreateWhitePixel() {
        return get()._createWhitePixel();
    }

    /**
     * @brief Creates a simple cube model with vertices and indices.
     *
     * @param size The size of the cube (default 1.0).
     * @param layout UV layout pattern (default: Atlas4x4)
     * @return A ModelAsset containing a cube.
     */
    static ModelAsset CreateCube(float size = 1.0f, CubeUVLayout layout = CubeUVLayout::Atlas4x4) {
        return get()._createCube(size, layout);
    }

    /// @brief Releases an asset's GPU/CPU resources and drops it from the cache.
    /// @tparam T The asset type (deduced from the argument).
    /// @param asset The asset to delete.
    template<typename T>
    static void Delete(T &asset) {
        get()._delete(asset);
    }

    /// @brief Returns the map of currently loaded music assets, keyed by file name.
    static std::unordered_map<std::string, MusicAsset> &GetLoadedMusics() {
        return get()._musics;
    }

    /**
     * @brief Get the embedded DroidSansMono font data
     * @return Pointer to font data and length as a pair
     */
    static std::pair<const unsigned char*, size_t> GetEmbeddedFontData() {
        return {DroidSansMono_ttf, get().DroidSansMono_ttf_len};
    }

    /**
     * @brief Cleans up all loaded assets.
     * @note Called automatically on shutdown. You rarely need to call this manually.
     */
    static void Cleanup() { get()._cleanup(); }


private:
    // Textures

    Texture _getTexture(const std::string &fileName);

    TextureAsset _loadTexture(const std::string &fileName);

    TextureAsset _loadTextureFile(const std::string &path);     // uncached; dispatches KTX2/SDL_image
    TextureAsset _loadKtx2(const uint8_t *data, size_t size);   // transcode KTX2/Basis -> BC GPU texture

    TextureAsset _loadFromPixelData(const vf2d &size, void *pixelData, std::string fileName);

    bool _copy_to_texture(void *src_data, uint32_t src_data_len, GpuTextureHandle dst_texture,
                          uint32_t dst_texture_width, uint32_t dst_texture_height);

    // ── Upload batching (see Begin/EndUploadBatch) ────────────────────────────
    // While batching, texture uploads share m_batchCmd and defer transfer-buffer release until a
    // flush; otherwise each upload acquires + submits its own command buffer (original behavior).
    void               _beginUploadBatch();
    void               _endUploadBatch();
    GpuCmdBufferHandle _batchAcquire();                       // shared cmd while batching, else fresh
    void               _batchTrack(GpuTransferBufferHandle tb, uint32_t bytes);  // defer release + budget
    void               _batchFinishUpload(GpuCmdBufferHandle cmd);  // submit+release now, or defer to flush
    void               _batchFlush();                         // submit shared cmd + release tracked buffers

    bool                                 m_uploadBatching = false;
    GpuCmdBufferHandle                   m_batchCmd       = 0;
    std::vector<GpuTransferBufferHandle> m_batchStaging;
    size_t                               m_batchBytes     = 0;

    void _setDefaultTextureScaleMode(ScaleMode mode);

    ScaleMode _getDefaultTextureScaleMode();

    TextureAsset _createEmptyTexture(const vf2d &size);
    TextureAsset _createEmptyTexture(const vf2d &size, GpuTextureFormat format);

    void _saveTextureAsPNG(Texture texture, const char *fileName);

    TextureAsset _createDepthTarget(uint32_t width, uint32_t height);

    TextureAsset _createWhitePixel();

    // Models
    
    ModelAsset _createCube(float size, CubeUVLayout layout);

    // Fonts

    Font _getFont(const std::string &fileName, int fontSize);

    // Audio

    Sound _getSound(const std::string &fileName);

    Music _getMusic(const std::string &fileName);

    // Shaders

    Shader _getShader(const std::string &fileName);

    // Backend-specific helper: load a single shader from disk into a fresh ShaderAsset.
    // Implementations live in assethandler_shaders_sdl.cpp / assethandler_shaders_webgpu.cpp.
    ShaderAsset _loadShaderFromDisk(const std::string &fileName);

    // Compute pipelines

    ComputePipelineAsset& _getComputePipeline(const std::string &fileName);

    //Containers

    std::unordered_map<std::string, FontAsset>             _fonts;
    std::unordered_map<std::string, MusicAsset>            _musics;
    std::unordered_map<std::string, ShaderAsset>           _shaders;
    std::unordered_map<std::string, SoundAsset>            _sounds;
    std::unordered_map<std::string, TextureAsset>          _textures;
    std::unordered_map<std::string, ComputePipelineAsset>  _computePipelines;

    ScaleMode defaultMode = ScaleMode::Nearest;

    // Thread safety
    std::mutex assetMutex;

    // Font cache
    ResourcePack* _fontCache = nullptr;
    void _initFontCache();
    void _saveFontCache();
    bool _loadFontFromCache(const std::string& fileName, int fontSize, FontAsset& outFont, const std::string& precomputedHash = "");
    void _saveFontToCache(const std::string& fileName, const FontAsset& font, const std::vector<unsigned char>& rgbaData, const std::string& precomputedHash = "");
    std::string _computeFontCacheKey(const std::string& fileName);
    std::string _computeFontCacheKeyFromData(const void* data, size_t size);
#if defined(LUMINOVEAU_HAVE_FONT_ATLAS_BLOB)
    // Load the default font from the baked atlas blob (tools/font_baker) instead of generating it.
    bool _loadDefaultFontFromBlob(FontAsset& font);
#endif

    // Cleanup
    void _cleanup();


    // default font asset Droid Sans Mono.ttf
    static const unsigned char DroidSansMono_ttf[];
    unsigned int               DroidSansMono_ttf_len = 119380;

    FontAsset defaultFont;

    template<typename T>
    void _delete(T &asset) {

        if constexpr (std::is_same_v<T, FontAsset>) {
            std::cout << "Deleting Font..." << std::endl;
            auto it = std::find_if(_fonts.begin(), _fonts.end(), [&](const auto &pair) {
                return &pair.second == &asset;
            });

            if (it != _fonts.end()) {
                // Cleanup MSDF resources
                if (asset.glyphs) {
                    delete asset.glyphs;
                    asset.glyphs = nullptr;
                }
                if (asset.glyphMap) {
                    delete asset.glyphMap;
                    asset.glyphMap = nullptr;
                }
                if (asset.atlasTexture) {
                    Renderer::GetGpu().releaseTexture(asset.atlasTexture);
                    asset.atlasTexture = 0;
                }
                if (asset.fontHandle) {
                    msdfgen::destroyFont(asset.fontHandle);
                    asset.fontHandle = nullptr;
                }
                if (asset.fontData) {
                    free(asset.fontData);
                    asset.fontData = nullptr;
                }
                _fonts.erase(it);
            } else {
                LOG_CRITICAL("font not found in the map");
            }
        } else if constexpr (std::is_same_v<T, MusicAsset>) {
            std::cout << "Deleting Music..." << std::endl;
            auto it = std::find_if(_musics.begin(), _musics.end(), [&](const auto &pair) {
                return &pair.second == &asset;
            });

            if (it != _musics.end()) {
                ma_sound_uninit(static_cast<MusicAsset>(asset).music);
                _musics.erase(it);
            } else {
                LOG_CRITICAL("MusicAsset not found in the map");
            }
        } else if constexpr (std::is_same_v<T, SoundAsset>) {
            std::cout << "Deleting Sound..." << std::endl;
            auto it = std::find_if(_sounds.begin(), _sounds.end(), [&](const auto &pair) {
                return &pair.second == &asset;
            });

            if (it != _sounds.end()) {
                ma_sound_uninit(static_cast<SoundAsset>(asset).sound);
                _sounds.erase(it);
            } else {
                LOG_CRITICAL("sound not found in the map");
            }
        } else if constexpr (std::is_same_v<T, TextureAsset>) {
            std::cout << "Deleting Texture..." << std::endl;
            auto it = std::find_if(_textures.begin(), _textures.end(), [&](const auto &pair) {
                return &pair.second == &asset;
            });

            if (it != _textures.end()) {
                if (asset.gpuTexture) {
                    Renderer::GetGpu().releaseTexture(asset.gpuTexture);
                    asset.gpuTexture = 0;
                }
                _textures.erase(it);
            } else {
                LOG_CRITICAL("texture not found in the map");
            }
        } else {
            LOG_CRITICAL("trying to delete invalid asset");
        }
    };

public:
    /// @cond INTERNAL
    AssetHandler(const AssetHandler &) = delete;

    ~AssetHandler() {
        _cleanup();
    }

    static AssetHandler &get() {
        static AssetHandler instance;
        return instance;
    }
    /// @endcond

private:
    AssetHandler();
};

//*/
