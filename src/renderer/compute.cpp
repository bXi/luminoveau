#include "compute.h"
#include "renderer.h"
#include "core/log/log.h"
#include "gpu/IGpu.h"

#include <algorithm>
#include <vector>
#include <utility>
#include <cstring>

void Compute::_clearBuilderState() {
    _pipeline = 0;
    _tcX = _tcY = _tcZ = 1;
    _readTextures.clear();
    _readWriteTextures.clear();
    _readBuffers.clear();
    _readWriteBuffers.clear();
    _uniforms.clear();
}
/// @endcond

// -----------------------------------------------------------------
// Public API
// -----------------------------------------------------------------

void Compute::_setPipeline(const ComputePipelineAsset &pipeline) {
    _pipeline = pipeline.pipeline;
    _tcX      = pipeline.threadcount_x;
    _tcY      = pipeline.threadcount_y;
    _tcZ      = pipeline.threadcount_z;
}

void Compute::_bindReadTexture(uint32_t slot, GpuTextureHandle tex) {
    if (slot >= _readTextures.size())
        _readTextures.resize(slot + 1, 0);
    _readTextures[slot] = tex;
}

void Compute::_bindReadTexture(uint32_t slot, const TextureAsset &tex) {
    _bindReadTexture(slot, tex.gpuTexture);
}

void Compute::_bindReadWriteTexture(uint32_t slot, GpuTextureHandle tex, uint32_t mipLevel, uint32_t layer) {
    _readWriteTextures.push_back({ slot, { tex, mipLevel, layer } });
}

void Compute::_bindReadWriteTexture(uint32_t slot, const TextureAsset &tex, uint32_t mipLevel, uint32_t layer) {
    _bindReadWriteTexture(slot, tex.gpuTexture, mipLevel, layer);
}

void Compute::_bindReadBuffer(uint32_t slot, GpuBufferHandle buf) {
    if (slot >= _readBuffers.size())
        _readBuffers.resize(slot + 1, 0);
    _readBuffers[slot] = buf;
}

void Compute::_bindReadWriteBuffer(uint32_t slot, GpuBufferHandle buf) {
    _readWriteBuffers.push_back({ slot, buf });
}

void Compute::_pushUniform(uint32_t slot, const void *data, uint32_t size) {
    std::vector<uint8_t> bytes(
        static_cast<const uint8_t *>(data),
        static_cast<const uint8_t *>(data) + size);
    auto it = std::find_if(_uniforms.begin(), _uniforms.end(),
        [slot](const auto &p) { return p.first == slot; });
    if (it != _uniforms.end()) {
        it->second = std::move(bytes);
    } else {
        _uniforms.push_back({ slot, std::move(bytes) });
    }
}

/// @cond INTERNAL
void Compute::_enqueueDispatch(uint32_t gx, uint32_t gy, uint32_t gz) {
    if (!_pipeline) {
        LOG_ERROR("Compute::Dispatch called without SetPipeline");
        return;
    }
    DispatchRecord rec;
    rec.pipeline          = _pipeline;
    rec.threadcount_x     = _tcX;
    rec.threadcount_y     = _tcY;
    rec.threadcount_z     = _tcZ;
    rec.readTextures      = _readTextures;
    rec.readWriteTextures = _readWriteTextures;
    rec.readBuffers       = _readBuffers;
    rec.readWriteBuffers  = _readWriteBuffers;
    rec.uniforms          = _uniforms;
    rec.groupX            = gx;
    rec.groupY            = gy;
    rec.groupZ            = gz;
    _queue.push_back(std::move(rec));
    _clearBuilderState();
}
/// @endcond

void Compute::_dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) {
    _enqueueDispatch(groupX, groupY, groupZ);
}

void Compute::_dispatchAuto(uint32_t totalX, uint32_t totalY, uint32_t totalZ) {
    if (!_pipeline) {
        LOG_ERROR("Compute::DispatchAuto called without SetPipeline");
        return;
    }
    const uint32_t gx = (totalX + _tcX - 1) / _tcX;
    const uint32_t gy = (totalY + _tcY - 1) / _tcY;
    const uint32_t gz = (totalZ + _tcZ - 1) / _tcZ;
    _enqueueDispatch(gx, gy, gz);
}

// -----------------------------------------------------------------
// Buffer helpers
// -----------------------------------------------------------------

GpuBufferHandle Compute::_createBuffer(uint32_t size, GpuBufferUsage usage) {
    GpuBufferCreateInfo info { size, usage };
    GpuBufferHandle     buf = Renderer::GetGpu().createBuffer(info);
    if (!buf)
        LOG_ERROR("Compute::CreateBuffer failed ({} bytes)", size);
    return buf;
}

void Compute::_uploadBufferData(GpuBufferHandle buffer, const void *data, uint32_t size) {
    IGpu                   &gpu = Renderer::GetGpu();
    GpuTransferBufferHandle tb  = gpu.createTransferBuffer({ size, GpuTransferUsage::Upload });
    if (!tb) {
        LOG_ERROR("Compute::UploadBufferData: failed to create transfer buffer");
        return;
    }

    void *mapped = gpu.mapTransferBuffer(tb, false);
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

void Compute::_destroyBuffer(GpuBufferHandle buffer) {
    if (buffer)
        Renderer::GetGpu().releaseBuffer(buffer);
}

/// @cond INTERNAL
// -----------------------------------------------------------------
// Internal
// -----------------------------------------------------------------

void Compute::_executeQueued(GpuCmdBufferHandle cmdBuf) {
    if (_queue.empty())
        return;
    IGpu &gpu = Renderer::GetGpu();

    for (const auto &rec : _queue) {
        auto rwTexSorted = rec.readWriteTextures;
        std::sort(rwTexSorted.begin(), rwTexSorted.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });

        std::vector<GpuStorageTextureBinding> rwTexBindings;
        rwTexBindings.reserve(rwTexSorted.size());
        for (const auto &[slot, bind] : rwTexSorted) {
            rwTexBindings.push_back({ bind.tex, bind.mipLevel, bind.layer, false });
        }

        auto rwBufSorted = rec.readWriteBuffers;
        std::sort(rwBufSorted.begin(), rwBufSorted.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });

        std::vector<GpuStorageBufferBinding> rwBufBindings;
        rwBufBindings.reserve(rwBufSorted.size());
        for (const auto &[slot, buf] : rwBufSorted) {
            rwBufBindings.push_back({ buf, false });
        }

        GpuComputePassHandle computePass = gpu.beginComputePass(
            cmdBuf,
            rwTexBindings.empty() ? nullptr : rwTexBindings.data(),
            static_cast<uint32_t>(rwTexBindings.size()),
            rwBufBindings.empty() ? nullptr : rwBufBindings.data(),
            static_cast<uint32_t>(rwBufBindings.size()));

        if (!computePass) {
            LOG_ERROR("Compute::ExecuteQueued: beginComputePass failed");
            continue;
        }

        gpu.bindComputePipeline(computePass, rec.pipeline);

        for (const auto &[slot, bytes] : rec.uniforms) {
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

void Compute::_reset() {
    _queue.clear();
    _clearBuilderState();
}
/// @endcond

