#pragma once

#include <memory>
#include <string>
#include <optional>

#include "SDL3/SDL.h"

#include "assethandler/assethandler.h"
#include "window/windowhandler.h"

#include "utils/vectors.h"
#include "utils/colors.h"
#include "utils/camera.h"

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
class Render2D {
public:
    /**
     * @brief Draws a pixel at the specified position with the given color.
     *
     * @param pos The position of the pixel.
     * @param color The color of the pixel.
     */
    static void DrawPixel(vi2d pos, Color color) { get()._drawPixel(pos, color); };

    /**
     * @brief Draws a line from the start to the end position with the given color.
     *
     * @param start The start position of the line.
     * @param end The end position of the line.
     * @param color The color of the line.
     */
    static void DrawLine(vf2d start, vf2d end, Color color) { get()._drawLine(start, end, color); };

    /**
     * @brief Draws a thick line from the start to the end position with the given color and width.
     *
     * @param start The start position of the line.
     * @param end The end position of the line.
     * @param color The color of the line.
     * @param width The width of the line.
     */
    static void DrawThickLine(vf2d start, vf2d end, Color color, float width) { get()._drawThickLine(start, end, color, width); };

    /**
     * @brief Draws a triangle with vertices v1, v2, and v3, filled with the given color.
     *
     * @param v1 The first vertex of the triangle.
     * @param v2 The second vertex of the triangle.
     * @param v3 The third vertex of the triangle.
     * @param color The color of the triangle.
     */
    static void DrawTriangle(vf2d v1, vf2d v2, vf2d v3, Color color) { get()._drawTriangle(v1, v2, v3, color); };

    /**
     * @brief Draws a rectangle at the specified position with the given size and color.
     *
     * @param pos The position of the rectangle.
     * @param size The size of the rectangle.
     * @param color The color of the rectangle.
     */
    static void DrawRectangle(vf2d pos, vf2d size, Color color) { get()._drawRectangle(pos, size, color); };

    /**
     * @brief Draws a rounded rectangle at the specified position with the given size, radius, and color.
     *
     * @param pos The position of the rectangle.
     * @param size The size of the rectangle.
     * @param radius The radius of the rounded corners.
     * @param color The color of the rectangle.
     */
    static void DrawRectangleRounded(vf2d pos, vf2d size, float radius, Color color) {
        get()._drawRectangleRounded(pos, size, radius, color);
    };

    /**
     * @brief Draws a circle at the specified position with the given radius and color.
     *
     * @param pos The center position of the circle.
     * @param radius The radius of the circle.
     * @param color The color of the circle.
     */
    static void DrawCircle(vf2d pos, float radius, Color color) { get()._drawCircle(pos, radius, color); };

    /**
     * @brief Draws an ellipse at the specified center position with the given radii and color.
     *
     * @param center The center position of the ellipse.
     * @param radiusX The radius of the ellipse along the X-axis.
     * @param radiusY The radius of the ellipse along the Y-axis.
     * @param color The color of the ellipse.
     */
    static void DrawEllipse(vf2d center, float radiusX, float radiusY, Color color) {
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
    static void DrawArc(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
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
    static void DrawTriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color) { get()._drawTriangleFilled(v1, v2, v3, color); };

    /**
     * @brief Draws a filled rectangle at the specified position with the given size and color.
     *
     * @param pos The position of the rectangle.
     * @param size The size of the rectangle.
     * @param color The color of the rectangle.
     */
    static void DrawRectangleFilled(vf2d pos, vf2d size, Color color) { get()._drawRectangleFilled(pos, size, color); };

    /**
     * @brief Draws a filled rounded rectangle at the specified position with the given size, radius, and color.
     *
     * @param pos The position of the rectangle.
     * @param size The size of the rectangle.
     * @param radius The radius of the rounded corners.
     * @param color The color of the rectangle.
     */
    static void DrawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color) {
        get()._drawRectangleRoundedFilled(pos, size, radius, color);
    };

    /**
     * @brief Draws a filled circle at the specified position with the given radius and color.
     *
     * @param pos The center position of the circle.
     * @param radius The radius of the circle.
     * @param color The color of the circle.
     */
    static void DrawCircleFilled(vf2d pos, float radius, Color color) { get()._drawCircleFilled(pos, radius, color); };

    /**
     * @brief Draws a filled ellipse at the specified center position with the given radii and color.
     *
     * @param center The center position of the ellipse.
     * @param radiusX The radius of the ellipse along the X-axis.
     * @param radiusY The radius of the ellipse along the Y-axis.
     * @param color The color of the ellipse.
     */
    static void DrawEllipseFilled(vf2d center, float radiusX, float radiusY, Color color) {
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
    static void DrawArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) {
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
    static void DrawTexture(Texture texture, vf2d pos, vf2d size, Color color = WHITE) {
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
    static void DrawTexturePart(Texture texture, vf2d pos, vf2d size, rectf src, Color color = WHITE) {
        get()._drawTexturePart(texture, pos, size, src, color);
    };

    /**
     * @brief Draws a texture using Mode7 transformation at the specified position with the given size, transformation parameters, and color.
     *
     * @param texture The texture to draw.
     * @param pos The position at which to draw the texture.
     * @param size The size of the drawn texture.
     * @param m7p The Mode7 transformation parameters.
     * @param color The color of the drawn texture.
     */
    static void DrawTextureMode7(Texture texture, vf2d pos, vf2d size, Mode7Parameters m7p, Color color = WHITE) {
        get()._drawTextureMode7(texture, pos, size, m7p, color);
    };

    /**
     * @brief Begins scissor mode with the specified area.
     *
     * @param area The area to apply scissor mode.
     */
    static void BeginScissorMode(rectf area) { get()._beginScissorMode(area); };

    /**
     * @brief Ends scissor mode.
     */
    static void EndScissorMode() { get()._endScissorMode(); };

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

private:

    void _drawPixel(vi2d pos, Color color);

    void _drawLine(vf2d start, vf2d end, Color color);

    void _drawThickLine(vf2d start, vf2d end, Color color, float width);

    void _drawTriangle(vf2d v1, vf2d v2, vf2d v3, Color color);

    void _drawRectangle(vf2d pos, vf2d size, Color color);

    void _drawRectangleRounded(vf2d pos, const vf2d& size, float radius, Color color);

    void _drawCircle(vf2d pos, float radius, Color color);

    void _drawEllipse(vf2d center, float radiusX, float radiusY, Color color);

    void _drawArc(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color);

    void _drawTriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color);

    void _drawRectangleFilled(vf2d pos, vf2d size, Color color);

    void _drawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color);

    void _drawCircleFilled(vf2d pos, float radius, Color color);

    void _drawEllipseFilled(vf2d center, float radiusX, float radiusY, Color color);

    void _drawArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color);

    void _drawTexture(Texture texture, vf2d pos, const vf2d& size, Color color = WHITE);

    void _drawTexturePart(Texture texture, const vf2d& pos, const vf2d& size, const rectf& src, Color color = WHITE);

    void _drawTextureMode7(Texture texture, vf2d pos, vf2d size, Mode7Parameters m7p, Color color = WHITE);

    void _beginScissorMode(rectf area);

    void _endScissorMode();

    void _beginMode2D();

    void _endMode2D();

    rectf _doCamera(const vf2d& pos, const vf2d& size);

    SDL_Renderer *renderer = nullptr;

    void _resetTargetRenderPass() { get()._setTargetRenderPass("2dsprites"); }

    void _setTargetRenderPass(const std::string& newTargetRenderPass) { get()._targetRenderPass = newTargetRenderPass; }

    std::string _targetRenderPass = "2dsprites";

//Singleton part
public:
    Render2D(const Render2D &) = delete;

    static Render2D &get() {
        static Render2D instance;
        return instance;
    }

private:
    Render2D() {
        renderer = Window::GetRenderer();
    }
};