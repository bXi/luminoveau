#pragma once

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "assettypes/texture.h"

#include "assethandler/assethandler.h"
#include "renderable.h"

#include "renderpass.h"

class TextRenderPass : public RenderPass {
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



    SDL_GPUBuffer *vertex_buffer{};
    SDL_GPUBuffer *index_buffer{};

    TextureAsset            m_depth_texture;
    SDL_GPUGraphicsPipeline *m_pipeline{nullptr};
    SDL_GPUTransferBuffer *m_transbuf{};

    std::string passname;

public:

    std::vector<Renderable> renderQueue;

public:
    TextRenderPass(const TextRenderPass &) = delete;

    TextRenderPass &operator=(const TextRenderPass &) = delete;

    TextRenderPass(TextRenderPass &&) = delete;

    TextRenderPass &operator=(TextRenderPass &&) = delete;

    explicit TextRenderPass(SDL_GPUDevice *m_gpu_device) : RenderPass(m_gpu_device) {
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
};
