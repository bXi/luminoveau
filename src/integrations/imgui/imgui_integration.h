#pragma once

#ifdef LUMINOVEAU_WITH_IMGUI

#include "SDL3/SDL.h"
#include "gpu/types.h"

namespace ImGuiIntegration {
    // Call after SDL_CreateWindow, before Renderer::InitRendering
    void Init(SDL_Window* window);
    // Call after GPU device is created (inside Renderer::InitRendering)
    void InitRenderer(SDL_Window* window);
    // Call before SDL_DestroyWindow
    void Shutdown();

    void ProcessEvent(SDL_Event* event);
    void NewFrame();
    void RenderFrame(GpuCmdBufferHandle cmd, GpuTextureHandle swapchain);
    void EndFrame();  // swapchain unavailable (minimized)
    void DrawDebugMenu();
}

#endif
