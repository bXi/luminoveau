// 3DS-backend implementation for ShaderRenderPass — permanent stub: the PICA200
// has no fragment shaders, so user shader passes cannot exist. Init succeeds so
// shared code paths keep working; Render() just clears/loads the target.

#include "renderer/passes/shaderrenderpass.h"

#include "core/log/log.h"
#include "gpu/IGpu.h"

bool ShaderRenderPass::Init(
    GpuTextureFormat /*swapchainTextureFormat*/, uint32_t /*surfaceWidth*/,
    uint32_t /*surfaceHeight*/, std::string name, bool logInit,
    size_t /*capacity*/, bool /*forceNoMSAA*/) {
    _passname = std::move(name);
    if (logInit)
        LOG_WARNING("ShaderRenderPass '{}': shader passes are not supported on 3DS (stub)", _passname);
    return true;
}

void ShaderRenderPass::Release(bool /*logRelease*/) { }

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
    ct.clearR  = colorTargetClearR;
    ct.clearG  = colorTargetClearG;
    ct.clearB  = colorTargetClearB;
    ct.clearA  = colorTargetClearA;
    auto rp    = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
    gpu.EndRenderPass(rp);
}
