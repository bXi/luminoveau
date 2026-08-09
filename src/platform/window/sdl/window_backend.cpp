// SDL-backend window helpers. Backend-neutral display-bounds query via SDL; the
// other hooks are no-ops because SDL's window/swapchain coordinates already match
// what window.cpp expects in the common path.

#include "platform/window/window_backend.h"

#include <SDL3/SDL.h>

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
    vf2d &outSize) {
#ifdef __3DS__
    // The 3DS renders to the fixed 400x240 top screen (citro3d SCREEN_LOGICAL_W/H). SDL's
    // window reports the requested InitWindow size, so override it or the game lays out for
    // the wrong resolution and the camera ortho (renderer/n3ds/init.cpp) mismatches the fb.
    outSize = { 400.0f, 240.0f };
    return true;
#else
    (void)outSize;
    return false;
#endif
}

bool WindowBackend::_getPhysicalSizeOverride(SDL_Window * /*window*/, WebGpuScaleMode /*scaleMode*/, vf2d &outSize) {
#ifdef __3DS__
    outSize = { 400.0f, 240.0f };
    return true;
#else
    (void)outSize;
    return false;
#endif
}
