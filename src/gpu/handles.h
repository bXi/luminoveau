#pragma once

#include <cstdint>
#include <utility>

#include "SDL3/SDL.h"

// ─────────────────────────────────────────────────────────────────────────────
// RAII wrappers for SDL_GPU resources.
// All types are move-only. Destruction releases the GPU resource automatically.
// ─────────────────────────────────────────────────────────────────────────────

struct GpuTexture {
    SDL_GPUTexture* handle = nullptr;
    SDL_GPUDevice*  device = nullptr;

    GpuTexture() = default;
    GpuTexture(SDL_GPUDevice* dev, SDL_GPUTexture* tex) : handle(tex), device(dev) {}

    ~GpuTexture() { reset(); }

    GpuTexture(GpuTexture&& o) noexcept : handle(o.handle), device(o.device) {
        o.handle = nullptr; o.device = nullptr;
    }
    GpuTexture& operator=(GpuTexture&& o) noexcept {
        if (this != &o) { reset(); handle = o.handle; device = o.device;
                          o.handle = nullptr; o.device = nullptr; }
        return *this;
    }
    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;

    void reset() {
        if (handle && device) { SDL_ReleaseGPUTexture(device, handle); }
        handle = nullptr; device = nullptr;
    }

    explicit operator bool() const { return handle != nullptr; }
};

struct GpuBuffer {
    SDL_GPUBuffer* handle = nullptr;
    SDL_GPUDevice* device = nullptr;

    GpuBuffer() = default;
    GpuBuffer(SDL_GPUDevice* dev, SDL_GPUBuffer* buf) : handle(buf), device(dev) {}

    ~GpuBuffer() { reset(); }

    GpuBuffer(GpuBuffer&& o) noexcept : handle(o.handle), device(o.device) {
        o.handle = nullptr; o.device = nullptr;
    }
    GpuBuffer& operator=(GpuBuffer&& o) noexcept {
        if (this != &o) { reset(); handle = o.handle; device = o.device;
                          o.handle = nullptr; o.device = nullptr; }
        return *this;
    }
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    void reset() {
        if (handle && device) { SDL_ReleaseGPUBuffer(device, handle); }
        handle = nullptr; device = nullptr;
    }

    explicit operator bool() const { return handle != nullptr; }
};

struct GpuTransferBuffer {
    SDL_GPUTransferBuffer* handle = nullptr;
    SDL_GPUDevice*         device = nullptr;

    GpuTransferBuffer() = default;
    GpuTransferBuffer(SDL_GPUDevice* dev, SDL_GPUTransferBuffer* buf)
        : handle(buf), device(dev) {}

    ~GpuTransferBuffer() { reset(); }

    GpuTransferBuffer(GpuTransferBuffer&& o) noexcept : handle(o.handle), device(o.device) {
        o.handle = nullptr; o.device = nullptr;
    }
    GpuTransferBuffer& operator=(GpuTransferBuffer&& o) noexcept {
        if (this != &o) { reset(); handle = o.handle; device = o.device;
                          o.handle = nullptr; o.device = nullptr; }
        return *this;
    }
    GpuTransferBuffer(const GpuTransferBuffer&) = delete;
    GpuTransferBuffer& operator=(const GpuTransferBuffer&) = delete;

    void reset() {
        if (handle && device) { SDL_ReleaseGPUTransferBuffer(device, handle); }
        handle = nullptr; device = nullptr;
    }

    explicit operator bool() const { return handle != nullptr; }
};

struct GpuShader {
    SDL_GPUShader* handle = nullptr;
    SDL_GPUDevice* device = nullptr;

    GpuShader() = default;
    GpuShader(SDL_GPUDevice* dev, SDL_GPUShader* s) : handle(s), device(dev) {}

    ~GpuShader() { reset(); }

    GpuShader(GpuShader&& o) noexcept : handle(o.handle), device(o.device) {
        o.handle = nullptr; o.device = nullptr;
    }
    GpuShader& operator=(GpuShader&& o) noexcept {
        if (this != &o) { reset(); handle = o.handle; device = o.device;
                          o.handle = nullptr; o.device = nullptr; }
        return *this;
    }
    GpuShader(const GpuShader&) = delete;
    GpuShader& operator=(const GpuShader&) = delete;

    void reset() {
        if (handle && device) { SDL_ReleaseGPUShader(device, handle); }
        handle = nullptr; device = nullptr;
    }

    explicit operator bool() const { return handle != nullptr; }
};

struct GpuGraphicsPipeline {
    SDL_GPUGraphicsPipeline* handle = nullptr;
    SDL_GPUDevice*           device = nullptr;

    GpuGraphicsPipeline() = default;
    GpuGraphicsPipeline(SDL_GPUDevice* dev, SDL_GPUGraphicsPipeline* p)
        : handle(p), device(dev) {}

    ~GpuGraphicsPipeline() { reset(); }

    GpuGraphicsPipeline(GpuGraphicsPipeline&& o) noexcept : handle(o.handle), device(o.device) {
        o.handle = nullptr; o.device = nullptr;
    }
    GpuGraphicsPipeline& operator=(GpuGraphicsPipeline&& o) noexcept {
        if (this != &o) { reset(); handle = o.handle; device = o.device;
                          o.handle = nullptr; o.device = nullptr; }
        return *this;
    }
    GpuGraphicsPipeline(const GpuGraphicsPipeline&) = delete;
    GpuGraphicsPipeline& operator=(const GpuGraphicsPipeline&) = delete;

    void reset() {
        if (handle && device) { SDL_ReleaseGPUGraphicsPipeline(device, handle); }
        handle = nullptr; device = nullptr;
    }

    explicit operator bool() const { return handle != nullptr; }
};

struct GpuComputePipeline {
    SDL_GPUComputePipeline* handle = nullptr;
    SDL_GPUDevice*          device = nullptr;

    GpuComputePipeline() = default;
    GpuComputePipeline(SDL_GPUDevice* dev, SDL_GPUComputePipeline* p)
        : handle(p), device(dev) {}

    ~GpuComputePipeline() { reset(); }

    GpuComputePipeline(GpuComputePipeline&& o) noexcept : handle(o.handle), device(o.device) {
        o.handle = nullptr; o.device = nullptr;
    }
    GpuComputePipeline& operator=(GpuComputePipeline&& o) noexcept {
        if (this != &o) { reset(); handle = o.handle; device = o.device;
                          o.handle = nullptr; o.device = nullptr; }
        return *this;
    }
    GpuComputePipeline(const GpuComputePipeline&) = delete;
    GpuComputePipeline& operator=(const GpuComputePipeline&) = delete;

    void reset() {
        if (handle && device) { SDL_ReleaseGPUComputePipeline(device, handle); }
        handle = nullptr; device = nullptr;
    }

    explicit operator bool() const { return handle != nullptr; }
};

struct GpuSampler {
    SDL_GPUSampler* handle = nullptr;
    SDL_GPUDevice*  device = nullptr;

    GpuSampler() = default;
    GpuSampler(SDL_GPUDevice* dev, SDL_GPUSampler* s) : handle(s), device(dev) {}

    ~GpuSampler() { reset(); }

    GpuSampler(GpuSampler&& o) noexcept : handle(o.handle), device(o.device) {
        o.handle = nullptr; o.device = nullptr;
    }
    GpuSampler& operator=(GpuSampler&& o) noexcept {
        if (this != &o) { reset(); handle = o.handle; device = o.device;
                          o.handle = nullptr; o.device = nullptr; }
        return *this;
    }
    GpuSampler(const GpuSampler&) = delete;
    GpuSampler& operator=(const GpuSampler&) = delete;

    void reset() {
        if (handle && device) { SDL_ReleaseGPUSampler(device, handle); }
        handle = nullptr; device = nullptr;
    }

    explicit operator bool() const { return handle != nullptr; }
};
