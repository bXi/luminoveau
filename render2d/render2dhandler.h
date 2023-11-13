#pragma once

#include <memory>
#include <string>
#include <optional>

#include "SDL3/SDL.h"

#include "texture/texturehandler.h"
#include "window/windowhandler.h"

#include "utils/vectors.h"
#include "utils/colors.h"

class Render2D {
public:
    static void DrawRectangle(vf2d pos, vf2d size, Color color)
    {
        get()._drawRectangle(pos, size, color);
    };

    static void DrawTexture(Texture texture, vf2d pos, vf2d size)
    {
        get()._drawTexture(texture, pos, size);
    };

private:




    void _drawRectangle(vf2d pos, vf2d size, Color color);
    void _drawTexture(Texture texture, vf2d pos, vf2d size);


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