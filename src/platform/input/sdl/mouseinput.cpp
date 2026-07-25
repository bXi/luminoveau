// SDL-backend mouse-position query. Uses SDL_GetGlobalMouseState + the window's screen
// position to compute window-relative logical coords, then applies LUMI_USE_PHYSICAL_PIXELS
// scaling if requested.

#include "platform/input/mouseinput_backend.h"

#include "SDL3/SDL.h"

#include "platform/window/window.h"

vf2d PlatformInputBackend::_getMousePosition() {
    int windowX = 0;
    int windowY = 0;
    SDL_GetWindowPosition(Window::GetWindow(), &windowX, &windowY);

    float absMouseX = 0;
    float absMouseY = 0;
    SDL_GetGlobalMouseState(&absMouseX, &absMouseY);

    // Global + window position are both in logical points (OS units); subtract for
    // window-relative logical points. NOTE: do NOT divide by Window::GetScale() here — that is the
    // SetScale render-divisor and Input::_getMousePosition() already applies it. Dividing here too
    // double-scaled the cursor (mouse /N^2 vs GetWidth /N), which broke mouse coords under SetScale.
    float relX = (absMouseX - (float)windowX);
    float relY = (absMouseY - (float)windowY);

#ifdef LUMI_USE_PHYSICAL_PIXELS
    float displayScale = Window::GetDisplayScale();
    relX *= displayScale;
    relY *= displayScale;
#endif

    return { relX, relY };
}

uint32_t PlatformInputBackend::_getMouseButtons() {
    return (uint32_t)SDL_GetMouseState(nullptr, nullptr);
}
