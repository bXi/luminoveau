#pragma once

#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <malloc.h>
#endif
#include <new>
#include <string>
#include <type_traits>

#include "core/log/log.h"

enum class BufferType { CPU,
    GPU };

// Type-erased base so the manager can store and manage all buffers uniformly
class BufferBase {
public:
    virtual ~BufferBase()                             = default;
    virtual void               Reset()                = 0;
    virtual void               Release()              = 0;
    virtual size_t             Count() const          = 0;
    virtual size_t             Capacity() const       = 0;
    virtual size_t             BytesUsed() const      = 0;
    virtual size_t             BytesAllocated() const = 0;
    virtual size_t             HighWatermark() const  = 0;
    virtual const std::string &Name() const           = 0;
    virtual BufferType         Type() const           = 0;
};

template <typename T>
class Buffer : public BufferBase {
public:
    Buffer(const std::string &name, size_t capacity, BufferType type)
        : _name(name)
        , _capacity(capacity)
        , _type(type) {

        size_t allocSize = capacity * sizeof(T);

#ifdef _WIN32
        _data = static_cast<T *>(_aligned_malloc(allocSize, 16));
#else
        _data = static_cast<T *>(std::aligned_alloc(16, allocSize));
#endif

        if (!_data) {
            LOG_CRITICAL("Buffer '{}': failed to allocate {} bytes", _name, allocSize);
        }

        LOG_DEBUG("Buffer '{}': allocated {} entries ({:.1f} MB)",
            _name, capacity, static_cast<float>(allocSize) / (1024.0f * 1024.0f));
    }

    ~Buffer() override {
        Release();
    }

    // No copy
    Buffer(const Buffer &)            = delete;
    Buffer &operator=(const Buffer &) = delete;

    // No move (manager owns these via pointer)
    Buffer(Buffer &&)            = delete;
    Buffer &operator=(Buffer &&) = delete;

    // Add an item - placement new, returns pointer to the new slot
    T *Add() {
        if (_count >= _capacity) {
            _grow();
        }

        T *slot = _data + _count;
        new (slot) T();
        _count++;

        if (_count > _highWatermark) {
            _highWatermark = _count;
        }

        return slot;
    }

    // Add by copying an existing item
    T *Add(const T &item) {
        if (_count >= _capacity) {
            _grow();
        }

        T *slot = _data + _count;
        new (slot) T(item);
        _count++;

        if (_count > _highWatermark) {
            _highWatermark = _count;
        }

        return slot;
    }

    // Indexed access - no bounds checking for performance
    T       &operator[](size_t i) { return _data[i]; }
    const T &operator[](size_t i) const { return _data[i]; }

    // Raw pointer + count for iteration and GPU upload
    T       *Data() { return _data; }
    const T *Data() const { return _data; }

    void Reset() override {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < _count; i++) {
                _data[i].~T();
            }
        }
        _count = 0;
    }

    void Release() override {
        if (!_data)
            return;

        // Destroy active items if needed
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < _count; i++) {
                _data[i].~T();
            }
        }

#ifdef _WIN32
        _aligned_free(_data);
#else
        std::free(_data);
#endif

        _data  = nullptr;
        _count = 0;
    }

    size_t             Count() const override { return _count; }
    size_t             Capacity() const override { return _capacity; }
    size_t             BytesUsed() const override { return _count * sizeof(T); }
    size_t             BytesAllocated() const override { return _capacity * sizeof(T); }
    size_t             HighWatermark() const override { return _highWatermark; }
    const std::string &Name() const override { return _name; }
    BufferType         Type() const override { return _type; }

private:
    void _grow() {
        size_t newCapacity = _capacity * 2;
        LOG_WARNING("Buffer '{}': capacity exceeded ({} items), growing to {} — increase initial capacity to avoid this",
            _name, _capacity, newCapacity);

#ifdef _WIN32
        T *newData = static_cast<T *>(_aligned_malloc(newCapacity * sizeof(T), 16));
#else
        T *newData = static_cast<T *>(std::aligned_alloc(16, newCapacity * sizeof(T)));
#endif
        if (!newData) {
            LOG_CRITICAL("Buffer '{}': failed to allocate {} bytes during _grow", _name, newCapacity * sizeof(T));
        }

        // Relocate existing items
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(newData, _data, _count * sizeof(T));
        } else {
            for (size_t i = 0; i < _count; i++) {
                new (newData + i) T(std::move(_data[i]));
                _data[i].~T();
            }
        }

#ifdef _WIN32
        _aligned_free(_data);
#else
        std::free(_data);
#endif
        _data     = newData;
        _capacity = newCapacity;
    }

    T          *_data          = nullptr;
    size_t      _count         = 0;
    size_t      _capacity      = 0;
    size_t      _highWatermark = 0;
    std::string _name;
    BufferType  _type;
};
