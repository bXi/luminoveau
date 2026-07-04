#pragma once

#include "gpu/types.h"
#include "math/vectors.h"

/**
 * @brief Represents a loaded texture asset.
 * GPU handles are opaque — the active backend owns the memory behind them.
 */
struct TextureAsset {
    int         width    = -1;         ///< Texture width in pixels (-1 if unloaded).
    int         height   = -1;         ///< Texture height in pixels (-1 if unloaded).
    const char* filename = nullptr;    ///< Source file the texture was loaded from, if any.

    GpuTextureHandle gpuTexture = 0;   ///< Opaque backend handle to the GPU texture.
    GpuSamplerHandle gpuSampler = 0;   ///< Opaque backend handle to the sampler.

    /// @brief Returns the texture size as a (width, height) vector.
    vi2d getSize() const { return {width, height}; }
};

using Texture = TextureAsset&;
