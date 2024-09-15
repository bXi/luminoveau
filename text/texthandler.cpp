#include "texthandler.h"

#include "luminoveau.h"

#include <utility>

void Text::_drawText(Font font, vf2d pos, std::string textToDraw, Color color) {

    if (textToDraw.empty()) return;

    auto size = _getRenderedTextSize(font, textToDraw);

    BLImage _blSurface;
    _blSurface.create(std::ceil(size.x), std::ceil(size.y), BL_FORMAT_PRGB32);

    BLContext ctx(_blSurface);

    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0x00000000u));
    ctx.fillAll();

    ctx.setFillStyle(BLRgba32(color.r, color.g, color.b, color.a));
    ctx.fillUtf8Text(BLPoint(0, _blSurface.height() * 0.75), *font.font, textToDraw.c_str());

    ctx.end();

    Render2D::DrawBlend2DImage(_blSurface, pos, {(float)_blSurface.width(), (float)_blSurface.height()});
}

int Text::_measureText(Font font, std::string textToDraw) {
    return (int)(_getRenderedTextSize(font, textToDraw).x + 0.1f);
}

vf2d Text::_getRenderedTextSize(Font font, std::string textToDraw) {

    if (textToDraw.empty()) return {0, 0};

    BLTextMetrics textMetrics;
    BLGlyphBuffer glyphBuffer;
    BLFontMetrics fm = font.font->metrics();

    glyphBuffer.setUtf8Text(textToDraw.c_str());
    font.font->shape(glyphBuffer);
    font.font->getTextMetrics(glyphBuffer, textMetrics);

    double width = textMetrics.advance.x;
    double height = fm.xHeight * 2.0;

    return {(float)width, (float)height};
}


TextureAsset Text::_drawTextToTexture(Font font, std::string textToDraw, Color color) {
    if (textToDraw.empty()) {
        // In this case we return space so we still render something for when the user uses
        // the height of the returned texture to position multiple lines of text.
        textToDraw = " ";
    };

    auto size = _getRenderedTextSize(font, textToDraw);

    BLImage _blSurface;
    _blSurface.create(std::ceil(size.x), std::ceil(size.y), BL_FORMAT_PRGB32);

    BLContext ctx(_blSurface);

    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0x00000000u));
    ctx.fillAll();

    ctx.setFillStyle(BLRgba32(color.r, color.g, color.b, color.a));
    ctx.fillUtf8Text(BLPoint(0, _blSurface.height() * 0.75), *font.font, textToDraw.c_str());

    ctx.end();

    BLImageData data;
    _blSurface.getData(&data);

    TextureAsset tex;

    SDL_Texture *textTexture = SDL_CreateTexture(Window::GetRenderer(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, _blSurface.width(), _blSurface.height());
    SDL_SetTextureBlendMode(textTexture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);

    SDL_UpdateTexture(textTexture, nullptr, data.pixelData, int(data.stride));

    tex.width = _blSurface.width();
    tex.height = _blSurface.height();

    tex.texture = textTexture;

    return tex;
}

void Text::_drawWrappedText(Font font, vf2d pos, std::string textToDraw, float maxWidth, Color color) {

    std::stringstream ss(textToDraw);
    std::string word;
    std::string currentLine;

    while (ss >> word) {
        std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
        float testLineWidth = MeasureText(font, testLine);

        if (testLineWidth <= maxWidth) {
            currentLine = testLine;
        } else {
            DrawText(font, pos, currentLine, color);
            pos.y += GetRenderedTextSize(font, currentLine).y;
            currentLine = word;
        }
    }

    if (!currentLine.empty()) {
        DrawText(font, pos, currentLine, color);
    }
}
