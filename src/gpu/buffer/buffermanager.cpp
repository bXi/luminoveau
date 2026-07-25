#include "buffermanager.h"
#include "core/log/log.h"

void BufferManager::_resetAll() {
    for (auto &buffer : _buffers) {
        buffer->Reset();
    }
}

void BufferManager::_destroyAll() {
    if (_buffers.empty())
        return;

    for (auto &buffer : _buffers) {
        LOG_INFO("Buffer '{}': released ({:.1f} MB, watermark: {})",
            buffer->Name(),
            static_cast<float>(buffer->BytesAllocated()) / (1024.0f * 1024.0f),
            buffer->HighWatermark());
    }

    _buffers.clear();
}

size_t BufferManager::_totalBytesUsed() const {
    size_t total = 0;
    for (const auto &buffer : _buffers) {
        total += buffer->BytesUsed();
    }
    return total;
}

size_t BufferManager::_totalBytesAllocated() const {
    size_t total = 0;
    for (const auto &buffer : _buffers) {
        total += buffer->BytesAllocated();
    }
    return total;
}

size_t BufferManager::_bufferCount() const {
    return _buffers.size();
}
