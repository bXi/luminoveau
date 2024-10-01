#include "render2dhandler.h"

void Render2D::_drawRectangle(vf2d pos, vf2d size, Color color) {
    FlushBatch();

    SDL_FRect dstRect = _doCamera(pos, size);;

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderRect(renderer, &dstRect);
}

void Render2D::_drawCircle(vf2d pos, float radius, Color color) {
    PrepareBatch();

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
    }

    _ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    _ctx.setStrokeWidth(1.0);  // Adjust the width as needed

    _ctx.strokeCircle(pos.x + radius + 1, pos.y + radius + 1,
                      radius - 0.5);  // Center at (radius, radius) with the specified radius
}

void Render2D::_drawRectangleRounded(vf2d pos, vf2d size, float radius, Color color) {
    PrepareBatch();


    if (Camera::IsActive()) {
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();
    }

    // Set composition operator and fill the background with transparent


    // Define stroke and fill style for the rectangle
    _ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    _ctx.setStrokeWidth(1.0);
    BLRoundRect rect = {
        pos.x + 0.5, pos.y + 0.5, size.x - 1, size.y - 1, radius, radius
    };

    // Create a path for the rounded rectangle
    BLPath path;
    path.addRoundRect(rect);

    // Fill the rounded rectangle
    _ctx.strokePath(path);

}

void Render2D::_drawLine(vf2d start, vf2d end, Color color) {

    PrepareBatch();

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        start = Camera::ToScreenSpace(start);
        end   = Camera::ToScreenSpace(end);
    }

    // Create an image large enough to encompass the line
    float     imgWidth  = std::abs(end.x - start.x) + 2;
    float     imgHeight = std::abs(end.y - start.y) + 2;

    // Set stroke style and draw the line
    _ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    _ctx.setStrokeWidth(1.0);  // Set the width of the line if needed
    _ctx.strokeLine(start.x, start.y, end.x, end.y);
}

void Render2D::_drawArc(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
    PrepareBatch();

}

void Render2D::_drawTexture(Texture texture, vf2d pos, vf2d size, Color color) {

    FlushBatch();

    SDL_FRect dstRect = _doCamera(pos, size);

    SDL_SetTextureColorMod(texture.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture.texture, color.a);

    SDL_RenderTextureRotated(renderer, texture.texture, nullptr, &dstRect, 0.0, nullptr, SDL_FLIP_NONE);
}

void Render2D::_drawTexturePart(Texture texture, vf2d pos, vf2d size, rectf src, Color color) {
    FlushBatch();

    SDL_FRect dstRect = _doCamera(pos, size);
    SDL_FRect srcRect = {src.x, src.y, std::abs(src.width), std::abs(src.height)};

    SDL_SetTextureColorMod(texture.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture.texture, color.a);

    // Set the flip flags
    int flipFlags = SDL_FLIP_NONE;
    if (src.width < 0.f) { flipFlags |= SDL_FLIP_HORIZONTAL; }
    if (src.height < 0.f) { flipFlags |= SDL_FLIP_VERTICAL; }

    SDL_RenderTextureRotated(renderer, texture.texture, &srcRect, &dstRect, 0.0, nullptr, (SDL_FlipMode) flipFlags);
}

void Render2D::_beginScissorMode(rectf area) {
    const SDL_Rect cliprect = area;
    SDL_SetRenderClipRect(renderer, &cliprect);
}

void Render2D::_endScissorMode() {
    SDL_SetRenderClipRect(renderer, nullptr);
}

void Render2D::_drawRectangleFilled(vf2d pos, vf2d size, Color color) {
    FlushBatch();

    SDL_FRect dstRect = _doCamera(pos, size);

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &dstRect);
}

void Render2D::_drawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color) {

    PrepareBatch();

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();  // Scale the radius with the camera's zoom
        size *= Camera::GetScale();    // Scale the size with the camera's zoom
    }

    // Define the fill style for the rectangle
    _ctx.setFillStyle(BLRgba32(color.r, color.g, color.b, color.a));

    BLRoundRect rect = {
        pos.x, pos.y, size.x, size.y, radius, radius
    };

    // Create a path for the rounded rectangle
    BLPath path;
    path.addRoundRect(rect);  // Draw at (0, 0) as the path will be offset when rendering

    // Fill the rounded rectangle
    _ctx.fillPath(path);

}

void Render2D::_drawCircleFilled(vf2d pos, float radius, Color color) {

    PrepareBatch();

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
    }


    _ctx.setFillStyle(BLRgba32(color.r, color.g, color.b, color.a));
    _ctx.fillCircle(pos.x + radius, pos.y + radius, radius);  // Center at (radius, radius) with the specified radius

}

void Render2D::_drawArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
    PrepareBatch();

    //TODO: create this function
}

void Render2D::_beginMode2D() {
    // Clear the screen
    Camera::Activate();
}

void Render2D::_endMode2D() {
    Camera::Deactivate();
}

rectf Render2D::_doCamera(vf2d pos, vf2d size) {

    rectf dstRect;

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        vf2d screenPos  = Camera::ToScreenSpace(pos);
        vf2d screenSize = Camera::ToScreenSpace(pos + size) - screenPos;

        dstRect = {screenPos.x, screenPos.y, screenSize.x, screenSize.y};
    } else {
        // Camera is not active, render directly in screen space
        dstRect = {pos.x, pos.y, size.x, size.y};
    }

    return dstRect;
}

void Render2D::_drawThickLine(vf2d start, vf2d end, Color color, float width) {

    PrepareBatch();

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        start = Camera::ToScreenSpace(start);
        end   = Camera::ToScreenSpace(end);

        // Scale the line width with the camera's zoom
        width *= Camera::GetScale();
    }

    // Set stroke style and line width
    _ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    _ctx.setStrokeWidth(width);  // Set the width of the line
    _ctx.strokeLine(start.x, start.y, end.x, end.y);

}

void Render2D::_drawTriangle(vf2d v1, vf2d v2, vf2d v3, Color color) {
    PrepareBatch();

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }

    // Set stroke style for the triangle
    _ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    _ctx.setStrokeWidth(1.0);  // Set the line thickness if needed

    // Create a path for the triangle
    BLPath path;
    path.moveTo(v1.x - 0.5, v1.y - 0.5);  // Offset to fit into the image
    path.lineTo(v2.x - 0.5, v2.y - 0.5);
    path.lineTo(v3.x - 0.5, v3.y - 0.5);
    path.close();  // Close the path to form a triangle

    // Stroke the triangle
    _ctx.strokePath(path);

}

void Render2D::_drawEllipse(vf2d center, float radiusX, float radiusY, Color color) {
    PrepareBatch();

    //TODO: figure out width/height to be pixel perfect
    //TODO: think about center vs topleft

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        center = Camera::ToScreenSpace(center);
        radiusX *= Camera::GetScale();  // Scale radiusX with camera zoom
        radiusY *= Camera::GetScale();  // Scale radiusY with camera zoom
    }

    // Set stroke style for the ellipse
    _ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    _ctx.setStrokeWidth(1.0);  // Set the line thickness if needed

    // Draw the ellipse centered at (radiusX, radiusY) in the image
    _ctx.strokeEllipse(center.x + radiusX + 0.5, center.y + radiusY + 0.5, radiusX, radiusY);
}

void Render2D::_drawTriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color) {
    PrepareBatch();

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }

    // Set fill style for the triangle
    _ctx.setFillStyle(BLRgba32(color.r, color.g, color.b, color.a));

    // Create a path for the filled triangle
    BLPath path;
    path.moveTo(v1.x, v1.y);  // Offset to fit into the image
    path.lineTo(v2.x, v2.y);
    path.lineTo(v3.x, v3.y);
    path.close();  // Close the path to form a triangle

    // Fill the triangle
    _ctx.fillPath(path);
}

void Render2D::_drawEllipseFilled(vf2d center, float radiusX, float radiusY, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        center = Camera::ToScreenSpace(center);
        radiusX *= Camera::GetScale();  // Scale radiusX with camera zoom
        radiusY *= Camera::GetScale();  // Scale radiusY with camera zoom
    }

    // Calculate the size of the image to fit the ellipse
    float imgWidth  = radiusX * 2 + 2;  // Adding a small margin for anti-aliasing
    float imgHeight = radiusY * 2 + 2;

    BLImage   img(static_cast<int>(imgWidth), static_cast<int>(imgHeight), BL_FORMAT_PRGB32);
    BLContext ctx(img);

    // Set composition operator and clear the background with transparent
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0x00000000u));  // Transparent background
    ctx.fillAll();

    // Set fill style for the ellipse
    ctx.setFillStyle(BLRgba32(color.r, color.g, color.b, color.a));

    // Draw the filled ellipse centered at (radiusX, radiusY) in the image
    ctx.fillEllipse(radiusX + 1, radiusY + 1, radiusX, radiusY);

    // End the context
    ctx.end();

    // Render the image to the screen
    Render2D::DrawBlend2DImage(img, center - vi2d{1, 1}, {imgWidth, imgHeight});
}

void Render2D::_drawPixel(vi2d pos, Color color) {

    PrepareBatch();

    _ctx.setFillStyle(BLRgba32(color.r, color.g, color.b, color.a));
    _ctx.fillRect(pos.x, pos.y, 1, 1);
}

void Render2D::_drawTextureMode7(Texture texture, vf2d pos, vf2d size, Mode7Parameters m7p, Color color) {

    float scaledA = std::abs(m7p.a) / static_cast<float>(m7p.snesScreenWidth);
    float scaledD = std::abs(m7p.d) / static_cast<float>(m7p.snesScreenHeight);

    SDL_FRect srcRect;
    srcRect.x = m7p.h;
    srcRect.y = m7p.v;
    srcRect.w = m7p.snesScreenWidth * scaledA;
    srcRect.h = m7p.snesScreenHeight * scaledD;

    //    ImGui::SetNextWindowSize({350,350});
    //    ImGui::Begin("Mode7 Debug values");
    //
    //    ImGui::Text("scaledA : %f", scaledA);
    //    ImGui::Text("scaledD : %f", scaledD);
    //
    //    ImGui::Text("srcRect.x : %f", srcRect.x);
    //    ImGui::Text("srcRect.y : %f", srcRect.y);
    //    ImGui::Text("srcRect.w : %f", srcRect.w);
    //    ImGui::Text("srcRect.h : %f", srcRect.h);

    SDL_FRect destRect = {pos.x, pos.y, size.x, size.y};

    bool shouldDrawBackground = false;

    if (srcRect.x + srcRect.w > texture.width) {
        float diffX = ((srcRect.x + srcRect.w) - (float) texture.width);
        destRect.w -= diffX * 2.f;
        srcRect.w -= diffX * 2.f;

        if (destRect.w < 0) destRect.w = 0;
        shouldDrawBackground = true;
    }

    if (srcRect.y + srcRect.h > texture.height) {
        float diffX = ((srcRect.y + srcRect.h) - (float) texture.height);
        destRect.h -= diffX * 2.f;
        srcRect.h -= diffX * 2.f;

        if (destRect.h < 0) destRect.h = 0;
        shouldDrawBackground = true;
    }

    //    ImGui::Text("destRect.x : %f", destRect.x);
    //    ImGui::Text("destRect.y : %f", destRect.y);
    //    ImGui::Text("destRect.w : %f", destRect.w);
    //    ImGui::Text("destRect.h : %f", destRect.h);

    SDL_SetTextureColorMod(texture.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture.texture, color.a);

    int flipFlags = SDL_FLIP_NONE;
    if (m7p.a < 0) { flipFlags |= SDL_FLIP_HORIZONTAL; }
    if (m7p.d < 0) { flipFlags |= SDL_FLIP_VERTICAL; }

    SDL_FPoint center = {(float) m7p.x0, (float) m7p.y0};

    if (shouldDrawBackground) {
        DrawRectangleFilled(pos, size, BLACK);
    }

    //    ImGui::End();

    SDL_RenderTextureRotated(Window::GetRenderer(), texture.texture, &srcRect, &destRect, 0.0, &center,
                             (SDL_FlipMode) flipFlags);
}

void Render2D::_drawBlend2DImage(BLImage img, vf2d pos, vf2d size, Color color) {

    BLImageData data;
    img.getData(&data);

    SDL_Texture *_tex = SDL_CreateTexture(Window::GetRenderer(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                          img.width(), img.height());
    SDL_SetTextureBlendMode(_tex, SDL_BLENDMODE_BLEND_PREMULTIPLIED);

    SDL_UpdateTexture(_tex, nullptr, data.pixelData, int(data.stride));

    SDL_FRect dstRect = {pos.x, pos.y, size.x, size.y};
    SDL_FRect srcRect = {0.f, 0.f, (float) img.width(), (float) img.height()};

    SDL_RenderTexture(Window::GetRenderer(), _tex, &srcRect, &dstRect);

    SDL_DestroyTexture(_tex);
}

void Render2D::_prepareBatch() {
    if (!_isBatchReady()) {
        _resetBatch(); // Initialize the batch if it isn't ready
    }
}

void Render2D::_flushBatch() {
    if (_isBatchReady()) {
        // Output the current batch (e.g., rendering it to the screen)
        _outputBatch();

        // Reset the batch state for the next round of drawing
        _resetBatch();
    }
}

void Render2D::_outputBatch() {

    _ctx.end();
    // Draw the batch image to the current rendering target
    _drawBlend2DImage(_img, {0, 0}, Window::GetSize(), WHITE);

    _img = BLImage(); // Reset to default state
    _ctx = BLContext(); // Reset to default state
}

void Render2D::_resetBatch() {
    // Create a new image and context for the next batch
    _img = BLImage(static_cast<int>(Window::GetWidth()), static_cast<int>(Window::GetHeight()), BL_FORMAT_PRGB32);
    _ctx = BLContext(_img);

    _ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    _ctx.setFillStyle(BLRgba32(0x00000000u));  // Transparent background
    _ctx.fillAll();
}

bool Render2D::_isBatchReady() {
    // Check if _img and _ctx are initialized
    return _img.width() > 0 && _img.height() > 0 && _ctx.isValid();
}

