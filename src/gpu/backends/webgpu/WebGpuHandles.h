#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <cstring>
#include <unordered_map>

#include <webgpu/webgpu.h>

#include "gpu/types.h"

// ─────────────────────────────────────────────────────────────────────────────
// WebGPU internal handle structs.
// Heap-allocated; pointer cast to/from uintptr_t handles.
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Bind group layout group indices (convention used across all pipelines)
// ─────────────────────────────────────────────────────────────────────────────
//  Graphics:
//    0 = vertex uniform buffers     (binding i = slot i)
//    1 = fragment uniform buffers   (binding i = slot i)
//    2 = fragment sampler+texture   (binding 2i = texture, 2i+1 = sampler)
//    3 = vertex storage buffers (read-only) OR fragment storage textures (binding i)
//  Compute:
//    0 = compute uniform buffers    (binding i = slot i)
//    1 = read-only storage buffers  (binding i = slot i)
//    2 = read-write storage buffers (binding i = slot i)
//    3 = compute sampler+texture    (binding 2i = texture, 2i+1 = sampler)
//    4 = RW storage textures        (binding i)

static constexpr uint32_t kWgpuMaxUniformSlots = 8;

struct WgpuTexture {
    WGPUTexture     texture     = nullptr;
    WGPUTextureView defaultView = nullptr;         // sampling view (Cube dimension for cube maps)
    WGPUTextureView faceViews[6] = {};             // per-layer 2D render views (cube/array targets)
    WGPUTextureFormat format    = WGPUTextureFormat_Undefined;
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t layerCount = 1;
};

struct WgpuBuffer {
    WGPUBuffer buffer = nullptr;
    uint32_t   size   = 0;
};

// CPU-side staging for uploads; separate GPU buffer for downloads
struct WgpuTransferBuffer {
    std::vector<uint8_t> stagingData;    // upload staging
    WGPUBuffer           downloadBuffer = nullptr; // download only
    uint32_t             size           = 0;
    bool                 isDownload     = false;
};

struct WgpuSampler {
    WGPUSampler sampler = nullptr;
};

struct WgpuShader {
    WGPUShaderModule module     = nullptr;
    std::string      entrypoint;
    GpuShaderStage   stage      = GpuShaderStage::Vertex;
    uint32_t samplerCount         = 0;
    uint32_t uniformBufferCount   = 0;
    uint32_t storageBufferCount   = 0;
    uint32_t storageTextureCount  = 0;
    uint32_t samplerCubeMask      = 0;   // bit i = sampler pair i is a cube texture
};

struct WgpuGraphicsPipeline {
    WGPURenderPipeline   pipeline = nullptr;
    WGPUPipelineLayout   layout   = nullptr;
    WGPUBindGroupLayout  bgLayouts[4] = {};
    // Persistent empty bind groups for slots whose BGL is null but lies before lastGroup.
    // Firefox enforces strict bind-group presence even for empty layouts; these are bound
    // at every bindGraphicsPipeline call so the draw never has a missing slot.
    WGPUBindGroupLayout  emptyBGL     = nullptr;
    WGPUBindGroup        emptyBG[4]   = {};

    // Resource counts (from vertex+fragment shader info)
    uint32_t vertexUniformCount         = 0;
    uint32_t fragmentUniformCount       = 0;
    uint32_t fragmentSamplerCount       = 0;
    uint32_t fragmentSamplerCubeMask    = 0;
    uint32_t fragmentStorageTexCount    = 0;
    uint32_t vertexStorageBufCount      = 0;
};

struct WgpuComputePipelineData {
    WGPUComputePipeline  pipeline = nullptr;
    WGPUPipelineLayout   layout   = nullptr;
    WGPUBindGroupLayout  bgLayouts[5] = {};  // 0=uniform, 1=ro bufs, 2=rw bufs, 3=samplers, 4=rw tex

    uint32_t uniformCount         = 0;
    uint32_t roStorageBufferCount = 0;
    uint32_t rwStorageBufferCount = 0;
    uint32_t samplerCount         = 0;
    uint32_t roStorageTexCount    = 0;
    uint32_t rwStorageTexCount    = 0;
};

// Per-draw uniform data cache (stored in render/compute pass context)
struct WgpuUniformCache {
    struct Slot {
        std::vector<uint8_t> data;
        bool dirty = false;
    };
    std::array<Slot, kWgpuMaxUniformSlots> slots;

    void set(uint32_t i, const void* d, uint32_t sz) {
        if (i >= kWgpuMaxUniformSlots) return;
        slots[i].data.resize(sz);
        std::memcpy(slots[i].data.data(), d, sz);
        slots[i].dirty = true;
    }
    void clear() {
        for (auto& s : slots) { s.dirty = false; }
    }
};

// Per-frame cleanup lists owned by WgpuCmdBuffer
struct WgpuFrameCleanup {
    std::vector<WGPUBuffer>                          tempBuffers;
    std::vector<std::pair<WGPUBuffer, uint32_t>>     tempUniformBuffers; // (buffer, alignedSize)
    std::vector<WGPUBindGroup>                       tempBindGroups;

    void releaseAll() {
        for (auto b  : tempBuffers)             wgpuBufferRelease(b);
        for (auto& p : tempUniformBuffers)      wgpuBufferRelease(p.first);
        for (auto bg : tempBindGroups)          wgpuBindGroupRelease(bg);
        tempBuffers.clear();
        tempUniformBuffers.clear();
        tempBindGroups.clear();
    }
};

struct WgpuCmdBuffer {
    WGPUCommandEncoder encoder = nullptr;

    // Uniform data pending flush before next draw/dispatch
    WgpuUniformCache vertexUniforms;
    WgpuUniformCache fragmentUniforms;
    WgpuUniformCache computeUniforms;

    WgpuFrameCleanup cleanup;
};

struct WgpuRenderPass {
    WGPURenderPassEncoder      encoder         = nullptr;
    WgpuCmdBuffer*             cmdBuf          = nullptr;
    const WgpuGraphicsPipeline* currentPipeline = nullptr;
    WGPUDevice                 device          = nullptr;
    WGPUQueue                  queue           = nullptr;
};

struct WgpuComputePass {
    WGPUComputePassEncoder          encoder         = nullptr;
    WgpuCmdBuffer*                  cmdBuf          = nullptr;
    const WgpuComputePipelineData*  currentPipeline = nullptr;
    WGPUDevice                      device          = nullptr;
    WGPUQueue                       queue           = nullptr;

    // RW storage buffers supplied via beginComputePass; bound to group 2 at dispatch time.
    struct RWBuf { WGPUBuffer buf; uint64_t size; };
    std::vector<RWBuf> pendingRWBuffers;

    // RW storage textures supplied via beginComputePass; bound to group 2 at dispatch time.
    struct RWTex { WGPUTextureView view; };
    std::vector<RWTex> pendingRWTextures;
};
