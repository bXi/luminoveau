#pragma once

#include <vector>
#include <unordered_map>

#include "SDL3/SDL.h"

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

/**
 * @brief Represents a font asset for rendering text using MSDF.
 */
// Forward declaration for cleanup
namespace msdfgen {
    class FontHandle;
}

struct FontAsset {
    msdfgen::FontHandle *fontHandle = nullptr;
    SDL_GPUTexture *atlasTexture = nullptr;
    int atlasWidth = 0;
    int atlasHeight = 0;
    
    std::vector<CachedGlyph> *glyphs = nullptr;
    std::unordered_map<uint32_t, size_t> *glyphMap = nullptr;  // codepoint -> glyph index
    
    void *fontData = nullptr;  // For cleanup
    int generatedSize = 0;
    int defaultRenderSize = -1;
    
    // Font metrics (in em-square units, multiply by generatedSize to get pixels)
    double ascender = 0.0;
    double descender = 0.0;
    double lineHeight = 0.0;
};

using Font = FontAsset &;
