#pragma once

#include "gpu/IGpu.h"
#include "gpu/IBackendAccess.h"

#include <webgpu/webgpu.h>
#include <vector>
#include <unordered_map>

// emdawnwebgpu names these without the "Flags" suffix.
// Provide aliases so code compiles against both Dawn and emdawnwebgpu.
#ifndef WGPUShaderStageFlags
using WGPUShaderStageFlags  = WGPUShaderStage;
#endif
#ifndef WGPUTextureUsageFlags
using WGPUTextureUsageFlags = WGPUTextureUsage;
#endif
#ifndef WGPUBufferUsageFlags
using WGPUBufferUsageFlags  = WGPUBufferUsage;
#endif

// ─────────────────────────────────────────────────────────────────────────────
// WebGpuGpuBackend — IGpu implementation over the WebGPU C API.
// Compatible with native Dawn and Emscripten (via emdawnwebgpu).
//
// Bind group convention (shared with WGSL shaders):
//   Graphics:  group 0 = vertex uniforms,  group 1 = fragment uniforms,
//              group 2 = frag sampler+tex,  group 3 = vertex storage buffers OR frag storage textures
//   Compute:   group 0 = compute uniforms, group 1 = RW storage textures,
//              group 2 = RW storage buffers, group 3 = compute sampler+tex
//
// Push-uniform emulation: per-draw small WGPUBuffers (mappedAtCreation),
// released at frame end.  A 4MB zero buffer provides padding for unused slots.
// ─────────────────────────────────────────────────────────────────────────────

struct SDL_Window;
struct WgpuGraphicsPipeline;
struct WgpuComputePipelineData;
struct WgpuRenderPass;
struct WgpuComputePass;
struct WgpuUniformCache;

class WebGpuGpuBackend final : public IGpu, public IBackendAccess {
public:
    WebGpuGpuBackend() = default;
    ~WebGpuGpuBackend() override;

    // IGpu ────────────────────────────────────────────────────────────────────

    bool init(void* windowHandle) override;
    void shutdown() override;
    void waitIdle() override;

    GpuCmdBufferHandle acquireCommandBuffer() override;
    void submitCommandBuffer(GpuCmdBufferHandle cmd) override;
    void presentSwapchain() override;

    GpuTextureHandle  acquireSwapchainTexture(GpuCmdBufferHandle cmd,
                                              uint32_t& outWidth,
                                              uint32_t& outHeight) override;
    GpuTextureFormat  getSwapchainFormat() const override;

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
    void bindVertexStorageBuffers(GpuRenderPassHandle pass, uint32_t first,
                                  const GpuBufferHandle* buffers, uint32_t count) override;
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

    void* getRawDevice() const override { return m_device; }
    void* getRawSampler(int /*scaleModeInt*/) const override { return nullptr; }

    // BC creation throws (not null-returns) on WebGPU when the feature is off, so the KTX2
    // loader must check support up-front instead of relying on a failed createTexture.
    bool supportsBCTextures() const override { return m_textureCompressionBC; }

private:
    WGPUInstance m_instance = nullptr;
    WGPUAdapter  m_adapter  = nullptr;
    WGPUDevice   m_device   = nullptr;
    bool         m_float32Filterable = false;
    bool         m_textureCompressionBC = false;   // BC7/BCn upload support (else KTX2 -> RGBA8)
    WGPUQueue    m_queue    = nullptr;
    WGPUSurface  m_surface  = nullptr;

    WGPUTextureFormat m_swapchainFormat = WGPUTextureFormat_BGRA8Unorm;
    uint32_t m_swapchainWidth  = 0;
    uint32_t m_swapchainHeight = 0;

    // Canvas dims captured during init() and never zeroed (m_swapchainWidth/Height get
    // reset to 0 on emscripten to force a first-frame surface reconfigure). Anything
    // running between backend init and the first acquireSwapchainTexture should read
    // these via the public accessors below.
public:
    uint32_t getInitialCanvasWidth()  const { return m_initialCanvasWidth;  }
    uint32_t getInitialCanvasHeight() const { return m_initialCanvasHeight; }
private:
    uint32_t m_initialCanvasWidth  = 0;
    uint32_t m_initialCanvasHeight = 0;

    // Current frame swapchain view (valid between acquireSwapchainTexture and submitCommandBuffer)
    WGPUTexture     m_currentSurfaceTex  = nullptr;
    WGPUTextureView m_currentSurfaceView = nullptr;

    // Small zero buffer used as placeholder for unused uniform slots in bind groups
    WGPUBuffer m_zeroBuffer = nullptr;
    static constexpr uint32_t kZeroBufSize = 1024;

    // Free list of recently-released uniform buffers keyed by size (Firefox doesn't reclaim
    // per-frame allocations fast enough; reusing them avoids "Not enough memory left").
    std::unordered_map<uint32_t, std::vector<WGPUBuffer>> m_uniformBufferPool;
    WGPUBuffer _acquireUniformBuffer(uint32_t alignedSize);
    void       _recycleUniformBuffer(WGPUBuffer buf, uint32_t alignedSize);

    // Cache of fragment-sampler bind groups keyed by (BGLayout, view, sampler) GPU handles.
    // Identical (texture, sampler) tuples on the same pipeline reuse the same BG so per-frame
    // bind-group allocations don't pile up in Firefox's WebGPU sandbox.
    struct SamplerBgKey {
        WGPUBindGroupLayout layout;
        WGPUTextureView     view;
        WGPUSampler         sampler;
        bool operator==(const SamplerBgKey& o) const {
            return layout == o.layout && view == o.view && sampler == o.sampler;
        }
    };
    struct SamplerBgKeyHash {
        size_t operator()(const SamplerBgKey& k) const noexcept {
            auto h = [](uint64_t x) {
                x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
                x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
                return (size_t)(x ^ (x >> 31));
            };
            return h((uintptr_t)k.layout) ^ (h((uintptr_t)k.view) << 1) ^ (h((uintptr_t)k.sampler) << 2);
        }
    };
    std::unordered_map<SamplerBgKey, WGPUBindGroup, SamplerBgKeyHash> m_samplerBgCache;
    void _evictSamplerBgsByView(WGPUTextureView view);
    void _evictSamplerBgsBySampler(WGPUSampler sampler);

    // Helpers
    void _flushVertexUniforms(WgpuRenderPass* rp);
    void _flushFragmentUniforms(WgpuRenderPass* rp);
    void _flushComputeUniforms(WgpuComputePass* cp);

    WGPUBindGroup _makeUniformBindGroup(WGPUBindGroupLayout layout,
                                        const WgpuUniformCache& cache,
                                        uint32_t slotCount,
                                        std::vector<std::pair<WGPUBuffer, uint32_t>>& uniformCleanup);

    WGPUBindGroupLayout _makeEmptyBGL();
    WGPUBindGroupLayout _makeUniformBGL(uint32_t count, WGPUShaderStageFlags visibility);
    // texFirst=false: binding even=sampler, odd=texture (graphics convention, post pair-swap).
    // texFirst=true:  binding even=texture, odd=sampler (compute — matches Tint's native order,
    //                 which the compute path does not pair-swap, and bindComputeSamplers).
    WGPUBindGroupLayout _makeSamplerBGL(uint32_t pairCount, WGPUShaderStageFlags visibility,
                                        bool texFirst = false, uint32_t cubeMask = 0);
    WGPUBindGroupLayout _makeStorageTexBGL(uint32_t count, WGPUShaderStageFlags visibility,
                                           WGPUStorageTextureAccess access,
                                           const GpuTextureFormat* perBindingFormats = nullptr,
                                           const bool* perBindingWriteOnly = nullptr);
    WGPUBindGroupLayout _makeStorageBufBGL(uint32_t count, WGPUShaderStageFlags visibility,
                                           WGPUBufferBindingType type);

    // Type mapping helpers
    static WGPUTextureFormat    toWGPU(GpuTextureFormat fmt);
    static WGPUTextureFormat    depthFormatToWGPU(GpuTextureFormat fmt);
    static WGPUTextureUsageFlags toWGPUUsage(GpuTextureUsage usage);
    static WGPUBufferUsageFlags  toWGPUUsage(GpuBufferUsage usage);
    static WGPUFilterMode        toWGPU(GpuFilter f);
    static WGPUAddressMode       toWGPU(GpuSamplerAddressMode m);
    static WGPUVertexFormat      toWGPU(GpuVertexElementFormat fmt);
    static WGPUBlendFactor       toWGPU(GpuBlendFactor f);
    static WGPUBlendOperation    toWGPU(GpuBlendOp op);
    static WGPULoadOp            toWGPU(GpuLoadOp op);
    static WGPUStoreOp           toWGPU(GpuStoreOp op);
    static GpuTextureFormat      fromWGPU(WGPUTextureFormat fmt);
};
