// WebGPU-backend bridge for ImGui (uses imgui_impl_wgpu).

#ifdef LUMINOVEAU_WITH_IMGUI

#include "integrations/imgui/imgui_backend.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_wgpu.h"

#include "renderer/renderer.h"
#include "gpu/backends/webgpu/WebGpuGpuBackend.h"
#include "gpu/backends/webgpu/WebGpuHandles.h"

#include <SDL3/SDL.h>

namespace ImGuiBackend {

void InitRenderer(SDL_Window* window) {
    ImGui_ImplSDL3_InitForOther(window);

    auto* wgpuBackend = static_cast<WebGpuGpuBackend*>(&Renderer::GetGpu());

    // Map engine swapchain format to WGPUTextureFormat for imgui pipeline creation
    WGPUTextureFormat wgpuFmt = WGPUTextureFormat_BGRA8Unorm;
    GpuTextureFormat  engineFmt = Renderer::GetGpu().getSwapchainFormat();
    if (engineFmt == GpuTextureFormat::R8G8B8A8_Unorm) wgpuFmt = WGPUTextureFormat_RGBA8Unorm;

    ImGui_ImplWGPU_InitInfo init_info = {};
    init_info.Device             = static_cast<WGPUDevice>(wgpuBackend->getRawDevice());
    init_info.NumFramesInFlight  = 1;
    init_info.RenderTargetFormat = wgpuFmt;
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&init_info);
}

void Shutdown() {
    ImGui_ImplWGPU_Shutdown();
}

void NewFrame() {
    ImGui_ImplWGPU_NewFrame();
}

void RenderFrame(GpuCmdBufferHandle cmd, GpuTextureHandle swapchain) {
    ImDrawData* draw_data = ImGui::GetDrawData();
    auto* cb   = reinterpret_cast<WgpuCmdBuffer*>(cmd);
    // WebGPU's swapchain handle is a raw WGPUTextureView (see WebGpuGpuBackend::acquireSwapchainTexture).
    auto  view = reinterpret_cast<WGPUTextureView>(swapchain);

    WGPURenderPassColorAttachment ca{};
    ca.depthSlice    = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.view          = view;
    ca.resolveTarget = nullptr;
    ca.loadOp        = WGPULoadOp_Load;
    ca.storeOp       = WGPUStoreOp_Store;
    ca.clearValue    = {0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments     = &ca;

    WGPURenderPassEncoder enc = wgpuCommandEncoderBeginRenderPass(cb->encoder, &rpDesc);
    ImGui_ImplWGPU_RenderDrawData(draw_data, enc);
    wgpuRenderPassEncoderEnd(enc);
    wgpuRenderPassEncoderRelease(enc);
}

} // namespace ImGuiBackend

#endif // LUMINOVEAU_WITH_IMGUI
