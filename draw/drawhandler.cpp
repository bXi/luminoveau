#include "drawhandler.h"

#include <utility>

void Draw::_initPixelBuffer() {
    // Get desktop size to match framebuffer dimensions
    SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* displayMode = SDL_GetCurrentDisplayMode(primaryDisplay);
    _pixelBufferWidth = displayMode ? displayMode->w : 3840;  // Fallback to 4K if can't get display
    _pixelBufferHeight = displayMode ? displayMode->h : 2160;

    LOG_INFO("initializing pixel buffer at desktop size: {}x{}", _pixelBufferWidth, _pixelBufferHeight);

    // Pre-allocate buffer for pixel data
    _pixelBufferData.resize(_pixelBufferWidth * _pixelBufferHeight, 0x00000000); // Transparent
    
    // Create GPU texture for pixel buffer
    SDL_GPUTextureCreateInfo texInfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = _pixelBufferWidth,
        .height = _pixelBufferHeight,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    
    _pixelTexture.gpuTexture = SDL_CreateGPUTexture(Renderer::GetDevice(), &texInfo);
    _pixelTexture.gpuSampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode());
    _pixelTexture.width = _pixelBufferWidth;
    _pixelTexture.height = _pixelBufferHeight;
    _pixelTexture.filename = "[Lumi]PixelBuffer";
    
    if (!_pixelTexture.gpuTexture) {
        LOG_ERROR("failed to create pixel buffer texture: {}", SDL_GetError());
        return;
    }
    
    SDL_SetGPUTextureName(Renderer::GetDevice(), _pixelTexture.gpuTexture, "[Lumi]PixelBuffer");
    
    // Create transfer buffer
    SDL_GPUTransferBufferCreateInfo transferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32)(_pixelBufferWidth * _pixelBufferHeight * sizeof(uint32_t)),
        .props = 0,
    };
    
    _pixelTransferBuffer = SDL_CreateGPUTransferBuffer(Renderer::GetDevice(), &transferInfo);
    
    if (!_pixelTransferBuffer) {
        LOG_ERROR("failed to create pixel transfer buffer: {}", SDL_GetError());
    }
}

void Draw::_flushPixels() {
    if (!_pixelsDirty) return;
    if (!_pixelTransferBuffer || !_pixelTexture.gpuTexture) return;
    
    // Map transfer buffer and copy pixel data
    void* mappedData = SDL_MapGPUTransferBuffer(Renderer::GetDevice(), _pixelTransferBuffer, false);
    if (mappedData) {
        std::memcpy(mappedData, _pixelBufferData.data(), _pixelBufferWidth * _pixelBufferHeight * sizeof(uint32_t));
        SDL_UnmapGPUTransferBuffer(Renderer::GetDevice(), _pixelTransferBuffer);
        
        // Upload to GPU texture
        SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(Renderer::GetDevice());
        if (!cmdBuf) {
            LOG_ERROR("Failed to acquire GPU command buffer: %s", SDL_GetError());
            return;
        }
        
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);
        if (!copyPass) {
            LOG_ERROR("failed to begin GPU copy pass: {}", SDL_GetError());
            return;
        }
        
        SDL_GPUTextureTransferInfo transferInfo = {
            .transfer_buffer = _pixelTransferBuffer,
            .offset = 0,
            .pixels_per_row = 0,
            .rows_per_layer = 0,
        };
        
        SDL_GPUTextureRegion destInfo = {
            .texture = _pixelTexture.gpuTexture,
            .mip_level = 0,
            .layer = 0,
            .x = 0,
            .y = 0,
            .z = 0,
            .w = _pixelBufferWidth,
            .h = _pixelBufferHeight,
            .d = 1,
        };
        
        SDL_UploadToGPUTexture(copyPass, &transferInfo, &destInfo, false);
        SDL_EndGPUCopyPass(copyPass);
        SDL_SubmitGPUCommandBuffer(cmdBuf);
    } else {
        LOG_ERROR("failed to map transfer buffer: {}", SDL_GetError());
        return;
    }
    
    // Clear buffer and dirty flag BEFORE drawing to avoid recursion issues
    std::fill(_pixelBufferData.begin(), _pixelBufferData.end(), 0x00000000);
    _pixelsDirty = false;
    
    // Draw the pixel buffer as a texture
    _drawTexture(_pixelTexture, {0, 0}, {(float)_pixelBufferWidth, (float)_pixelBufferHeight}, WHITE);
}

void Draw::_cleanupPixelBuffer() {
    if (_pixelTransferBuffer) {
        SDL_ReleaseGPUTransferBuffer(Renderer::GetDevice(), _pixelTransferBuffer);
        _pixelTransferBuffer = nullptr;
    }
    
    if (_pixelTexture.gpuTexture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), _pixelTexture.gpuTexture);
        _pixelTexture.gpuTexture = nullptr;
    }
    
    _pixelBufferData.clear();
}

void Draw::_drawRectangle(const vf2d& pos, const vf2d& size, Color color) {
    _flushPixels();  // Auto-flush before drawing
    
    rectf dstRect = _doCamera(pos, size);

    // Draw the four sides of the rectangle using lines
    vf2d topLeft = dstRect.pos;
    vf2d topRight = {dstRect.pos.x + dstRect.size.x, dstRect.pos.y};
    vf2d bottomRight = {dstRect.pos.x + dstRect.size.x, dstRect.pos.y + dstRect.size.y};
    vf2d bottomLeft = {dstRect.pos.x, dstRect.pos.y + dstRect.size.y};

    // Top edge
    _drawLine(topLeft, topRight, color);
    // Right edge
    _drawLine(topRight, bottomRight, color);
    // Bottom edge
    _drawLine(bottomRight, bottomLeft, color);
    // Left edge
    _drawLine(bottomLeft, topLeft, color);
}

void Draw::_drawCircle(vf2d pos, float radius, Color color, int segments = 32) {
    _flushPixels();  // Auto-flush before drawing

    float angleStep = 2.0f * PI / segments;

vf2d first = {
    pos.x + std::cos(0.0f) * radius,
    pos.y + std::sin(0.0f) * radius - 0.5f  // Offset by half pixel
};

    vf2d prev = first;

    for (int i = 1; i < segments; i++) {  // Changed: i < segments (not <=)
        float a = i * angleStep;
        vf2d curr = {
            pos.x + std::cos(a) * radius,
            pos.y + std::sin(a) * radius
        };

        _drawLine(prev, curr, color);
        prev = curr;
    }

    // Explicitly close the circle
    _drawLine(prev, first, color);
}

void Draw::_drawRectangleRounded(vf2d pos, const vf2d &size, float radius, Color color) {
    LUMI_UNUSED(size, radius, color);
    if (Camera::IsActive()) {
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();
    }
}

void Draw::_drawLine(vf2d start, vf2d end, Color color) {
    _flushPixels();  // Auto-flush before drawing
    
    if (Camera::IsActive()) {
        start = Camera::ToScreenSpace(start);
        end   = Camera::ToScreenSpace(end);
    }

    vf2d line = end - start;

    // Early exit for zero-length lines
    if (line.x == 0.0f && line.y == 0.0f) return;

    float length = std::hypot(line.x, line.y);
    float angle = std::atan2(line.y, line.x);  // Keep in radians

    vf2d size = {length, 1.0f};
    vf2d pivot = {0.0f, 0.5f};

    _drawRotatedTexture(Renderer::WhitePixel(), start, size, angle, pivot, color);
}

void Draw::_drawArc(const vf2d& center, float radius, float startAngle, float endAngle, int segments, Color color) {
    LUMI_UNUSED(center, radius, startAngle, endAngle, segments, color);
}

void Draw::_drawTexture(TextureType texture, const vf2d& pos, const vf2d &size, Color color) {
    _flushPixels();  // Auto-flush before drawing
    
    SDL_FRect dstRect = _doCamera(pos, size);

    Renderable renderable = {
        .texture = texture,
        .geometry = Renderer::GetQuadGeometry(),

        .x = dstRect.x,
        .y = dstRect.y,
        .z = (float)Renderer::GetZIndex() / (float)MAX_SPRITES,  // Normalize to 0.0-1.0

        .rotation = 0.f,


        .tex_u = 0.f,
        .tex_v = 0.f,
        .tex_w = 1.f,
        .tex_h = 1.f,

        .r = (float) color.r / 255.f,
        .g = (float) color.g / 255.f,
        .b = (float) color.b / 255.f,
        .a = (float) color.a / 255.f,

        .w = size.x,
        .h = size.y,

        .pivot_x = 0.5f,
        .pivot_y = 0.5f,
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_drawTexturePart(TextureType texture, const vf2d &pos, const vf2d &size, const rectf &src, Color color) {
    SDL_FRect dstRect = _doCamera(pos, size);
    SDL_FRect srcRect = {src.x, src.y, std::abs(src.width), std::abs(src.height)};

    float u0 = (srcRect.x / (float) texture.getSize().x);
    float v0 = (srcRect.y / (float) texture.getSize().y);
    float u1 = ((srcRect.x + srcRect.w) / (float) texture.getSize().x);
    float v1 = ((srcRect.y + srcRect.h) / (float) texture.getSize().y);

    Renderable renderable = {
        .texture = texture,
        .geometry = Renderer::GetQuadGeometry(),

        .x = dstRect.x,
        .y = dstRect.y,
        .z = (float)Renderer::GetZIndex() / (float)MAX_SPRITES,  // Normalize to 0.0-1.0

        .rotation = 0.f,

        .tex_u = u0,
        .tex_v = v0,
        .tex_w = u1 - u0,
        .tex_h = v1 - v0,

        .r = (float) color.r / 255.f,
        .g = (float) color.g / 255.f,
        .b = (float) color.b / 255.f,
        .a = (float) color.a / 255.f,

        .w = dstRect.w,
        .h = dstRect.h,

        .pivot_x = 0.5f,
        .pivot_y = 0.5f,
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_drawRotatedTexture(Draw::TextureType texture, const vf2d& pos, const vf2d &size, float angle, const vf2d& pivot, Color color) {

    Renderable renderable = {
        .texture = texture,
        .geometry = Renderer::GetQuadGeometry(),

        .x = pos.x,
        .y = pos.y,
        .z = (float)Renderer::GetZIndex() / (float)MAX_SPRITES,  // Normalize to 0.0-1.0

        .rotation = angle,  // Angle is already in radians

        .tex_u = 0.f,
        .tex_v = 0.f,
        .tex_w = 1.f,
        .tex_h = 1.f,

        .r = (float) color.r / 255.f,
        .g = (float) color.g / 255.f,
        .b = (float) color.b / 255.f,
        .a = (float) color.a / 255.f,

        .w = size.x,
        .h = size.y,

        .pivot_x = pivot.x,
        .pivot_y = pivot.y,
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_drawRotatedTexturePart(Draw::TextureType texture, const vf2d &pos, const vf2d &size, const rectf &src, float angle, const vf2d& pivot,
                                   Color color) {
    SDL_FRect srcRect = {src.x, src.y, std::abs(src.width), std::abs(src.height)};

    float u0 = (srcRect.x / (float) texture.getSize().x);
    float v0 = (srcRect.y / (float) texture.getSize().y);
    float u1 = ((srcRect.x + srcRect.w) / (float) texture.getSize().x);
    float v1 = ((srcRect.y + srcRect.h) / (float) texture.getSize().y);

    Renderable renderable = {
        .texture = texture,
        .geometry = Renderer::GetQuadGeometry(),

        .x = pos.x,
        .y = pos.y,
        .z = (float)Renderer::GetZIndex() / (float)MAX_SPRITES,  // Normalize to 0.0-1.0

        .rotation = angle,  // Angle is already in radians

        .tex_u = u0,
        .tex_v = v0,
        .tex_w = u1,
        .tex_h = v1,

        .r = (float) color.r / 255.f,
        .g = (float) color.g / 255.f,
        .b = (float) color.b / 255.f,
        .a = (float) color.a / 255.f,

        .w = size.x,
        .h = size.y,

        .pivot_x = pivot.x,
        .pivot_y = pivot.x,
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_setScissorMode(const rectf& area) {
    Renderer::SetScissorMode(_targetRenderPass, area);
}

void Draw::_drawRectangleFilled(vf2d pos, vf2d size, Color color) {
    _flushPixels();  // Auto-flush before drawing

    rectf dstRect = _doCamera(pos, size);

    Texture(Renderer::WhitePixel(), dstRect.pos, dstRect.size, color);
}

void Draw::_drawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color) {
    _flushPixels();  // Auto-flush before drawing
    
    // Clamp radius to not exceed half of the smallest dimension
    radius = std::min(radius, std::min(size.x, size.y) / 2.0f);
    
    // Apply camera transformation
    if (Camera::IsActive()) {
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();
        size *= Camera::GetScale();
    }
    
    // Calculate normalized corner radius (0.0 to 0.5)
    float normalizedRadius = radius / std::min(size.x, size.y);
    normalizedRadius = std::min(0.5f, normalizedRadius);
    
    // Get the rounded rectangle geometry with 8 segments per corner
    Geometry2D* geom = Renderer::GetRoundedRectGeometry(normalizedRadius, 8);
    
    // Create renderable using the geometry
    Renderable renderable = {
        .texture = Renderer::WhitePixel(),
        .geometry = geom,

        .x = pos.x,
        .y = pos.y,
        .z = (float)Renderer::GetZIndex() / (float)MAX_SPRITES,

        .rotation = 0.f,

        .tex_u = 0.f,
        .tex_v = 0.f,
        .tex_w = 1.f,
        .tex_h = 1.f,

        .r = (float) color.r / 255.f,
        .g = (float) color.g / 255.f,
        .b = (float) color.b / 255.f,
        .a = (float) color.a / 255.f,

        .w = size.x,
        .h = size.y,

        .pivot_x = 0.5f,
        .pivot_y = 0.5f,
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_drawCircleFilled(vf2d pos, float radius, Color color) {
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        pos = Camera::ToScreenSpace(pos);
        radius *= Camera::GetScale();
    }

    Renderable renderable = {
        .texture = Renderer::WhitePixel(),  // Use white pixel texture for tinting
        .geometry = Renderer::GetCircleGeometry(32),  // Use 32-segment circle

        .x = pos.x,
        .y = pos.y,
        .z = (float)Renderer::GetZIndex() / (float)MAX_SPRITES,  // Normalize to 0.0-1.0

        .rotation = 0.f,

        .tex_u = 0.f,
        .tex_v = 0.f,
        .tex_w = 1.f,
        .tex_h = 1.f,

        .r = (float) color.r / 255.f,
        .g = (float) color.g / 255.f,
        .b = (float) color.b / 255.f,
        .a = (float) color.a / 255.f,

        .w = radius,
        .h = radius,

        .pivot_x = 0.5f,
        .pivot_y = 0.5f,
    };

    Renderer::AddToRenderQueue(_targetRenderPass, renderable);
}

void Draw::_drawArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
    LUMI_UNUSED(center, radius, startAngle, endAngle, segments, color);

    //TODO: create this function
}

void Draw::_beginMode2D() {
    // Clear the screen
    Camera::Activate();
}

void Draw::_endMode2D() {
    Camera::Deactivate();
}

rectf Draw::_doCamera(const vf2d &pos, const vf2d &size) {

    rectf dstRect;

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        dstRect.pos  = Camera::ToScreenSpace(pos);
        dstRect.size = Camera::ToScreenSpace(pos + size) - dstRect.pos;

    } else {
        // Camera is not active, render directly in screen space

        dstRect.pos = pos;
        dstRect.size = size;
    }

    return dstRect;
}

void Draw::_drawThickLine(vf2d start, vf2d end, Color color, float width) {
    _flushPixels();  // Auto-flush before drawing
    
    if (Camera::IsActive()) {
        start = Camera::ToScreenSpace(start);
        end   = Camera::ToScreenSpace(end);
        width *= Camera::GetScale();
    }

    vf2d line = end - start;

    // Early exit for (nearly) zero-length lines
    const float EPS = 1e-6f;
    if (std::fabs(line.x) < EPS && std::fabs(line.y) < EPS) return;

    float length = std::hypot(line.x, line.y);
    float angle = std::atan2(line.y, line.x);  // Keep in radians

    // draw rectangle with pivot at left-center so we can place it exactly at `start`
    vf2d size = { length, width };
    vf2d pivot = { 0.0f, 0.5f };
    vf2d offsetStart = start - vf2d(0.f, width/2.f); // left-center of the rotated rectangle should be at `start`

    _drawRotatedTexture(Renderer::WhitePixel(), offsetStart, size, angle, pivot, color);
}


void Draw::_drawTriangle(vf2d v1, vf2d v2, vf2d v3, Color color) {
    LUMI_UNUSED(color);

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }
}

void Draw::_drawEllipse(vf2d center, float radiusX, float radiusY, Color color) {
    LUMI_UNUSED(radiusX, radiusY, color);

    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        center = Camera::ToScreenSpace(center);
        radiusX *= Camera::GetScale();  // Scale radiusX with camera zoom
        radiusY *= Camera::GetScale();  // Scale radiusY with camera zoom
    }
}

void Draw::_drawTriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color) {
    LUMI_UNUSED(color);
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        v1 = Camera::ToScreenSpace(v1);
        v2 = Camera::ToScreenSpace(v2);
        v3 = Camera::ToScreenSpace(v3);
    }
}

void Draw::_drawEllipseFilled(vf2d center, float radiusX, float radiusY, Color color) {
    LUMI_UNUSED(radiusX, radiusY, color);
    if (Camera::IsActive()) {
        // Convert world space coordinates to screen space
        center = Camera::ToScreenSpace(center);
        radiusX *= Camera::GetScale();  // Scale radiusX with camera zoom
        radiusY *= Camera::GetScale();  // Scale radiusY with camera zoom
    }
}

void Draw::_drawPixel(const vi2d& pos, Color color) {
    // Lazy initialize pixel buffer on first use
    if (!_pixelTransferBuffer) {
        _initPixelBuffer();
    }
    
    // Apply camera transform if active
    vi2d finalPos = pos;
    if (Camera::IsActive()) {
        vf2d transformed = Camera::ToScreenSpace({(float)pos.x, (float)pos.y});
        finalPos = {(int)transformed.x, (int)transformed.y};
    }
    
    // Write directly to pixel buffer
    if (finalPos.x >= 0 && finalPos.x < (int)_pixelBufferWidth && 
        finalPos.y >= 0 && finalPos.y < (int)_pixelBufferHeight) {
        uint32_t index = finalPos.y * _pixelBufferWidth + finalPos.x;
        // Pack RGBA into uint32 (R8G8B8A8)
        _pixelBufferData[index] = color.r | (color.g << 8) | (color.b << 16) | (color.a << 24);
        _pixelsDirty = true;
    }
}