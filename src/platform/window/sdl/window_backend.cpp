// SDL-backend window helpers. Backend-neutral display-bounds query via SDL; the
// other hooks are no-ops because SDL's window/swapchain coordinates already match
// what window.cpp expects in the common path.

#include "platform/window/window_backend.h"

#include <SDL3/SDL.h>

void WindowBackend::GetDisplayBounds(uint32_t& outW, uint32_t& outH) {
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(primary)) {
        // mode->pixel_density can be 0 on platforms without HiDPI info; fall back to 1.0.
        float density = (mode->pixel_density > 0.0f) ? mode->pixel_density : 1.0f;
        outW = (uint32_t)(mode->w * density);
        outH = (uint32_t)(mode->h * density);
    }
}

void WindowBackend::PostInit(SDL_Window*) {
    // SDL backend's swapchain dimensions already match SDL_GetWindowSizeInPixels;
    // no extra projection refresh needed before the first frame.
}

bool WindowBackend::HandleResize(int /*newWidth*/, int /*newHeight*/, WebGpuScaleMode /*scaleMode*/) {
    return false;  // Defer to the caller's standard _setSize() path.
}

bool WindowBackend::GetSizeOverride(SDL_Window* /*window*/, WebGpuScaleMode /*scaleMode*/,
                                    int /*webGpuRenderWidth*/, int /*webGpuRenderHeight*/,
                                    vf2d& /*outSize*/) {
    return false;
}

bool WindowBackend::GetPhysicalSizeOverride(SDL_Window* /*window*/, WebGpuScaleMode /*scaleMode*/, vf2d& /*outSize*/) {
    return false;
}
