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
    template <typename T>
    static Buffer<T> *Create(const std::string &name, size_t capacity, BufferType type = BufferType::CPU) {
        return Get()._create<T>(name, capacity, type);
    }

    /**
     * @brief Resets all managed buffers (counter reset, POD-aware destruction).
     */
    static void ResetAll() { Get()._resetAll(); }

    /**
     * @brief Releases and destroys all managed buffers. Call during shutdown.
     */
    static void DestroyAll() { Get()._destroyAll(); }

    /**
     * @brief Returns the total bytes actively used across all buffers.
     */
    static size_t TotalBytesUsed() { return Get()._totalBytesUsed(); }

    /**
     * @brief Returns the total bytes allocated across all buffers.
     */
    static size_t TotalBytesAllocated() { return Get()._totalBytesAllocated(); }

    /**
     * @brief Returns the number of managed buffers.
     */
    static size_t BufferCount() { return Get()._bufferCount(); }

    /**
     * @brief Access all buffers for debug overlay / logging.
     */
    static const std::vector<std::unique_ptr<BufferBase>> &GetBuffers() { return Get()._buffers; }

private:
    template <typename T>
    Buffer<T> *_create(const std::string &name, size_t capacity, BufferType type) {
        for (auto &existing : _buffers) {
            if (existing->Name() == name) {
                return static_cast<Buffer<T> *>(existing.get());
            }
        }
        auto  buffer = std::make_unique<Buffer<T>>(name, capacity, type);
        auto *ptr    = buffer.get();
        _buffers.push_back(std::move(buffer));
        return ptr;
    }

    void   _resetAll();
    void   _destroyAll();
    size_t _totalBytesUsed() const;
    size_t _totalBytesAllocated() const;
    size_t _bufferCount() const;

    std::vector<std::unique_ptr<BufferBase>> _buffers;

public:
    BufferManager(const BufferManager &) = delete;

    static BufferManager &Get() {
        static BufferManager instance;
        return instance;
    }

private:
    BufferManager() = default;
    ~BufferManager() { _destroyAll(); }
};
