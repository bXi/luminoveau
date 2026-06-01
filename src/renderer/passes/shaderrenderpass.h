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
    glm::vec2 lastMousePos = {0, 0};

    GpuGraphicsPipelineHandle m_pipeline                  = 0;
    TextureAsset              m_depth_texture;

    GpuTextureHandle          resultTexture               = 0;
    GpuTextureHandle          inputTexture                = 0;
    GpuGraphicsPipelineHandle finalrender_pipeline        = 0;

    Renderable                fs;
    TextureAsset              transparentPixel;

    UniformBuffer             uniformBuffer;

    GpuShaderHandle           vertex_shader               = 0;
    GpuShaderHandle           fragment_shader             = 0;

    GpuShaderHandle           finalrender_fragment_shader = 0;
    GpuShaderHandle           finalrender_vertex_shader   = 0;

    uint32_t                  m_desktop_width             = 0;
    uint32_t                  m_desktop_height            = 0;

    std::vector<std::string>  foundSamplers;

    void _loadSamplerNamesFromShader(const std::vector<uint8_t> &spirvBinary);
    void _loadUniformsFromShader(const std::vector<uint8_t> &spirvBinary);
    void _renderShaderOutputToFramebuffer(GpuCmdBufferHandle cmd_buffer, GpuTextureHandle target_texture, GpuTextureHandle result_texture, const glm::mat4 &camera);

    std::vector<Renderable> renderQueue;

public:
    ShaderAsset             vertShader;
    ShaderAsset             fragShader;

    ShaderRenderPass(const ShaderRenderPass &) = delete;
    ShaderRenderPass &operator=(const ShaderRenderPass &) = delete;
    ShaderRenderPass(ShaderRenderPass &&) = delete;
    ShaderRenderPass &operator=(ShaderRenderPass &&) = delete;

    ShaderRenderPass() : RenderPass() {}

    [[nodiscard]] bool init(
        GpuTextureFormat swapchain_texture_format, uint32_t surface_width,
        uint32_t surface_height, std::string name, bool logInit = true,
        size_t capacity = 0, bool forceNoMSAA = false
    ) override;

    void release(bool logRelease = true) override;

    UniformBuffer &getUniformBuffer() override;

    void render(
        GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4 &camera
    ) override;

    void addToRenderQueue(const Renderable &renderable) override {
        renderQueue.push_back(renderable);
    }

    void resetRenderQueue() override {
        renderQueue.clear();
    }

    // ShaderRenderPass reads from fbContent; require the previous pass to resolve MSAA first.
    bool needsResolvedInput() const override { return true; }
};
