#pragma once

#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <malloc.h>
#endif
#include <new>
#include <string>
#include <type_traits>

#include "log/loghandler.h"

enum class BufferType { CPU, GPU };

// Type-erased base so the manager can store and manage all buffers uniformly
class BufferBase {
public:
    virtual ~BufferBase() = default;
    virtual void Reset() = 0;
    virtual void Release() = 0;
    virtual size_t Count() const = 0;
    virtual size_t Capacity() const = 0;
    virtual size_t BytesUsed() const = 0;
    virtual size_t BytesAllocated() const = 0;
    virtual size_t HighWatermark() const = 0;
    virtual const std::string& Name() const = 0;
    virtual BufferType Type() const = 0;
};

template<typename T>
class Buffer : public BufferBase {
public:
    Buffer(const std::string& name, size_t capacity, BufferType type)
        : m_name(name), m_capacity(capacity), m_type(type) {

        size_t allocSize = capacity * sizeof(T);

#ifdef _WIN32
        m_data = static_cast<T*>(_aligned_malloc(allocSize, 16));
#else
        m_data = static_cast<T*>(std::aligned_alloc(16, allocSize));
#endif

        if (!m_data) {
            LOG_CRITICAL("Buffer '{}': failed to allocate {} bytes", m_name, allocSize);
        }

        LOG_DEBUG("Buffer '{}': allocated {} entries ({:.1f} MB)",
            m_name, capacity, static_cast<float>(allocSize) / (1024.0f * 1024.0f));
    }

    ~Buffer() override {
        Release();
    }

    // No copy
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // No move (manager owns these via pointer)
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    // Add an item - placement new, returns pointer to the new slot
    T* Add() {
        if (m_count >= m_capacity) {
            LOG_ERROR("Buffer '{}': capacity exceeded ({} items)", m_name, m_capacity);
            return nullptr; // LOG_ERROR throws, but just in case
        }

        T* slot = m_data + m_count;
        new (slot) T();
        m_count++;

        if (m_count > m_highWatermark) {
            m_highWatermark = m_count;
        }

        return slot;
    }

    // Add by copying an existing item
    T* Add(const T& item) {
        if (m_count >= m_capacity) {
            LOG_ERROR("Buffer '{}': capacity exceeded ({} items)", m_name, m_capacity);
            return nullptr;
        }

        T* slot = m_data + m_count;
        new (slot) T(item);
        m_count++;

        if (m_count > m_highWatermark) {
            m_highWatermark = m_count;
        }

        return slot;
    }

    // Indexed access - no bounds checking for performance
    T& operator[](size_t i) { return m_data[i]; }
    const T& operator[](size_t i) const { return m_data[i]; }

    // Raw pointer + count for iteration and GPU upload
    T* Data() { return m_data; }
    const T* Data() const { return m_data; }

    void Reset() override {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < m_count; i++) {
                m_data[i].~T();
            }
        }
        m_count = 0;
    }

    void Release() override {
        if (!m_data) return;

        // Destroy active items if needed
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < m_count; i++) {
                m_data[i].~T();
            }
        }

#ifdef _WIN32
        _aligned_free(m_data);
#else
        std::free(m_data);
#endif

        m_data = nullptr;
        m_count = 0;
    }

    size_t Count() const override { return m_count; }
    size_t Capacity() const override { return m_capacity; }
    size_t BytesUsed() const override { return m_count * sizeof(T); }
    size_t BytesAllocated() const override { return m_capacity * sizeof(T); }
    size_t HighWatermark() const override { return m_highWatermark; }
    const std::string& Name() const override { return m_name; }
    BufferType Type() const override { return m_type; }

private:
    T* m_data = nullptr;
    size_t m_count = 0;
    size_t m_capacity = 0;
    size_t m_highWatermark = 0;
    std::string m_name;
    BufferType m_type;
};
