#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include "gpu/types.h"

/**
 * @brief Represents a compiled shader asset.
 * The gpuShader handle is opaque — the active backend owns the native object.
 */
struct ShaderAsset {
    GpuShaderHandle gpuShader = 0;     ///< Opaque backend handle to the compiled shader.

    std::string shaderFilename;        ///< Source file the shader was loaded from.

    uint32_t samplerCount        = 0;  ///< Number of sampler bindings the shader declares.
    uint32_t uniformBufferCount  = 0;  ///< Number of uniform buffer bindings.
    uint32_t storageBufferCount  = 0;  ///< Number of storage buffer bindings.
    uint32_t storageTextureCount = 0;  ///< Number of storage texture bindings.

    std::vector<uint8_t> fileData;     ///< Raw shader bytecode/source bytes.

    std::unordered_map<std::string, std::string> frameBufferToSamplerMapping; ///< Maps framebuffer names to sampler slots.

    /// Inline uniform byte offsets by name (WebGPU; SDL uses the Shaders metadata cache).
    std::unordered_map<std::string, size_t> uniformOffsets;
    /// Inline uniform byte sizes by name (WebGPU; SDL uses the Shaders metadata cache).
    std::unordered_map<std::string, size_t> uniformSizes;
};

using Shader = ShaderAsset&;
