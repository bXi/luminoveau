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

    // Fragment uniform block: the lighting inputs the per-pixel fragment shader needs. Mirrors the
    // light fields of SceneUniforms; pushed to the fragment stage each frame. Lighting moved from
    // per-vertex (Gouraud) to per-pixel so point lights work on large low-poly surfaces (e.g. a
    // floor that's a single big cube). Layout must match the LightData cbuffer in model3d.frag.
    struct LightData {
        glm::mat4 shadowViewProj;      // directional shadow caster's view-projection
        glm::vec4 cameraPos;
        glm::vec4 ambientLight;
        glm::vec4 lightPositions[4];
        glm::vec4 lightColors[4];
        glm::vec4 lightParams[4];
        glm::vec4 pointLightPosFar;    // xyz = point shadow caster world pos, w = far range
        int       lightCount;
        int       shadowLight;         // directional caster index, or -1
        int       pointShadowLight;    // point (cube) caster index, or -1
        int       padding;
    };

    // Vertex uniform for the directional shadow depth pass. Matches ShadowParams in shadow.vert.
    struct ShadowParams {
        glm::mat4 lightViewProj;
        uint32_t  baseInstance;
        uint32_t  pad[3];
    };

    // Vertex uniform for a point-light cube shadow face. Matches CubeShadowParams in shadowcube.vert.
    struct CubeShadowParams {
        glm::mat4 faceViewProj;
        uint32_t  baseInstance;
        uint32_t  pad[3];
    };

    // Fragment uniform for the cube shadow depth pass. Matches CubeShadowFragParams in shadowcube.frag.
    struct CubeShadowFragParams {
        glm::vec4 lightPosFar;         // xyz = light pos, w = far range
    };

    static constexpr uint32_t kShadowRes     = 8192;   // directional shadow map resolution (keep in sync with 'res' in model3d.frag)
    static constexpr uint32_t kCubeShadowRes = 2048;   // per-face point shadow resolution
    static constexpr float    kPointFar      = 30.0f;  // point-shadow far range (distance normaliser)

    // Shadow resources (directional caster). The shadow map stores clip-space depth in R32F;
    // a depth buffer resolves the nearest occluder during the shadow render.
    GpuShaderHandle           shadow_vert_shader = 0;
    GpuShaderHandle           shadow_frag_shader = 0;
    GpuGraphicsPipelineHandle m_shadowPipeline   = 0;
    GpuTextureHandle          shadowColorTex     = 0;   // R32F depth map (sampled in main pass)
    GpuTextureHandle          shadowDepthTex     = 0;   // D32F throwaway for occlusion
    GpuSamplerHandle          shadowSampler      = 0;

    // Shadow resources (point-light cube caster). Stores linear distance-to-light in an R32F cube;
    // rendered one face at a time into a color layer (SDL depth targets can't select a layer).
    GpuShaderHandle           shadowcube_vert_shader = 0;
    GpuShaderHandle           shadowcube_frag_shader = 0;
    GpuGraphicsPipelineHandle m_cubeShadowPipeline   = 0;
    GpuTextureHandle          shadowCubeTex          = 0;   // R32F cube (6 layers), sampled in main pass
    GpuTextureHandle          shadowCubeDepthTex     = 0;   // D32F per-face throwaway
    GpuSamplerHandle          shadowCubeSampler      = 0;

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
