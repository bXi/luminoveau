#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "assets/texture/texture.h"

#include "renderer/renderer.h"

#include "assets/assethandler.h"
#include "gpu/renderable.h"

#include "gpu/renderpass.h"

#include "gpu/buffer/uniformobject.h"

class ShaderRenderPass : public RenderPass {
    // SDL-only members carried unconditionally so the header stays backend-neutral.
    // WebGPU stub never touches them.
    glm::vec2 _lastMousePos = { 0, 0 };

    GpuGraphicsPipelineHandle _pipeline = 0;
    TextureAsset              _depthTexture;

    GpuTextureHandle          _resultTexture       = 0;
    GpuTextureHandle          _inputTexture        = 0;
    GpuGraphicsPipelineHandle _finalRenderPipeline = 0;

    Renderable   _fs;
    TextureAsset _transparentPixel;

    UniformBuffer _uniformBuffer;

    GpuShaderHandle _vertexShader   = 0;
    GpuShaderHandle _fragmentShader = 0;

    GpuShaderHandle _finalRenderFragmentShader = 0;
    GpuShaderHandle _finalRenderVertexShader   = 0;

    uint32_t _desktopWidth  = 0;
    uint32_t _desktopHeight = 0;

    std::vector<std::string> _foundSamplers;

    void _loadSamplerNamesFromShader(const std::vector<uint8_t> &spirvBinary);
    void _loadUniformsFromShader(const std::vector<uint8_t> &spirvBinary);
    void _renderShaderOutputToFramebuffer(GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, GpuTextureHandle resultTexture, const glm::mat4 &camera);

    std::vector<Renderable> _renderQueue;

public:
    ShaderAsset vertShader;
    ShaderAsset fragShader;

    ShaderRenderPass(const ShaderRenderPass &)            = delete;
    ShaderRenderPass &operator=(const ShaderRenderPass &) = delete;
    ShaderRenderPass(ShaderRenderPass &&)                 = delete;
    ShaderRenderPass &operator=(ShaderRenderPass &&)      = delete;

    ShaderRenderPass()
        : RenderPass() { }

    [[nodiscard]] bool Init(
        GpuTextureFormat swapchainTextureFormat, uint32_t surfaceWidth,
        uint32_t surfaceHeight, std::string name, bool logInit = true,
        size_t capacity = 0, bool forceNoMSAA = false) override;

    void Release(bool logRelease = true) override;

    // _desktopWidth/_desktopHeight are the denominator for the STEP-1 crop UV in Render()
    // (see sdl/shaderrenderpass.cpp) that pulls the live window region out of the desktop-sized
    // fbContent. Without this override they're frozen at whatever Init() saw, so the crop drifts
    // wrong as soon as the window is resized away from its startup size.
    void OnResize(uint32_t surfaceWidth, uint32_t surfaceHeight) override;

    UniformBuffer &GetUniformBuffer() override;

    void Render(
        GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4 &camera) override;

    void AddToRenderQueue(const Renderable &renderable) override {
        _renderQueue.push_back(renderable);
    }

    void ResetRenderQueue() override {
        _renderQueue.clear();
    }

    // ShaderRenderPass reads from fbContent; require the previous pass to resolve MSAA first.
    bool NeedsResolvedInput() const override { return true; }
};
