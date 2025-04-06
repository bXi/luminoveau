#pragma once

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include "assettypes/texture.h"
#include "assethandler/assethandler.h"
#include "renderable.h"
#include "utils/uniformobject.h"

class RenderPass {
protected:
    struct Uniforms {
        glm::mat4 camera;
        glm::mat4 model;
        glm::vec2 flipped;

        // TODO: fix this ugly mess
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


    SDL_GPUDevice* m_gpu_device;
    TextureAsset m_depth_texture;
    SDL_GPUGraphicsPipeline* m_pipeline{nullptr};

    std::string passname;


public:
    SDL_GPULoadOp color_target_info_loadop = SDL_GPU_LOADOP_LOAD;
    SDL_FColor color_target_info_clear_color = {0.0, 0.0, 0.0, 0.0};
    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;
    RenderPass(RenderPass&&) = delete;
    RenderPass& operator=(RenderPass&&) = delete;

    explicit RenderPass(SDL_GPUDevice* gpu_device) : m_gpu_device(gpu_device) {}
    virtual ~RenderPass() = default;

    [[nodiscard]] virtual bool init(
        SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width,
        uint32_t surface_height, std::string name
    ) = 0;

    virtual void release() = 0;

    virtual void render(
        SDL_GPUCommandBuffer* cmd_buffer, SDL_GPUTexture* target_texture, const glm::mat4& camera
    ) = 0;

    virtual void addToRenderQueue(const Renderable &renderable) = 0;
    virtual void resetRenderQueue() = 0;

    virtual UniformBuffer &getUniformBuffer() = 0;

    SDL_GPURenderPass *render_pass{nullptr};

    bool           _scissorEnabled = false;
    SDL_Rect *_scissorRect = new SDL_Rect{0, 0, 0, 0};
};
