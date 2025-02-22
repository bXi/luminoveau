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

            auto v1 = sequence->xy[sequence->indices[i + 1]];
            auto v5 = sequence->xy[sequence->indices[i + 5]];
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



            vf2d scale = {1.0f, 1.0f};
            glm::vec2 flipped{1.0f, 1.0f};

            // Given parameters
            glm::vec2 rPosition = glm::vec2(minX + pos.x, pos.y + (0 - maxY));    // World position
            glm::vec2 rSize     = {maxX - minX, maxY - minY}; // Original texture size in pixels
            glm::vec2 rScale    = scale;             // Scale factors (can be non-uniform)
            float     rZ_index  = (float) Renderer::GetZIndex() / (float) INT32_MAX;

            glm::mat4 scale_matrix       = glm::scale(glm::mat4(1.0f), glm::vec3(rSize * rScale, 1.0f));
            glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(rPosition, rZ_index));

            glm::mat4 model_matrix = translation_matrix * scale_matrix;


            // Set up the Renderable object with UVs
            Renderable ren = {
                .texture = tex,
                .size = glm::vec2(maxX - minX, maxY - minY),
                .uv = {
                    glm::vec2(uvMaxX, uvMaxY),  // Top-right
                    glm::vec2(uvMinX, uvMaxY),  // Top-left
                    glm::vec2(uvMaxX, uvMinY),  // Bottom-right
                    glm::vec2(uvMinX, uvMaxY),  // Top-left (repeated for triangle)
                    glm::vec2(uvMinX, uvMinY),  // Bottom-left
                    glm::vec2(uvMaxX, uvMinY)   // Bottom-right
                },
                .model = model_matrix,
                .flipped = flipped,
                .tintColor = color,
                .transform = {
                    .position = glm::vec2(minX + pos.x, pos.y + (0 - maxY)),
                },
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

    auto width  = (float)tw;
    auto height = (float)th;

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
