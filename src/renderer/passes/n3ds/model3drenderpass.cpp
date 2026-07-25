// 3DS-backend implementation for Model3DRenderPass — M1 stub. 3D on the PICA200
// is possible (M2: picasso model shader, fixed-function lighting) but out of the
// minimal-2D scope; this pass is not attached by renderer/n3ds/init.cpp, the stub
// exists so shared code that instantiates the class still links.

#include "renderer/passes/model3drenderpass.h"

#include "core/log/log.h"
#include "gpu/IGpu.h"

void Model3DRenderPass::_createShaders() { }

void Model3DRenderPass::_uploadModelToGPU(ModelAsset * /*model*/) { }

bool Model3DRenderPass::Init(
    GpuTextureFormat /*swapchainTextureFormat*/, uint32_t /*surfaceWidth*/,
    uint32_t /*surfaceHeight*/, std::string name, bool logInit,
    size_t /*capacity*/, bool /*forceNoMSAA*/) {
    _passname = std::move(name);
    if (logInit)
        LOG_WARNING("Model3DRenderPass '{}': 3D rendering is not supported on 3DS yet (stub)", _passname);
    return true;
}

void Model3DRenderPass::Release(bool /*logRelease*/) { }

void Model3DRenderPass::Render(
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
