#include "compute.h"
#include "renderer.h"
#include "core/log/log.h"
#include "gpu/IGpu.h"

#include <algorithm>
#include <vector>
#include <utility>
#include <cstring>

namespace Compute {

// -----------------------------------------------------------------
// Internal dispatch record
// -----------------------------------------------------------------

/// @cond INTERNAL
struct RWTextureBind {
    GpuTextureHandle tex;
    uint32_t         mipLevel;
    uint32_t         layer;
};

struct DispatchRecord {
    GpuComputePipelineHandle pipeline    = 0;
    uint32_t threadcount_x = 1, threadcount_y = 1, threadcount_z = 1;

    std::vector<GpuTextureHandle>                              readTextures;
    std::vector<std::pair<uint32_t, RWTextureBind>>            readWriteTextures;
    std::vector<GpuBufferHandle>                               readBuffers;
    std::vector<std::pair<uint32_t, GpuBufferHandle>>          readWriteBuffers;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>>     uniforms;

    uint32_t groupX = 1, groupY = 1, groupZ = 1;
};
/// @endcond

/// @cond INTERNAL
// -----------------------------------------------------------------
// Builder state
// -----------------------------------------------------------------

static GpuComputePipelineHandle                             s_pipeline = 0;
static uint32_t s_tcX = 1, s_tcY = 1, s_tcZ = 1;

static std::vector<GpuTextureHandle>                               s_readTextures;
static std::vector<std::pair<uint32_t, RWTextureBind>>             s_readWriteTextures;
static std::vector<GpuBufferHandle>                                s_readBuffers;
static std::vector<std::pair<uint32_t, GpuBufferHandle>>           s_readWriteBuffers;
static std::vector<std::pair<uint32_t, std::vector<uint8_t>>>      s_uniforms;

static std::vector<DispatchRecord> s_queue;

// -----------------------------------------------------------------

static void _clearBuilderState() {
    s_pipeline = 0;
    s_tcX = s_tcY = s_tcZ = 1;
    s_readTextures.clear();
    s_readWriteTextures.clear();
    s_readBuffers.clear();
    s_readWriteBuffers.clear();
    s_uniforms.clear();
}
/// @endcond

// -----------------------------------------------------------------
// Public API
// -----------------------------------------------------------------

void SetPipeline(const ComputePipelineAsset& pipeline) {
    s_pipeline = pipeline.pipeline;
    s_tcX = pipeline.threadcount_x;
    s_tcY = pipeline.threadcount_y;
    s_tcZ = pipeline.threadcount_z;
}

void BindReadTexture(uint32_t slot, GpuTextureHandle tex) {
    if (slot >= s_readTextures.size()) s_readTextures.resize(slot + 1, 0);
    s_readTextures[slot] = tex;
}

void BindReadTexture(uint32_t slot, const TextureAsset& tex) {
    BindReadTexture(slot, tex.gpuTexture);
}

void BindReadWriteTexture(uint32_t slot, GpuTextureHandle tex, uint32_t mipLevel, uint32_t layer) {
    s_readWriteTextures.push_back({slot, {tex, mipLevel, layer}});
}

void BindReadWriteTexture(uint32_t slot, const TextureAsset& tex, uint32_t mipLevel, uint32_t layer) {
    BindReadWriteTexture(slot, tex.gpuTexture, mipLevel, layer);
}

void BindReadBuffer(uint32_t slot, GpuBufferHandle buf) {
    if (slot >= s_readBuffers.size()) s_readBuffers.resize(slot + 1, 0);
    s_readBuffers[slot] = buf;
}

void BindReadWriteBuffer(uint32_t slot, GpuBufferHandle buf) {
    s_readWriteBuffers.push_back({slot, buf});
}

void PushUniform(uint32_t slot, const void* data, uint32_t size) {
    std::vector<uint8_t> bytes(
        static_cast<const uint8_t*>(data),
        static_cast<const uint8_t*>(data) + size
    );
    auto it = std::find_if(s_uniforms.begin(), s_uniforms.end(),
                           [slot](const auto& p){ return p.first == slot; });
    if (it != s_uniforms.end()) {
        it->second = std::move(bytes);
    } else {
        s_uniforms.push_back({slot, std::move(bytes)});
    }
}

/// @cond INTERNAL
static void _enqueueDispatch(uint32_t gx, uint32_t gy, uint32_t gz) {
    if (!s_pipeline) {
        LOG_ERROR("Compute::Dispatch called without SetPipeline");
        return;
    }
    DispatchRecord rec;
    rec.pipeline          = s_pipeline;
    rec.threadcount_x     = s_tcX;
    rec.threadcount_y     = s_tcY;
    rec.threadcount_z     = s_tcZ;
    rec.readTextures      = s_readTextures;
    rec.readWriteTextures = s_readWriteTextures;
    rec.readBuffers       = s_readBuffers;
    rec.readWriteBuffers  = s_readWriteBuffers;
    rec.uniforms          = s_uniforms;
    rec.groupX = gx;
    rec.groupY = gy;
    rec.groupZ = gz;
    s_queue.push_back(std::move(rec));
    _clearBuilderState();
}
/// @endcond

void Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) {
    _enqueueDispatch(groupX, groupY, groupZ);
}

void DispatchAuto(uint32_t totalX, uint32_t totalY, uint32_t totalZ) {
    if (!s_pipeline) {
        LOG_ERROR("Compute::DispatchAuto called without SetPipeline");
        return;
    }
    const uint32_t gx = (totalX + s_tcX - 1) / s_tcX;
    const uint32_t gy = (totalY + s_tcY - 1) / s_tcY;
    const uint32_t gz = (totalZ + s_tcZ - 1) / s_tcZ;
    _enqueueDispatch(gx, gy, gz);
}

// -----------------------------------------------------------------
// Buffer helpers
// -----------------------------------------------------------------

GpuBufferHandle CreateBuffer(uint32_t size, GpuBufferUsage usage) {
    GpuBufferCreateInfo info{ size, usage };
    GpuBufferHandle buf = Renderer::GetGpu().createBuffer(info);
    if (!buf) LOG_ERROR("Compute::CreateBuffer failed ({} bytes)", size);
    return buf;
}

void UploadBufferData(GpuBufferHandle buffer, const void* data, uint32_t size) {
    IGpu& gpu = Renderer::GetGpu();
    GpuTransferBufferHandle tb = gpu.createTransferBuffer({ size, GpuTransferUsage::Upload });
    if (!tb) { LOG_ERROR("Compute::UploadBufferData: failed to create transfer buffer"); return; }

    void* mapped = gpu.mapTransferBuffer(tb, false);
    if (!mapped) {
        LOG_ERROR("Compute::UploadBufferData: failed to map transfer buffer");
        gpu.releaseTransferBuffer(tb);
        return;
    }
    std::memcpy(mapped, data, size);
    gpu.unmapTransferBuffer(tb);

    GpuCmdBufferHandle cmd = gpu.acquireCommandBuffer();
    gpu.uploadToBuffer(cmd, tb, 0, buffer, 0, size);
    gpu.submitCommandBuffer(cmd);
    // No waitIdle: queue ordering guarantees the upload completes before any later dispatch
    // that reads the buffer, and releasing the transfer buffer after submit is deferred-safe.
    gpu.releaseTransferBuffer(tb);
}

void DestroyBuffer(GpuBufferHandle buffer) {
    if (buffer) Renderer::GetGpu().releaseBuffer(buffer);
}

/// @cond INTERNAL
// -----------------------------------------------------------------
// Internal
// -----------------------------------------------------------------

void _ExecuteQueued(GpuCmdBufferHandle cmdBuf) {
    if (s_queue.empty()) return;
    IGpu& gpu = Renderer::GetGpu();

    for (const auto& rec : s_queue) {
        auto rwTexSorted = rec.readWriteTextures;
        std::sort(rwTexSorted.begin(), rwTexSorted.end(),
                  [](const auto& a, const auto& b){ return a.first < b.first; });

        std::vector<GpuStorageTextureBinding> rwTexBindings;
        rwTexBindings.reserve(rwTexSorted.size());
        for (const auto& [slot, bind] : rwTexSorted) {
            rwTexBindings.push_back({ bind.tex, bind.mipLevel, bind.layer, false });
        }

        auto rwBufSorted = rec.readWriteBuffers;
        std::sort(rwBufSorted.begin(), rwBufSorted.end(),
                  [](const auto& a, const auto& b){ return a.first < b.first; });

        std::vector<GpuStorageBufferBinding> rwBufBindings;
        rwBufBindings.reserve(rwBufSorted.size());
        for (const auto& [slot, buf] : rwBufSorted) {
            rwBufBindings.push_back({ buf, false });
        }

        GpuComputePassHandle computePass = gpu.beginComputePass(
            cmdBuf,
            rwTexBindings.empty() ? nullptr : rwTexBindings.data(),
            static_cast<uint32_t>(rwTexBindings.size()),
            rwBufBindings.empty() ? nullptr : rwBufBindings.data(),
            static_cast<uint32_t>(rwBufBindings.size())
        );

        if (!computePass) {
            LOG_ERROR("Compute::_ExecuteQueued: beginComputePass failed");
            continue;
        }

        gpu.bindComputePipeline(computePass, rec.pipeline);

        for (const auto& [slot, bytes] : rec.uniforms) {
            gpu.pushComputeUniformData(cmdBuf, slot, bytes.data(),
                                       static_cast<uint32_t>(bytes.size()));
        }

        if (!rec.readTextures.empty()) {
            gpu.bindComputeStorageTextures(computePass, 0,
                rec.readTextures.data(),
                static_cast<uint32_t>(rec.readTextures.size()));
        }

        if (!rec.readBuffers.empty()) {
            gpu.bindComputeStorageBuffers(computePass, 0,
                rec.readBuffers.data(),
                static_cast<uint32_t>(rec.readBuffers.size()));
        }

        gpu.dispatchCompute(computePass, rec.groupX, rec.groupY, rec.groupZ);
        gpu.endComputePass(computePass);
    }
}

void _Reset() {
    s_queue.clear();
    _clearBuilderState();
}
/// @endcond

} // namespace Compute
