// Private bridge to the active imgui rendering backend (SDLGPU3 / WGPU).
// imgui_integration.cpp calls these; the SDL or WebGPU TU provides the impls.

#pragma once

#include "gpu/types.h"

struct SDL_Window;

namespace ImGuiBackend {
    void InitRenderer(SDL_Window* window);
    void Shutdown();
    void NewFrame();
    void RenderFrame(GpuCmdBufferHandle cmd, GpuTextureHandle swapchain);
}
