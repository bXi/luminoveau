#pragma once

#include <string>
#include <cstdint>

#include "gpu/types.h"

/**
 * @brief Represents a compute pipeline asset.
 * The pipeline handle is opaque — the active backend owns the native object.
 */
struct ComputePipelineAsset {
    GpuComputePipelineHandle pipeline = 0;  ///< Opaque backend handle to the native pipeline object.
    std::string filename;                   ///< Source shader path the pipeline was loaded from.

    uint32_t threadcount_x = 1;             ///< Workgroup size in X, as declared in the shader.
    uint32_t threadcount_y = 1;             ///< Workgroup size in Y, as declared in the shader.
    uint32_t threadcount_z = 1;             ///< Workgroup size in Z, as declared in the shader.

    uint32_t num_samplers                    = 0;  ///< Number of sampler bindings the shader declares.
    uint32_t num_readonly_storage_textures   = 0;  ///< Number of read-only storage texture bindings.
    uint32_t num_readwrite_storage_textures  = 0;  ///< Number of read-write storage texture bindings.
    uint32_t num_readonly_storage_buffers    = 0;  ///< Number of read-only storage buffer bindings.
    uint32_t num_readwrite_storage_buffers   = 0;  ///< Number of read-write storage buffer bindings.
    uint32_t num_uniform_buffers             = 0;  ///< Number of uniform buffer bindings.
};
