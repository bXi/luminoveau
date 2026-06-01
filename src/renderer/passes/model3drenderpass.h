#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gpu/renderpass.h"
#include "renderer/renderer.h"

#include "assets/model/model.h"
#include "scene/scene3d.h"

class Model3DRenderPass : public RenderPass {
private:
    struct SceneUniforms {
        glm::mat4 viewProj;
        glm::mat4 models[16];
        glm::vec4 cameraPos;
        glm::vec4 ambientLight;
        glm::vec4 lightPositions[4];   // xyz = position, w = type (0=point, 1=directional, 2=spot)
        glm::vec4 lightColors[4];      // rgb = color, a = intensity
        glm::vec4 lightParams[4];      // For attenuation, angles, etc.
        int       lightCount;
        int       modelCount;
        int       padding[2];
    };

    // ── Shared resources ──────────────────────────────────────────────────────
    GpuShaderHandle           vertex_shader         = 0;
    GpuShaderHandle           fragment_shader       = 0;
    GpuGraphicsPipelineHandle m_pipeline            = 0;
    GpuBufferHandle           uniformBuffer         = 0;
    GpuTransferBufferHandle   uniformTransferBuffer = 0;
    GpuTextureHandle          depth_texture         = 0;
    uint32_t                  surface_width         = 0;
    uint32_t                  surface_height        = 0;

    // SDL-only MSAA state. WebGPU runs at sample-count-1 today; if MSAA lands there,
    // these members are inert (always zero) and can stay shared.
    GpuTextureHandle          msaa_color_texture    = 0;
    GpuTextureHandle          msaa_depth_texture    = 0;
    GpuSampleCount            current_sample_count  = GpuSampleCount::x1;

    void createShaders();
    void uploadModelToGPU(ModelAsset *model);

public:
    Model3DRenderPass(const Model3DRenderPass &) = delete;
    Model3DRenderPass &operator=(const Model3DRenderPass &) = delete;
    Model3DRenderPass(Model3DRenderPass &&) = delete;
    Model3DRenderPass &operator=(Model3DRenderPass &&) = delete;

    Model3DRenderPass() : RenderPass() {}

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
