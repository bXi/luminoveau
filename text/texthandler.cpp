#include "texthandler.h"

#include <algorithm>

void Text::_drawText(Font font, const vf2d &pos, const std::string &textToDraw, Color color) {

    if (textToDraw.empty()) return;
    if (std::all_of(textToDraw.begin(), textToDraw.end(), isspace)) return;

    TTF_Text *text = TTF_CreateText(font.textEngine, font.ttfFont, textToDraw.c_str(), textToDraw.length());

    TTF_SetTextWrapWidth(text, 0);

    TTF_GPUAtlasDrawSequence *sequence = TTF_GetGPUTextDrawData(text);

    if (!sequence) {
        throw std::runtime_error(Helpers::TextFormat("%s: failed to create a font sequence: %s", CURRENT_METHOD(), SDL_GetError()));
    }

    do {
        TextureAsset tex = {
            .gpuTexture = sequence->atlas_texture,
            .gpuSampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode()),
        };

        for (int i = 0; i + 5 < sequence->num_indices; i += 6) {

            auto v1  = sequence->xy[sequence->indices[i + 1]];
            auto v5  = sequence->xy[sequence->indices[i + 5]];
            //
            auto uv2 = sequence->uv[sequence->indices[i + 2]];
            auto uv3 = sequence->uv[sequence->indices[i + 3]];

            float minX = v5.x;
            float minY = v5.y;
            float maxX = v1.x;
            float maxY = v1.y;

            float uvMinX = uv3.x;
            float uvMinY = uv3.y;
            float uvMaxX = uv2.x;
            float uvMaxY = uv2.y;

            // Set up the Renderable object with UVs
            Renderable ren = {
                .texture = tex,

                .x = minX + pos.x,
                .y = pos.y + (0 - maxY),
                .z = (float) Renderer::GetZIndex(),

                .rotation = 0.f,

                .tex_u = uvMinX,
                .tex_v = uvMinY,
                .tex_w = uvMaxX - uvMinX,
                .tex_h = uvMaxY - uvMinY,

                .r = (float) color.r / 255.f,
                .g = (float) color.g / 255.f,
                .b = (float) color.b / 255.f,
                .a = (float) color.a / 255.f,

                .w        = maxX - minX,
                .h        = maxY - minY,
            };

            // Add to the render queue
            Renderer::AddToRenderQueue("2dsprites", ren);
        }

        sequence = sequence->next;
    } while (sequence);

    TTF_DestroyText(text);
}

int Text::_measureText(Font font, std::string textToDraw) {
    return (int) (_getRenderedTextSize(font, textToDraw).x + 0.1f);
}

vf2d Text::_getRenderedTextSize(Font font, const std::string &textToDraw) {

    if (textToDraw.empty()) return {0, 0};

    TTF_Text *text = TTF_CreateText(font.textEngine, font.ttfFont, textToDraw.c_str(), textToDraw.length());

    int tw, th;
    TTF_GetTextSize(text, &tw, &th);

    auto width  = (float) tw;
    auto height = (float) th;

    TTF_DestroyText(text);

    return {width, height};
}

TextureAsset Text::_drawTextToTexture(Font font, std::string textToDraw, Color color) {
    if (textToDraw.empty()) {
        // In this case we return space so we still render something for when the user uses
        // the height of the returned texture to position multiple lines of text.
        textToDraw = " ";
    };

    auto size = _getRenderedTextSize(font, textToDraw);

    TextureAsset tex;

    tex.width  = size.x;
    tex.height = size.y;

    return tex;
}

void Text::_drawWrappedText(Font font, vf2d pos, std::string textToDraw, float maxWidth, Color color) {

    std::stringstream ss(textToDraw);
    std::string       word;
    std::string       currentLine;

    while (ss >> word) {
        std::string testLine      = currentLine.empty() ? word : currentLine.append(" ").append(word);
        float       testLineWidth = MeasureText(font, testLine);

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
