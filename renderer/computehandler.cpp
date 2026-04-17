#include "computehandler.h"
#include "rendererhandler.h"
#include "../log/loghandler.h"

#include <algorithm>
#include <vector>
#include <utility>

namespace Compute {

// -----------------------------------------------------------------
// Internal dispatch record — one entry per Dispatch() call
// -----------------------------------------------------------------

struct RWTextureBind {
    SDL_GPUTexture* tex;
    uint32_t        mipLevel;
    uint32_t        layer;
};

struct DispatchRecord {
    SDL_GPUComputePipeline* pipeline = nullptr;
    uint32_t threadcount_x = 1, threadcount_y = 1, threadcount_z = 1;

    // Read-only textures indexed by slot (gaps filled with nullptr)
    std::vector<SDL_GPUTexture*> readTextures;

    // Read-write textures: (slot, binding) — sorted by slot at execution time
    std::vector<std::pair<uint32_t, RWTextureBind>> readWriteTextures;

    // Read-only buffers indexed by slot
    std::vector<SDL_GPUBuffer*> readBuffers;

    // Read-write buffers: (slot, buffer) — sorted by slot at execution time
    std::vector<std::pair<uint32_t, SDL_GPUBuffer*>> readWriteBuffers;

    // Uniform data per slot
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> uniforms;

    uint32_t groupX = 1, groupY = 1, groupZ = 1;
};

// -----------------------------------------------------------------
// Builder state (accumulates between SetPipeline/Bind* and Dispatch)
// -----------------------------------------------------------------

static SDL_GPUComputePipeline* s_pipeline    = nullptr;
static uint32_t s_tcX = 1, s_tcY = 1, s_tcZ = 1;

static std::vector<SDL_GPUTexture*>                             s_readTextures;
static std::vector<std::pair<uint32_t, RWTextureBind>>          s_readWriteTextures;
static std::vector<SDL_GPUBuffer*>                              s_readBuffers;
static std::vector<std::pair<uint32_t, SDL_GPUBuffer*>>         s_readWriteBuffers;
static std::vector<std::pair<uint32_t, std::vector<uint8_t>>>   s_uniforms;

static std::vector<DispatchRecord> s_queue;

// -----------------------------------------------------------------

static void _clearBuilderState() {
    s_pipeline = nullptr;
    s_tcX = s_tcY = s_tcZ = 1;
    s_readTextures.clear();
    s_readWriteTextures.clear();
    s_readBuffers.clear();
    s_readWriteBuffers.clear();
    s_uniforms.clear();
}

// -----------------------------------------------------------------
// Public API
// -----------------------------------------------------------------

void SetPipeline(const ComputePipelineAsset& pipeline) {
    s_pipeline = pipeline.pipeline;
    s_tcX = pipeline.threadcount_x;
    s_tcY = pipeline.threadcount_y;
    s_tcZ = pipeline.threadcount_z;
}

void BindReadTexture(uint32_t slot, SDL_GPUTexture* tex) {
    if (slot >= s_readTextures.size()) s_readTextures.resize(slot + 1, nullptr);
    s_readTextures[slot] = tex;
}

void BindReadTexture(uint32_t slot, const TextureAsset& tex) {
    BindReadTexture(slot, tex.gpuTexture);
}

void BindReadWriteTexture(uint32_t slot, SDL_GPUTexture* tex,
                          uint32_t mipLevel, uint32_t layer) {
    s_readWriteTextures.push_back({slot, {tex, mipLevel, layer}});
}

void BindReadWriteTexture(uint32_t slot, const TextureAsset& tex,
                          uint32_t mipLevel, uint32_t layer) {
    BindReadWriteTexture(slot, tex.gpuTexture, mipLevel, layer);
}

void BindReadBuffer(uint32_t slot, SDL_GPUBuffer* buf) {
    if (slot >= s_readBuffers.size()) s_readBuffers.resize(slot + 1, nullptr);
    s_readBuffers[slot] = buf;
}

void BindReadWriteBuffer(uint32_t slot, SDL_GPUBuffer* buf) {
    s_readWriteBuffers.push_back({slot, buf});
}

void PushUniform(uint32_t slot, const void* data, uint32_t size) {
    std::vector<uint8_t> bytes(
        static_cast<const uint8_t*>(data),
        static_cast<const uint8_t*>(data) + size
    );
    // Replace any existing entry for this slot
    auto it = std::find_if(s_uniforms.begin(), s_uniforms.end(),
                           [slot](const auto& p){ return p.first == slot; });
    if (it != s_uniforms.end()) {
        it->second = std::move(bytes);
    } else {
        s_uniforms.push_back({slot, std::move(bytes)});
    }
}

static void _enqueueDispatch(uint32_t gx, uint32_t gy, uint32_t gz) {
    if (!s_pipeline) {
        LOG_ERROR("Compute::Dispatch called without SetPipeline");
        return;
    }
    DispatchRecord rec;
    rec.pipeline        = s_pipeline;
    rec.threadcount_x   = s_tcX;
    rec.threadcount_y   = s_tcY;
    rec.threadcount_z   = s_tcZ;
    rec.readTextures    = s_readTextures;
    rec.readWriteTextures = s_readWriteTextures;
    rec.readBuffers     = s_readBuffers;
    rec.readWriteBuffers = s_readWriteBuffers;
    rec.uniforms        = s_uniforms;
    rec.groupX = gx;
    rec.groupY = gy;
    rec.groupZ = gz;
    s_queue.push_back(std::move(rec));
    _clearBuilderState();
}

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

SDL_GPUBuffer* CreateBuffer(uint32_t size, SDL_GPUBufferUsageFlags usage) {
    SDL_GPUBufferCreateInfo info = {
        .usage = usage,
        .size  = size,
        .props = 0
    };
    SDL_GPUBuffer* buf = SDL_CreateGPUBuffer(Renderer::GetDevice(), &info);
    if (!buf) {
        LOG_ERROR("Compute::CreateBuffer failed ({} bytes): {}", size, SDL_GetError());
    }
    return buf;
}

void UploadBufferData(SDL_GPUBuffer* buffer, const void* data, uint32_t size) {
    SDL_GPUDevice* device = Renderer::GetDevice();

    SDL_GPUTransferBufferCreateInfo tbInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = size,
        .props = 0
    };
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    if (!tb) {
        LOG_ERROR("Compute::UploadBufferData: failed to create transfer buffer: {}", SDL_GetError());
        return;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, tb, false);
    if (!mapped) {
        LOG_ERROR("Compute::UploadBufferData: failed to map transfer buffer: {}", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, tb);
        return;
    }
    SDL_memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation src = {.transfer_buffer = tb, .offset = 0};
    SDL_GPUBufferRegion            dst = {.buffer = buffer,      .offset = 0, .size = size};
    SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_WaitForGPUIdle(device);

    SDL_ReleaseGPUTransferBuffer(device, tb);
}

void DestroyBuffer(SDL_GPUBuffer* buffer) {
    if (buffer) SDL_ReleaseGPUBuffer(Renderer::GetDevice(), buffer);
}

// -----------------------------------------------------------------
// Internal
// -----------------------------------------------------------------

void _ExecuteQueued(SDL_GPUCommandBuffer* cmdBuf) {
    for (const auto& rec : s_queue) {
        // Push uniform data onto the command buffer before the pass
        for (const auto& [slot, bytes] : rec.uniforms) {
            SDL_PushGPUComputeUniformData(cmdBuf, slot,
                                          bytes.data(),
                                          static_cast<uint32_t>(bytes.size()));
        }

        // Build sorted RW texture bindings
        auto rwTexSorted = rec.readWriteTextures;
        std::sort(rwTexSorted.begin(), rwTexSorted.end(),
                  [](const auto& a, const auto& b){ return a.first < b.first; });

        std::vector<SDL_GPUStorageTextureReadWriteBinding> rwTexBindings;
        rwTexBindings.reserve(rwTexSorted.size());
        for (const auto& [slot, bind] : rwTexSorted) {
            rwTexBindings.push_back({
                .texture   = bind.tex,
                .mip_level = bind.mipLevel,
                .layer     = bind.layer,
                .cycle     = false,
            });
        }

        // Build sorted RW buffer bindings
        auto rwBufSorted = rec.readWriteBuffers;
        std::sort(rwBufSorted.begin(), rwBufSorted.end(),
                  [](const auto& a, const auto& b){ return a.first < b.first; });

        std::vector<SDL_GPUStorageBufferReadWriteBinding> rwBufBindings;
        rwBufBindings.reserve(rwBufSorted.size());
        for (const auto& [slot, buf] : rwBufSorted) {
            rwBufBindings.push_back({.buffer = buf, .cycle = false});
        }

        SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(
            cmdBuf,
            rwTexBindings.empty() ? nullptr : rwTexBindings.data(),
            static_cast<uint32_t>(rwTexBindings.size()),
            rwBufBindings.empty() ? nullptr : rwBufBindings.data(),
            static_cast<uint32_t>(rwBufBindings.size())
        );

        if (!computePass) {
            LOG_ERROR("SDL_BeginGPUComputePass failed: {}", SDL_GetError());
            continue;
        }

        SDL_BindGPUComputePipeline(computePass, rec.pipeline);

        if (!rec.readTextures.empty()) {
            SDL_BindGPUComputeStorageTextures(computePass, 0,
                rec.readTextures.data(),
                static_cast<uint32_t>(rec.readTextures.size()));
        }

        if (!rec.readBuffers.empty()) {
            SDL_BindGPUComputeStorageBuffers(computePass, 0,
                rec.readBuffers.data(),
                static_cast<uint32_t>(rec.readBuffers.size()));
        }

        SDL_DispatchGPUCompute(computePass, rec.groupX, rec.groupY, rec.groupZ);

        SDL_EndGPUComputePass(computePass);
    }
}

void _Reset() {
    s_queue.clear();
    _clearBuilderState();
}

} // namespace Compute
