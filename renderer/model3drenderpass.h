#pragma once

#include <vector>
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "renderpass.h"
#include "renderer/rendererhandler.h"
#include "assettypes/model.h"
#include "utils/scene3d.h"

class Model3DRenderPass : public RenderPass {
private:
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

public:
    Model3DRenderPass(const Model3DRenderPass &) = delete;

    Model3DRenderPass &operator=(const Model3DRenderPass &) = delete;

    Model3DRenderPass(Model3DRenderPass &&) = delete;

    Model3DRenderPass &operator=(Model3DRenderPass &&) = delete;

    explicit Model3DRenderPass(SDL_GPUDevice *gpu_device) : RenderPass(gpu_device) {}

    [[nodiscard]] bool init(
        SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width,
        uint32_t surface_height, std::string name, bool logInit = true
    ) override;

    void release(bool logRelease = true) override;

    void render(
        SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
    ) override;

    // These are required by base class but not used for 3D rendering
    void addToRenderQueue(const Renderable &renderable) override {}

    void resetRenderQueue() override {}

    UniformBuffer &getUniformBuffer() override {
        static UniformBuffer dummy;
        return dummy;
    }

// Shader binaries
static const uint8_t model3d_vert_bin[];
static const size_t model3d_vert_bin_len = 7004;

static const uint8_t model3d_frag_bin[];
static const size_t model3d_frag_bin_len = 976;
};
