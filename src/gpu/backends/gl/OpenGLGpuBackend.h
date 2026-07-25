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

    bool Init(void *windowHandle) override;
    void Shutdown() override;
    void WaitIdle() override;

    GpuCmdBufferHandle AcquireCommandBuffer() override;
    void               SubmitCommandBuffer(GpuCmdBufferHandle cmd) override;

    GpuTextureHandle AcquireSwapchainTexture(GpuCmdBufferHandle cmd,
        uint32_t                                               &outWidth,
        uint32_t                                               &outHeight) override;
    GpuTextureFormat GetSwapchainFormat() const override;

    GpuRenderPassHandle BeginRenderPass(GpuCmdBufferHandle cmd,
        const GpuColorTargetInfo                          *colorTargets,
        uint32_t                                           colorTargetCount,
        const GpuDepthStencilTargetInfo                   *depthTarget) override;
    void                EndRenderPass(GpuRenderPassHandle pass) override;

    GpuComputePassHandle BeginComputePass(GpuCmdBufferHandle cmd,
        const GpuStorageTextureBinding *rwTextures, uint32_t rwTexCount,
        const GpuStorageBufferBinding *rwBuffers, uint32_t rwBufCount) override;
    void                 EndComputePass(GpuComputePassHandle pass) override;

    void BindGraphicsPipeline(GpuRenderPassHandle pass, GpuGraphicsPipelineHandle pipeline) override;
    void BindComputePipeline(GpuComputePassHandle pass, GpuComputePipelineHandle pipeline) override;

    void BindVertexBuffers(GpuRenderPassHandle pass, uint32_t firstBinding,
        const GpuBufferBinding *bindings, uint32_t count) override;
    void BindIndexBuffer(GpuRenderPassHandle pass, GpuBufferBinding binding,
        bool use16BitIndices) override;

    void BindVertexSamplers(GpuRenderPassHandle pass, uint32_t firstBinding,
        const GpuTextureSamplerBinding *bindings, uint32_t count) override;
    void BindFragmentSamplers(GpuRenderPassHandle pass, uint32_t firstBinding,
        const GpuTextureSamplerBinding *bindings, uint32_t count) override;
    void BindFragmentStorageTextures(GpuRenderPassHandle pass, uint32_t firstBinding,
        const GpuTextureHandle *textures, uint32_t count) override;
    void BindVertexStorageBuffers(GpuRenderPassHandle /*pass*/, uint32_t /*first*/,
        const GpuBufferHandle * /*buffers*/, uint32_t /*count*/) override { }
    void BindComputeSamplers(GpuComputePassHandle pass, uint32_t firstBinding,
        const GpuTextureSamplerBinding *bindings, uint32_t count) override;
    void BindComputeStorageTextures(GpuComputePassHandle pass, uint32_t firstBinding,
        const GpuTextureHandle *textures, uint32_t count) override;
    void BindComputeStorageBuffers(GpuComputePassHandle pass, uint32_t firstBinding,
        const GpuBufferHandle *buffers, uint32_t count) override;

    void PushVertexUniformData(GpuCmdBufferHandle cmd, uint32_t slot,
        const void *data, uint32_t size) override;
    void PushFragmentUniformData(GpuCmdBufferHandle cmd, uint32_t slot,
        const void *data, uint32_t size) override;
    void PushComputeUniformData(GpuCmdBufferHandle cmd, uint32_t slot,
        const void *data, uint32_t size) override;

    void DrawPrimitives(GpuRenderPassHandle pass, uint32_t vertexCount,
        uint32_t instanceCount, uint32_t firstVertex,
        uint32_t firstInstance) override;
    void DrawIndexedPrimitives(GpuRenderPassHandle pass, uint32_t indexCount,
        uint32_t instanceCount, uint32_t firstIndex,
        int32_t vertexOffset, uint32_t firstInstance) override;

    void DispatchCompute(GpuComputePassHandle pass,
        uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) override;

    void SetScissor(GpuRenderPassHandle pass,
        int32_t x, int32_t y, uint32_t w, uint32_t h) override;
    void SetViewport(GpuRenderPassHandle pass,
        float x, float y, float w, float h,
        float minDepth, float maxDepth) override;

    GpuTextureHandle          CreateTexture(const GpuTextureCreateInfo &info) override;
    GpuBufferHandle           CreateBuffer(const GpuBufferCreateInfo &info) override;
    GpuTransferBufferHandle   CreateTransferBuffer(const GpuTransferBufferCreateInfo &info) override;
    GpuSamplerHandle          CreateSampler(const GpuSamplerCreateInfo &info) override;
    GpuShaderHandle           CreateShader(const GpuShaderCreateInfo &info) override;
    GpuGraphicsPipelineHandle CreateGraphicsPipeline(const GpuGraphicsPipelineCreateInfo &info) override;
    GpuComputePipelineHandle  CreateComputePipeline(const GpuComputePipelineCreateInfo &info) override;

    void ReleaseTexture(GpuTextureHandle handle) override;
    void ReleaseBuffer(GpuBufferHandle handle) override;
    void ReleaseTransferBuffer(GpuTransferBufferHandle handle) override;
    void ReleaseSampler(GpuSamplerHandle handle) override;
    void ReleaseShader(GpuShaderHandle handle) override;
    void ReleaseGraphicsPipeline(GpuGraphicsPipelineHandle handle) override;
    void ReleaseComputePipeline(GpuComputePipelineHandle handle) override;

    void *MapTransferBuffer(GpuTransferBufferHandle handle, bool cycle) override;
    void  UnmapTransferBuffer(GpuTransferBufferHandle handle) override;

    void UploadToTexture(GpuCmdBufferHandle cmd,
        const GpuTransferBufferRegion      &src,
        const GpuTextureRegion             &dst,
        bool                                cycle) override;
    void UploadToBuffer(GpuCmdBufferHandle cmd,
        GpuTransferBufferHandle src, uint32_t srcOffset,
        GpuBufferHandle dst, uint32_t dstOffset,
        uint32_t size, bool cycle) override;
    void DownloadFromTexture(GpuCmdBufferHandle cmd,
        const GpuTextureRegion                 &src,
        const GpuTransferBufferRegion          &dst) override;

    void BlitTexture(GpuCmdBufferHandle cmd,
        GpuTextureHandle src, GpuTextureHandle dst,
        uint32_t srcX, uint32_t srcY, uint32_t srcW, uint32_t srcH,
        uint32_t dstX, uint32_t dstY, uint32_t dstW, uint32_t dstH,
        GpuFilter filter) override;

    // IBackendAccess ──────────────────────────────────────────────────────────

    void *GetRawDevice() const override { return nullptr; }
    void *GetRawSampler(int /*scaleModeInt*/) const override { return nullptr; }
};
