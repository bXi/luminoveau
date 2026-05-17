#pragma once

#include <string>
#include <cstdint>

#include "gpu/types.h"

/**
 * @brief Represents a compute pipeline asset.
 * The pipeline handle is opaque — the active backend owns the native object.
 */
struct ComputePipelineAsset {
    GpuComputePipelineHandle pipeline = 0;
    std::string filename;

    uint32_t threadcount_x = 1;
    uint32_t threadcount_y = 1;
    uint32_t threadcount_z = 1;

    uint32_t num_samplers                    = 0;
    uint32_t num_readonly_storage_textures   = 0;
    uint32_t num_readwrite_storage_textures  = 0;
    uint32_t num_readonly_storage_buffers    = 0;
    uint32_t num_readwrite_storage_buffers   = 0;
    uint32_t num_uniform_buffers             = 0;
};
