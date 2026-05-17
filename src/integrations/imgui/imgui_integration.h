#pragma once

#ifdef LUMINOVEAU_WITH_IMGUI

#include "SDL3/SDL.h"

struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPUTexture;

namespace ImGuiIntegration {
    // Call after SDL_CreateWindow, before Renderer::InitRendering
    void Init(SDL_Window* window);
    // Call after GPU device is created (inside Renderer::InitRendering)
    void InitRenderer(SDL_GPUDevice* device, SDL_Window* window);
    // Call before SDL_DestroyWindow
    void Shutdown();

    void ProcessEvent(SDL_Event* event);
    void NewFrame();
    void RenderFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapchain);
    void EndFrame();  // swapchain unavailable (minimized)
    void DrawDebugMenu();
}

#endif
