#pragma once

#include <string>
#include <cstdint>

#include "gpu/types.h"

/**
 * @brief Represents a compute pipeline asset.
 * The pipeline handle is opaque — the active backend owns the native object.
 */
struct ComputePipelineAsset {
    GpuComputePipelineHandle pipeline = 0; ///< Opaque backend handle to the native pipeline object.
    std::string              filename;     ///< Source shader path the pipeline was loaded from.

    uint32_t threadCountX = 1; ///< Workgroup size in X, as declared in the shader.
    uint32_t threadCountY = 1; ///< Workgroup size in Y, as declared in the shader.
    uint32_t threadCountZ = 1; ///< Workgroup size in Z, as declared in the shader.

    uint32_t numSamplers                 = 0; ///< Number of sampler bindings the shader declares.
    uint32_t numReadonlyStorageTextures  = 0; ///< Number of read-only storage texture bindings.
    uint32_t numReadwriteStorageTextures = 0; ///< Number of read-write storage texture bindings.
    uint32_t numReadonlyStorageBuffers   = 0; ///< Number of read-only storage buffer bindings.
    uint32_t numReadwriteStorageBuffers  = 0; ///< Number of read-write storage buffer bindings.
    uint32_t numUniformBuffers           = 0; ///< Number of uniform buffer bindings.
};
