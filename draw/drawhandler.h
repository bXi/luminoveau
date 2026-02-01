#pragma once

#include <memory>
#include <string>
#include <optional>

#include "SDL3/SDL.h"

#include "assethandler/assethandler.h"
#include "window/windowhandler.h"
#include "renderer/rendererhandler.h"

#include "utils/vectors.h"
#include "utils/colors.h"
#include "utils/camera.h"

#include "utils/constants.h"

#include "assettypes/effect.h"

#include <functional>
#include <vector>

struct Mode7Parameters {
    int h = 0;
    int v = 0;
    int x0 = 0;
    int y0 = 0;
    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;
    int snesScreenWidth = 256;
    int snesScreenHeight = 224;
};

/**
 * @brief Provides functionality for 2D rendering operations.
 */
class Draw {
    using TextureType = Texture;
public:
    /**
     * @brief Draws a pixel at the specified position with the given color.
     *
     * @param pos The position of the pixel.
     * @param color The color of the pixel.
     */
    static void Pixel(vi2d pos, Color color) { get()._drawPixel(pos, color); };

    /**
     * @brief Flushes queued pixels to the screen. Called automatically before other draw operations.
     * Should be called at the end of the frame to ensure all pixels are rendered.
     * Can also be called manually for explicit control over layering.
     */
    static void FlushPixels() { get()._flushPixels(); };

    /**
     * @brief Draws a line from the start to the end position with the given color.
     *
     * @param start The start position of the line.
     * @param end The end position of the line.
     * @param color The color of the line.
     */
    static void Line(vf2d start, vf2d end, Color color) { get()._drawLine(start, end, color); };

    /**
     * @brief Draws a thick line from the start to the end position with the given color and width.
     *
     * @param start The start position of the line.
     * @param end The end position of the line.
     * @param color The color of the line.
     * @param width The width of the line.
     */
    static void ThickLine(vf2d start, vf2d end, Color color, float width) { get()._drawThickLine(start, end, color, width); };

    /**
     * @brief Draws a triangle with vertices v1, v2, and v3, filled with the given color.
     *
     * @param v1 The first vertex of the triangle.
     * @param v2 The second vertex of the triangle.
     * @param v3 The third vertex of the triangle.
     * @param color The color of the triangle.
     */
    static void Triangle(vf2d v1, vf2d v2, vf2d v3, Color color) { get()._drawTriangle(v1, v2, v3, color); };

    /**
     * @brief Draws a rectangle at the specified position with the given size and color.
     *
     * @param pos The position of the rectangle.
     * @param size The size of the rectangle.
     * @param color The color of the rectangle.
     */
    static void Rectangle(vf2d pos, vf2d size, Color color) { get()._drawRectangle(pos, size, color); };

    /**
     * @brief Draws a rounded rectangle at the specified position with the given size, radius, and color.
     *
     * @param pos The position of the rectangle.
     * @param size The size of the rectangle.
     * @param radius The radius of the rounded corners.
     * @param color The color of the rectangle.
     */
    static void RectangleRounded(vf2d pos, vf2d size, float radius, Color color) {
        get()._drawRectangleRounded(pos, size, radius, color);
    };

    /**
     * @brief Draws a circle at the specified position with the given radius and color.
     *
     * @param pos The center position of the circle.
     * @param radius The radius of the circle.
     * @param color The color of the circle.
     * @param segments The amount of segments to use. (Defaults to 32)
     */
    static void Circle(vf2d pos, float radius, Color color, int segments = 32) { get()._drawCircle(pos, radius, color, segments); };

    /**
     * @brief Draws an ellipse at the specified center position with the given radii and color.
     *
     * @param center The center position of the ellipse.
     * @param radiusX The radius of the ellipse along the X-axis.
     * @param radiusY The radius of the ellipse along the Y-axis.
     * @param color The color of the ellipse.
     */
    static void Ellipse(vf2d center, float radiusX, float radiusY, Color color) {
        get()._drawEllipse(center, radiusX, radiusY, color);
    };

    /**
     * @brief Draws an arc at the specified center position with the given radius, angles, number of segments, and color.
     *
     * @param center The center position of the arc.
     * @param radius The radius of the arc.
     * @param startAngle The starting angle of the arc in radians.
     * @param endAngle The ending angle of the arc in radians.
     * @param segments The number of segments to approximate the arc.
     * @param color The color of the arc.
     */
    static void Arc(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
        get()._drawArc(center, radius, startAngle, endAngle, segments, color);
    };

    /**
     * @brief Draws a filled triangle with vertices v1, v2, and v3, filled with the given color.
     *
     * @param v1 The first vertex of the triangle.
     * @param v2 The second vertex of the triangle.
     * @param v3 The third vertex of the triangle.
     * @param color The color of the triangle.
     */
    static void TriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color) { get()._drawTriangleFilled(v1, v2, v3, color); };

    /**
     * @brief Draws a filled rectangle at the specified position with the given size and color.
     *
     * @param pos The position of the rectangle.
     * @param size The size of the rectangle.
     * @param color The color of the rectangle.
     */
    static void RectangleFilled(vf2d pos, vf2d size, Color color) { get()._drawRectangleFilled(pos, size, color); };

    /**
     * @brief Draws a filled rounded rectangle at the specified position with the given size, radius, and color.
     *
     * @param pos The position of the rectangle.
     * @param size The size of the rectangle.
     * @param radius The radius of the rounded corners.
     * @param color The color of the rectangle.
     */
    static void RectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color) {
        get()._drawRectangleRoundedFilled(pos, size, radius, color);
    };

    /**
     * @brief Draws a filled circle at the specified position with the given radius and color.
     *
     * @param pos The center position of the circle.
     * @param radius The radius of the circle.
     * @param color The color of the circle.
     */
    static void CircleFilled(vf2d pos, float radius, Color color) { get()._drawCircleFilled(pos, radius, color); };

    /**
     * @brief Draws a filled ellipse at the specified center position with the given radii and color.
     *
     * @param center The center position of the ellipse.
     * @param radiusX The radius of the ellipse along the X-axis.
     * @param radiusY The radius of the ellipse along the Y-axis.
     * @param color The color of the ellipse.
     */
    static void EllipseFilled(vf2d center, float radiusX, float radiusY, Color color) {
        get()._drawEllipseFilled(center, radiusX, radiusY, color);
    };

    /**
     * @brief Draws a filled arc at the specified center position with the given radius, angles, number of segments, and color.
     *
     * @param center The center position of the arc.
     * @param radius The radius of the arc.
     * @param startAngle The starting angle of the arc in radians.
     * @param endAngle The ending angle of the arc in radians.
     * @param segments The number of segments to approximate the arc.
     * @param color The color of the arc.
     */
    static void ArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
        get()._drawArcFilled(center, radius, startAngle, endAngle, segments, color);
    };

    /**
     * @brief Draws a texture at the specified position with the given size and color.
     *
     * @param texture The texture to draw.
     * @param pos The position at which to draw the texture.
     * @param size The size of the drawn texture.
     * @param color The color of the drawn texture.
     */
    static void Texture(Texture texture, vf2d pos, vf2d size, Color color = WHITE) {
        get()._drawTexture(texture, pos, size, color);
    };

    /**
     * @brief Draws a part of a texture at the specified position with the given size, source rectangle, and color.
     *
     * @param texture The texture to draw.
     * @param pos The position at which to draw the texture.
     * @param size The size of the drawn texture.
     * @param src The source rectangle defining which part of the texture to draw.
     * @param color The color of the drawn texture.
     */
    static void TexturePart(TextureType texture, vf2d pos, vf2d size, rectf src, Color color = WHITE) {
        get()._drawTexturePart(texture, pos, size, src, color);
    };

    /**
     * @brief Draws a texture at the specified position with the given size and color.
     *
     * @param texture The texture to draw.
     * @param pos The position at which to draw the texture.
     * @param size The size of the drawn texture.
     * @param color The color of the drawn texture.
     * @param angle The angle of rotation.
     * @param pivot Where on the texture to rotate in normalized space.
     */
    static void RotatedTexture(TextureType texture, vf2d pos, vf2d size, float angle, vf2d pivot = {0.5f, 0.5f}, Color color = WHITE) {
        get()._drawRotatedTexture(texture, pos, size, angle, pivot, color);
    };

    /**
     * @brief Draws a part of a texture at the specified position with the given size, source rectangle, and color.
     *
     * @param texture The texture to draw.
     * @param pos The position at which to draw the texture.
     * @param size The size of the drawn texture.
     * @param src The source rectangle defining which part of the texture to draw.
     * @param color The color of the drawn texture.
     * @param angle The angle of rotation.
     * @param pivot Where on the texture to rotate in normalized space.
     */
    static void RotatedTexturePart(TextureType texture, vf2d pos, vf2d size, rectf src, float angle, vf2d pivot, Color color = WHITE) {
        get()._drawRotatedTexturePart(texture, pos, size, src, angle, pivot, color);
    };

    /**
     * @brief Draws a texture with Mode 7 affine transformation.
     * Applies a single affine transform to the entire texture using SNES-style Mode 7 parameters.
     *
     * @param texture The texture to draw.
     * @param pos The position at which to draw the texture.
     * @param size The size of the drawn texture.
     * @param params Mode 7 transformation parameters (matrix, origin, scroll).
     * @param color The color tint to apply.
     */
    static void Mode7Texture(TextureType texture, vf2d pos, vf2d size, const Mode7Parameters& params, Color color = WHITE) {
        get()._drawMode7Texture(texture, pos, size, params, color);
    };

    /**
     * @brief Draws a texture with per-scanline Mode 7 transformation for perspective effects.
     * Each horizontal scanline can have different transformation parameters, enabling
     * classic SNES-style perspective effects like roads, floors, etc.
     *
     * @param texture The texture to draw.
     * @param pos The position at which to draw the texture.
     * @param size The size of the drawn texture.
     * @param getParamsForLine Callback that returns Mode7Parameters for each scanline.
     * @param color The color tint to apply.
     * @param scanlineStep Process every Nth scanline (default 1). Higher values = fewer strips, better performance.
     */
    static void Mode7TextureScanline(TextureType texture, vf2d pos, vf2d size, 
        std::function<Mode7Parameters(int)> getParamsForLine, 
        Color color = WHITE, int scanlineStep = 1) {
        get()._drawMode7TextureScanline(texture, pos, size, getParamsForLine, color, scanlineStep);
    };

    /**
     * @brief Begins scissor mode with the specified area.
     *
     * @param area The area to apply scissor mode.
     */
    static void SetScissorMode(const rectf& area) { get()._setScissorMode(area); };

    /**
     * @brief Begins 2D rendering mode.
     */
    static void BeginMode2D() { get()._beginMode2D(); };

    /**
     * @brief Ends 2D rendering mode.
     */
    static void EndMode2D() { get()._endMode2D(); };

    static void ResetTargetRenderPass() { get()._resetTargetRenderPass(); }

    static void SetTargetRenderPass(const std::string& newTargetRenderPass) { get()._setTargetRenderPass(newTargetRenderPass); }

    static std::string GetTargetRenderPass() { return get()._getTargetRenderPass(); }

    /**
     * @brief Sets the active effect for subsequent draw calls.
     * Replaces any previously set effect.
     * 
     * @param effect The effect to apply
     */
    static void SetEffect(const EffectAsset& effect) { get()._setEffect(effect); }

    /**
     * @brief Adds an additional effect to the effect stack.
     * Effects are applied in the order they were added.
     * 
     * @param effect The effect to add to the stack
     */
    static void AddEffect(const EffectAsset& effect) { get()._addEffect(effect); }

    /**
     * @brief Removes a specific effect from the effect stack.
     * 
     * @param effect The effect to remove
     */
    static void RemoveEffect(const EffectAsset& effect) { get()._removeEffect(effect); }

    /**
     * @brief Clears all active effects.
     */
    static void ClearEffects() { get()._clearEffects(); }

    /**
     * @brief Gets the current effect stack (for internal use by Renderer).
     */
    static const std::vector<EffectAsset>& GetEffectStack() { return get()._effectStack; }

private:

    void _drawPixel(const vi2d& pos, Color color);

    void _drawLine(vf2d start, vf2d end, Color color);

    void _drawThickLine(vf2d start, vf2d end, Color color, float width);

    void _drawTriangle(vf2d v1, vf2d v2, vf2d v3, Color color);

    void _drawRectangle(const vf2d& pos, const vf2d& size, Color color);

    void _drawRectangleRounded(vf2d pos, const vf2d& size, float radius, Color color);

    void _drawCircle(vf2d pos, float radius, Color color, int segments);

    void _drawEllipse(vf2d center, float radiusX, float radiusY, Color color);

    void _drawArc(const vf2d& center, float radius, float startAngle, float endAngle, int segments, Color color);

    void _drawTriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color);

    void _drawRectangleFilled(vf2d pos, vf2d size, Color color);

    void _drawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color);

    void _drawCircleFilled(vf2d pos, float radius, Color color);

    void _drawEllipseFilled(vf2d center, float radiusX, float radiusY, Color color);

    void _drawArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color);

    void _drawTexture(TextureType texture, const vf2d& pos, const vf2d& size, Color color = WHITE);

    void _drawTexturePart(TextureType texture, const vf2d& pos, const vf2d& size, const rectf& src, Color color = WHITE);

    void _drawRotatedTexture(TextureType texture, vf2d pos, vf2d size, float angle, const vf2d& pivot,Color color = WHITE);

    void _drawRotatedTexturePart(TextureType texture, vf2d pos, vf2d size, const rectf& src, float angle, const vf2d& pivot,Color color = WHITE);

    void _drawMode7Texture(TextureType texture, vf2d pos, vf2d size, const Mode7Parameters& params, Color color);

    void _drawMode7TextureScanline(TextureType texture, vf2d pos, vf2d size, std::function<Mode7Parameters(int)> getParamsForLine, Color color, int scanlineStep);

    void _setScissorMode(const rectf& area);

    void _beginMode2D();

    void _endMode2D();

    rectf _doCamera(const vf2d& pos, const vf2d& size);

    SDL_Renderer *renderer = nullptr;

    void _resetTargetRenderPass() { get()._setTargetRenderPass("2dsprites"); }

    void _setTargetRenderPass(const std::string& newTargetRenderPass) { get()._targetRenderPass = newTargetRenderPass; }

    std::string _getTargetRenderPass() { return get()._targetRenderPass; }

    std::string _targetRenderPass = "2dsprites";

    // Effect system
    std::vector<EffectAsset> _effectStack;
    
    void _setEffect(const EffectAsset& effect);
    void _addEffect(const EffectAsset& effect);
    void _removeEffect(const EffectAsset& effect);
    void _clearEffects();

    // Pixel buffer system
    TextureAsset _pixelTexture;
    SDL_GPUTransferBuffer* _pixelTransferBuffer = nullptr;
    bool _pixelsDirty = false;
    uint32_t _pixelBufferWidth = 0;
    uint32_t _pixelBufferHeight = 0;
    std::vector<uint32_t> _pixelBufferData;  // RGBA8888 format
    
    void _initPixelBuffer();
    void _flushPixels();
    void _cleanupPixelBuffer();

//Singleton part
public:
    Draw(const Draw &) = delete;

    static Draw &get() {
        static Draw instance;
        return instance;
    }

private:
    Draw() {

    }
    
    ~Draw() {
        _cleanupPixelBuffer();
    }
};