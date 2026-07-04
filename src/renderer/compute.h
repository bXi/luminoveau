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

    /**
     * @brief Sets the compute pipeline (shader) used by the next dispatch.
     * @param pipeline The compiled compute pipeline to run.
     */
    void SetPipeline(const ComputePipelineAsset& pipeline);

    /**
     * @brief Binds a read-only texture to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader samples from.
     * @param tex The texture handle to bind.
     */
    void BindReadTexture(uint32_t slot, GpuTextureHandle tex);
    /**
     * @brief Binds a read-only texture asset to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader samples from.
     * @param tex The texture asset to bind.
     */
    void BindReadTexture(uint32_t slot, const TextureAsset& tex);

    /**
     * @brief Binds a read-write (storage) texture to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader writes to.
     * @param tex The texture handle to bind.
     * @param mipLevel The mip level to bind. Defaults to 0.
     * @param layer The array layer to bind. Defaults to 0.
     */
    void BindReadWriteTexture(uint32_t slot, GpuTextureHandle tex,
                              uint32_t mipLevel = 0, uint32_t layer = 0);
    /**
     * @brief Binds a read-write (storage) texture asset to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader writes to.
     * @param tex The texture asset to bind.
     * @param mipLevel The mip level to bind. Defaults to 0.
     * @param layer The array layer to bind. Defaults to 0.
     */
    void BindReadWriteTexture(uint32_t slot, const TextureAsset& tex,
                              uint32_t mipLevel = 0, uint32_t layer = 0);

    /**
     * @brief Binds a read-only storage buffer to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader reads from.
     * @param buf The buffer handle to bind.
     */
    void BindReadBuffer(uint32_t slot, GpuBufferHandle buf);
    /**
     * @brief Binds a read-write storage buffer to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader reads and writes.
     * @param buf The buffer handle to bind.
     */
    void BindReadWriteBuffer(uint32_t slot, GpuBufferHandle buf);

    /**
     * @brief Pushes raw uniform data to a shader slot for the next dispatch.
     * @param slot The uniform binding slot index.
     * @param data Pointer to the uniform data to upload.
     * @param size Size of the data in bytes.
     */
    void PushUniform(uint32_t slot, const void* data, uint32_t size);

    /**
     * @brief Pushes a typed uniform value to a shader slot for the next dispatch.
     * @tparam T The uniform value type (its size is deduced via sizeof).
     * @param slot The uniform binding slot index.
     * @param value The value to upload as uniform data.
     */
    template<typename T>
    void PushUniform(uint32_t slot, const T& value) {
        PushUniform(slot, &value, static_cast<uint32_t>(sizeof(T)));
    }

    // -----------------------------------------------------------------
    // Dispatch
    // -----------------------------------------------------------------

    /**
     * @brief Queues a dispatch with an explicit workgroup count.
     *
     * The dispatch runs at the start of EndFrame with the currently bound pipeline,
     * textures, buffers and uniforms.
     *
     * @param groupX Number of workgroups in X.
     * @param groupY Number of workgroups in Y. Defaults to 1.
     * @param groupZ Number of workgroups in Z. Defaults to 1.
     */
    void Dispatch(uint32_t groupX, uint32_t groupY = 1, uint32_t groupZ = 1);
    /**
     * @brief Queues a dispatch sized by total thread count, rounding up to whole workgroups.
     *
     * Workgroup counts are computed as ceil(total / pipeline threadcount) per axis, so you
     * pass the number of elements to process rather than the number of groups.
     *
     * @param totalX Total threads needed in X (e.g. particle count).
     * @param totalY Total threads needed in Y. Defaults to 1.
     * @param totalZ Total threads needed in Z. Defaults to 1.
     */
    void DispatchAuto(uint32_t totalX, uint32_t totalY = 1, uint32_t totalZ = 1);

    // -----------------------------------------------------------------
    // Buffer helpers
    // -----------------------------------------------------------------

    /**
     * @brief Creates a GPU buffer for use as compute input/output.
     * @param size Size of the buffer in bytes.
     * @param usage Usage flags describing how the buffer will be bound.
     * @return A handle to the newly created buffer.
     */
    GpuBufferHandle CreateBuffer(uint32_t size, GpuBufferUsage usage);
    /**
     * @brief Uploads data into an existing GPU buffer.
     * @param buffer The destination buffer handle.
     * @param data Pointer to the source data.
     * @param size Number of bytes to upload.
     */
    void UploadBufferData(GpuBufferHandle buffer, const void* data, uint32_t size);
    /**
     * @brief Destroys a GPU buffer and frees its memory.
     * @param buffer The buffer handle to destroy.
     */
    void DestroyBuffer(GpuBufferHandle buffer);

    /// @cond INTERNAL
    // Internal — called by Renderer::_endFrame()
    void _ExecuteQueued(GpuCmdBufferHandle cmdBuf);
    void _Reset();
    /// @endcond

} // namespace Compute
