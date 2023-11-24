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


class Render2D {
public:
    [[maybe_unused]] static void DrawLine(vf2d start, vf2d end, float width, Color color) { get()._drawLine(start, end, width, color); };

    [[maybe_unused]] static void DrawRectangle(vf2d pos, vf2d size, Color color) { get()._drawRectangle(pos, size, color); };
    [[maybe_unused]] static void DrawRectangleRounded(vf2d pos, vf2d size, float radius, Color color) { get()._drawRectangleRounded(pos, size, radius, color); };
    [[maybe_unused]] static void DrawCircle(vf2d pos, float radius, Color color) { get()._drawCircle(pos, radius, color); };
    [[maybe_unused]] static void DrawCircleSector(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) { get()._drawCircleSector(center, radius, startAngle, endAngle, segments, color); };

    [[maybe_unused]] static void DrawRectangleFilled(vf2d pos, vf2d size, Color color) { get()._drawRectangleFilled(pos, size, color); };
    [[maybe_unused]] static void DrawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color) { get()._drawRectangleRoundedFilled(pos, size, radius, color); };
    [[maybe_unused]] static void DrawCircleFilled(vf2d pos, float radius, Color color) { get()._drawCircleFilled(pos, radius, color); };
    [[maybe_unused]] static void DrawCircleSectorFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color) { get()._drawCircleSectorFilled(center, radius, startAngle, endAngle, segments, color); };

    [[maybe_unused]] static void DrawTexture(Texture texture, vf2d pos, vf2d size, Color color = WHITE) { get()._drawTexture(texture, pos, size, color); };
    [[maybe_unused]] static void DrawTexturePart(Texture texture, vf2d pos, vf2d size, Rectangle src, Color color = WHITE) { get()._drawTexturePart(texture, pos, size, src, color); };

    [[maybe_unused]] static void BeginScissorMode(Rectangle area) { get()._beginScissorMode(area); };
    [[maybe_unused]] static void EndScissorMode() { get()._endScissorMode(); };

    [[maybe_unused]] static void BeginMode2D() { get()._beginMode2D(); };
    [[maybe_unused]] static void EndMode2D() { get()._endMode2D(); };

private:
    void _drawLine(vf2d start, vf2d end, float width, Color color);

    void _drawRectangle(vf2d pos, vf2d size, Color color);
    void _drawRectangleRounded(vf2d pos, vf2d size, float radius, Color color);
    void _drawCircle(vf2d pos, float radius, Color color);
    void _drawCircleSector(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color);

    void _drawRectangleFilled(vf2d pos, vf2d size, Color color);
    void _drawRectangleRoundedFilled(vf2d pos, vf2d size, float radius, Color color);
    void _drawCircleFilled(vf2d pos, float radius, Color color);
    void _drawCircleSectorFilled(vf2d center, float radius, float startAngle, float endAngle, int segments, Color color);

    void _drawTexture(Texture texture, vf2d pos, vf2d size, Color color = WHITE);
    void _drawTexturePart(Texture texture, vf2d pos, vf2d size, Rectangle src, Color color = WHITE);

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