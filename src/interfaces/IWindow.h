#pragma once

#include <string>
#include <functional>

#include "math/vectors.h"

struct SDL_Window;
union SDL_Event;

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual void initWindow(const std::string& title, int width, int height,
                            int scale = 1, unsigned int flags = 0) = 0;
    virtual void close() = 0;

    virtual SDL_Window* getWindow() = 0;

    virtual void setTitle(const std::string& title) = 0;
    virtual void setIcon(const std::string& filename) = 0;
    virtual void setCursor(const std::string& filename) = 0;

    virtual void setScale(int scale) = 0;
    virtual float getScale() = 0;

    virtual void setSize(int width, int height) = 0;
    virtual void setScaledSize(int width, int height, int scale = 0) = 0;

    virtual vf2d getSize(bool realSize = false) = 0;
    virtual vf2d getPhysicalSize() = 0;
    virtual float getDisplayScale() = 0;

    virtual void startFrame() = 0;
    virtual void endFrame() = 0;
    virtual void handleInput() = 0;

    virtual void toggleFullscreen() = 0;
    virtual bool isFullscreen() = 0;

    virtual bool shouldQuit() = 0;
    virtual void signalQuit() = 0;

    virtual double getRunTime() = 0;
    virtual double getFrameTime() = 0;
    virtual int getFPS(float ms = 400.f) = 0;

    virtual void takeScreenshot(const std::string& filename = "") = 0;
    virtual bool hasPendingScreenshot() = 0;
    virtual std::string getAndClearPendingScreenshot() = 0;

    virtual void setTextInputCallback(std::function<void(const char*)> callback) = 0;

    virtual void processEvent(SDL_Event* event) = 0;
};
