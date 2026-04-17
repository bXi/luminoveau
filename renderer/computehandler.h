#pragma once

#include <cstdint>
#include <SDL3/SDL.h>

#include "assettypes/computepipeline.h"
#include "assettypes/texture.h"

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

    /// Set the compute pipeline to use for the next dispatch.
    void SetPipeline(const ComputePipelineAsset& pipeline);

    /// Bind a read-only storage texture to the given slot.
    void BindReadTexture(uint32_t slot, SDL_GPUTexture* tex);
    void BindReadTexture(uint32_t slot, const TextureAsset& tex);

    /// Bind a read-write storage texture (declared with read-write usage).
    void BindReadWriteTexture(uint32_t slot, SDL_GPUTexture* tex,
                              uint32_t mipLevel = 0, uint32_t layer = 0);
    void BindReadWriteTexture(uint32_t slot, const TextureAsset& tex,
                              uint32_t mipLevel = 0, uint32_t layer = 0);

    /// Bind a read-only storage buffer to the given slot.
    void BindReadBuffer(uint32_t slot, SDL_GPUBuffer* buf);

    /// Bind a read-write storage buffer to the given slot.
    void BindReadWriteBuffer(uint32_t slot, SDL_GPUBuffer* buf);

    /// Push raw uniform data to a uniform slot (std140 layout).
    void PushUniform(uint32_t slot, const void* data, uint32_t size);

    /// Convenience typed overload.
    template<typename T>
    void PushUniform(uint32_t slot, const T& value) {
        PushUniform(slot, &value, static_cast<uint32_t>(sizeof(T)));
    }

    // -----------------------------------------------------------------
    // Dispatch
    // -----------------------------------------------------------------

    /**
     * Queue a dispatch with explicit workgroup counts.
     * Snapshots all currently-set state; subsequent SetPipeline / Bind* calls
     * do not affect already-queued dispatches.
     */
    void Dispatch(uint32_t groupX, uint32_t groupY = 1, uint32_t groupZ = 1);

    /**
     * Queue a dispatch, auto-calculating group counts from the pipeline's
     * thread-group dimensions so that at least totalX * totalY * totalZ
     * invocations are launched.
     *
     * Requires SetPipeline to have been called first.
     */
    void DispatchAuto(uint32_t totalX, uint32_t totalY = 1, uint32_t totalZ = 1);

    // -----------------------------------------------------------------
    // Buffer helpers
    // -----------------------------------------------------------------

    /**
     * Create a raw GPU storage buffer.
     *
     * @param size  Size in bytes.
     * @param usage Combination of SDL_GPUBufferUsageFlags, e.g.
     *              SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
     *              SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE
     */
    SDL_GPUBuffer* CreateBuffer(uint32_t size, SDL_GPUBufferUsageFlags usage);

    /**
     * Upload initial data into a storage buffer via a one-shot copy pass.
     * Blocks until the upload is complete (uses SDL_WaitForGPUIdle).
     *
     * @param buffer     Destination GPU buffer (must include UPLOAD usage or be writable).
     * @param data       Source CPU data.
     * @param size       Bytes to copy.
     */
    void UploadBufferData(SDL_GPUBuffer* buffer, const void* data, uint32_t size);

    /// Release a buffer created with CreateBuffer().
    void DestroyBuffer(SDL_GPUBuffer* buffer);

    // -----------------------------------------------------------------
    // Internal — called by Renderer::_endFrame()
    // -----------------------------------------------------------------
    void _ExecuteQueued(SDL_GPUCommandBuffer* cmdBuf);
    void _Reset();

} // namespace Compute
