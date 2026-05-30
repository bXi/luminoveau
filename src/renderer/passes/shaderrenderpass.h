#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "assets/texture/texture.h"

#include "renderer/renderer.h"

#include "assets/assethandler.h"
#include "gpu/renderable.h"

#include "gpu/renderpass.h"

#include "gpu/buffer/uniformobject.h"

#ifndef LUMINOVEAU_WEBGPU_BACKEND
#include <SDL3/SDL_gpu.h>
#include "spirv_cross.hpp"
#include "gpu/backends/sdl/sdlgpu.h"
#endif

class ShaderRenderPass : public RenderPass {
#ifndef LUMINOVEAU_WEBGPU_BACKEND
    glm::vec2 lastMousePos = {0, 0};

    SDL_GPUGraphicsPipeline *m_pipeline{nullptr};
    TextureAsset m_depth_texture;

    SDL_GPUTexture* resultTexture = nullptr;
    SDL_GPUTexture* inputTexture = nullptr;
    SDL_GPUGraphicsPipeline* finalrender_pipeline = nullptr;

    Renderable   fs;
    TextureAsset transparentPixel;

    UniformBuffer uniformBuffer;

    SDL_GPUShader *vertex_shader   = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;

    SDL_GPUShader *finalrender_fragment_shader = nullptr;
    SDL_GPUShader *finalrender_vertex_shader   = nullptr;

    uint32_t m_desktop_width = 0;
    uint32_t m_desktop_height = 0;

    std::vector<std::string> foundSamplers;

    void _loadSamplerNamesFromShader(const std::vector<uint8_t> &spirvBinary);
    void _loadUniformsFromShader(const std::vector<uint8_t> &spirvBinary);
    void _copyFramebufferToInput(SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *framebuffer_texture, const glm::mat4 &camera);
    void _renderShaderOutputToFramebuffer(SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, SDL_GPUTexture *result_texture, const glm::mat4 &camera);
#endif
    std::vector<Renderable> renderQueue;

public:
    ShaderAsset             vertShader;
    ShaderAsset             fragShader;

    ShaderRenderPass(const ShaderRenderPass &) = delete;
    ShaderRenderPass &operator=(const ShaderRenderPass &) = delete;
    ShaderRenderPass(ShaderRenderPass &&) = delete;
    ShaderRenderPass &operator=(ShaderRenderPass &&) = delete;

#ifndef LUMINOVEAU_WEBGPU_BACKEND
    explicit ShaderRenderPass(SDL_GPUDevice*) : RenderPass() {}
#else
    ShaderRenderPass() : RenderPass() {}
#endif

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
