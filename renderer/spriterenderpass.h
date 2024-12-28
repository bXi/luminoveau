#pragma once

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "renderer/rendererhandler.h"

#include "assettypes/texture.h"

#include "assethandler/assethandler.h"
#include "renderable.h"

#include "renderpass.h"

class SpriteRenderPass : public RenderPass {
    struct Uniforms {
        glm::mat4 camera;
        glm::mat4 model;
        glm::vec2 flipped;

        //TODO: fix this ugly mess
        glm::vec2 uv0;
        glm::vec2 uv1;
        glm::vec2 uv2;
        glm::vec2 uv3;
        glm::vec2 uv4;
        glm::vec2 uv5;

        float tintColorR = 1.0f;
        float tintColorG = 1.0f;
        float tintColorB = 1.0f;
        float tintColorA = 1.0f;
    };

    TextureAsset            m_depth_texture;
    SDL_GPUGraphicsPipeline *m_pipeline{nullptr};

    std::string passname;

    SDL_GPUShader *vertex_shader   = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;

public:

    std::vector<Renderable> renderQueue;

public:
    SpriteRenderPass(const SpriteRenderPass &) = delete;

    SpriteRenderPass &operator=(const SpriteRenderPass &) = delete;

    SpriteRenderPass(SpriteRenderPass &&) = delete;

    SpriteRenderPass &operator=(SpriteRenderPass &&) = delete;

    explicit SpriteRenderPass(SDL_GPUDevice *m_gpu_device) : RenderPass(m_gpu_device) {
    }

    [[nodiscard]] bool init(
        SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width,
        uint32_t surface_height, std::string name
    ) override;

    void release() override;

    void render(
        SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
    ) override;

    void addToRenderQueue(const Renderable &renderable) override {
        renderQueue.push_back(renderable);
    }

    void resetRenderQueue() override {
        renderQueue.clear();
    }

    UniformBuffer& getUniformBuffer() override {

        return uniformBuffer;
    }

    UniformBuffer uniformBuffer;

    static const uint8_t sprite_frag_bin[];
    static const size_t  sprite_frag_bin_len = 1000;

    static const uint8_t sprite_vert_bin[];
    static const size_t  sprite_vert_bin_len = 3212;

    void createShaders();
};
