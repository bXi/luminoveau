#include "texthandler.h"

#include <algorithm>

void Text::_drawText(Font font, const vf2d &pos, const std::string &textToDraw, Color color) {

    if (textToDraw.empty()) return;

    TTF_Text *text = TTF_CreateText(font.textEngine, font.ttfFont, textToDraw.c_str(), textToDraw.length());

    TTF_SetTextWrapWidth(text, 0);

    TTF_GPUAtlasDrawSequence *sequence = TTF_GetGPUTextDrawData(text);

    if (!sequence) {
        throw std::runtime_error(Helpers::TextFormat("%s: failed to create a font sequence: %s", CURRENT_METHOD(), SDL_GetError()));
    }

    do {
        TextureAsset tex = {
            .gpuTexture = sequence->atlas_texture,
            .gpuSampler = textSampler,
        };

        for (int i = 0; i + 5 < sequence->num_indices; i += 6) {

            auto xy = (Uint8 *) sequence->xy;
            auto uv = (Uint8 *) sequence->uv;

            auto v1 = glm::vec2(((float *) (xy + sequence->indices[i + 1] * sequence->xy_stride))[0],
                                ((float *) (xy + sequence->indices[i + 1] * sequence->xy_stride))[1]);

            auto v5 = glm::vec2(((float *) (xy + sequence->indices[i + 5] * sequence->xy_stride))[0],
                                ((float *) (xy + sequence->indices[i + 5] * sequence->xy_stride))[1]);

            auto uv2 = glm::vec2(((float *) (uv + sequence->indices[i + 2] * sequence->uv_stride))[0],
                                 ((float *) (uv + sequence->indices[i + 2] * sequence->uv_stride))[1]);

            auto uv3 = glm::vec2(((float *) (uv + sequence->indices[i + 3] * sequence->uv_stride))[0],
                                 ((float *) (uv + sequence->indices[i + 3] * sequence->uv_stride))[1]);

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
                .size = glm::vec2(maxX - minX, maxY - minY),
                .transform = {
                    .position = glm::vec2(minX + pos.x, pos.y + (0 - maxY)),
                },
                .uv = {
                    glm::vec2(uvMaxX, uvMaxY),  // Top-right
                    glm::vec2(uvMinX, uvMaxY),  // Top-left
                    glm::vec2(uvMaxX, uvMinY),  // Bottom-right
                    glm::vec2(uvMinX, uvMaxY),  // Top-left (repeated for triangle)
                    glm::vec2(uvMinX, uvMinY),  // Bottom-left
                    glm::vec2(uvMaxX, uvMinY)   // Bottom-right
                },
                .tintColor = color,
            };

            // Add to the render queue
            Window::AddToRenderQueue("2dsprites", ren);
        }

        sequence = sequence->next;
    } while (sequence);
}

int Text::_measureText(Font font, std::string textToDraw) {
    return (int) (_getRenderedTextSize(font, textToDraw).x + 0.1f);
}

vf2d Text::_getRenderedTextSize(Font font, const std::string &textToDraw) {

    if (textToDraw.empty()) return {0, 0};

    TTF_Text *text = TTF_CreateText(font.textEngine, font.ttfFont, textToDraw.c_str(), textToDraw.length());

    int tw, th;
    TTF_GetTextSize(text, &tw, &th);

    auto width  = (float)tw;
    auto height = (float)th;

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
        std::string testLine      = currentLine.empty() ? word : currentLine + " " + word;
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
