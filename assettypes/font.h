#pragma once

#include <vector>
#include <unordered_map>

#include "SDL3/SDL.h"

// Forward declarations for MSDF
namespace msdfgen {
    class FontHandle;
}

namespace msdf_atlas {
    class GlyphGeometry;
}

/**
 * @brief Represents a font asset for rendering text using MSDF.
 */
struct FontAsset {
    msdfgen::FontHandle *fontHandle = nullptr;
    std::vector<msdf_atlas::GlyphGeometry> *glyphs = nullptr;
    SDL_GPUTexture *atlasTexture = nullptr;
    int atlasWidth = 0;
    int atlasHeight = 0;
    std::unordered_map<uint32_t, size_t> *glyphMap = nullptr;  // codepoint -> glyph index
    
    void *fontData = nullptr;  // For cleanup
    int generatedSize = 0;
    int defaultRenderSize = -1;
    
    // Font metrics (in em-square units, multiply by generatedSize to get pixels)
    double ascender = 0.0;    // Distance from baseline to top of tallest glyph
    double descender = 0.0;   // Distance from baseline to bottom of lowest glyph (negative)
    double lineHeight = 0.0;  // Recommended line spacing
};

using Font = FontAsset &;
