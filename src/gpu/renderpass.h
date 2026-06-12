#pragma once

#include <string>

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "gpu/types.h"
#include "assets/texture/texture.h"
#include "gpu/renderable.h"
#include "gpu/buffer/uniformobject.h"

class RenderPass {
protected:
    struct Uniforms {
        glm::mat4 camera;
        glm::mat4 model;
        glm::vec2 flipped;

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

    GpuGraphicsPipelineHandle m_pipeline = 0;
    TextureAsset              m_depth_texture;
    std::string               passname;

public:
    GpuLoadOp        color_target_info_loadop      = GpuLoadOp::Load;
    float            color_target_clear_r           = 0.0f;
    float            color_target_clear_g           = 0.0f;
    float            color_target_clear_b           = 0.0f;
    float            color_target_clear_a           = 0.0f;
    GpuTextureHandle renderTargetDepth              = 0;
    GpuTextureHandle renderTargetResolve            = 0;

    virtual bool needsResolvedInput() const { return false; }

    RenderPass(const RenderPass&)            = delete;
    RenderPass& operator=(const RenderPass&) = delete;
    RenderPass(RenderPass&&)                 = delete;
    RenderPass& operator=(RenderPass&&)      = delete;

    RenderPass() = default;
    virtual ~RenderPass() = default;

    virtual bool init(GpuTextureFormat swapchainFormat,
                      uint32_t surfaceWidth, uint32_t surfaceHeight,
                      std::string name, bool logInit = true,
                      size_t capacity = 0, bool forceNoMSAA = false) = 0;

    virtual void release(bool logRelease = true) = 0;

    // Cheap window-resize hook: recreate ONLY size-dependent targets (not pipelines/geometry).
    // Default no-op for passes whose targets are framebuffer/desktop-sized with a live viewport.
    virtual void onResize(uint32_t /*surfaceWidth*/, uint32_t /*surfaceHeight*/) {}

    virtual void render(GpuCmdBufferHandle  cmdBuffer,
                        GpuTextureHandle    targetTexture,
                        const glm::mat4&    camera) = 0;

    virtual void addToRenderQueue(const Renderable& renderable) = 0;
    virtual void resetRenderQueue() = 0;
    virtual UniformBuffer& getUniformBuffer() = 0;

    GpuRenderPassHandle render_pass = 0;

    bool     _scissorEnabled = false;
    int32_t  _scissorX       = 0;
    int32_t  _scissorY       = 0;
    uint32_t _scissorW       = 0;
    uint32_t _scissorH       = 0;
};
