// SDL-backend bridge for ImGui (uses imgui_impl_sdlgpu3).

#ifdef LUMINOVEAU_WITH_IMGUI

#include "integrations/imgui/imgui_backend.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"

#include "renderer/renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

namespace ImGuiBackend {

void InitRenderer(SDL_Window* window) {
    SDL_GPUDevice* device = Renderer::GetDevice();
    ImGui_ImplSDL3_InitForSDLGPU(window);

    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device               = device;
    init_info.ColorTargetFormat    = SDL_GetGPUSwapchainTextureFormat(device, window);
    init_info.MSAASamples          = SDL_GPU_SAMPLECOUNT_1;
    init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    init_info.PresentMode          = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);
}

void Shutdown() {
    ImGui_ImplSDLGPU3_Shutdown();
}

void NewFrame() {
    ImGui_ImplSDLGPU3_NewFrame();
}

void RenderFrame(GpuCmdBufferHandle cmd, GpuTextureHandle swapchain) {
    ImDrawData* draw_data = ImGui::GetDrawData();
    auto* sdlCmd       = reinterpret_cast<SDL_GPUCommandBuffer*>(cmd);
    auto* sdlSwapchain = reinterpret_cast<SDL_GPUTexture*>(swapchain);

    ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, sdlCmd);

    SDL_GPUColorTargetInfo color_target_info = {};
    color_target_info.texture              = sdlSwapchain;
    color_target_info.mip_level            = 0;
    color_target_info.layer_or_depth_plane = 0;
    color_target_info.clear_color          = {0.25f, 0.25f, 0.25f, 0.0f};
    color_target_info.load_op              = SDL_GPU_LOADOP_LOAD;
    color_target_info.store_op             = SDL_GPU_STOREOP_STORE;

#ifdef LUMIDEBUG
    SDL_PushGPUDebugGroup(sdlCmd, "[Lumi] ImGuiRenderPass::render");
#endif

    auto render_pass = SDL_BeginGPURenderPass(sdlCmd, &color_target_info, 1, nullptr);
    ImGui_ImplSDLGPU3_RenderDrawData(draw_data, sdlCmd, render_pass);
    SDL_EndGPURenderPass(render_pass);

#ifdef LUMIDEBUG
    SDL_PopGPUDebugGroup(sdlCmd);
#endif
}

} // namespace ImGuiBackend

#endif // LUMINOVEAU_WITH_IMGUI
