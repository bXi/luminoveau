#include "buffermanager.h"
#include "log/loghandler.h"

void BufferManager::_resetAll() {
    for (auto& buffer : m_buffers) {
        buffer->Reset();
    }
}

void BufferManager::_destroyAll() {
    if (m_buffers.empty()) return;

    for (auto& buffer : m_buffers) {
        LOG_INFO("Buffer '{}': released ({:.1f} MB, watermark: {})",
            buffer->Name(),
            static_cast<float>(buffer->BytesAllocated()) / (1024.0f * 1024.0f),
            buffer->HighWatermark());
    }

    m_buffers.clear();
}

size_t BufferManager::_totalBytesUsed() const {
    size_t total = 0;
    for (const auto& buffer : m_buffers) {
        total += buffer->BytesUsed();
    }
    return total;
}

size_t BufferManager::_totalBytesAllocated() const {
    size_t total = 0;
    for (const auto& buffer : m_buffers) {
        total += buffer->BytesAllocated();
    }
    return total;
}

size_t BufferManager::_bufferCount() const {
    return m_buffers.size();
}
