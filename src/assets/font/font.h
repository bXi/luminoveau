#pragma once

#include <vector>
#include <unordered_map>

#include "gpu/types.h"

/// @cond INTERNAL
/**
 * @brief Lightweight cached glyph data — stores only what's needed for rendering.
 * Replaces msdf_atlas::GlyphGeometry in the hot path.
 */
struct CachedGlyph {
    uint32_t codepoint = 0;
    double advance = 0.0;

    // Plane bounds (em-square coordinates)
    double pl = 0.0, pb = 0.0, pr = 0.0, pt = 0.0;

    // Atlas bounds (pixel coordinates in atlas)
    double al = 0.0, ab = 0.0, ar = 0.0, at = 0.0;
};
/// @endcond

/**
 * @brief Represents a font asset for rendering text using MSDF.
 */
// Forward declaration for cleanup
namespace msdfgen {
    class FontHandle;
}

/// @brief A loaded font with its MSDF glyph atlas and metrics.
struct FontAsset {
    msdfgen::FontHandle *fontHandle = nullptr;  ///< Underlying msdfgen font handle.
    GpuTextureHandle atlasTexture = 0;          ///< GPU texture holding the MSDF glyph atlas.
    int atlasWidth = 0;                         ///< Atlas texture width in pixels.
    int atlasHeight = 0;                        ///< Atlas texture height in pixels.

    std::vector<CachedGlyph> *glyphs = nullptr; ///< Cached per-glyph atlas/metric data.
    std::unordered_map<uint32_t, size_t> *glyphMap = nullptr;  ///< Maps codepoint to glyph index.

    void *fontData = nullptr;                   ///< Owned font file bytes (kept for cleanup).
    int generatedSize = 0;                      ///< Pixel size the atlas was generated at.
    int defaultRenderSize = -1;                 ///< Default render size in pixels (-1 = use generatedSize).

    double ascender = 0.0;                       ///< Ascender height in em-square units (× generatedSize for pixels).
    double descender = 0.0;                      ///< Descender depth in em-square units.
    double lineHeight = 0.0;                     ///< Line height in em-square units.
};

using Font = FontAsset &;
