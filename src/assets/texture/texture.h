#pragma once

#include "gpu/types.h"
#include "math/vectors.h"

/**
 * @brief Represents a loaded texture asset.
 * GPU handles are opaque — the active backend owns the memory behind them.
 */
struct TextureAsset {
    int         width    = -1;
    int         height   = -1;
    const char* filename = nullptr;

    GpuTextureHandle gpuTexture = 0;
    GpuSamplerHandle gpuSampler = 0;

    vi2d getSize() const { return {width, height}; }
};

using Texture = TextureAsset&;
