#pragma once

#include "gpu/IGpu.h"
#include "gpu/IBackendAccess.h"

#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────
// Citro3dGpuBackend — IGpu implementation over citro3d / PICA200 (Nintendo 3DS).
//
// Scope (M1): the 2D path only. Vertex/index/texture resources, one render pass
// at a time, the sprite pipeline (CPU-expanded vertices, see
// renderer/passes/n3ds/spriterenderpass.cpp) and the framebuffer->screen
// composite. No compute, no SPIRV, no MSAA, no fences.
//
// Conventions (all orientation decisions live in this backend):
//  * The top screen's physical framebuffer is 240x400, rotated 90°. Matrices
//    pushed while rendering to the screen get the citro3d "tilt" fix-up
//    pre-multiplied; offscreen targets get only the GL->PICA depth remap.
//  * Textures are padded to power-of-two. Logical UVs are mapped onto the POT
//    texture in the vertex shader via a per-texture scale/offset uniform
//    ("uvscale"), including the t-axis flip PICA's bottom-origin layout needs.
//  * "Fragment shaders" are TEV combiner presets (the PICA200 has none).
// ─────────────────────────────────────────────────────────────────────────────

// TEV combiner presets, passed to CreateShader for GpuShaderStage::Fragment via
// GpuShaderCreateInfo::code (pointer to one of these values, codeSize = 4).
enum class CtrTevPreset : uint32_t {
    Modulate = 1, // texture x vertex color — sprites, text, composite blit
};

class Citro3dGpuBackend final : public IGpu, public IBackendAccess {
public:
    Citro3dGpuBackend() = default;
    ~Citro3dGpuBackend() override;

    // ── IBackendAccess ────────────────────────────────────────────────────────
    void *GetRawDevice() const override { return nullptr; }
    void *GetRawSampler(int /*scaleModeInt*/) const override { return nullptr; }

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    bool Init(void *windowHandle) override;
    void Shutdown() override;
    void WaitIdle() override;

    // ── Frame management ──────────────────────────────────────────────────────
    GpuCmdBufferHandle AcquireCommandBuffer() override;
    void               SubmitCommandBuffer(GpuCmdBufferHandle cmd) override;
    void               PresentSwapchain() override;

    GpuTextureHandle AcquireSwapchainTexture(GpuCmdBufferHandle cmd,
        uint32_t &outWidth, uint32_t &outHeight) override;
    GpuTextureFormat GetSwapchainFormat() const override;

    uint32_t    FrameDrawCalls() const override { return _frameDrawCalls; }
    uint64_t    FrameDrawVerts() const override { return _frameDrawVerts; }
    void        ResetFrameDrawStats() override { _frameDrawCalls = 0; _frameDrawVerts = 0; }
    const char *BackendName() const override { return "citro3d"; }
    bool        SupportsBCTextures() const override { return false; }

    // ── Render pass ───────────────────────────────────────────────────────────
    GpuRenderPassHandle BeginRenderPass(GpuCmdBufferHandle cmd,
        const GpuColorTargetInfo *colorTargets, uint32_t colorTargetCount,
        const GpuDepthStencilTargetInfo *depthTarget) override;
    void EndRenderPass(GpuRenderPassHandle pass) override;

    // ── Compute (unsupported on PICA200 — all stubs) ──────────────────────────
    GpuComputePassHandle BeginComputePass(GpuCmdBufferHandle cmd,
        const GpuStorageTextureBinding *rwTextures, uint32_t rwTexCount,
        const GpuStorageBufferBinding *rwBuffers, uint32_t rwBufCount) override;
    void EndComputePass(GpuComputePassHandle pass) override;
    void BindComputePipeline(GpuComputePassHandle pass, GpuComputePipelineHandle pipeline) override;
    void BindComputeSamplers(GpuComputePassHandle pass, uint32_t firstBinding,
        const GpuTextureSamplerBinding *bindings, uint32_t count) override;
    void BindComputeStorageTextures(GpuComputePassHandle pass, uint32_t firstBinding,
        const GpuTextureHandle *textures, uint32_t count) override;
    void BindComputeStorageBuffers(GpuComputePassHandle pass, uint32_t firstBinding,
        const GpuBufferHandle *buffers, uint32_t count) override;
    void PushComputeUniformData(GpuCmdBufferHandle cmd, uint32_t slot,
        const void *data, uint32_t size) override;
    void DispatchCompute(GpuComputePassHandle pass,
        uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) override;

    // ── Pipeline / resource binding ───────────────────────────────────────────
    void BindGraphicsPipeline(GpuRenderPassHandle pass, GpuGraphicsPipelineHandle pipeline) override;
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
    void BindVertexStorageBuffers(GpuRenderPassHandle pass, uint32_t first,
        const GpuBufferHandle *buffers, uint32_t count) override;

    // ── Uniforms ──────────────────────────────────────────────────────────────
    void PushVertexUniformData(GpuCmdBufferHandle cmd, uint32_t slotIndex,
        const void *data, uint32_t size) override;
    void PushFragmentUniformData(GpuCmdBufferHandle cmd, uint32_t slotIndex,
        const void *data, uint32_t size) override;

    // ── Draws ─────────────────────────────────────────────────────────────────
    void DrawPrimitives(GpuRenderPassHandle pass, uint32_t vertexCount,
        uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
    void DrawIndexedPrimitives(GpuRenderPassHandle pass, uint32_t indexCount,
        uint32_t instanceCount, uint32_t firstIndex,
        int32_t vertexOffset, uint32_t firstInstance) override;

    // ── Scissor / viewport ────────────────────────────────────────────────────
    void SetScissor(GpuRenderPassHandle pass, int32_t x, int32_t y,
        uint32_t w, uint32_t h) override;
    void SetViewport(GpuRenderPassHandle pass, float x, float y,
        float w, float h, float minDepth, float maxDepth) override;

    // ── Resource creation / release ───────────────────────────────────────────
    GpuTextureHandle          CreateTexture(const GpuTextureCreateInfo &info) override;
    GpuBufferHandle           CreateBuffer(const GpuBufferCreateInfo &info) override;
    GpuTransferBufferHandle   CreateTransferBuffer(const GpuTransferBufferCreateInfo &info) override;
    GpuSamplerHandle          CreateSampler(const GpuSamplerCreateInfo &info) override;
    GpuShaderHandle           CreateShader(const GpuShaderCreateInfo &info) override;
    GpuGraphicsPipelineHandle CreateGraphicsPipeline(const GpuGraphicsPipelineCreateInfo &info) override;
    GpuComputePipelineHandle  CreateComputePipeline(const GpuComputePipelineCreateInfo &info) override;

    GpuShaderHandle          CreateShaderFromSPIRV(const GpuShaderCreateInfo &info) override;
    GpuComputePipelineHandle CreateComputePipelineFromSPIRV(const uint8_t *code, size_t codeSize,
        const char *entrypoint, GpuComputeReflection *outReflection) override;

    void ReleaseTexture(GpuTextureHandle handle) override;
    void ReleaseBuffer(GpuBufferHandle handle) override;
    void ReleaseTransferBuffer(GpuTransferBufferHandle handle) override;
    void ReleaseSampler(GpuSamplerHandle handle) override;
    void ReleaseShader(GpuShaderHandle handle) override;
    void ReleaseGraphicsPipeline(GpuGraphicsPipelineHandle handle) override;
    void ReleaseComputePipeline(GpuComputePipelineHandle handle) override;

    // ── Transfer ──────────────────────────────────────────────────────────────
    void *MapTransferBuffer(GpuTransferBufferHandle handle, bool cycle) override;
    void  UnmapTransferBuffer(GpuTransferBufferHandle handle) override;

    void UploadToTexture(GpuCmdBufferHandle cmd, const GpuTransferBufferRegion &src,
        const GpuTextureRegion &dst, bool cycle) override;
    void UploadToBuffer(GpuCmdBufferHandle cmd, GpuTransferBufferHandle src, uint32_t srcOffset,
        GpuBufferHandle dst, uint32_t dstOffset, uint32_t size, bool cycle) override;
    void DownloadFromTexture(GpuCmdBufferHandle cmd, const GpuTextureRegion &src,
        const GpuTransferBufferRegion &dst) override;

    void BlitTexture(GpuCmdBufferHandle cmd, GpuTextureHandle src, GpuTextureHandle dst,
        uint32_t srcX, uint32_t srcY, uint32_t srcW, uint32_t srcH,
        uint32_t dstX, uint32_t dstY, uint32_t dstW, uint32_t dstH,
        GpuFilter filter) override;

private:
    // Frame state. One command buffer exists conceptually; it becomes "the frame"
    // the moment the renderer acquires the swapchain on it (C3D_FrameBegin), and
    // plain upload acquisitions never open a frame (transfers run immediately).
    bool _inFrame = false;

    uint32_t _frameDrawCalls = 0;
    uint64_t _frameDrawVerts = 0;

    // Opaque struct definitions + all citro3d types live in the .cpp — this header
    // must stay includable without <citro3d.h> (renderer/n3ds/init.cpp includes it
    // alongside engine headers that define conflicting names).
    struct State;
    State *_s = nullptr;
};
