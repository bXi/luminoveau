#include "gpu/IGpu.h"
#include "core/log/log.h"

#include "stb_image_write.h"

#include <cstdlib>
#include <cstring>
#include <thread>
#include <utility>

void IGpu::requestScreenshot(GpuCmdBufferHandle cmd,
                              GpuTextureHandle src,
                              uint32_t width, uint32_t height,
                              const std::string& filename) {
    if (!cmd || !src || width == 0 || height == 0) return;

    size_t dataSize = static_cast<size_t>(width) * height * 4;
    GpuTransferBufferHandle xfer = createTransferBuffer({ (uint32_t)dataSize, GpuTransferUsage::Download });
    if (!xfer) {
        LOG_ERROR("requestScreenshot: failed to create transfer buffer");
        return;
    }

    GpuTextureRegion srcRegion{};
    srcRegion.texture = src;
    srcRegion.width   = width;
    srcRegion.height  = height;
    srcRegion.depth   = 1;
    GpuTransferBufferRegion dstInfo{};
    dstInfo.transferBuffer = xfer;
    dstInfo.pixels_per_row = width;
    dstInfo.rows_per_layer = height;
    downloadFromTexture(cmd, srcRegion, dstInfo);

    PendingScreenshot p;
    p.filename       = filename;
    p.transferBuffer = xfer;
    p.width          = width;
    p.height         = height;
    p.dataSize       = dataSize;
    p.isBGRA         = (getSwapchainFormat() == GpuTextureFormat::B8G8R8A8_Unorm);
    m_pendingScreenshots.push_back(std::move(p));
}

void IGpu::processPendingScreenshots() {
    if (m_pendingScreenshots.empty()) return;

    // Wait for the GPU to finish the staged downloads.
    waitIdle();

    for (auto& p : m_pendingScreenshots) {
        if (!p.transferBuffer) continue;

        void* gpuData = mapTransferBuffer(p.transferBuffer, false);
        if (gpuData) {
            // Copy to a heap buffer the background thread will own.
            unsigned char* pixelCopy = (unsigned char*)malloc(p.dataSize);
            if (pixelCopy) {
                std::memcpy(pixelCopy, gpuData, p.dataSize);

                std::string filename = p.filename;
                uint32_t    width    = p.width;
                uint32_t    height   = p.height;
                size_t      dataSize = p.dataSize;
                bool        isBGRA   = p.isBGRA;

                std::thread([pixelCopy, filename, width, height, dataSize, isBGRA]() {
                    if (isBGRA) {
                        for (size_t i = 0; i + 3 < dataSize; i += 4) {
                            std::swap(pixelCopy[i + 0], pixelCopy[i + 2]);
                        }
                    }
                    if (stbi_write_png(filename.c_str(), (int)width, (int)height, 4,
                                       pixelCopy, (int)(width * 4))) {
                        LOG_INFO("Screenshot saved: {}", filename);
                    } else {
                        LOG_ERROR("Failed to save screenshot: {}", filename);
                    }
                    free(pixelCopy);
                }).detach();
            }
            unmapTransferBuffer(p.transferBuffer);
        }

        releaseTransferBuffer(p.transferBuffer);
        p.transferBuffer = 0;
    }
    m_pendingScreenshots.clear();
}
