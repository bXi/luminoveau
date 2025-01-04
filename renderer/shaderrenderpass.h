#pragma once

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "assettypes/texture.h"

#include "renderer/rendererhandler.h"

#include "assethandler/assethandler.h"
#include "renderable.h"

#include "renderpass.h"

#include "utils/uniformobject.h"

#include "spirv_cross.hpp"

class ShaderRenderPass : public RenderPass {
    glm::vec2 lastMousePos = {0, 0};

    SDL_GPUGraphicsPipeline *m_pipeline{nullptr};
    TextureAsset m_depth_texture;

    SDL_GPUTexture* resultTexture;

    std::string passname;

    int _frameCounter = 0;

    Renderable   fs;
    TextureAsset transparentPixel;

    UniformBuffer uniformBuffer;

public:

    SDL_GPUShader *vertex_shader   = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;

    ShaderAsset             vertShader;
    ShaderAsset             fragShader;

    void loadUniformsFromShader(const std::vector<uint8_t> &spirvBinary);
    void _renderShaderOutputToFramebuffer(SDL_GPUCommandBuffer *cmd_buffer, const glm::mat4 &camera, SDL_GPUTexture *target_texture, SDL_GPUTexture *result_texture);
    std::vector<Renderable> renderQueue;

public:
    ShaderRenderPass(const ShaderRenderPass &) = delete;

    ShaderRenderPass &operator=(const ShaderRenderPass &) = delete;

    ShaderRenderPass(ShaderRenderPass &&) = delete;

    ShaderRenderPass &operator=(ShaderRenderPass &&) = delete;

    explicit ShaderRenderPass(SDL_GPUDevice *m_gpu_device) : RenderPass(m_gpu_device) {
    }

    [[nodiscard]] bool init(
        SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width,
        uint32_t surface_height, std::string name
    ) override;

    void release() override;

    UniformBuffer &getUniformBuffer() override;

    void render(
        SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
    ) override;

    void addToRenderQueue(const Renderable &renderable) override {
        renderQueue.push_back(renderable);
    }

    void resetRenderQueue() override {
        renderQueue.clear();
    }

};
