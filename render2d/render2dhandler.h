#pragma once

#include <memory>
#include <string>
#include <optional>

#include "SDL3/SDL.h"

#include "texture/texturehandler.h"
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

class Render2D {
public:
    [[maybe_unused]] static void DrawPixel(vi2d pos, Color color) { get()._drawPixel(pos, color); };
    [[maybe_unused]] static void DrawLine(vf2d start, vf2d end, Color color) { get()._drawLine(start, end, color); };
    [[maybe_unused]] static void DrawThickLine(vf2d start, vf2d end, Color color, float width) { get()._drawThickLine(start, end, color, width); };


    [[maybe_unused]] static void DrawTriangle(vf2d v1, vf2d v2, vf2d v3, Color color) { get()._drawTriangle(v1, v2, v3, color); };
    [[maybe_unused]] static void DrawRectangle(vf2d pos, vf2d size, Color color) { get()._drawRectangle(pos, size, color); };
    [[maybe_unused]] static void DrawRectangleRounded(vf2d pos, vf2d size, float radius, Color color) { get()._drawRectangleRounded(pos, size, radius, color); };
    [[maybe_unused]] static void DrawCircle(vf2d pos, float radius, Color color) { get()._drawCircle(pos, radius, color); };
    [[maybe_unused]] static void DrawEllipse(vf2d center, float radiusX, float radiusY, Color color) { get()._drawEllipse(center, radiusX, radiusY, color); };
    [[maybe_unused]] static void DrawArc(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) { get()._drawArc(center, radius, startAngle, endAngle, segments, color); };
    [[maybe_unused]] static void DrawTriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color) { get()._drawTriangleFilled(v1, v2, v3, color); };

    [[maybe_unused]] static void DrawRectangleFilled(vf2d pos, vf2d size, Color color) { get()._drawRectangleFilled(pos, size, color); };
    [[maybe_unused]] static void DrawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color) { get()._drawRectangleRoundedFilled(pos, size, radius, color); };
    [[maybe_unused]] static void DrawCircleFilled(vf2d pos, float radius, Color color) { get()._drawCircleFilled(pos, radius, color); };
    [[maybe_unused]] static void DrawEllipseFilled(vf2d center, float radiusX, float radiusY, Color color) { get()._drawEllipseFilled(center, radiusX, radiusY, color); };
    [[maybe_unused]] static void DrawArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) { get()._drawArcFilled(center, radius, startAngle, endAngle, segments, color); };

    [[maybe_unused]] static void DrawTexture(Texture texture, vf2d pos, vf2d size, Color color = WHITE) { get()._drawTexture(texture, pos, size, color); };
    [[maybe_unused]] static void DrawTexturePart(Texture texture, vf2d pos, vf2d size, Rectangle src, Color color = WHITE) { get()._drawTexturePart(texture, pos, size, src, color); };

    [[maybe_unused]] static void DrawTextureMode7(Texture texture, vf2d pos, vf2d size, Mode7Parameters m7p, Color color = WHITE) { get()._drawTextureMode7(texture, pos, size, m7p, color); };

    [[maybe_unused]] static void BeginScissorMode(Rectangle area) { get()._beginScissorMode(area); };
    [[maybe_unused]] static void EndScissorMode() { get()._endScissorMode(); };

    [[maybe_unused]] static void BeginMode2D() { get()._beginMode2D(); };
    [[maybe_unused]] static void EndMode2D() { get()._endMode2D(); };

private:

    void _drawPixel(vi2d pos, Color color);
    void _drawLine(vf2d start, vf2d end, Color color);
    void _drawThickLine(vf2d start, vf2d end, Color color, float width);

    void _drawTriangle(vf2d v1, vf2d v2, vf2d v3, Color color);
    void _drawRectangle(vf2d pos, vf2d size, Color color);
    void _drawRectangleRounded(vf2d pos, vf2d size, float radius, Color color);
    void _drawCircle(vf2d pos, float radius, Color color);
    void _drawEllipse(vf2d center, float radiusX, float radiusY, Color color);
    void _drawArc(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color);

    void _drawTriangleFilled(vf2d v1, vf2d v2, vf2d v3, Color color);
    void _drawRectangleFilled(vf2d pos, vf2d size, Color color);
    void _drawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color);
    void _drawCircleFilled(vf2d pos, float radius, Color color);
    void _drawEllipseFilled(vf2d center, float radiusX, float radiusY, Color color);
    void _drawArcFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color);

    void _drawTexture(Texture texture, vf2d pos, vf2d size, Color color = WHITE);
    void _drawTexturePart(Texture texture, vf2d pos, vf2d size, Rectangle src, Color color = WHITE);

    void _drawTextureMode7(Texture texture, vf2d pos, vf2d size, Mode7Parameters m7p, Color color = WHITE);

    void _beginScissorMode(Rectangle area);
    void _endScissorMode();

    void _beginMode2D();
    void _endMode2D();

    Rectangle _doCamera(vf2d pos, vf2d size);
    SDL_Renderer* renderer = nullptr;

//Singleton part
public:
    Render2D(const Render2D&) = delete;
    static Render2D& get() { static Render2D instance; return instance; }
private:
    Render2D() {
        renderer = Window::GetRenderer();
    }
};