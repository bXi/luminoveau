#include "texthandler.h"
#include "draw/drawhandler.h"
#include <algorithm>

// Helper function to decode UTF-8 character
static uint32_t decodeUTF8(const std::string& str, size_t& pos) {
    unsigned char c = str[pos++];
    if (c < 0x80) return c;
    
    uint32_t codepoint = 0;
    int bytes = 0;
    
    if ((c & 0xE0) == 0xC0) { codepoint = c & 0x1F; bytes = 1; }
    else if ((c & 0xF0) == 0xE0) { codepoint = c & 0x0F; bytes = 2; }
    else if ((c & 0xF8) == 0xF0) { codepoint = c & 0x07; bytes = 3; }
    else return '?';  // Invalid
    
    for (int i = 0; i < bytes && pos < str.length(); ++i) {
        codepoint = (codepoint << 6) | (str[pos++] & 0x3F);
    }
    
    return codepoint;
}

void Text::_drawText(Font font, const vf2d &pos, const std::string &textToDraw, Color color, float renderSize) {

    vf2d newPos = pos;

    if (Camera::IsActive()) {
        newPos = Camera::ToScreenSpace(pos);
    }

    if (textToDraw.empty()) return;
    if (std::all_of(textToDraw.begin(), textToDraw.end(), isspace)) return;

    // Determine scale factor for MSDF rendering
    float scale = 1.0f;
    if (renderSize < 0.0f) {
        if (font.defaultRenderSize > 0 && font.generatedSize > 0) {
            scale = static_cast<float>(font.defaultRenderSize) / static_cast<float>(font.generatedSize);
        }
    } else if (font.generatedSize > 0) {
        scale = renderSize / static_cast<float>(font.generatedSize);
    }

    // Use proper ascender from font metrics
    double ascender_px = font.ascender * font.generatedSize;
    
    // Convert top-left -> baseline
    newPos.y += static_cast<float>(ascender_px * scale);

    // MSDF rendering - iterate through UTF-8 string
    float cursorX = 0.0f;

    for (size_t i = 0; i < textToDraw.length(); ) {
        uint32_t codepoint = decodeUTF8(textToDraw, i);

        // Look up glyph
        auto it = font.glyphMap->find(codepoint);
        if (it == font.glyphMap->end()) continue;

        const CachedGlyph &glyph = (*font.glyphs)[it->second];

        double advance = glyph.advance;

        // Plane bounds (em-square coordinates)
        double pl = glyph.pl, pb = glyph.pb, pr = glyph.pr, pt = glyph.pt;

        // Atlas bounds with 0.5px inset to avoid sampling neighbour
        double al = glyph.al + 0.5;
        double ab = glyph.ab + 0.5;
        double ar = glyph.ar - 0.5;
        double at = glyph.at - 0.5;

        // Scale plane bounds from em-square to pixel space
        pl *= font.generatedSize;
        pb *= font.generatedSize;
        pr *= font.generatedSize;
        pt *= font.generatedSize;
        advance *= font.generatedSize;

        // Create renderable quad
        Renderable ren = {
            .texture = {
                .gpuTexture = font.atlasTexture,
                .gpuSampler = Renderer::GetSampler(ScaleMode::LINEAR),
            },
            .geometry = Renderer::GetQuadGeometry(),

            .x = static_cast<float>(newPos.x + (cursorX + pl) * scale),
            .y = static_cast<float>(newPos.y - pt * scale),
            .z = (float)Renderer::GetZIndex(),

            .rotation = 0.f,

            .tex_u = static_cast<float>(al / font.atlasWidth),
            .tex_v = static_cast<float>(1.0f - (at / font.atlasHeight)),
            .tex_w = static_cast<float>((ar - al) / font.atlasWidth),
            .tex_h = static_cast<float>((at - ab) / font.atlasHeight),

            .r = color.r / 255.f,
            .g = color.g / 255.f,
            .b = color.b / 255.f,
            .a = color.a / 255.f,

            .w = static_cast<float>((pr - pl) * scale),
            .h = static_cast<float>((pt - pb) * scale),

            .pivot_x = 0.5f,
            .pivot_y = 0.5f,
            .isSDF = true,
        };

        Renderer::AddToRenderQueue(Draw::GetTargetRenderPass(), ren);

        cursorX += advance;
    }
}


int Text::_measureText(Font font, std::string textToDraw, float renderSize) {
    return (int) (_getRenderedTextSize(font, textToDraw, renderSize).x + 0.1f);
}

vf2d Text::_getRenderedTextSize(Font font, const std::string &textToDraw, float renderSize) {
    if (textToDraw.empty()) return {0, 0};

    float scale = 1.0f;
    if (renderSize < 0.0f) {
        if (font.defaultRenderSize > 0 && font.generatedSize > 0) {
            scale = static_cast<float>(font.defaultRenderSize) / static_cast<float>(font.generatedSize);
        }
    } else if (font.generatedSize > 0) {
        scale = renderSize / static_cast<float>(font.generatedSize);
    }

    double ascender_px = font.ascender * font.generatedSize;

    float cursorX = 0.0f;
    float maxRight = 0.0f;

    for (size_t i = 0; i < textToDraw.length(); ) {
        uint32_t codepoint = decodeUTF8(textToDraw, i);

        auto it = font.glyphMap->find(codepoint);
        if (it == font.glyphMap->end()) continue;

        const CachedGlyph &glyph = (*font.glyphs)[it->second];

        double advance = glyph.advance;
        double pr = glyph.pr;
        
        pr *= font.generatedSize;
        advance *= font.generatedSize;
        
        float glyphRight = static_cast<float>((cursorX + pr) * scale);
        maxRight = std::max(maxRight, glyphRight);

        cursorX += advance;
    }
    
    float finalWidth = std::max(maxRight, cursorX * scale);

    return {finalWidth, static_cast<float>(ascender_px * scale)};
}

TextureAsset Text::_drawTextToTexture(Font font, std::string textToDraw, Color color) {
    LUMI_UNUSED(color);
    if (textToDraw.empty()) {
        textToDraw = " ";
    };

    auto size = _getRenderedTextSize(font, textToDraw);

    TextureAsset tex;
    tex.width  = size.x;
    tex.height = size.y;

    return tex;
}

void Text::_drawWrappedText(Font font, vf2d pos, std::string textToDraw, float maxWidth, Color color, float renderSize) {
    if (textToDraw.empty() || maxWidth <= 0) return;

    float scale = 1.0f;
    if (renderSize < 0.0f) {
        if (font.defaultRenderSize > 0 && font.generatedSize > 0) {
            scale = static_cast<float>(font.defaultRenderSize) / static_cast<float>(font.generatedSize);
        }
    } else if (font.generatedSize > 0) {
        scale = renderSize / static_cast<float>(font.generatedSize);
    }

    float lineHeight = static_cast<float>(font.lineHeight * font.generatedSize * scale);

    std::stringstream ss(textToDraw);
    std::string word, currentLine;

    while (ss >> word) {
        std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
        float testLineWidth = GetRenderedTextSize(font, testLine, renderSize).x;

        if (testLineWidth <= maxWidth) {
            currentLine = testLine;
        } else {
            if (!currentLine.empty()) {
                DrawText(font, pos, currentLine, color, renderSize);
                pos.y += lineHeight;
            }
            currentLine = word;
        }
    }

    if (!currentLine.empty()) {
        DrawText(font, pos, currentLine, color, renderSize);
    }
}
