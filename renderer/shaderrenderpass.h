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
#include "sdl_gpu_structs.h"

class ShaderRenderPass : public RenderPass {
    glm::vec2 lastMousePos = {0, 0};

    SDL_GPUGraphicsPipeline *m_pipeline{nullptr};
    TextureAsset m_depth_texture;

    SDL_GPUTexture* resultTexture = nullptr;  // Window-sized output
    SDL_GPUTexture* inputTexture = nullptr;   // Window-sized input (copy of framebuffer window region)
    SDL_GPUGraphicsPipeline* finalrender_pipeline = nullptr;

    Renderable   fs;
    TextureAsset transparentPixel;

    UniformBuffer uniformBuffer;

    SDL_GPUShader *vertex_shader   = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;

    SDL_GPUShader *finalrender_fragment_shader = nullptr;
    SDL_GPUShader *finalrender_vertex_shader   = nullptr;

    // Desktop-sized texture dimensions for UV scaling
    uint32_t m_desktop_width = 0;
    uint32_t m_desktop_height = 0;

    std::vector<std::string> foundSamplers;

    void _loadSamplerNamesFromShader(const std::vector<uint8_t> &spirvBinary);
    void _loadUniformsFromShader(const std::vector<uint8_t> &spirvBinary);
    void _copyFramebufferToInput(SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *framebuffer_texture, const glm::mat4 &camera);
    void _renderShaderOutputToFramebuffer(SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, SDL_GPUTexture *result_texture, const glm::mat4 &camera);
    std::vector<Renderable> renderQueue;

public:
    ShaderAsset             vertShader;
    ShaderAsset             fragShader;


    ShaderRenderPass(const ShaderRenderPass &) = delete;

    ShaderRenderPass &operator=(const ShaderRenderPass &) = delete;

    ShaderRenderPass(ShaderRenderPass &&) = delete;

    ShaderRenderPass &operator=(ShaderRenderPass &&) = delete;

    explicit ShaderRenderPass(SDL_GPUDevice *m_gpu_device) : RenderPass(m_gpu_device) {
    }

    [[nodiscard]] bool init(
        SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width,
        uint32_t surface_height, std::string name, bool logInit = true
    ) override;

    void release(bool logRelease = true) override;

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
