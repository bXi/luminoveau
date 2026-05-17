#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "gpu/types.h"
#include "types/color.h"
#include "math/rectangles.h"

class RenderPass;
class Geometry2D;
struct FrameBuffer;
struct UniformBuffer;
struct ShaderAsset;
struct SpriteRenderTargetConfig;
struct TextureAsset;
using Texture = TextureAsset&;

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual void initRendering() = 0;
    virtual void close() = 0;

    virtual void startFrame() = 0;
    virtual void endFrame() = 0;
    virtual void reset() = 0;

    virtual void clearBackground(Color color) = 0;

    virtual void addToRenderQueue(const std::string& passname,
                                  const Renderable& renderable) = 0;

    virtual void addShaderPass(const std::string& passname,
                               const ShaderAsset& vert,
                               const ShaderAsset& frag,
                               std::vector<std::string> targetBuffers = {}) = 0;
    virtual void removeShaderPass(const std::string& passname) = 0;

    virtual void attachRenderPassToFrameBuffer(RenderPass* pass,
                                               const std::string& passname,
                                               const std::string& fbName) = 0;

    virtual UniformBuffer& getUniformBuffer(const std::string& passname) = 0;

    virtual void createFrameBuffer(const std::string& fbname) = 0;
    virtual void setFramebufferRenderToScreen(const std::string& fbName, bool render) = 0;
    virtual FrameBuffer* getFramebuffer(const std::string& fbname) = 0;

    virtual uint32_t getZIndex() = 0;

    virtual GpuSamplerHandle    getSampler(ScaleMode mode) = 0;
    virtual GpuRenderPassHandle getRenderPass(const std::string& passname) = 0;
    virtual RenderPass*         findRenderPass(const std::string& passname) = 0;

    virtual void setScissorMode(const std::string& passname, rectf cliprect) = 0;
    virtual void onResize() = 0;
    virtual void updateCameraProjection() = 0;

    virtual Texture whitePixel() = 0;

    virtual Geometry2D* getQuadGeometry() = 0;
    virtual Geometry2D* getCircleGeometry(int segments = 32) = 0;
    virtual Geometry2D* getRoundedRectGeometry(float rx, float ry,
                                               int segments = 8) = 0;

    virtual GpuSampleCount getSampleCount() = 0;
    virtual void setSampleCount(GpuSampleCount count) = 0;

    virtual void createSpriteRenderTarget(const std::string& name,
                                          const SpriteRenderTargetConfig& config = {}) = 0;
    virtual void removeSpriteRenderTarget(const std::string& name,
                                          bool removeFramebuffer = true) = 0;
};
