#include "render2dhandler.h"

#include <utility>

void Render2D::_drawRectangle(vf2d pos, vf2d size, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderRect(renderer, &dstRect);
}

void Render2D::_drawCircle(vf2d pos, float radius, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
    }

    int imgSize = static_cast<int>(radius * 2) + 2;  // Adding a margin for anti-aliasing

    BLImage   circleImage(imgSize, imgSize, BL_FORMAT_PRGB32);
    BLContext ctx(circleImage);

    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0x00000000u));
    ctx.fillAll();

    ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    ctx.setStrokeWidth(1.0);  // Adjust the width as needed
    ctx.strokeCircle(radius + 1, radius + 1, radius - 0.5);  // Center at (radius, radius) with the specified radius

    ctx.end();

    Render2D::DrawBlend2DImage(circleImage, pos - vi2d{1,1}, {(float) imgSize, (float) imgSize});
}

void Render2D::_drawRectangleRounded(vf2d pos, const vf2d& size, float radius, Color color) {
    if (Camera::IsActive()) {
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();
    }

    BLImage   img(size.x + 2, size.y + 2, BL_FORMAT_PRGB32);
    BLContext ctx(img);

    // Set composition operator and fill the background with transparent
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0x00000000u));  // Transparent background
    ctx.fillAll();

    // Define stroke and fill style for the rectangle
    ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    ctx.setStrokeWidth(1.0);

    BLRoundRect rect = {
        1.5, 1.5, size.x - 1, size.y - 1, radius, radius
    };

    // Create a path for the rounded rectangle
    BLPath path;
    path.addRoundRect(rect);

    // Fill the rounded rectangle
    ctx.strokePath(path);

    // End the context
    ctx.end();

    // Render the image to the screen
    Render2D::DrawBlend2DImage(img, pos - vi2d{1,1}, size + vi2d{2,2});
}

void Render2D::_drawLine(vf2d start, vf2d end, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        start = Camera::ToScreenSpace(start);
        end   = Camera::ToScreenSpace(end);
    }

    // Create an image large enough to encompass the line
    float     imgWidth  = std::abs(end.x - start.x) + 2;
    float     imgHeight = std::abs(end.y - start.y) + 2;
    BLImage   img(static_cast<int>(imgWidth), static_cast<int>(imgHeight), BL_FORMAT_PRGB32);
    BLContext ctx(img);

    // Set composition operator and clear the background with transparent
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0x00000000u));  // Transparent background
    ctx.fillAll();

    // Set stroke style and draw the line
    ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    ctx.setStrokeWidth(1.0);  // Set the width of the line if needed
    ctx.strokeLine(0, 0, imgWidth-2, imgHeight-2);

    // End the context
    ctx.end();

    // Render the image containing the line
    Render2D::DrawBlend2DImage(img, start, {imgWidth, imgHeight});
}

void Render2D::_drawArc(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
}

void Render2D::_drawTexture(Texture texture, vf2d pos, const vf2d& size, Color color) {

    SDL_FRect dstRect = _doCamera(pos, size);

    SDL_SetTextureColorMod(texture.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture.texture, color.a);

    SDL_RenderTextureRotated(renderer, texture.texture, nullptr, &dstRect, 0.0, nullptr, SDL_FLIP_NONE);

    vf2d scale = size / texture.getSize();

    Renderable tex = {
        .texture = texture,
        .size = {texture.width, texture.height},
        .transform = {
            .position = { pos.x, pos.y},
            .scale = {scale.x, scale.y }
        },
        .tintColor = color,

    };

    Window::AddToRenderQueue(_targetRenderPass, tex);

}

void Render2D::_drawTexturePart(Texture texture, const vf2d& pos, const vf2d& size, const rectf& src, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);
    SDL_FRect srcRect = {src.x, src.y, std::abs(src.width), std::abs(src.height)};

    SDL_SetTextureColorMod(texture.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture.texture, color.a);

    // Set the flip flags
    int flipFlags = SDL_FLIP_NONE;
    if (src.width < 0.f) { flipFlags |= SDL_FLIP_HORIZONTAL; }
    if (src.height < 0.f) { flipFlags |= SDL_FLIP_VERTICAL; }

    SDL_RenderTextureRotated(renderer, texture.texture, &srcRect, &dstRect, 0.0, nullptr, (SDL_FlipMode) flipFlags);


    vf2d scale = size / texture.getSize();

    float u0 = srcRect.x / (float)texture.getSize().x;
    float v0 = srcRect.y / (float)texture.getSize().y;
    float u1 = (srcRect.x + srcRect.w) / (float)texture.getSize().x;
    float v1 = (srcRect.y + srcRect.h) / (float)texture.getSize().y;

    Renderable renderable = {
        .texture = texture,
        .size = {texture.width, texture.height},
        .flipped_horizontally = (src.width >= 0.f),
        .flipped_vertically = (src.height < 0.f),
        .transform = {
            .position = { dstRect.x, dstRect.y},
            .scale = {scale.x, scale.y }
        },
        .uv = {
            glm::vec2(u1, v1),  // Top-right
            glm::vec2(u0, v1),  // Top-left
            glm::vec2(u1, v0),  // Bottom-right
            glm::vec2(u0, v1),  // Top-left (repeated for triangle)
            glm::vec2(u0, v0),  // Bottom-left
            glm::vec2(u1, v0)   // Bottom-right
        },
        .tintColor = color,
    };

    Window::AddToRenderQueue("2dsprites", renderable);

}

void Render2D::_beginScissorMode(rectf area) {
    const SDL_Rect cliprect = area;
    SDL_SetRenderClipRect(renderer, &cliprect);
}

void Render2D::_endScissorMode() {
    SDL_SetRenderClipRect(renderer, nullptr);
}

void Render2D::_drawRectangleFilled(vf2d pos, vf2d size, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &dstRect);
}

void Render2D::_drawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color) {

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();  // Scale the radius with the camera's zoom
        size *= Camera::GetScale();    // Scale the size with the camera's zoom
    }

    BLImage   img(size.x + 2, size.y + 2, BL_FORMAT_PRGB32);
    BLContext ctx(img);

    // Set composition operator and clear the background with transparent
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0x00000000u));  // Transparent background
    ctx.fillAll();

    // Define the fill style for the rectangle
    ctx.setFillStyle(BLRgba32(color.r, color.g, color.b, color.a));

    BLRoundRect rect = {
        1, 1, size.x, size.y, radius, radius
    };

    // Create a path for the rounded rectangle
    BLPath path;
    path.addRoundRect(rect);  // Draw at (0, 0) as the path will be offset when rendering

    // Fill the rounded rectangle
    ctx.fillPath(path);

    // End the context
    ctx.end();

    // Render the image to the screen at the specified position
    Render2D::DrawBlend2DImage(img, pos - vi2d{1, 1}, size + vi2d{2,2});
}

void Render2D::_drawCircleFilled(vf2d pos, float radius, Color color) {

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
    }

    int imgSize = static_cast<int>(radius * 2) + 2;  // Adding a margin for anti-aliasing

    BLImage   circleImage(imgSize, imgSize, BL_FORMAT_PRGB32);
    BLContext ctx(circleImage);

    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0x00000000u));
    ctx.fillAll();

    ctx.setFillStyle(BLRgba32(color.r, color.g, color.b, color.a));
    ctx.fillCircle(radius+1.f, radius+1.f, radius);  // Center at (radius, radius) with the specified radius

    ctx.end();

    Render2D::DrawBlend2DImage(circleImage, pos - vf2d{1.f, 1.f}, {(float) imgSize, (float) imgSize});
}

void Render2D::_drawArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
    //TODO: create this function
}

void Render2D::_beginMode2D() {
    // Clear the screen
    Camera::Activate();
}

void Render2D::_endMode2D() {
    Camera::Deactivate();
}

rectf Render2D::_doCamera(const vf2d& pos, const vf2d& size) {

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

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        start = Camera::ToScreenSpace(start);
        end   = Camera::ToScreenSpace(end);

        // Scale the line width with the camera's zoom
        width *= Camera::GetScale();
    }

    // Create an image large enough to encompass the line
    float     imgWidth  = std::abs(end.x - start.x) + 2 + width;  // Add margin for width
    float     imgHeight = std::abs(end.y - start.y) + 2 + width;
    BLImage   img(static_cast<int>(imgWidth), static_cast<int>(imgHeight), BL_FORMAT_PRGB32);
    BLContext ctx(img);

    // Set composition operator and clear the background with transparent
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0x00000000u));  // Transparent background
    ctx.fillAll();

    // Set stroke style and line width
    ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    ctx.setStrokeWidth(width);  // Set the width of the line
    ctx.strokeLine(width, width, imgWidth - 2 , imgHeight - 2 );

    // End the context
    ctx.end();

    // Render the image containing the line
    Render2D::DrawBlend2DImage(img, start - vf2d{width, width}, {imgWidth, imgHeight});
}

void Render2D::_drawTriangle(vf2d v1, vf2d v2, vf2d v3, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }

    // Calculate the bounding box for the triangle to create an appropriate image size
    float minX = std::min({v1.x, v2.x, v3.x});
    float minY = std::min({v1.y, v2.y, v3.y});
    float maxX = std::max({v1.x, v2.x, v3.x});
    float maxY = std::max({v1.y, v2.y, v3.y});

    float imgWidth  = maxX - minX + 2;
    float imgHeight = maxY - minY + 2;

    BLImage   img(static_cast<int>(imgWidth), static_cast<int>(imgHeight), BL_FORMAT_PRGB32);
    BLContext ctx(img);

    // Set composition operator and clear the background with transparent
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0x00000000u));  // Transparent background
    ctx.fillAll();

    // Set stroke style for the triangle
    ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    ctx.setStrokeWidth(1.0);  // Set the line thickness if needed

    // Create a path for the triangle
    BLPath path;
    path.moveTo(v1.x - minX - 0.5, v1.y - minY - 0.5);  // Offset to fit into the image
    path.lineTo(v2.x - minX - 0.5, v2.y - minY - 0.5);
    path.lineTo(v3.x - minX - 0.5, v3.y - minY - 0.5);
    path.close();  // Close the path to form a triangle

    // Stroke the triangle
    ctx.strokePath(path);

    // End the context
    ctx.end();

    // Render the image to the screen
    Render2D::DrawBlend2DImage(img, {minX, minY}, {imgWidth, imgHeight});
}

void Render2D::_drawEllipse(vf2d center, float radiusX, float radiusY, Color color) {
    //TODO: figure out width/height to be pixel perfect
    //TODO: think about center vs topleft

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

    // Set stroke style for the ellipse
    ctx.setStrokeStyle(BLRgba32(color.r, color.g, color.b, color.a));
    ctx.setStrokeWidth(1.0);  // Set the line thickness if needed

    // Draw the ellipse centered at (radiusX, radiusY) in the image
    ctx.strokeEllipse(radiusX + 0.5, radiusY + 0.5, radiusX, radiusY);

    // End the context
    ctx.end();

    // Render the image to the screen
    Render2D::DrawBlend2DImage(img, center, {imgWidth, imgHeight});
}

void Render2D::_drawTriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }

    // Calculate the bounding box for the triangle to create an appropriate image size
    float minX = std::min({v1.x, v2.x, v3.x});
    float minY = std::min({v1.y, v2.y, v3.y});
    float maxX = std::max({v1.x, v2.x, v3.x});
    float maxY = std::max({v1.y, v2.y, v3.y});

    float imgWidth = maxX - minX + 2;
    float imgHeight = maxY - minY + 2;

    BLImage img(static_cast<int>(imgWidth), static_cast<int>(imgHeight), BL_FORMAT_PRGB32);
    BLContext ctx(img);

    // Set composition operator and clear the background with transparent
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(0x00000000u));  // Transparent background
    ctx.fillAll();

    // Set fill style for the triangle
    ctx.setFillStyle(BLRgba32(color.r, color.g, color.b, color.a));

    // Create a path for the filled triangle
    BLPath path;
    path.moveTo(v1.x - minX, v1.y - minY);  // Offset to fit into the image
    path.lineTo(v2.x - minX, v2.y - minY);
    path.lineTo(v3.x - minX, v3.y - minY);
    path.close();  // Close the path to form a triangle

    // Fill the triangle
    ctx.fillPath(path);

    // End the context
    ctx.end();

    // Render the image to the screen
    Render2D::DrawBlend2DImage(img, {minX, minY}, {imgWidth, imgHeight});
}

void Render2D::_drawEllipseFilled(vf2d center, float radiusX, float radiusY, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        center = Camera::ToScreenSpace(center);
        radiusX *= Camera::GetScale();  // Scale radiusX with camera zoom
        radiusY *= Camera::GetScale();  // Scale radiusY with camera zoom
    }

    // Calculate the size of the image to fit the ellipse
    float imgWidth = radiusX * 2 + 2;  // Adding a small margin for anti-aliasing
    float imgHeight = radiusY * 2 + 2;

    BLImage img(static_cast<int>(imgWidth), static_cast<int>(imgHeight), BL_FORMAT_PRGB32);
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
    Render2D::DrawBlend2DImage(img, center - vi2d{1,1}, {imgWidth, imgHeight});

}

void Render2D::_drawPixel(vi2d pos, Color color) {
    SDL_SetRenderDrawColor(Window::GetRenderer(), color.r, color.g, color.b, color.a);
    SDL_RenderPoint(Window::GetRenderer(), pos.x, pos.y);
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

    SDL_RenderTextureRotated(Window::GetRenderer(), texture.texture, &srcRect, &destRect, 0.0, &center,
                             (SDL_FlipMode) flipFlags);
}

void Render2D::_drawBlend2DImage(const BLImage& img, vf2d pos, vf2d origsize, Color color, std::string fileName) {

    vi2d size = {img.width(), img.height()};

    BLImageData data;
    img.getData(&data);

    size.x -= 1;

//    TextureAsset texture = AssetHandler::LoadFromPixelData(size, data.pixelData, std::move(fileName));
//
//    Renderable renderable = {
//        .texture = texture,
//        .size = {img.width(), img.height()},
//        .transform = {
//            .position = { pos.x, pos.y},
//            .scale = {1.f, 1.f }
//        },
//        .tintColor = color,
//        .temporary = true,
//    };

//    Window::AddToRenderQueue(renderable);
}


