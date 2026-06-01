// WebGPU/Emscripten window helpers. Mirrors what window.cpp used to do inside the
// LUMINOVEAU_WEBGPU_BACKEND / __EMSCRIPTEN__ ifdef ladders.

#include "platform/window/window_backend.h"

#include "renderer/renderer.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <SDL3/SDL.h>
#endif

void WindowBackend::GetDisplayBounds(uint32_t& outW, uint32_t& outH) {
#ifdef __EMSCRIPTEN__
    // Browser physical-pixel resolution (CSS screen size × devicePixelRatio).
    outW = (uint32_t)EM_ASM_INT(return Math.floor(window.screen.width  * (window.devicePixelRatio || 1)));
    outH = (uint32_t)EM_ASM_INT(return Math.floor(window.screen.height * (window.devicePixelRatio || 1)));
    if (outW == 0 || outH == 0) {
        outW = 3840u;
        outH = 2160u;
    }
#else
    // Dawn-on-desktop WebGPU build: SDL still owns the OS window.
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(primary)) {
        float density = (mode->pixel_density > 0.0f) ? mode->pixel_density : 1.0f;
        outW = (uint32_t)(mode->w * density);
        outH = (uint32_t)(mode->h * density);
    }
#endif
}

void WindowBackend::PostInit(SDL_Window* /*window*/) {
    // Update the camera to match the canvas size that WebGpuGpuBackend::init() set.
    // Do NOT route through Window::_setSize() — that would set _sizeDirty and force
    // a frame-0 _reset(), destroying pipelines that were just compiled and waited on.
    Renderer::UpdateCameraProjection();
}

bool WindowBackend::HandleResize(int /*newWidth*/, int /*newHeight*/, WebGpuScaleMode scaleMode) {
#ifdef __EMSCRIPTEN__
    // The browser already resized the canvas. Calling SDL_SetWindowSize here would DPI-scale
    // the logical dimensions onto canvas.style.*, inflating window.innerWidth and the
    // swapchain beyond the actual viewport. Just refresh the camera and let the next frame
    // re-derive sizes via _onResize.
    if (scaleMode == WebGpuScaleMode::Native) {
        Renderer::UpdateCameraProjection();
        return true;  // window.cpp will mark _sizeDirty and skip _setSize.
    }
    return false;
#else
    (void)scaleMode;
    return false;
#endif
}

bool WindowBackend::GetPhysicalSizeOverride(SDL_Window* /*window*/, WebGpuScaleMode scaleMode, vf2d& outSize) {
    if (scaleMode != WebGpuScaleMode::Native) return false;
    uint32_t cw = Renderer::GetCanvasWidth();
    uint32_t ch = Renderer::GetCanvasHeight();
    if (cw == 0 || ch == 0) return false;
    outSize = { (float)cw, (float)ch };
    return true;
}

bool WindowBackend::GetSizeOverride(SDL_Window* window, WebGpuScaleMode scaleMode,
                                    int webGpuRenderWidth, int webGpuRenderHeight,
                                    vf2d& outSize) {
    if (scaleMode == WebGpuScaleMode::Native) {
#ifdef __EMSCRIPTEN__
        // SDL_GetWindowSizeInPixels applies DPI scaling that diverges from the CSS-pixel-based
        // swapchain size acquireSwapchainTexture sets. Prefer the actual swapchain dims.
        uint32_t cw = Renderer::GetCanvasWidth();
        uint32_t ch = Renderer::GetCanvasHeight();
        if (cw > 0 && ch > 0) {
            outSize = { (float)cw, (float)ch };
            return true;
        }
#endif
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        outSize = { (float)w, (float)h };
        return true;
    }

    // Non-Native scale modes always render to a fixed render-target size.
    outSize = { (float)webGpuRenderWidth, (float)webGpuRenderHeight };
    return true;
}
