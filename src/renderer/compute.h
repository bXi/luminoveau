#pragma once

#include <cstdint>

#include "gpu/types.h"
#include "assets/compute/computepipeline.h"
#include "assets/texture/texture.h"

/**
 * @brief Compute dispatch API — mirrors the Draw:: pattern for graphics.
 *
 * Usage per frame:
 *   Compute::SetPipeline(pipeline);
 *   Compute::BindReadWriteBuffer(0, particleBuffer);
 *   Compute::PushUniform(0, deltaTime);
 *   Compute::DispatchAuto(numParticles);   // groups = ceil(n / threadcount_x)
 *
 * All queued dispatches execute at the start of EndFrame, before any render
 * passes, so their outputs are ready for sampling in the same frame.
 */
namespace Compute {

    // -----------------------------------------------------------------
    // State setters — call any combination before Dispatch / DispatchAuto
    // -----------------------------------------------------------------

    void SetPipeline(const ComputePipelineAsset& pipeline);

    void BindReadTexture(uint32_t slot, GpuTextureHandle tex);
    void BindReadTexture(uint32_t slot, const TextureAsset& tex);

    void BindReadWriteTexture(uint32_t slot, GpuTextureHandle tex,
                              uint32_t mipLevel = 0, uint32_t layer = 0);
    void BindReadWriteTexture(uint32_t slot, const TextureAsset& tex,
                              uint32_t mipLevel = 0, uint32_t layer = 0);

    void BindReadBuffer(uint32_t slot, GpuBufferHandle buf);
    void BindReadWriteBuffer(uint32_t slot, GpuBufferHandle buf);

    void PushUniform(uint32_t slot, const void* data, uint32_t size);

    template<typename T>
    void PushUniform(uint32_t slot, const T& value) {
        PushUniform(slot, &value, static_cast<uint32_t>(sizeof(T)));
    }

    // -----------------------------------------------------------------
    // Dispatch
    // -----------------------------------------------------------------

    void Dispatch(uint32_t groupX, uint32_t groupY = 1, uint32_t groupZ = 1);
    void DispatchAuto(uint32_t totalX, uint32_t totalY = 1, uint32_t totalZ = 1);

    // -----------------------------------------------------------------
    // Buffer helpers
    // -----------------------------------------------------------------

    GpuBufferHandle CreateBuffer(uint32_t size, GpuBufferUsage usage);
    void UploadBufferData(GpuBufferHandle buffer, const void* data, uint32_t size);
    void DestroyBuffer(GpuBufferHandle buffer);

    // -----------------------------------------------------------------
    // Internal — called by Renderer::_endFrame()
    // -----------------------------------------------------------------
    void _ExecuteQueued(GpuCmdBufferHandle cmdBuf);
    void _Reset();

} // namespace Compute
