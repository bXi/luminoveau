// WebGPU-backend implementation for ShaderRenderPass — stub until user-shader support
// lands on WebGPU. Init succeeds; Render() just clears the target.

#include "renderer/passes/shaderrenderpass.h"
#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "platform/window/window.h"

bool ShaderRenderPass::Init(
    GpuTextureFormat /*swapchain_texture_format*/, uint32_t /*surface_width*/,
    uint32_t /*surface_height*/, std::string name, bool logInit,
    size_t /*capacity*/, bool /*forceNoMSAA*/) {
    _passname = std::move(name);
    if (logInit)
        LOG_INFO("ShaderRenderPass stub Init (WebGPU): {}", _passname);
    return true;
}

void ShaderRenderPass::Release(bool /*logRelease*/) {
    // WebGPU stub — nothing to release
}

UniformBuffer &ShaderRenderPass::GetUniformBuffer() {
    static UniformBuffer dummy;
    return dummy;
}

void ShaderRenderPass::Render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4 &) {
    auto              &gpu = Renderer::GetGpu();
    GpuColorTargetInfo ct {};
    ct.texture = targetTexture;
    ct.loadOp  = colorTargetInfoLoadOp;
    ct.storeOp = GpuStoreOp::Store;
    auto rp    = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
    gpu.EndRenderPass(rp);
}
