#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gpu/renderpass.h"
#include "renderer/renderer.h"

#include "assets/model/model.h"
#include "scene/scene3d.h"
#ifndef LUMINOVEAU_WEBGPU_BACKEND
#include <SDL3/SDL_gpu.h>
#endif

class Model3DRenderPass : public RenderPass {
private:
#ifndef LUMINOVEAU_WEBGPU_BACKEND
    SDL_GPUDevice* m_gpu_device        = nullptr;
    SDL_GPUTexture *msaa_color_texture = nullptr;
    SDL_GPUTexture *msaa_depth_texture = nullptr;

    SDL_GPUShader *vertex_shader   = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;

    SDL_GPUSampleCount current_sample_count = SDL_GPU_SAMPLECOUNT_1;  // Track current MSAA

    struct SceneUniforms {
        glm::mat4 viewProj;     // Combined view-projection matrix
        glm::mat4 models[16];   // Array of model matrices (max 16 models)
        glm::vec4 cameraPos;    // Camera position for lighting
        glm::vec4 ambientLight; // Ambient light color

        // Light data (supporting up to 4 lights)
        glm::vec4 lightPositions[4];   // xyz = position, w = type (0=point, 1=directional, 2=spot)
        glm::vec4 lightColors[4];      // rgb = color, a = intensity
        glm::vec4 lightParams[4];      // For attenuation, angles, etc.
        int       lightCount;
        int       modelCount;  // Number of models
        int       padding[2]; // Align to 16 bytes
    };

    SDL_GPUBuffer         *uniformBuffer         = nullptr;
    SDL_GPUTransferBuffer *uniformTransferBuffer = nullptr;

    uint32_t surface_width  = 0;
    uint32_t surface_height = 0;

    void createShaders();

    void createPipeline(SDL_GPUTextureFormat swapchain_format);

    void uploadModelToGPU(ModelAsset *model);
#else
    // WebGPU state
    struct SceneUniforms {
        glm::mat4 viewProj;
        glm::mat4 models[16];
        glm::vec4 cameraPos;
        glm::vec4 ambientLight;
        glm::vec4 lightPositions[4];
        glm::vec4 lightColors[4];
        glm::vec4 lightParams[4];
        int       lightCount;
        int       modelCount;
        int       padding[2];
    };

    GpuShaderHandle           m_vert_shader      = 0;
    GpuShaderHandle           m_frag_shader      = 0;
    GpuGraphicsPipelineHandle m_pipeline_wgpu    = 0;
    GpuBufferHandle           m_uniform_buf      = 0;
    GpuTransferBufferHandle   m_uniform_xfer_buf = 0;
    GpuTextureHandle          m_depth_tex        = 0;
    GpuSamplerHandle          m_linear_sampler   = 0;
    uint32_t                  m_surface_width    = 0;
    uint32_t                  m_surface_height   = 0;

    void uploadModelToGPU(ModelAsset *model);
#endif // LUMINOVEAU_WEBGPU_BACKEND

public:
    Model3DRenderPass(const Model3DRenderPass &) = delete;

    Model3DRenderPass &operator=(const Model3DRenderPass &) = delete;

    Model3DRenderPass(Model3DRenderPass &&) = delete;

    Model3DRenderPass &operator=(Model3DRenderPass &&) = delete;

#ifndef LUMINOVEAU_WEBGPU_BACKEND
    explicit Model3DRenderPass(SDL_GPUDevice *gpu_device) : RenderPass() {
        m_gpu_device = gpu_device;
    }
#else
    Model3DRenderPass() : RenderPass() {}
#endif

    [[nodiscard]] bool init(
        GpuTextureFormat swapchain_texture_format, uint32_t surface_width,
        uint32_t surface_height, std::string name, bool logInit = true,
        size_t capacity = 0, bool forceNoMSAA = false
    ) override;

    void release(bool logRelease = true) override;

    void render(
        GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4 &camera
    ) override;

    // These are required by base class but not used for 3D rendering
    void addToRenderQueue(const Renderable &renderable) override {}

    void resetRenderQueue() override {}

    UniformBuffer &getUniformBuffer() override {
        static UniformBuffer dummy;
        return dummy;
    }
};
