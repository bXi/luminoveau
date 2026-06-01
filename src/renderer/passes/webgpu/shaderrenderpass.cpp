// WebGPU-backend implementation for ShaderRenderPass — stub until user-shader support
// lands on WebGPU. Init succeeds; render() just clears the target.

#include "renderer/passes/shaderrenderpass.h"
#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "platform/window/window.h"

bool ShaderRenderPass::init(
    GpuTextureFormat /*swapchain_texture_format*/, uint32_t /*surface_width*/,
    uint32_t /*surface_height*/, std::string name, bool logInit,
    size_t /*capacity*/, bool /*forceNoMSAA*/) {
    passname = std::move(name);
    if (logInit) LOG_INFO("ShaderRenderPass stub init (WebGPU): {}", passname);
    return true;
}

void ShaderRenderPass::release(bool /*logRelease*/) {
    // WebGPU stub — nothing to release
}

UniformBuffer& ShaderRenderPass::getUniformBuffer() {
    static UniformBuffer dummy;
    return dummy;
}

void ShaderRenderPass::render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4&) {
    auto& gpu = Renderer::GetGpu();
    GpuColorTargetInfo ct{};
    ct.texture = targetTexture;
    ct.loadOp  = color_target_info_loadop;
    ct.storeOp = GpuStoreOp::Store;
    auto rp = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
    gpu.endRenderPass(rp);
}
