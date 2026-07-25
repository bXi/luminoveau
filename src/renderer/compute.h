#pragma once

#include <cstdint>
#include <vector>
#include <utility>

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
class Compute {
public:
    // -----------------------------------------------------------------
    // State setters — call any combination before Dispatch / DispatchAuto
    // -----------------------------------------------------------------

    /**
     * @brief Sets the compute pipeline (shader) used by the next dispatch.
     * @param pipeline The compiled compute pipeline to run.
     */
    static void SetPipeline(const ComputePipelineAsset &pipeline) { Get()._setPipeline(pipeline); }

    /**
     * @brief Binds a read-only texture to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader samples from.
     * @param tex The texture handle to bind.
     */
    static void BindReadTexture(uint32_t slot, GpuTextureHandle tex) { Get()._bindReadTexture(slot, tex); }
    /**
     * @brief Binds a read-only texture asset to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader samples from.
     * @param tex The texture asset to bind.
     */
    static void BindReadTexture(uint32_t slot, const TextureAsset &tex) { Get()._bindReadTexture(slot, tex); }

    /**
     * @brief Binds a read-write (storage) texture to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader writes to.
     * @param tex The texture handle to bind.
     * @param mipLevel The mip level to bind. Defaults to 0.
     * @param layer The array layer to bind. Defaults to 0.
     */
    static void BindReadWriteTexture(uint32_t slot, GpuTextureHandle tex, uint32_t mipLevel = 0, uint32_t layer = 0) { Get()._bindReadWriteTexture(slot, tex, mipLevel, layer); }
    /**
     * @brief Binds a read-write (storage) texture asset to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader writes to.
     * @param tex The texture asset to bind.
     * @param mipLevel The mip level to bind. Defaults to 0.
     * @param layer The array layer to bind. Defaults to 0.
     */
    static void BindReadWriteTexture(uint32_t slot, const TextureAsset &tex, uint32_t mipLevel = 0, uint32_t layer = 0) { Get()._bindReadWriteTexture(slot, tex, mipLevel, layer); }

    /**
     * @brief Binds a read-only storage buffer to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader reads from.
     * @param buf The buffer handle to bind.
     */
    static void BindReadBuffer(uint32_t slot, GpuBufferHandle buf) { Get()._bindReadBuffer(slot, buf); }
    /**
     * @brief Binds a read-write storage buffer to a shader slot for the next dispatch.
     * @param slot The binding slot index the shader reads and writes.
     * @param buf The buffer handle to bind.
     */
    static void BindReadWriteBuffer(uint32_t slot, GpuBufferHandle buf) { Get()._bindReadWriteBuffer(slot, buf); }

    /**
     * @brief Pushes raw uniform data to a shader slot for the next dispatch.
     * @param slot The uniform binding slot index.
     * @param data Pointer to the uniform data to upload.
     * @param size Size of the data in bytes.
     */
    static void PushUniform(uint32_t slot, const void *data, uint32_t size) { Get()._pushUniform(slot, data, size); }

    /**
     * @brief Pushes a typed uniform value to a shader slot for the next dispatch.
     * @tparam T The uniform value type (its size is deduced via sizeof).
     * @param slot The uniform binding slot index.
     * @param value The value to upload as uniform data.
     */
    template <typename T>
    static void PushUniform(uint32_t slot, const T &value) {
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
    static void Dispatch(uint32_t groupX, uint32_t groupY = 1, uint32_t groupZ = 1) { Get()._dispatch(groupX, groupY, groupZ); }
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
    static void DispatchAuto(uint32_t totalX, uint32_t totalY = 1, uint32_t totalZ = 1) { Get()._dispatchAuto(totalX, totalY, totalZ); }

    // -----------------------------------------------------------------
    // Buffer helpers
    // -----------------------------------------------------------------

    /**
     * @brief Creates a GPU buffer for use as compute input/output.
     * @param size Size of the buffer in bytes.
     * @param usage Usage flags describing how the buffer will be bound.
     * @return A handle to the newly created buffer.
     */
    static GpuBufferHandle CreateBuffer(uint32_t size, GpuBufferUsage usage) { return Get()._createBuffer(size, usage); }
    /**
     * @brief Uploads data into an existing GPU buffer.
     * @param buffer The destination buffer handle.
     * @param data Pointer to the source data.
     * @param size Number of bytes to upload.
     */
    static void UploadBufferData(GpuBufferHandle buffer, const void *data, uint32_t size) { Get()._uploadBufferData(buffer, data, size); }
    /**
     * @brief Destroys a GPU buffer and frees its memory.
     * @param buffer The buffer handle to destroy.
     */
    static void DestroyBuffer(GpuBufferHandle buffer) { Get()._destroyBuffer(buffer); }

    /// @cond INTERNAL
    // Internal — called by Renderer::_endFrame()
    static void ExecuteQueued(GpuCmdBufferHandle cmdBuf) { Get()._executeQueued(cmdBuf); }
    static void Reset() { Get()._reset(); }
    /// @endcond

private:
    /// @cond INTERNAL
    struct RWTextureBind {
        GpuTextureHandle tex;
        uint32_t         mipLevel;
        uint32_t         layer;
    };

    struct DispatchRecord {
        GpuComputePipelineHandle pipeline     = 0;
        uint32_t                 threadCountX = 1, threadCountY = 1, threadCountZ = 1;

        std::vector<GpuTextureHandle>                          readTextures;
        std::vector<std::pair<uint32_t, RWTextureBind>>        readWriteTextures;
        std::vector<GpuBufferHandle>                           readBuffers;
        std::vector<std::pair<uint32_t, GpuBufferHandle>>      readWriteBuffers;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> uniforms;

        uint32_t groupX = 1, groupY = 1, groupZ = 1;
    };
    /// @endcond

    void _setPipeline(const ComputePipelineAsset &pipeline);
    void _bindReadTexture(uint32_t slot, GpuTextureHandle tex);
    void _bindReadTexture(uint32_t slot, const TextureAsset &tex);
    void _bindReadWriteTexture(uint32_t slot, GpuTextureHandle tex, uint32_t mipLevel, uint32_t layer);
    void _bindReadWriteTexture(uint32_t slot, const TextureAsset &tex, uint32_t mipLevel, uint32_t layer);
    void _bindReadBuffer(uint32_t slot, GpuBufferHandle buf);
    void _bindReadWriteBuffer(uint32_t slot, GpuBufferHandle buf);
    void _pushUniform(uint32_t slot, const void *data, uint32_t size);

    void _dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ);
    void _dispatchAuto(uint32_t totalX, uint32_t totalY, uint32_t totalZ);

    GpuBufferHandle _createBuffer(uint32_t size, GpuBufferUsage usage);
    void            _uploadBufferData(GpuBufferHandle buffer, const void *data, uint32_t size);
    void            _destroyBuffer(GpuBufferHandle buffer);

    void _executeQueued(GpuCmdBufferHandle cmdBuf);
    void _reset();

    void _clearBuilderState();
    void _enqueueDispatch(uint32_t gx, uint32_t gy, uint32_t gz);

    // -----------------------------------------------------------------
    // Builder state
    // -----------------------------------------------------------------

    GpuComputePipelineHandle _pipeline = 0;
    uint32_t                 _tcX = 1, _tcY = 1, _tcZ = 1;

    std::vector<GpuTextureHandle>                          _readTextures;
    std::vector<std::pair<uint32_t, RWTextureBind>>        _readWriteTextures;
    std::vector<GpuBufferHandle>                           _readBuffers;
    std::vector<std::pair<uint32_t, GpuBufferHandle>>      _readWriteBuffers;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> _uniforms;

    std::vector<DispatchRecord> _queue;

public:
    /// @cond INTERNAL
    Compute(const Compute &) = delete;

    static Compute &Get() {
        static Compute instance;
        return instance;
    }
    /// @endcond

private:
    Compute() = default;
};
