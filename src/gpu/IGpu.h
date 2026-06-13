#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/types.h"

// ─────────────────────────────────────────────────────────────────────────────
// Compute pass binding structs (used in beginComputePass)
// ─────────────────────────────────────────────────────────────────────────────

struct GpuStorageTextureBinding {
    GpuTextureHandle texture  = 0;
    uint32_t         mipLevel = 0;
    uint32_t         layer    = 0;
    bool             cycle    = false;
};

struct GpuStorageBufferBinding {
    GpuBufferHandle buffer = 0;
    bool            cycle  = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// IGpu — GPU backend interface.
// Implemented by SdlGpuBackend, OpenGLGpuBackend, SoftwareGpuBackend, etc.
// All methods use engine types only — no SDL/GL/platform headers here.
// ─────────────────────────────────────────────────────────────────────────────

class IGpu {
public:
    virtual ~IGpu() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    virtual bool init(void* windowHandle) = 0;
    virtual void shutdown() = 0;
    virtual void waitIdle() = 0;

    // ── Frame management ──────────────────────────────────────────────────────

    virtual GpuCmdBufferHandle acquireCommandBuffer() = 0;
    virtual void submitCommandBuffer(GpuCmdBufferHandle cmd) = 0;

    // GPU timing fence (optional). Default: just submit, no fence (0) — backends without
    // fence support report no GPU time. SDL overrides these for the perf HUD.
    virtual GpuFenceHandle submitCommandBufferAndAcquireFence(GpuCmdBufferHandle cmd) {
        submitCommandBuffer(cmd); return 0;
    }
    virtual void waitFence(GpuFenceHandle /*fence*/) {}      // block until the GPU work completes
    virtual void releaseFence(GpuFenceHandle /*fence*/) {}

    // Perf HUD: per-frame draw stats + backend name. Defaults = unsupported.
    virtual uint32_t frameDrawCalls() const { return 0; }
    virtual uint64_t frameDrawVerts() const { return 0; }
    virtual void     resetFrameDrawStats()   {}
    virtual const char *backendName() const  { return "GPU"; }
    // Whether BC/BCn compressed textures can be created. Default true: backends that simply
    // null-return on an unsupported format (SDL/Metal) let the KTX2 loader fall back via that.
    // WebGPU overrides false when the device lacks texture-compression-bc, because there
    // createTexture *throws* instead of returning null, so the loader must skip BC up-front.
    virtual bool supportsBCTextures() const { return true; }
    // Releases the current swapchain texture/view; called once per frame after the final submit.
    // Default no-op covers backends where the swapchain is presented implicitly by submit.
    virtual void presentSwapchain() {}

    // Returns a GpuTextureHandle for the swapchain image.
    // width/height are set to the current swapchain dimensions.
    virtual GpuTextureHandle acquireSwapchainTexture(GpuCmdBufferHandle cmd,
                                                     uint32_t& outWidth,
                                                     uint32_t& outHeight) = 0;

    virtual GpuTextureFormat getSwapchainFormat() const = 0;

    // ── Render pass ───────────────────────────────────────────────────────────

    virtual GpuRenderPassHandle beginRenderPass(GpuCmdBufferHandle             cmd,
                                                const GpuColorTargetInfo*      colorTargets,
                                                uint32_t                       colorTargetCount,
                                                const GpuDepthStencilTargetInfo* depthTarget) = 0;

    virtual void endRenderPass(GpuRenderPassHandle pass) = 0;

    // ── Compute pass ──────────────────────────────────────────────────────────

    virtual GpuComputePassHandle beginComputePass(
        GpuCmdBufferHandle              cmd,
        const GpuStorageTextureBinding* readwriteTextures, uint32_t rwTexCount,
        const GpuStorageBufferBinding*  readwriteBuffers,  uint32_t rwBufCount) = 0;

    virtual void endComputePass(GpuComputePassHandle pass) = 0;

    // ── Pipeline binding ──────────────────────────────────────────────────────

    virtual void bindGraphicsPipeline(GpuRenderPassHandle pass,
                                      GpuGraphicsPipelineHandle pipeline) = 0;

    virtual void bindComputePipeline(GpuComputePassHandle pass,
                                     GpuComputePipelineHandle pipeline) = 0;

    // ── Vertex / index binding ────────────────────────────────────────────────

    virtual void bindVertexBuffers(GpuRenderPassHandle pass,
                                   uint32_t firstBinding,
                                   const GpuBufferBinding* bindings,
                                   uint32_t count) = 0;

    virtual void bindIndexBuffer(GpuRenderPassHandle pass,
                                 GpuBufferBinding binding,
                                 bool use16BitIndices = false) = 0;

    // ── Texture / sampler binding ─────────────────────────────────────────────

    virtual void bindVertexSamplers(GpuRenderPassHandle pass,
                                    uint32_t firstBinding,
                                    const GpuTextureSamplerBinding* bindings,
                                    uint32_t count) = 0;

    virtual void bindFragmentSamplers(GpuRenderPassHandle pass,
                                      uint32_t firstBinding,
                                      const GpuTextureSamplerBinding* bindings,
                                      uint32_t count) = 0;

    virtual void bindFragmentStorageTextures(GpuRenderPassHandle pass,
                                             uint32_t firstBinding,
                                             const GpuTextureHandle* textures,
                                             uint32_t count) = 0;

    virtual void bindVertexStorageBuffers(GpuRenderPassHandle pass,
                                          uint32_t first,
                                          const GpuBufferHandle* buffers,
                                          uint32_t count) = 0;

    virtual void bindComputeSamplers(GpuComputePassHandle pass,
                                     uint32_t firstBinding,
                                     const GpuTextureSamplerBinding* bindings,
                                     uint32_t count) = 0;

    virtual void bindComputeStorageTextures(GpuComputePassHandle pass,
                                            uint32_t firstBinding,
                                            const GpuTextureHandle* textures,
                                            uint32_t count) = 0;

    virtual void bindComputeStorageBuffers(GpuComputePassHandle pass,
                                           uint32_t firstBinding,
                                           const GpuBufferHandle* buffers,
                                           uint32_t count) = 0;

    // ── Uniform push ──────────────────────────────────────────────────────────

    virtual void pushVertexUniformData(GpuCmdBufferHandle cmd,
                                       uint32_t slotIndex,
                                       const void* data,
                                       uint32_t size) = 0;

    virtual void pushFragmentUniformData(GpuCmdBufferHandle cmd,
                                         uint32_t slotIndex,
                                         const void* data,
                                         uint32_t size) = 0;

    virtual void pushComputeUniformData(GpuCmdBufferHandle cmd,
                                        uint32_t slotIndex,
                                        const void* data,
                                        uint32_t size) = 0;

    // ── Draw calls ────────────────────────────────────────────────────────────

    virtual void drawPrimitives(GpuRenderPassHandle pass,
                                uint32_t vertexCount,
                                uint32_t instanceCount = 1,
                                uint32_t firstVertex   = 0,
                                uint32_t firstInstance = 0) = 0;

    virtual void drawIndexedPrimitives(GpuRenderPassHandle pass,
                                       uint32_t indexCount,
                                       uint32_t instanceCount = 1,
                                       uint32_t firstIndex    = 0,
                                       int32_t  vertexOffset  = 0,
                                       uint32_t firstInstance = 0) = 0;

    // ── Compute dispatch ──────────────────────────────────────────────────────

    virtual void dispatchCompute(GpuComputePassHandle pass,
                                 uint32_t groupsX,
                                 uint32_t groupsY,
                                 uint32_t groupsZ) = 0;

    // ── Scissor / viewport ────────────────────────────────────────────────────

    virtual void setScissor(GpuRenderPassHandle pass,
                            int32_t x, int32_t y,
                            uint32_t w, uint32_t h) = 0;

    virtual void setViewport(GpuRenderPassHandle pass,
                             float x, float y,
                             float w, float h,
                             float minDepth = 0.0f,
                             float maxDepth = 1.0f) = 0;

    // ── Resource creation ─────────────────────────────────────────────────────

    virtual GpuTextureHandle          createTexture(const GpuTextureCreateInfo& info) = 0;
    virtual GpuBufferHandle           createBuffer(const GpuBufferCreateInfo& info) = 0;
    virtual GpuTransferBufferHandle   createTransferBuffer(const GpuTransferBufferCreateInfo& info) = 0;
    virtual GpuSamplerHandle          createSampler(const GpuSamplerCreateInfo& info) = 0;
    virtual GpuShaderHandle           createShader(const GpuShaderCreateInfo& info) = 0;
    virtual GpuGraphicsPipelineHandle createGraphicsPipeline(const GpuGraphicsPipelineCreateInfo& info) = 0;
    virtual GpuComputePipelineHandle  createComputePipeline(const GpuComputePipelineCreateInfo& info) = 0;

    // ── Resource release ──────────────────────────────────────────────────────

    virtual void releaseTexture(GpuTextureHandle handle) = 0;
    virtual void releaseBuffer(GpuBufferHandle handle) = 0;
    virtual void releaseTransferBuffer(GpuTransferBufferHandle handle) = 0;
    virtual void releaseSampler(GpuSamplerHandle handle) = 0;
    virtual void releaseShader(GpuShaderHandle handle) = 0;
    virtual void releaseGraphicsPipeline(GpuGraphicsPipelineHandle handle) = 0;
    virtual void releaseComputePipeline(GpuComputePipelineHandle handle) = 0;

    // ── Transfer / upload ─────────────────────────────────────────────────────

    virtual void* mapTransferBuffer(GpuTransferBufferHandle handle, bool cycle = false) = 0;
    virtual void  unmapTransferBuffer(GpuTransferBufferHandle handle) = 0;

    virtual void uploadToTexture(GpuCmdBufferHandle cmd,
                                 const GpuTransferBufferRegion& src,
                                 const GpuTextureRegion& dst,
                                 bool cycle = false) = 0;

    virtual void uploadToBuffer(GpuCmdBufferHandle cmd,
                                GpuTransferBufferHandle src, uint32_t srcOffset,
                                GpuBufferHandle dst,         uint32_t dstOffset,
                                uint32_t size,
                                bool cycle = false) = 0;

    virtual void downloadFromTexture(GpuCmdBufferHandle cmd,
                                     const GpuTextureRegion& src,
                                     const GpuTransferBufferRegion& dst) = 0;

    // ── Texture copy ──────────────────────────────────────────────────────────

    virtual void blitTexture(GpuCmdBufferHandle cmd,
                             GpuTextureHandle src,
                             GpuTextureHandle dst,
                             uint32_t srcX, uint32_t srcY, uint32_t srcW, uint32_t srcH,
                             uint32_t dstX, uint32_t dstY, uint32_t dstW, uint32_t dstH,
                             GpuFilter filter = GpuFilter::Linear) = 0;

    // ── Screenshot ────────────────────────────────────────────────────────────
    // Two-phase async capture (default impl uses downloadFromTexture + stb_image_write,
    // works on every backend). Override only if a native path is faster (e.g. WebGPU
    // canvas.toBlob via JS bridge).
    //
    //   requestScreenshot()           — call with a live cmdbuf; stages a download.
    //                                   The cmdbuf must be submitted before processing.
    //   processPendingScreenshots()   — call after submit; waits, maps, writes PNG.

    void requestScreenshot(GpuCmdBufferHandle cmd,
                           GpuTextureHandle src,
                           uint32_t width, uint32_t height,
                           const std::string& filename);

    void processPendingScreenshots();

protected:
    struct PendingScreenshot {
        std::string             filename;
        GpuTransferBufferHandle transferBuffer = 0;
        uint32_t                width          = 0;
        uint32_t                height         = 0;
        size_t                  dataSize       = 0;
        bool                    isBGRA         = false;
    };
    std::vector<PendingScreenshot> m_pendingScreenshots;
};
