#pragma once

#include "gpu/IGpu.h"
#include "gpu/IBackendAccess.h"

// ─────────────────────────────────────────────────────────────────────────────
// OpenGLGpuBackend — IGpu implementation over OpenGL 4.5+ / OpenGL ES 3.0+.
// Compatible with desktop, Emscripten (WebGL2), and Android (GLES3).
// TODO: implement
// ─────────────────────────────────────────────────────────────────────────────

class OpenGLGpuBackend final : public IGpu, public IBackendAccess {
public:
    OpenGLGpuBackend() = default;
    ~OpenGLGpuBackend() override;

    bool init(void* windowHandle) override;
    void shutdown() override;
    void waitIdle() override;

    GpuCmdBufferHandle acquireCommandBuffer() override;
    void submitCommandBuffer(GpuCmdBufferHandle cmd) override;

    GpuTextureHandle acquireSwapchainTexture(GpuCmdBufferHandle cmd,
                                             uint32_t& outWidth,
                                             uint32_t& outHeight) override;
    GpuTextureFormat getSwapchainFormat() const override;

    GpuRenderPassHandle beginRenderPass(GpuCmdBufferHandle cmd,
                                        const GpuColorTargetInfo* colorTargets,
                                        uint32_t colorTargetCount,
                                        const GpuDepthStencilTargetInfo* depthTarget) override;
    void endRenderPass(GpuRenderPassHandle pass) override;

    GpuComputePassHandle beginComputePass(GpuCmdBufferHandle cmd,
                                          const GpuStorageTextureBinding* rwTextures, uint32_t rwTexCount,
                                          const GpuStorageBufferBinding*  rwBuffers,  uint32_t rwBufCount) override;
    void endComputePass(GpuComputePassHandle pass) override;

    void bindGraphicsPipeline(GpuRenderPassHandle pass, GpuGraphicsPipelineHandle pipeline) override;
    void bindComputePipeline(GpuComputePassHandle pass, GpuComputePipelineHandle pipeline) override;

    void bindVertexBuffers(GpuRenderPassHandle pass, uint32_t firstBinding,
                           const GpuBufferBinding* bindings, uint32_t count) override;
    void bindIndexBuffer(GpuRenderPassHandle pass, GpuBufferBinding binding,
                         bool use16BitIndices) override;

    void bindVertexSamplers(GpuRenderPassHandle pass, uint32_t firstBinding,
                            const GpuTextureSamplerBinding* bindings, uint32_t count) override;
    void bindFragmentSamplers(GpuRenderPassHandle pass, uint32_t firstBinding,
                              const GpuTextureSamplerBinding* bindings, uint32_t count) override;
    void bindFragmentStorageTextures(GpuRenderPassHandle pass, uint32_t firstBinding,
                                     const GpuTextureHandle* textures, uint32_t count) override;
    void bindVertexStorageBuffers(GpuRenderPassHandle /*pass*/, uint32_t /*first*/,
                                  const GpuBufferHandle* /*buffers*/, uint32_t /*count*/) override {}
    void bindComputeSamplers(GpuComputePassHandle pass, uint32_t firstBinding,
                             const GpuTextureSamplerBinding* bindings, uint32_t count) override;
    void bindComputeStorageTextures(GpuComputePassHandle pass, uint32_t firstBinding,
                                    const GpuTextureHandle* textures, uint32_t count) override;
    void bindComputeStorageBuffers(GpuComputePassHandle pass, uint32_t firstBinding,
                                   const GpuBufferHandle* buffers, uint32_t count) override;

    void pushVertexUniformData(GpuCmdBufferHandle cmd, uint32_t slot,
                               const void* data, uint32_t size) override;
    void pushFragmentUniformData(GpuCmdBufferHandle cmd, uint32_t slot,
                                 const void* data, uint32_t size) override;
    void pushComputeUniformData(GpuCmdBufferHandle cmd, uint32_t slot,
                                const void* data, uint32_t size) override;

    void drawPrimitives(GpuRenderPassHandle pass, uint32_t vertexCount,
                        uint32_t instanceCount, uint32_t firstVertex,
                        uint32_t firstInstance) override;
    void drawIndexedPrimitives(GpuRenderPassHandle pass, uint32_t indexCount,
                               uint32_t instanceCount, uint32_t firstIndex,
                               int32_t vertexOffset, uint32_t firstInstance) override;

    void dispatchCompute(GpuComputePassHandle pass,
                         uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) override;

    void setScissor(GpuRenderPassHandle pass,
                    int32_t x, int32_t y, uint32_t w, uint32_t h) override;
    void setViewport(GpuRenderPassHandle pass,
                     float x, float y, float w, float h,
                     float minDepth, float maxDepth) override;

    GpuTextureHandle          createTexture(const GpuTextureCreateInfo& info) override;
    GpuBufferHandle           createBuffer(const GpuBufferCreateInfo& info) override;
    GpuTransferBufferHandle   createTransferBuffer(const GpuTransferBufferCreateInfo& info) override;
    GpuSamplerHandle          createSampler(const GpuSamplerCreateInfo& info) override;
    GpuShaderHandle           createShader(const GpuShaderCreateInfo& info) override;
    GpuGraphicsPipelineHandle createGraphicsPipeline(const GpuGraphicsPipelineCreateInfo& info) override;
    GpuComputePipelineHandle  createComputePipeline(const GpuComputePipelineCreateInfo& info) override;

    void releaseTexture(GpuTextureHandle handle) override;
    void releaseBuffer(GpuBufferHandle handle) override;
    void releaseTransferBuffer(GpuTransferBufferHandle handle) override;
    void releaseSampler(GpuSamplerHandle handle) override;
    void releaseShader(GpuShaderHandle handle) override;
    void releaseGraphicsPipeline(GpuGraphicsPipelineHandle handle) override;
    void releaseComputePipeline(GpuComputePipelineHandle handle) override;

    void* mapTransferBuffer(GpuTransferBufferHandle handle, bool cycle) override;
    void  unmapTransferBuffer(GpuTransferBufferHandle handle) override;

    void uploadToTexture(GpuCmdBufferHandle cmd,
                         const GpuTransferBufferRegion& src,
                         const GpuTextureRegion& dst,
                         bool cycle) override;
    void uploadToBuffer(GpuCmdBufferHandle cmd,
                        GpuTransferBufferHandle src, uint32_t srcOffset,
                        GpuBufferHandle dst, uint32_t dstOffset,
                        uint32_t size, bool cycle) override;
    void downloadFromTexture(GpuCmdBufferHandle cmd,
                             const GpuTextureRegion& src,
                             const GpuTransferBufferRegion& dst) override;

    void blitTexture(GpuCmdBufferHandle cmd,
                     GpuTextureHandle src, GpuTextureHandle dst,
                     uint32_t srcX, uint32_t srcY, uint32_t srcW, uint32_t srcH,
                     uint32_t dstX, uint32_t dstY, uint32_t dstW, uint32_t dstH,
                     GpuFilter filter) override;

    // IBackendAccess ──────────────────────────────────────────────────────────

    void* getRawDevice() const override { return nullptr; }
    void* getRawSampler(int /*scaleModeInt*/) const override { return nullptr; }
};
