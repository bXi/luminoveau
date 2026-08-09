#pragma once

#include "gpu/types.h"
#include "math/vectors.h"

/**
 * @brief Represents a loaded texture asset.
 * GPU handles are opaque — the active backend owns the memory behind them.
 */
struct TextureAsset {
    int         width    = -1;      ///< Texture width in pixels (-1 if unloaded).
    int         height   = -1;      ///< Texture height in pixels (-1 if unloaded).
    const char *filename = nullptr; ///< Source file the texture was loaded from, if any.

    GpuTextureHandle gpuTexture = 0; ///< Opaque backend handle to the GPU texture.
    GpuSamplerHandle gpuSampler = 0; ///< Opaque backend handle to the sampler.

    /// Sprite's sub-rect within its (possibly shared atlas page) GPU texture; default (0,0,1,1) =
    /// whole texture. width/height stay the sprite's own pixels, so sprite-local UV math is unchanged.
    float atlasU = 0.0f, atlasV = 0.0f;   ///< Sub-rect origin in page UV space.
    float atlasSU = 1.0f, atlasSV = 1.0f; ///< Sub-rect size in page UV space.

    /// True when backed by a shared atlas page (sub-rect != full).
    bool IsAtlased() const { return atlasU != 0.0f || atlasV != 0.0f || atlasSU != 1.0f || atlasSV != 1.0f; }

    /// Maps a sprite-local UV [0,1] into page UV space (identity for non-atlas textures).
    void MapUV(float localU, float localV, float &outU, float &outV) const {
        outU = atlasU + localU * atlasSU;
        outV = atlasV + localV * atlasSV;
    }

    /// @brief Returns the texture size as a (width, height) vector.
    vi2d GetSize() const { return { width, height }; }
};

using Texture = TextureAsset &;
