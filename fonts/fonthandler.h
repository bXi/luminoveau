#pragma once

#include <unordered_map>
//*
#include "configuration/configuration.h"
#include "window/windowhandler.h"

#include "utils/helpers.h"
#include "utils/colors.h"

#include "SDL3_ttf/SDL_ttf.h"

struct Font
{
    TTF_Font* font = nullptr;
};


class Fonts {
public:
    static Font GetFont(const char* fileName, const int fontSize)
    {
        return get()._getFont(fileName, fontSize);
    }

    static void DrawText(const char* fileName, const int fontSize, vf2d pos, const char* textToDraw, Color color)
    {
        get()._drawText(fileName, fontSize, pos, textToDraw, color);
    }

    static int MeasureText(const char *fileName, const int fontSize, std::string text) {
        return get()._measureText(fileName,fontSize, text);
    }

private:
    std::unordered_map<std::string, Font> _fonts;

    Font _getFont(const char* fileName, int fontSize);
    void _drawText(const char* fileName, const int fontSize, vf2d pos, std::string textToDraw, Color color);
    int _measureText(const char *fileName, const int fontSize, std::string text);
public:
    Fonts(const Fonts&) = delete;
    static Fonts& get() { static Fonts instance; return instance; }
private:
    Fonts() {
        TTF_Init();
    }
    ;
};

//*/;