// Per-backend window helpers. Each GPU backend ships its own translation unit under
// platform/window/<backend>/window_backend.cpp; CMake selects which one links into
// the build. The hooks below collect the four spots where window.cpp's logic actually
// differs between SDL native and WebGPU/Emscripten — display-bounds query, post-Init
// projection refresh, browser-driven canvas resize handling, and WebGPU's CSS-pixel
// override for _getSize().

#pragma once

#include <cstdint>

#include "math/vectors.h"
#include "platform/window/window.h"  // WebGpuScaleMode

struct SDL_Window;

namespace WindowBackend {
    // Fills the primary display's physical-pixel resolution. SDL backend queries SDL;
    // WebGPU/Emscripten backend asks the browser. Caller pre-initializes outW/outH
    // with a safe 4K fallback in case the backend can't determine actual bounds.
    void GetDisplayBounds(uint32_t& outW, uint32_t& outH);

    // Runs once after Renderer::InitRendering finishes. SDL backend: no-op. WebGPU
    // backend: kicks UpdateCameraProjection so the camera matches the canvas size
    // the GPU backend just picked, without going through _setSize() (which would
    // trigger a frame-0 reset and discard freshly compiled pipelines).
    void PostInit(SDL_Window* window);

    // Called from the SDL_EVENT_WINDOW_RESIZED handler. SDL backend returns false;
    // caller falls through to _setSize(). WebGPU/Emscripten backend in Native mode
    // returns true after refreshing the projection — the browser already resized the
    // canvas, so calling SDL_SetWindowSize would re-apply DPI and overscale the swapchain.
    bool HandleResize(int newWidth, int newHeight, WebGpuScaleMode scaleMode);

    // Lets the WebGPU backend override _getSize() when the swapchain dimensions
    // diverge from SDL's window dimensions (canvas CSS pixels vs SDL HighDPI). Returns
    // true and fills outSize when overriding; false on SDL (caller continues normal
    // SDL-based size logic).
    bool GetSizeOverride(SDL_Window* window, WebGpuScaleMode scaleMode,
                         int webGpuRenderWidth, int webGpuRenderHeight,
                         vf2d& outSize);

    // Same idea for _getPhysicalSize(). On web, SDL_GetWindowSizeInPixels can disagree
    // with the actual swapchain pixel size (browser canvas attribute drives the surface,
    // not SDL's HighPixelDensity logic); the backend reports the swapchain dims instead.
    bool GetPhysicalSizeOverride(SDL_Window* window, WebGpuScaleMode scaleMode, vf2d& outSize);
}
