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
    GpuShaderHandle gpuShader = 0;

    std::string shaderFilename;

    uint32_t samplerCount        = 0;
    uint32_t uniformBufferCount  = 0;
    uint32_t storageBufferCount  = 0;
    uint32_t storageTextureCount = 0;

    std::vector<uint8_t> fileData;

    std::unordered_map<std::string, std::string> frameBufferToSamplerMapping;
};

using Shader = ShaderAsset&;
