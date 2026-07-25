// 3DS-specific initialisation for Renderer::_initRendering().
// Compiled only when LUMINOVEAU_N3DS_BACKEND is set (see cmake/Sources.cmake).
// Mirrors renderer/webgpu/init.cpp, minus everything the PICA200 can't do:
// one framebuffer (400x240 top screen), one sprite pass, no 3D pass, no compute.

#include "renderer/renderer.h"

#include "gpu/backends/n3ds/Citro3dGpuBackend.h"

#include "assets/assethandler.h"
#include "core/log/log.h"
#include "draw/particles.h"
#include "gpu/presets.h"
#include "platform/window/window.h"
#include "renderer/passes/spriterenderpass.h"
#include "renderer/shaders.h"

// Embedded picasso shbins (cmake/N3dsShaders.cmake).
extern "C" {
extern const uint8_t  lumi_composite_shbin[];
extern const uint32_t lumi_composite_shbin_size;
}

void Renderer::_initRendering() {
    auto *backend = new Citro3dGpuBackend();
    _gpu.reset(backend);
    if (!_gpu->Init(Window::GetWindow())) {
        LOG_CRITICAL("Citro3dGpuBackend::Init() failed");
        return;
    }

    // The 3DS canvas never changes: top screen, 400x240.
    Renderer::SetCanvasSize(400, 240);

    // Both samplers clamp: textures are padded to power-of-two, and Repeat would
    // sample the padding at the logical edges.
    GpuSamplerCreateInfo nearestInfo {
        .minFilter = GpuFilter::Nearest,
        .magFilter = GpuFilter::Nearest,
        .mipFilter = GpuFilter::Nearest,
        .addressU  = GpuSamplerAddressMode::ClampToEdge,
        .addressV  = GpuSamplerAddressMode::ClampToEdge,
        .addressW  = GpuSamplerAddressMode::ClampToEdge,
    };
    GpuSamplerCreateInfo linearInfo = nearestInfo;
    linearInfo.minFilter            = GpuFilter::Linear;
    linearInfo.magFilter            = GpuFilter::Linear;
    linearInfo.mipFilter            = GpuFilter::Linear;
    _samplers[ScaleMode::Nearest]   = _gpu->CreateSampler(nearestInfo);
    _samplers[ScaleMode::Linear]    = _gpu->CreateSampler(linearInfo);

    _camera = glm::ortho(0.0f, (float)Window::GetWidth(), (float)Window::GetHeight(), 0.0f);

    _fs = AssetHandler::CreateEmptyTexture({ 1, 1 });

    // Composite ("render-to-texture") pipeline: passthrough vertex shader + modulate
    // TEV. No vertex input — the backend recognises bindingCount==0 as the composite
    // quad and synthesizes the 6 vertices from the pushed Uniforms blob.
    GpuShaderCreateInfo rttVertInfo {};
    rttVertInfo.code     = lumi_composite_shbin;
    rttVertInfo.codeSize = lumi_composite_shbin_size;
    rttVertInfo.stage    = GpuShaderStage::Vertex;
    _rttVertexShader     = _gpu->CreateShader(rttVertInfo);

    const CtrTevPreset  rttTev = CtrTevPreset::Modulate;
    GpuShaderCreateInfo rttFragInfo {};
    rttFragInfo.code     = reinterpret_cast<const uint8_t *>(&rttTev);
    rttFragInfo.codeSize = sizeof(rttTev);
    rttFragInfo.stage    = GpuShaderStage::Fragment;
    _rttFragmentShader   = _gpu->CreateShader(rttFragInfo);

    if (!_rttVertexShader || !_rttFragmentShader) {
        LOG_CRITICAL("Failed to create composite shaders");
        return;
    }

    GpuGraphicsPipelineCreateInfo rttPipeInfo {};
    rttPipeInfo.vertexShader      = _rttVertexShader;
    rttPipeInfo.fragmentShader    = _rttFragmentShader;
    rttPipeInfo.colorTargetFormat = _gpu->GetSwapchainFormat();
    rttPipeInfo.blend             = GpuPresets::AlphaBlendKeepDstAlpha;
    rttPipeInfo.hasDepthTarget    = false;
    _renderToTexturePipeline      = _gpu->CreateGraphicsPipeline(rttPipeInfo);

    GpuGraphicsPipelineCreateInfo rttAddPipeInfo = rttPipeInfo;
    rttAddPipeInfo.blend                         = {
                                .blendEnabled   = true,
                                .srcColorFactor = GpuBlendFactor::One,
                                .dstColorFactor = GpuBlendFactor::One,
                                .colorOp        = GpuBlendOp::Add,
                                .srcAlphaFactor = GpuBlendFactor::One,
                                .dstAlphaFactor = GpuBlendFactor::One,
                                .alphaOp        = GpuBlendOp::Add,
    };
    _renderToTexturePipelineAdditive = _gpu->CreateGraphicsPipeline(rttAddPipeInfo);

    // Primary framebuffer — the display is fixed 400x240 so no desktop-size headroom
    // is needed (the backing texture is POT-padded to 512x256 by the backend).
    const int fbWidth  = 400;
    const int fbHeight = 240;

    auto *framebuffer      = new FrameBuffer;
    framebuffer->fbContent = AssetHandler::CreateEmptyTexture({ (float)fbWidth, (float)fbHeight }).gpuTexture;
    framebuffer->width     = fbWidth;
    framebuffer->height    = fbHeight;

    // Only the 2D sprite pass — no 3D on the PICA backend (model3d pass is a stub,
    // attaching it would just burn a render-target switch per frame).
    framebuffer->renderpasses.emplace_back("2dsprites", new SpriteRenderPass());
    framebuffer->renderpasses.back().second->colorTargetInfoLoadOp = GpuLoadOp::Clear;
    framebuffer->renderpasses.back().second->colorTargetClearR     = 0.f;
    framebuffer->renderpasses.back().second->colorTargetClearG     = 0.f;
    framebuffer->renderpasses.back().second->colorTargetClearB     = 0.f;
    framebuffer->renderpasses.back().second->colorTargetClearA     = 1.f;

    _frameBuffers.emplace_back("primaryFramebuffer", framebuffer);

    for (auto &[fbName, fb] : _frameBuffers) {
        for (auto &[rpName, rp] : fb->renderpasses) {
            rp->Init(_gpu->GetSwapchainFormat(), fbWidth, fbHeight, rpName, true, 0, true);
        }
    }

    _whitePixelTexture = AssetHandler::CreateWhitePixel();

    Particles::Init();
}

// No runtime compute on 3DS — the asset handler and Compute:: paths treat an empty
// asset as "unsupported" and no-op.
ComputePipelineAsset Renderer::CreateComputePipelineAsset(const std::string &shaderPath) {
    LOG_WARNING("Compute shaders are not supported on 3DS ('{}')", shaderPath.c_str());
    return {};
}
