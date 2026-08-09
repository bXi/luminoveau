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

    virtual bool Init(void *windowHandle) = 0;
    virtual void Shutdown()               = 0;
    virtual void WaitIdle()               = 0;

    // ── Frame management ──────────────────────────────────────────────────────

    virtual GpuCmdBufferHandle AcquireCommandBuffer()                      = 0;
    virtual void               SubmitCommandBuffer(GpuCmdBufferHandle cmd) = 0;

    // GPU timing fence (optional). Default: just submit, no fence (0) — backends without
    // fence support report no GPU time. SDL overrides these for the perf HUD.
    virtual GpuFenceHandle SubmitCommandBufferAndAcquireFence(GpuCmdBufferHandle cmd) {
        SubmitCommandBuffer(cmd);
        return 0;
    }
    virtual void WaitFence(GpuFenceHandle /*fence*/) { } // block until the GPU work completes
    virtual void ReleaseFence(GpuFenceHandle /*fence*/) { }

    // Perf HUD: per-frame draw stats + backend name. Defaults = unsupported.
    virtual uint32_t    FrameDrawCalls() const { return 0; }
    virtual uint64_t    FrameDrawVerts() const { return 0; }
    virtual void        ResetFrameDrawStats() { }
    virtual const char *BackendName() const { return "GPU"; }
    // Whether BC/BCn compressed textures can be created. Default true: backends that simply
    // null-return on an unsupported format (SDL/Metal) let the KTX2 loader fall back via that.
    // WebGPU overrides false when the device lacks texture-compression-bc, because there
    // createTexture *throws* instead of returning null, so the loader must skip BC up-front.
    virtual bool SupportsBCTextures() const { return true; }
    // Releases the current swapchain texture/view; called once per frame after the final submit.
    // Default no-op covers backends where the swapchain is presented implicitly by submit.
    virtual void PresentSwapchain() { }

    // Returns a GpuTextureHandle for the swapchain image.
    // width/height are set to the current swapchain dimensions.
    virtual GpuTextureHandle AcquireSwapchainTexture(GpuCmdBufferHandle cmd,
        uint32_t                                                       &outWidth,
        uint32_t                                                       &outHeight)
        = 0;

    virtual GpuTextureFormat GetSwapchainFormat() const = 0;

    // ── Multi-window ──────────────────────────────────────────────────────────
    // One device can drive several OS windows. Claim each secondary window once before
    // rendering to it; ReleaseWindow when it closes. SetSwapchainWindow selects which
    // claimed window AcquireSwapchainTexture / PresentSwapchain / GetSwapchainFormat act on
    // (passing nullptr restores the primary). Defaults suit single-window backends
    // (web/gl/sw): claim/release succeed trivially, the active window never changes.
    virtual bool ClaimWindow(void * /*window*/) { return true; }
    virtual void ReleaseWindow(void * /*window*/) { }
    virtual void SetSwapchainWindow(void * /*window*/) { }

    // ── Render pass ───────────────────────────────────────────────────────────

    virtual GpuRenderPassHandle BeginRenderPass(GpuCmdBufferHandle cmd,
        const GpuColorTargetInfo                                  *colorTargets,
        uint32_t                                                   colorTargetCount,
        const GpuDepthStencilTargetInfo                           *depthTarget)
        = 0;

    virtual void EndRenderPass(GpuRenderPassHandle pass) = 0;

    // ── Compute pass ──────────────────────────────────────────────────────────

    virtual GpuComputePassHandle BeginComputePass(
        GpuCmdBufferHandle              cmd,
        const GpuStorageTextureBinding *readwriteTextures, uint32_t rwTexCount,
        const GpuStorageBufferBinding *readwriteBuffers, uint32_t rwBufCount)
        = 0;

    virtual void EndComputePass(GpuComputePassHandle pass) = 0;

    // ── Pipeline binding ──────────────────────────────────────────────────────

    virtual void BindGraphicsPipeline(GpuRenderPassHandle pass,
        GpuGraphicsPipelineHandle                         pipeline)
        = 0;

    virtual void BindComputePipeline(GpuComputePassHandle pass,
        GpuComputePipelineHandle                          pipeline)
        = 0;

    // ── Vertex / index binding ────────────────────────────────────────────────

    virtual void BindVertexBuffers(GpuRenderPassHandle pass,
        uint32_t                                       firstBinding,
        const GpuBufferBinding                        *bindings,
        uint32_t                                       count)
        = 0;

    virtual void BindIndexBuffer(GpuRenderPassHandle pass,
        GpuBufferBinding                             binding,
        bool                                         use16BitIndices = false)
        = 0;

    // ── Texture / sampler binding ─────────────────────────────────────────────

    virtual void BindVertexSamplers(GpuRenderPassHandle pass,
        uint32_t                                        firstBinding,
        const GpuTextureSamplerBinding                 *bindings,
        uint32_t                                        count)
        = 0;

    virtual void BindFragmentSamplers(GpuRenderPassHandle pass,
        uint32_t                                          firstBinding,
        const GpuTextureSamplerBinding                   *bindings,
        uint32_t                                          count)
        = 0;

    virtual void BindFragmentStorageTextures(GpuRenderPassHandle pass,
        uint32_t                                                 firstBinding,
        const GpuTextureHandle                                  *textures,
        uint32_t                                                 count)
        = 0;

    virtual void BindVertexStorageBuffers(GpuRenderPassHandle pass,
        uint32_t                                              first,
        const GpuBufferHandle                                *buffers,
        uint32_t                                              count)
        = 0;

    virtual void BindComputeSamplers(GpuComputePassHandle pass,
        uint32_t                                          firstBinding,
        const GpuTextureSamplerBinding                   *bindings,
        uint32_t                                          count)
        = 0;

    virtual void BindComputeStorageTextures(GpuComputePassHandle pass,
        uint32_t                                                 firstBinding,
        const GpuTextureHandle                                  *textures,
        uint32_t                                                 count)
        = 0;

    virtual void BindComputeStorageBuffers(GpuComputePassHandle pass,
        uint32_t                                                firstBinding,
        const GpuBufferHandle                                  *buffers,
        uint32_t                                                count)
        = 0;

    // ── Uniform push ──────────────────────────────────────────────────────────

    virtual void PushVertexUniformData(GpuCmdBufferHandle cmd,
        uint32_t                                          slotIndex,
        const void                                       *data,
        uint32_t                                          size)
        = 0;

    virtual void PushFragmentUniformData(GpuCmdBufferHandle cmd,
        uint32_t                                            slotIndex,
        const void                                         *data,
        uint32_t                                            size)
        = 0;

    virtual void PushComputeUniformData(GpuCmdBufferHandle cmd,
        uint32_t                                           slotIndex,
        const void                                        *data,
        uint32_t                                           size)
        = 0;

    // ── Draw calls ────────────────────────────────────────────────────────────

    virtual void DrawPrimitives(GpuRenderPassHandle pass,
        uint32_t                                    vertexCount,
        uint32_t                                    instanceCount = 1,
        uint32_t                                    firstVertex   = 0,
        uint32_t                                    firstInstance = 0)
        = 0;

    virtual void DrawIndexedPrimitives(GpuRenderPassHandle pass,
        uint32_t                                           indexCount,
        uint32_t                                           instanceCount = 1,
        uint32_t                                           firstIndex    = 0,
        int32_t                                            vertexOffset  = 0,
        uint32_t                                           firstInstance = 0)
        = 0;

    // ── Compute dispatch ──────────────────────────────────────────────────────

    virtual void DispatchCompute(GpuComputePassHandle pass,
        uint32_t                                      groupsX,
        uint32_t                                      groupsY,
        uint32_t                                      groupsZ)
        = 0;

    // ── Scissor / viewport ────────────────────────────────────────────────────

    virtual void SetScissor(GpuRenderPassHandle pass,
        int32_t x, int32_t y,
        uint32_t w, uint32_t h)
        = 0;

    virtual void SetViewport(GpuRenderPassHandle pass,
        float x, float y,
        float w, float h,
        float minDepth = 0.0f,
        float maxDepth = 1.0f)
        = 0;

    // ── Resource creation ─────────────────────────────────────────────────────

    virtual GpuTextureHandle          CreateTexture(const GpuTextureCreateInfo &info)                   = 0;
    virtual GpuBufferHandle           CreateBuffer(const GpuBufferCreateInfo &info)                     = 0;
    virtual GpuTransferBufferHandle   CreateTransferBuffer(const GpuTransferBufferCreateInfo &info)     = 0;
    virtual GpuSamplerHandle          CreateSampler(const GpuSamplerCreateInfo &info)                   = 0;
    virtual GpuShaderHandle           CreateShader(const GpuShaderCreateInfo &info)                     = 0;
    virtual GpuGraphicsPipelineHandle CreateGraphicsPipeline(const GpuGraphicsPipelineCreateInfo &info) = 0;
    virtual GpuComputePipelineHandle  CreateComputePipeline(const GpuComputePipelineCreateInfo &info)   = 0;

    // ── SPIRV entry points ────────────────────────────────────────────────────
    //
    // createShader / createComputePipeline above take bytecode already in the build's
    // native format (the pre-compiled built-in shader blobs). These two take *SPIRV* and
    // let the backend translate it for the running device — that is how asset shaders
    // loaded at runtime get onto the GPU.
    //
    // Backends that cannot consume SPIRV (WebGPU wants WGSL) return a null handle.

    /// @brief Creates a graphics shader from SPIRV, translating to the native format if needed.
    /// @param info Shader bytecode (SPIRV), stage and reflected binding counts.
    /// @return A shader handle, or 0 if the backend cannot consume SPIRV.
    virtual GpuShaderHandle CreateShaderFromSPIRV(const GpuShaderCreateInfo &info) = 0;

    /// @brief Creates a compute pipeline from SPIRV, reflecting its resource layout.
    /// @param code SPIRV bytecode.
    /// @param codeSize Size of the bytecode in bytes.
    /// @param entrypoint Entry-point function name.
    /// @param outReflection Filled with the layout reflected out of the shader. May be null.
    /// @return A pipeline handle, or 0 if the backend cannot consume SPIRV.
    virtual GpuComputePipelineHandle CreateComputePipelineFromSPIRV(const uint8_t *code, size_t codeSize,
        const char *entrypoint, GpuComputeReflection *outReflection)
        = 0;

    // ── Resource release ──────────────────────────────────────────────────────

    virtual void ReleaseTexture(GpuTextureHandle handle)                   = 0;
    virtual void ReleaseBuffer(GpuBufferHandle handle)                     = 0;
    virtual void ReleaseTransferBuffer(GpuTransferBufferHandle handle)     = 0;
    virtual void ReleaseSampler(GpuSamplerHandle handle)                   = 0;
    virtual void ReleaseShader(GpuShaderHandle handle)                     = 0;
    virtual void ReleaseGraphicsPipeline(GpuGraphicsPipelineHandle handle) = 0;
    virtual void ReleaseComputePipeline(GpuComputePipelineHandle handle)   = 0;

    // ── Transfer / upload ─────────────────────────────────────────────────────

    virtual void *MapTransferBuffer(GpuTransferBufferHandle handle, bool cycle = false) = 0;
    virtual void  UnmapTransferBuffer(GpuTransferBufferHandle handle)                   = 0;

    virtual void UploadToTexture(GpuCmdBufferHandle cmd,
        const GpuTransferBufferRegion              &src,
        const GpuTextureRegion                     &dst,
        bool                                        cycle = false)
        = 0;

    virtual void UploadToBuffer(GpuCmdBufferHandle cmd,
        GpuTransferBufferHandle src, uint32_t srcOffset,
        GpuBufferHandle dst, uint32_t dstOffset,
        uint32_t size,
        bool     cycle = false)
        = 0;

    virtual void DownloadFromTexture(GpuCmdBufferHandle cmd,
        const GpuTextureRegion                         &src,
        const GpuTransferBufferRegion                  &dst)
        = 0;

    // ── Texture copy ──────────────────────────────────────────────────────────

    virtual void BlitTexture(GpuCmdBufferHandle cmd,
        GpuTextureHandle                        src,
        GpuTextureHandle                        dst,
        uint32_t srcX, uint32_t srcY, uint32_t srcW, uint32_t srcH,
        uint32_t dstX, uint32_t dstY, uint32_t dstW, uint32_t dstH,
        GpuFilter filter = GpuFilter::Linear)
        = 0;

    // ── Screenshot ────────────────────────────────────────────────────────────
    // Two-phase async capture (default impl uses downloadFromTexture + stb_image_write,
    // works on every backend). Override only if a native path is faster (e.g. WebGPU
    // canvas.toBlob via JS bridge).
    //
    //   RequestScreenshot()           — call with a live cmdbuf; stages a download.
    //                                   The cmdbuf must be submitted before processing.
    //   ProcessPendingScreenshots()   — call after submit; waits, maps, writes PNG.

    void RequestScreenshot(GpuCmdBufferHandle cmd,
        GpuTextureHandle                      src,
        uint32_t width, uint32_t height,
        const std::string &filename);

    void ProcessPendingScreenshots();

protected:
    struct PendingScreenshot {
        std::string             filename;
        GpuTransferBufferHandle transferBuffer = 0;
        uint32_t                width          = 0;
        uint32_t                height         = 0;
        size_t                  dataSize       = 0;
        bool                    isBGRA         = false;
    };
    std::vector<PendingScreenshot> _pendingScreenshots;
};
