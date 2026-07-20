// Cross-backend public API for the engine's shader subsystem.
//
// Shader/pipeline creation goes through IGpu (createShaderFromSPIRV /
// createComputePipelineFromSPIRV), so nothing here is backend-specific. The SPIRV
// toolchain itself (glslang transpile, spirv_cross reflection, SDL_shadercross) is a
// private implementation detail of renderer/shaders.cpp; the WebGPU build supplies its
// own definitions for the lifecycle hooks in renderer/webgpu/shaders.cpp.

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "gpu/types.h"
#include "assets/shader/shader.h"
#include "assets/compute/computepipeline.h"
#include "file/filehandler.h" // PhysFSFileData
#include "file/resourcepack.h"

/**
 * @brief Resource layout reflected out of a shader, plus the cache bookkeeping used to
 *        decide whether an on-disk compile is still valid.
 */
struct ShaderMetadata {
    std::string                             source_hash;     ///< Hash of the source the blob was built from.
    std::vector<std::string>                sampler_names;   ///< Reflected sampler binding names.
    std::unordered_map<std::string, size_t> uniform_offsets; ///< Reflected uniform byte offsets.
    std::unordered_map<std::string, size_t> uniform_sizes;   ///< Reflected uniform byte sizes.

    uint32_t num_samplers         = 0; ///< Number of sampler bindings.
    uint32_t num_uniform_buffers  = 0; ///< Number of uniform buffer bindings.
    uint32_t num_storage_buffers  = 0; ///< Number of storage buffer bindings.
    uint32_t num_storage_textures = 0; ///< Number of storage texture bindings.

    /// Backend-native bytecode format tag the blob was compiled for. Opaque here; on the
    /// SDL backend it holds an SDL_GPUShaderFormat value.
    uint32_t shader_format = 0;

    /// @brief Serializes the metadata to the on-disk cache format.
    std::string serialize() const;
    /// @brief Parses metadata back out of the on-disk cache format.
    static ShaderMetadata deserialize(const std::string &data);
};

/// @brief Shader subsystem: lifecycle, entry-point queries and asset-shader creation.
class Shaders {
public:
    /// @brief Engine startup hook. SDL wires up SDL_shadercross + the on-disk shader cache;
    ///        WebGPU is a no-op (WGSL is compiled by the browser at module-create time).
    static void Init() { get()._init(); }

    /// @brief Engine shutdown hook. SDL persists the shader cache and tears down SDL_shadercross.
    static void Quit() { get()._quit(); }

    /// @brief Returns the entry-point name for the built-in vertex shaders on the active backend.
    static const char *GetVertexEntryPoint() { return get()._getVertexEntryPoint(); }
    /// @brief Returns the entry-point name for the built-in fragment shaders on the active backend.
    static const char *GetFragmentEntryPoint() { return get()._getFragmentEntryPoint(); }
    /// @brief Returns the entry-point name for the built-in compute shaders on the active backend.
    static const char *GetComputeEntryPoint() { return get()._getComputeEntryPoint(); }

    /**
     * @brief Loads an asset shader and creates its GPU shader via the active backend.
     * @param filename Shader path inside the resource pack.
     * @param stage Which pipeline stage the shader is for.
     * @return The shader asset, with an opaque backend handle.
     */
    static ShaderAsset CreateShaderAsset(const std::string &filename, GpuShaderStage stage) { return get()._createShaderAsset(filename, stage); }

    /**
     * @brief Loads a compute shader and creates its pipeline via the active backend.
     * @param filename Compute shader path inside the resource pack.
     * @return The compute pipeline asset, with its reflected thread counts filled in.
     */
    static ComputePipelineAsset CreateComputePipeline(const std::string &filename) { return get()._createComputePipeline(filename); }

    /**
     * @brief Creates a compute pipeline from SPIRV already held in memory.
     * @param spirvBytes SPIRV bytecode.
     * @param spirvSize Size of the bytecode in bytes.
     * @return The compute pipeline asset, with its reflected thread counts filled in.
     */
    static ComputePipelineAsset CreateComputePipelineFromBytes(const uint8_t *spirvBytes, size_t spirvSize) { return get()._createComputePipelineFromBytes(spirvBytes, spirvSize); }

    /**
     * @brief Returns the reflected metadata for a shader, compiling/caching as needed.
     * @param filename Shader path inside the resource pack.
     */
    static ShaderMetadata GetShaderMetadata(const std::string &filename) { return get()._getShaderMetadata(filename); }

private:
    void        _init();
    void        _quit();
    const char *_getVertexEntryPoint();
    const char *_getFragmentEntryPoint();
    const char *_getComputeEntryPoint();

    ShaderAsset          _createShaderAsset(const std::string &filename, GpuShaderStage stage);
    ComputePipelineAsset _createComputePipeline(const std::string &filename);
    ComputePipelineAsset _createComputePipelineFromBytes(const uint8_t *spirvBytes, size_t spirvSize);

    GpuShaderHandle _createGpuShader(const std::string &filename, GpuShaderStage stage);
    ShaderMetadata  _getShaderMetadata(const std::string &filename);
    PhysFSFileData  _getShader(const std::string &filename);
    uint32_t        _getShaderFormat(const std::string &filename);

    // On-disk shader cache (backed by _shaderCache).
    bool _loadCachedShader(const std::string &cacheKey, std::vector<uint8_t> &outData);
    bool _loadCachedMetadata(const std::string &metadataKey, ShaderMetadata &outMetadata);
    void _saveCachedShader(const std::string &cacheKey, const std::vector<uint8_t> &data);
    void _saveCachedMetadata(const std::string &metadataKey, const ShaderMetadata &metadata);

    std::unordered_map<std::string, ShaderMetadata> _metadataCache;
    std::unordered_map<std::string, PhysFSFileData> _shaderDataCache;
    ResourcePack                                   *_shaderCache = nullptr;

public:
    /// @cond INTERNAL
    Shaders(const Shaders &) = delete;

    static Shaders &get() {
        static Shaders instance;
        return instance;
    }
    /// @endcond

private:
    Shaders() = default;
};
