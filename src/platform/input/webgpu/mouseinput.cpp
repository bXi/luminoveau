// WebGPU-backend mouse-position query.
// - Emscripten path: hooks document-level mouse events directly via JS so the coordinate
//   space tracks #canvas's bounding rect (which is what the WebGPU swapchain renders to).
//   SDL3-emscripten's own SDL_GetMouseState has drifted from canvas pixels in past releases.
// - Native (Dawn) path: SDL_GetMouseState scaled from window-logical to canvas-pixel coords.
// Both paths finish with Renderer::CanvasToLogical so callers see engine logical units.

#include "platform/input/mouseinput_backend.h"

#include "platform/window/window.h"
#include "renderer/renderer.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include "SDL3/SDL.h"
#endif

vf2d PlatformInputBackend::GetMousePosition() {
#ifdef __EMSCRIPTEN__
    static bool s_lumiMouseInitialized = false;
    if (!s_lumiMouseInitialized) {
        s_lumiMouseInitialized = true;
        EM_ASM({
            window._lumiLastMouseEvt = null;
            var listener = function(e) { window._lumiLastMouseEvt = e; };
            window.addEventListener('mousemove', listener, true);
            window.addEventListener('mousedown', listener, true);
            window.addEventListener('mouseup',   listener, true);
        });
    }
    static float s_canvasMouseX = 0.0f, s_canvasMouseY = 0.0f;
    EM_ASM({
        var c = document.querySelector('#canvas');
        if (!c || !window._lumiLastMouseEvt) return;
        var r = c.getBoundingClientRect();
        var px = window._lumiLastMouseEvt.clientX - r.left;
        var py = window._lumiLastMouseEvt.clientY - r.top;
        var sx = r.width  > 0 ? c.width  / r.width  : 1.0;
        var sy = r.height > 0 ? c.height / r.height : 1.0;
        setValue($0, px * sx, 'float');
        setValue($1, py * sy, 'float');
    }, &s_canvasMouseX, &s_canvasMouseY);
    return Renderer::CanvasToLogical(s_canvasMouseX, s_canvasMouseY);
#else
    float cx = 0.0f, cy = 0.0f;
    SDL_GetMouseState(&cx, &cy);
    int sdlW = 0, sdlH = 0;
    SDL_GetWindowSize(Window::GetWindow(), &sdlW, &sdlH);
    const uint32_t canvasW = Renderer::GetCanvasWidth();
    const uint32_t canvasH = Renderer::GetCanvasHeight();
    if (sdlW > 0 && sdlH > 0 && canvasW > 0 && canvasH > 0) {
        cx = cx * (float)canvasW / (float)sdlW;
        cy = cy * (float)canvasH / (float)sdlH;
    }
    return Renderer::CanvasToLogical(cx, cy);
#endif
}
