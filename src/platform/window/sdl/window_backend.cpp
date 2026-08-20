// SDL-backend window helpers. Backend-neutral display-bounds query via SDL; the
// other hooks are no-ops because SDL's window/swapchain coordinates already match
// what window.cpp expects in the common path.

#include "platform/window/window_backend.h"

#include <SDL3/SDL.h>

#ifdef SDL_PLATFORM_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

void WindowBackend::_getDisplayBounds(uint32_t &outW, uint32_t &outH) {
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    if (const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(primary)) {
        // mode->pixel_density can be 0 on platforms without HiDPI info; fall back to 1.0.
        float density = (mode->pixel_density > 0.0f) ? mode->pixel_density : 1.0f;
        outW          = (uint32_t)(mode->w * density);
        outH          = (uint32_t)(mode->h * density);
    }
}

void WindowBackend::_postInit(SDL_Window *) {
    // SDL backend's swapchain dimensions already match SDL_GetWindowSizeInPixels;
    // no extra projection refresh needed before the first frame.
}

bool WindowBackend::_handleResize(int /*newWidth*/, int /*newHeight*/, WebGpuScaleMode /*scaleMode*/) {
    return false; // Defer to the caller's standard _setSize() path.
}

bool WindowBackend::_getSizeOverride(SDL_Window * /*window*/, WebGpuScaleMode /*scaleMode*/,
    int /*webGpuRenderWidth*/, int /*webGpuRenderHeight*/,
    vf2d & /*outSize*/) {
    return false;
}

bool WindowBackend::_getPhysicalSizeOverride(SDL_Window * /*window*/, WebGpuScaleMode /*scaleMode*/, vf2d & /*outSize*/) {
    return false;
}

void WindowBackend::_setWindowBackgroundColor(SDL_Window *window, uint8_t r, uint8_t g, uint8_t b) {
#ifdef SDL_PLATFORM_WIN32
    if (!window)
        return;
    auto *hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd)
        return;
    // SDL windows share one window class, so this brush covers every current and future window.
    // The class owns the brush after SetClassLongPtr, so delete the brush we previously installed
    // (never the process default) when replacing it.
    static HBRUSH s_ownBrush = nullptr;
    HBRUSH        brush       = CreateSolidBrush(RGB(r, g, b));
    SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)brush);
    if (s_ownBrush)
        DeleteObject(s_ownBrush);
    s_ownBrush = brush;
#else
    (void)window;
    (void)r;
    (void)g;
    (void)b;
#endif
}
