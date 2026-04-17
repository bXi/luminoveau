#pragma once

#include <string>
#include <cstdint>
#include <SDL3/SDL.h>

struct ComputePipelineAsset {
    SDL_GPUComputePipeline* pipeline = nullptr;
    std::string filename;

    // Thread group dimensions declared in the shader
    uint32_t threadcount_x = 1;
    uint32_t threadcount_y = 1;
    uint32_t threadcount_z = 1;

    // Resource binding counts (from shader reflection)
    uint32_t num_samplers = 0;
    uint32_t num_readonly_storage_textures  = 0;
    uint32_t num_readwrite_storage_textures = 0;
    uint32_t num_readonly_storage_buffers   = 0;
    uint32_t num_readwrite_storage_buffers  = 0;
    uint32_t num_uniform_buffers = 0;
};
