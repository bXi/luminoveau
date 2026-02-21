#pragma once

#include <memory>
#include <string>
#include <vector>

#include "buffer.h"

class BufferManager {
public:

    /**
     * @brief Creates a named buffer owned by the manager.
     *
     * @tparam T The type to store in the buffer.
     * @param name Debug name for logging and diagnostics.
     * @param capacity Maximum number of items the buffer can hold.
     * @param type CPU or GPU buffer type.
     * @return Raw pointer to the created buffer. Lifetime managed by BufferManager.
     */
    template<typename T>
    static Buffer<T>* Create(const std::string& name, size_t capacity, BufferType type = BufferType::CPU) {
        return get()._create<T>(name, capacity, type);
    }

    /**
     * @brief Resets all managed buffers (counter reset, POD-aware destruction).
     */
    static void ResetAll() { get()._resetAll(); }

    /**
     * @brief Releases and destroys all managed buffers. Call during shutdown.
     */
    static void DestroyAll() { get()._destroyAll(); }

    /**
     * @brief Returns the total bytes actively used across all buffers.
     */
    static size_t TotalBytesUsed() { return get()._totalBytesUsed(); }

    /**
     * @brief Returns the total bytes allocated across all buffers.
     */
    static size_t TotalBytesAllocated() { return get()._totalBytesAllocated(); }

    /**
     * @brief Returns the number of managed buffers.
     */
    static size_t BufferCount() { return get()._bufferCount(); }

    /**
     * @brief Access all buffers for debug overlay / logging.
     */
    static const std::vector<std::unique_ptr<BufferBase>>& GetBuffers() { return get().m_buffers; }

private:

    template<typename T>
    Buffer<T>* _create(const std::string& name, size_t capacity, BufferType type) {
        auto buffer = std::make_unique<Buffer<T>>(name, capacity, type);
        auto* ptr = buffer.get();
        m_buffers.push_back(std::move(buffer));
        return ptr;
    }

    void _resetAll();
    void _destroyAll();
    size_t _totalBytesUsed() const;
    size_t _totalBytesAllocated() const;
    size_t _bufferCount() const;

    std::vector<std::unique_ptr<BufferBase>> m_buffers;

public:
    BufferManager(const BufferManager&) = delete;

    static BufferManager& get() {
        static BufferManager instance;
        return instance;
    }

private:
    BufferManager() = default;
    ~BufferManager() { _destroyAll(); }
};
