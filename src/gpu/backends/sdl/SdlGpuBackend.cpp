#include "gpu/backends/sdl/SdlGpuBackend.h"
#include "gpu/backends/sdl/sdlgpu.h"
#include "profiler/perf.h"

#include <SDL3/SDL.h>
#include <vector>
#include <cstring>
#include <unordered_map>

// ── VRAM accounting (tracked allocations -> Perf HUD) ─────────────────────────
namespace {
    std::unordered_map<void*, size_t> s_allocSizes;
    int64_t s_vramTotal = 0;

    // Bits per pixel for the formats we allocate (BC7 is 8bpp; uncompressed by channel size).
    int bppOf(GpuTextureFormat f) {
        switch (f) {
            case GpuTextureFormat::R8_Unorm:                                   return 8;
            case GpuTextureFormat::R8G8_Unorm: case GpuTextureFormat::R16_Float:
            case GpuTextureFormat::D16_Unorm:                                  return 16;
            case GpuTextureFormat::R8G8B8A8_Unorm: case GpuTextureFormat::B8G8R8A8_Unorm:
            case GpuTextureFormat::R8G8B8A8_Unorm_SRGB: case GpuTextureFormat::B8G8R8A8_Unorm_SRGB:
            case GpuTextureFormat::R16G16_Float: case GpuTextureFormat::R32_Float:
            case GpuTextureFormat::R10G10B10A2_Unorm: case GpuTextureFormat::D32_Float:
            case GpuTextureFormat::D24_Unorm:                                  return 32;
            case GpuTextureFormat::R16G16B16A16_Float: case GpuTextureFormat::R32G32_Float:
            case GpuTextureFormat::D32_Float_S8_Uint:                          return 64;
            case GpuTextureFormat::R32G32B32A32_Float:                         return 128;
            case GpuTextureFormat::BC7_Unorm: case GpuTextureFormat::ASTC_4x4_Unorm:
            case GpuTextureFormat::B5G6R5_Unorm:                               return 8;
            default:                                                           return 32;
        }
    }
    size_t texBytes(const GpuTextureCreateInfo& i) {
        uint32_t layers = i.depthOrLayers ? i.depthOrLayers : 1;
        double base = (double)i.width * i.height * layers * bppOf(i.format) / 8.0;
        if (i.numLevels > 1) base *= 1.34;   // approx mip chain
        return (size_t)base;
    }
    void vramAdd(void* h, size_t bytes) {
        if (!h || !bytes) return;
        s_allocSizes[h] = bytes; s_vramTotal += (int64_t)bytes;
        Perf::ReportVRAM(s_vramTotal);
    }
    void vramRemove(void* h) {
        auto it = s_allocSizes.find(h);
        if (it == s_allocSizes.end()) return;
        s_vramTotal -= (int64_t)it->second; s_allocSizes.erase(it);
        if (s_vramTotal < 0) s_vramTotal = 0;
        Perf::ReportVRAM(s_vramTotal);
    }

    uint32_t s_drawCalls = 0;
    uint64_t s_drawVerts = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

SdlGpuBackend::SdlGpuBackend(SDL_GPUDevice* device) : m_device(device) {}

SdlGpuBackend::~SdlGpuBackend() {
    // Phase 1: Renderer owns m_device lifecycle — do not release here.
}

bool SdlGpuBackend::init(void* windowHandle) {
    m_window = static_cast<SDL_Window*>(windowHandle);
    return m_device != nullptr;
}

void SdlGpuBackend::shutdown() {
    if (m_device) {
        SDL_DestroyGPUDevice(m_device);
        m_device = nullptr;
    }
}

void SdlGpuBackend::waitIdle() {
    SDL_WaitForGPUIdle(m_device);
}

// ─────────────────────────────────────────────────────────────────────────────
// Command buffers
// ─────────────────────────────────────────────────────────────────────────────

GpuCmdBufferHandle SdlGpuBackend::acquireCommandBuffer() {
    return reinterpret_cast<GpuCmdBufferHandle>(SDL_AcquireGPUCommandBuffer(m_device));
}

void SdlGpuBackend::submitCommandBuffer(GpuCmdBufferHandle cmd) {
    SDL_SubmitGPUCommandBuffer(reinterpret_cast<SDL_GPUCommandBuffer*>(cmd));
}

GpuFenceHandle SdlGpuBackend::submitCommandBufferAndAcquireFence(GpuCmdBufferHandle cmd) {
    SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(reinterpret_cast<SDL_GPUCommandBuffer*>(cmd));
    return reinterpret_cast<GpuFenceHandle>(f);
}

void SdlGpuBackend::waitFence(GpuFenceHandle fence) {
    if (!fence) return;
    SDL_GPUFence* f = reinterpret_cast<SDL_GPUFence*>(fence);
    SDL_WaitForGPUFences(m_device, true, &f, 1);   // block until the GPU finishes this submission
}

void SdlGpuBackend::releaseFence(GpuFenceHandle fence) {
    if (fence) SDL_ReleaseGPUFence(m_device, reinterpret_cast<SDL_GPUFence*>(fence));
}

// ─────────────────────────────────────────────────────────────────────────────
// Swapchain
// ─────────────────────────────────────────────────────────────────────────────

GpuTextureHandle SdlGpuBackend::acquireSwapchainTexture(GpuCmdBufferHandle cmd,
                                                         uint32_t& outWidth,
                                                         uint32_t& outHeight) {
    SDL_GPUTexture* tex = nullptr;
    SDL_AcquireGPUSwapchainTexture(
        reinterpret_cast<SDL_GPUCommandBuffer*>(cmd),
        m_window, &tex, &outWidth, &outHeight);
    return reinterpret_cast<GpuTextureHandle>(tex);
}

GpuTextureFormat SdlGpuBackend::getSwapchainFormat() const {
    if (!m_device || !m_window) return GpuTextureFormat::Invalid;
    return fromSDL(SDL_GetGPUSwapchainTextureFormat(m_device, m_window));
}

// ─────────────────────────────────────────────────────────────────────────────
// Render pass
// ─────────────────────────────────────────────────────────────────────────────

GpuRenderPassHandle SdlGpuBackend::beginRenderPass(GpuCmdBufferHandle cmd,
                                                    const GpuColorTargetInfo* colorTargets,
                                                    uint32_t colorTargetCount,
                                                    const GpuDepthStencilTargetInfo* depthTarget) {
    std::vector<SDL_GPUColorTargetInfo> sdlColors(colorTargetCount);
    for (uint32_t i = 0; i < colorTargetCount; ++i) {
        const auto& ct = colorTargets[i];
        sdlColors[i] = SDL_GPUColorTargetInfo{
            .texture              = reinterpret_cast<SDL_GPUTexture*>(ct.texture),
            .mip_level            = ct.mipLevel,
            .layer_or_depth_plane = ct.layer,
            .clear_color          = {ct.clearR, ct.clearG, ct.clearB, ct.clearA},
            .load_op              = toSDL(ct.loadOp),
            .store_op             = toSDL(ct.storeOp),
            .resolve_texture      = reinterpret_cast<SDL_GPUTexture*>(ct.resolveTexture),
            .cycle                = false,
            .cycle_resolve_texture = false,
        };
    }

    SDL_GPUDepthStencilTargetInfo  depthInfoStorage{};
    SDL_GPUDepthStencilTargetInfo* sdlDepth = nullptr;
    if (depthTarget && depthTarget->texture) {
        depthInfoStorage = {
            .texture            = reinterpret_cast<SDL_GPUTexture*>(depthTarget->texture),
            .clear_depth        = depthTarget->clearDepth,
            .load_op            = toSDL(depthTarget->loadOp),
            .store_op           = toSDL(depthTarget->storeOp),
            .stencil_load_op    = SDL_GPU_LOADOP_DONT_CARE,
            .stencil_store_op   = SDL_GPU_STOREOP_DONT_CARE,
            .cycle              = false,
            .clear_stencil      = depthTarget->clearStencil,
        };
        sdlDepth = &depthInfoStorage;
    }

    return reinterpret_cast<GpuRenderPassHandle>(
        SDL_BeginGPURenderPass(
            reinterpret_cast<SDL_GPUCommandBuffer*>(cmd),
            sdlColors.data(), colorTargetCount,
            sdlDepth));
}

void SdlGpuBackend::endRenderPass(GpuRenderPassHandle pass) {
    SDL_EndGPURenderPass(reinterpret_cast<SDL_GPURenderPass*>(pass));
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute pass
// ─────────────────────────────────────────────────────────────────────────────

GpuComputePassHandle SdlGpuBackend::beginComputePass(GpuCmdBufferHandle cmd,
                                                      const GpuStorageTextureBinding* rwTextures, uint32_t rwTexCount,
                                                      const GpuStorageBufferBinding*  rwBuffers,  uint32_t rwBufCount) {
    std::vector<SDL_GPUStorageTextureReadWriteBinding> sdlTex(rwTexCount);
    for (uint32_t i = 0; i < rwTexCount; ++i) {
        sdlTex[i] = {
            .texture   = reinterpret_cast<SDL_GPUTexture*>(rwTextures[i].texture),
            .mip_level = rwTextures[i].mipLevel,
            .layer     = rwTextures[i].layer,
            .cycle     = rwTextures[i].cycle,
        };
    }
    std::vector<SDL_GPUStorageBufferReadWriteBinding> sdlBuf(rwBufCount);
    for (uint32_t i = 0; i < rwBufCount; ++i) {
        sdlBuf[i] = {
            .buffer = reinterpret_cast<SDL_GPUBuffer*>(rwBuffers[i].buffer),
            .cycle  = rwBuffers[i].cycle,
        };
    }
    return reinterpret_cast<GpuComputePassHandle>(
        SDL_BeginGPUComputePass(
            reinterpret_cast<SDL_GPUCommandBuffer*>(cmd),
            sdlTex.data(), rwTexCount,
            sdlBuf.data(), rwBufCount));
}

void SdlGpuBackend::endComputePass(GpuComputePassHandle pass) {
    SDL_EndGPUComputePass(reinterpret_cast<SDL_GPUComputePass*>(pass));
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline binding
// ─────────────────────────────────────────────────────────────────────────────

void SdlGpuBackend::bindGraphicsPipeline(GpuRenderPassHandle pass, GpuGraphicsPipelineHandle pipeline) {
    SDL_BindGPUGraphicsPipeline(
        reinterpret_cast<SDL_GPURenderPass*>(pass),
        reinterpret_cast<SDL_GPUGraphicsPipeline*>(pipeline));
}

void SdlGpuBackend::bindComputePipeline(GpuComputePassHandle pass, GpuComputePipelineHandle pipeline) {
    SDL_BindGPUComputePipeline(
        reinterpret_cast<SDL_GPUComputePass*>(pass),
        reinterpret_cast<SDL_GPUComputePipeline*>(pipeline));
}

// ─────────────────────────────────────────────────────────────────────────────
// Vertex / index binding
// ─────────────────────────────────────────────────────────────────────────────

void SdlGpuBackend::bindVertexBuffers(GpuRenderPassHandle pass, uint32_t firstBinding,
                                       const GpuBufferBinding* bindings, uint32_t count) {
    std::vector<SDL_GPUBufferBinding> sdlBindings(count);
    for (uint32_t i = 0; i < count; ++i) {
        sdlBindings[i] = {
            .buffer = reinterpret_cast<SDL_GPUBuffer*>(bindings[i].buffer),
            .offset = bindings[i].offset,
        };
    }
    SDL_BindGPUVertexBuffers(
        reinterpret_cast<SDL_GPURenderPass*>(pass),
        firstBinding, sdlBindings.data(), count);
}

void SdlGpuBackend::bindIndexBuffer(GpuRenderPassHandle pass, GpuBufferBinding binding,
                                     bool use16BitIndices) {
    SDL_GPUBufferBinding sdlBinding{
        .buffer = reinterpret_cast<SDL_GPUBuffer*>(binding.buffer),
        .offset = binding.offset,
    };
    SDL_BindGPUIndexBuffer(
        reinterpret_cast<SDL_GPURenderPass*>(pass),
        &sdlBinding,
        use16BitIndices ? SDL_GPU_INDEXELEMENTSIZE_16BIT : SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture / sampler binding
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<SDL_GPUTextureSamplerBinding> toSDLBindings(const GpuTextureSamplerBinding* b, uint32_t count) {
    std::vector<SDL_GPUTextureSamplerBinding> out(count);
    for (uint32_t i = 0; i < count; ++i) {
        out[i] = {
            .texture = reinterpret_cast<SDL_GPUTexture*>(b[i].texture),
            .sampler = reinterpret_cast<SDL_GPUSampler*>(b[i].sampler),
        };
    }
    return out;
}

void SdlGpuBackend::bindVertexSamplers(GpuRenderPassHandle pass, uint32_t firstBinding,
                                        const GpuTextureSamplerBinding* bindings, uint32_t count) {
    auto sdl = toSDLBindings(bindings, count);
    SDL_BindGPUVertexSamplers(reinterpret_cast<SDL_GPURenderPass*>(pass), firstBinding, sdl.data(), count);
}

void SdlGpuBackend::bindFragmentSamplers(GpuRenderPassHandle pass, uint32_t firstBinding,
                                          const GpuTextureSamplerBinding* bindings, uint32_t count) {
    auto sdl = toSDLBindings(bindings, count);
    SDL_BindGPUFragmentSamplers(reinterpret_cast<SDL_GPURenderPass*>(pass), firstBinding, sdl.data(), count);
}

void SdlGpuBackend::bindFragmentStorageTextures(GpuRenderPassHandle pass, uint32_t firstBinding,
                                                 const GpuTextureHandle* textures, uint32_t count) {
    std::vector<SDL_GPUTexture*> sdlTex(count);
    for (uint32_t i = 0; i < count; ++i)
        sdlTex[i] = reinterpret_cast<SDL_GPUTexture*>(textures[i]);
    SDL_BindGPUFragmentStorageTextures(reinterpret_cast<SDL_GPURenderPass*>(pass), firstBinding, sdlTex.data(), count);
}

void SdlGpuBackend::bindVertexStorageBuffers(GpuRenderPassHandle pass, uint32_t first,
                                              const GpuBufferHandle* buffers, uint32_t count) {
    auto* rp = reinterpret_cast<SDL_GPURenderPass*>(pass);
    std::vector<SDL_GPUBuffer*> sdlBufs(count);
    for (uint32_t i = 0; i < count; ++i)
        sdlBufs[i] = reinterpret_cast<SDL_GPUBuffer*>(buffers[first + i]);
    SDL_BindGPUVertexStorageBuffers(rp, first, sdlBufs.data(), count);
}

void SdlGpuBackend::bindComputeSamplers(GpuComputePassHandle pass, uint32_t firstBinding,
                                         const GpuTextureSamplerBinding* bindings, uint32_t count) {
    auto sdl = toSDLBindings(bindings, count);
    SDL_BindGPUComputeSamplers(reinterpret_cast<SDL_GPUComputePass*>(pass), firstBinding, sdl.data(), count);
}

void SdlGpuBackend::bindComputeStorageTextures(GpuComputePassHandle pass, uint32_t firstBinding,
                                                const GpuTextureHandle* textures, uint32_t count) {
    std::vector<SDL_GPUTexture*> sdlTex(count);
    for (uint32_t i = 0; i < count; ++i)
        sdlTex[i] = reinterpret_cast<SDL_GPUTexture*>(textures[i]);
    SDL_BindGPUComputeStorageTextures(reinterpret_cast<SDL_GPUComputePass*>(pass), firstBinding, sdlTex.data(), count);
}

void SdlGpuBackend::bindComputeStorageBuffers(GpuComputePassHandle pass, uint32_t firstBinding,
                                               const GpuBufferHandle* buffers, uint32_t count) {
    std::vector<SDL_GPUBuffer*> sdlBuf(count);
    for (uint32_t i = 0; i < count; ++i)
        sdlBuf[i] = reinterpret_cast<SDL_GPUBuffer*>(buffers[i]);
    SDL_BindGPUComputeStorageBuffers(reinterpret_cast<SDL_GPUComputePass*>(pass), firstBinding, sdlBuf.data(), count);
}

// ─────────────────────────────────────────────────────────────────────────────
// Uniform push
// ─────────────────────────────────────────────────────────────────────────────

void SdlGpuBackend::pushVertexUniformData(GpuCmdBufferHandle cmd, uint32_t slot, const void* data, uint32_t size) {
    SDL_PushGPUVertexUniformData(reinterpret_cast<SDL_GPUCommandBuffer*>(cmd), slot, data, size);
}

void SdlGpuBackend::pushFragmentUniformData(GpuCmdBufferHandle cmd, uint32_t slot, const void* data, uint32_t size) {
    SDL_PushGPUFragmentUniformData(reinterpret_cast<SDL_GPUCommandBuffer*>(cmd), slot, data, size);
}

void SdlGpuBackend::pushComputeUniformData(GpuCmdBufferHandle cmd, uint32_t slot, const void* data, uint32_t size) {
    SDL_PushGPUComputeUniformData(reinterpret_cast<SDL_GPUCommandBuffer*>(cmd), slot, data, size);
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw calls
// ─────────────────────────────────────────────────────────────────────────────

void SdlGpuBackend::drawPrimitives(GpuRenderPassHandle pass,
                                    uint32_t vertexCount, uint32_t instanceCount,
                                    uint32_t firstVertex,  uint32_t firstInstance) {
    SDL_DrawGPUPrimitives(reinterpret_cast<SDL_GPURenderPass*>(pass),
                          vertexCount, instanceCount, firstVertex, firstInstance);
    s_drawCalls++; s_drawVerts += (uint64_t)vertexCount * (instanceCount ? instanceCount : 1);
}

void SdlGpuBackend::drawIndexedPrimitives(GpuRenderPassHandle pass,
                                           uint32_t indexCount,    uint32_t instanceCount,
                                           uint32_t firstIndex,    int32_t  vertexOffset,
                                           uint32_t firstInstance) {
    SDL_DrawGPUIndexedPrimitives(reinterpret_cast<SDL_GPURenderPass*>(pass),
                                 indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    s_drawCalls++; s_drawVerts += (uint64_t)indexCount * (instanceCount ? instanceCount : 1);
}

uint32_t SdlGpuBackend::frameDrawCalls() const { return s_drawCalls; }
uint64_t SdlGpuBackend::frameDrawVerts() const { return s_drawVerts; }
void     SdlGpuBackend::resetFrameDrawStats()  { s_drawCalls = 0; s_drawVerts = 0; }
const char *SdlGpuBackend::backendName() const { return SDL_GetGPUDeviceDriver(m_device); }

void SdlGpuBackend::dispatchCompute(GpuComputePassHandle pass,
                                     uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) {
    SDL_DispatchGPUCompute(reinterpret_cast<SDL_GPUComputePass*>(pass), groupsX, groupsY, groupsZ);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scissor / viewport
// ─────────────────────────────────────────────────────────────────────────────

void SdlGpuBackend::setScissor(GpuRenderPassHandle pass,
                                int32_t x, int32_t y, uint32_t w, uint32_t h) {
    SDL_Rect rect{x, y, (int)w, (int)h};
    SDL_SetGPUScissor(reinterpret_cast<SDL_GPURenderPass*>(pass), &rect);
}

void SdlGpuBackend::setViewport(GpuRenderPassHandle pass,
                                 float x, float y, float w, float h,
                                 float minDepth, float maxDepth) {
    SDL_GPUViewport vp{x, y, w, h, minDepth, maxDepth};
    SDL_SetGPUViewport(reinterpret_cast<SDL_GPURenderPass*>(pass), &vp);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource creation
// ─────────────────────────────────────────────────────────────────────────────

GpuTextureHandle SdlGpuBackend::createTexture(const GpuTextureCreateInfo& info) {
    SDL_GPUTextureCreateInfo ci = toSDL(info);
    auto tex = reinterpret_cast<GpuTextureHandle>(SDL_CreateGPUTexture(m_device, &ci));
    vramAdd((void*)tex, texBytes(info));
    return tex;
}

GpuBufferHandle SdlGpuBackend::createBuffer(const GpuBufferCreateInfo& info) {
    SDL_GPUBufferCreateInfo ci = toSDL(info);
    auto buf = reinterpret_cast<GpuBufferHandle>(SDL_CreateGPUBuffer(m_device, &ci));
    vramAdd((void*)buf, info.size);
    return buf;
}

GpuTransferBufferHandle SdlGpuBackend::createTransferBuffer(const GpuTransferBufferCreateInfo& info) {
    SDL_GPUTransferBufferCreateInfo ci = toSDL(info);
    return reinterpret_cast<GpuTransferBufferHandle>(SDL_CreateGPUTransferBuffer(m_device, &ci));
}

GpuSamplerHandle SdlGpuBackend::createSampler(const GpuSamplerCreateInfo& info) {
    SDL_GPUSamplerCreateInfo ci = toSDL(info);
    return reinterpret_cast<GpuSamplerHandle>(SDL_CreateGPUSampler(m_device, &ci));
}

GpuShaderHandle SdlGpuBackend::createShader(const GpuShaderCreateInfo& info) {
    // Shader format is compiled in based on the active shader backend.
#if defined(__ANDROID__) || !defined(LUMINOVEAU_SHADER_BACKEND_DXIL) && !defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
    SDL_GPUShaderFormat fmt = SDL_GPU_SHADERFORMAT_SPIRV;
#elif defined(LUMINOVEAU_SHADER_BACKEND_DXIL)
    SDL_GPUShaderFormat fmt = SDL_GPU_SHADERFORMAT_DXIL;
#elif defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
    SDL_GPUShaderFormat fmt = SDL_GPU_SHADERFORMAT_METALLIB;
#endif

    SDL_GPUShaderStage sdlStage;
    switch (info.stage) {
        case GpuShaderStage::Vertex:   sdlStage = SDL_GPU_SHADERSTAGE_VERTEX;   break;
        case GpuShaderStage::Fragment: sdlStage = SDL_GPU_SHADERSTAGE_FRAGMENT; break;
        default:                       sdlStage = SDL_GPU_SHADERSTAGE_VERTEX;   break;
    }

    SDL_GPUShaderCreateInfo ci{
        .code_size            = info.codeSize,
        .code                 = info.code,
        .entrypoint           = info.entrypoint,
        .format               = fmt,
        .stage                = sdlStage,
        .num_samplers         = info.samplerCount,
        .num_storage_textures = info.storageTextureCount,
        .num_storage_buffers  = info.storageBufferCount,
        .num_uniform_buffers  = info.uniformBufferCount,
    };
    return reinterpret_cast<GpuShaderHandle>(SDL_CreateGPUShader(m_device, &ci));
}

GpuGraphicsPipelineHandle SdlGpuBackend::createGraphicsPipeline(const GpuGraphicsPipelineCreateInfo& info) {
    std::vector<SDL_GPUVertexAttribute> attrs(info.attributeCount);
    for (uint32_t i = 0; i < info.attributeCount; ++i) {
        attrs[i] = {
            .location    = info.attributes[i].location,
            .buffer_slot = info.attributes[i].binding,
            .format      = toSDL(info.attributes[i].format),
            .offset      = info.attributes[i].offset,
        };
    }
    std::vector<SDL_GPUVertexBufferDescription> bufs(info.bindingCount);
    for (uint32_t i = 0; i < info.bindingCount; ++i) {
        bufs[i] = {
            .slot               = info.bindings[i].binding,
            .pitch              = info.bindings[i].stride,
            .input_rate         = info.bindings[i].instanceStepping
                                      ? SDL_GPU_VERTEXINPUTRATE_INSTANCE
                                      : SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = info.bindings[i].instanceStepping ? 1u : 0u,
        };
    }

    // One or more color targets (MRT). colorTargetCount == 0 → the single colorTargetFormat.
    uint32_t numColor = info.colorTargetCount ? info.colorTargetCount : 1u;
    std::vector<SDL_GPUColorTargetDescription> colorDescs(numColor);
    for (uint32_t i = 0; i < numColor; ++i) {
        colorDescs[i] = {
            .format      = toSDL(info.colorTargetCount ? info.colorTargetFormats[i] : info.colorTargetFormat),
            .blend_state = toSDL(info.colorTargetCount ? info.colorTargetBlends[i]  : info.blend),
        };
    }

    SDL_GPUGraphicsPipelineCreateInfo ci{
        .vertex_shader   = reinterpret_cast<SDL_GPUShader*>(info.vertexShader),
        .fragment_shader = reinterpret_cast<SDL_GPUShader*>(info.fragmentShader),
        .vertex_input_state = {
            .vertex_buffer_descriptions = bufs.data(),
            .num_vertex_buffers         = info.bindingCount,
            .vertex_attributes          = attrs.data(),
            .num_vertex_attributes      = info.attributeCount,
        },
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {
            .fill_mode  = toSDL(info.fillMode),
            .cull_mode  = toSDL(info.cullMode),
            .front_face = toSDL(info.frontFace),
        },
        .multisample_state = {
            .sample_count = toSDL(info.sampleCount),
        },
        .depth_stencil_state = {
            .compare_op        = SDL_GPU_COMPAREOP_LESS,
            .enable_depth_test  = info.hasDepthTarget,
            .enable_depth_write = info.hasDepthTarget,
        },
        .target_info = {
            .color_target_descriptions     = colorDescs.data(),
            .num_color_targets             = numColor,
            .depth_stencil_format          = toSDL(info.depthTargetFormat),
            .has_depth_stencil_target      = info.hasDepthTarget,
        },
    };
    return reinterpret_cast<GpuGraphicsPipelineHandle>(SDL_CreateGPUGraphicsPipeline(m_device, &ci));
}

GpuComputePipelineHandle SdlGpuBackend::createComputePipeline(const GpuComputePipelineCreateInfo& info) {
    // Shader cross-compilation is handled by SDL_ShaderCross; raw SPIRV bytes expected.
    SDL_GPUComputePipelineCreateInfo ci{
        .code_size                         = info.codeSize,
        .code                              = info.code,
        .entrypoint                        = info.entrypoint,
        .format                            = SDL_GPU_SHADERFORMAT_SPIRV,
        .num_samplers                      = info.samplerCount,
        .num_readonly_storage_textures     = info.readonlyStorageTextureCount,
        .num_readonly_storage_buffers      = info.readonlyStorageBufferCount,
        .num_readwrite_storage_textures    = info.readwriteStorageTextureCount,
        .num_readwrite_storage_buffers     = info.readwriteStorageBufferCount,
        .num_uniform_buffers               = info.uniformBufferCount,
        .threadcount_x                     = info.threadCountX,
        .threadcount_y                     = info.threadCountY,
        .threadcount_z                     = info.threadCountZ,
    };
    return reinterpret_cast<GpuComputePipelineHandle>(SDL_CreateGPUComputePipeline(m_device, &ci));
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource release
// ─────────────────────────────────────────────────────────────────────────────

void SdlGpuBackend::releaseTexture(GpuTextureHandle handle) {
    if (handle) { vramRemove((void*)handle); SDL_ReleaseGPUTexture(m_device, reinterpret_cast<SDL_GPUTexture*>(handle)); }
}

void SdlGpuBackend::releaseBuffer(GpuBufferHandle handle) {
    if (handle) { vramRemove((void*)handle); SDL_ReleaseGPUBuffer(m_device, reinterpret_cast<SDL_GPUBuffer*>(handle)); }
}

void SdlGpuBackend::releaseTransferBuffer(GpuTransferBufferHandle handle) {
    if (handle) SDL_ReleaseGPUTransferBuffer(m_device, reinterpret_cast<SDL_GPUTransferBuffer*>(handle));
}

void SdlGpuBackend::releaseSampler(GpuSamplerHandle handle) {
    if (handle) SDL_ReleaseGPUSampler(m_device, reinterpret_cast<SDL_GPUSampler*>(handle));
}

void SdlGpuBackend::releaseShader(GpuShaderHandle handle) {
    if (handle) SDL_ReleaseGPUShader(m_device, reinterpret_cast<SDL_GPUShader*>(handle));
}

void SdlGpuBackend::releaseGraphicsPipeline(GpuGraphicsPipelineHandle handle) {
    if (handle) SDL_ReleaseGPUGraphicsPipeline(m_device, reinterpret_cast<SDL_GPUGraphicsPipeline*>(handle));
}

void SdlGpuBackend::releaseComputePipeline(GpuComputePipelineHandle handle) {
    if (handle) SDL_ReleaseGPUComputePipeline(m_device, reinterpret_cast<SDL_GPUComputePipeline*>(handle));
}

// ─────────────────────────────────────────────────────────────────────────────
// Transfer buffer mapping
// ─────────────────────────────────────────────────────────────────────────────

void* SdlGpuBackend::mapTransferBuffer(GpuTransferBufferHandle handle, bool cycle) {
    return SDL_MapGPUTransferBuffer(m_device, reinterpret_cast<SDL_GPUTransferBuffer*>(handle), cycle);
}

void SdlGpuBackend::unmapTransferBuffer(GpuTransferBufferHandle handle) {
    SDL_UnmapGPUTransferBuffer(m_device, reinterpret_cast<SDL_GPUTransferBuffer*>(handle));
}

// ─────────────────────────────────────────────────────────────────────────────
// Upload / download
// ─────────────────────────────────────────────────────────────────────────────

void SdlGpuBackend::uploadToTexture(GpuCmdBufferHandle cmd,
                                     const GpuTransferBufferRegion& src,
                                     const GpuTextureRegion& dst,
                                     bool cycle) {
    auto* cmdBuf    = reinterpret_cast<SDL_GPUCommandBuffer*>(cmd);
    auto* copyPass  = SDL_BeginGPUCopyPass(cmdBuf);

    SDL_GPUTextureTransferInfo transferInfo{
        .transfer_buffer = reinterpret_cast<SDL_GPUTransferBuffer*>(src.transferBuffer),
        .offset          = src.offset,
        .pixels_per_row  = src.pixels_per_row,
        .rows_per_layer  = src.rows_per_layer,
    };
    SDL_GPUTextureRegion region{
        .texture   = reinterpret_cast<SDL_GPUTexture*>(dst.texture),
        .mip_level = dst.mipLevel,
        .layer     = dst.layer,
        .x = dst.x, .y = dst.y, .z = dst.z,
        .w = dst.width, .h = dst.height, .d = dst.depth,
    };
    SDL_UploadToGPUTexture(copyPass, &transferInfo, &region, cycle);
    SDL_EndGPUCopyPass(copyPass);
}

void SdlGpuBackend::uploadToBuffer(GpuCmdBufferHandle cmd,
                                    GpuTransferBufferHandle src, uint32_t srcOffset,
                                    GpuBufferHandle dst,         uint32_t dstOffset,
                                    uint32_t size, bool cycle) {
    auto* cmdBuf   = reinterpret_cast<SDL_GPUCommandBuffer*>(cmd);
    auto* copyPass = SDL_BeginGPUCopyPass(cmdBuf);

    SDL_GPUTransferBufferLocation srcLoc{
        .transfer_buffer = reinterpret_cast<SDL_GPUTransferBuffer*>(src),
        .offset          = srcOffset,
    };
    SDL_GPUBufferRegion dstRegion{
        .buffer = reinterpret_cast<SDL_GPUBuffer*>(dst),
        .offset = dstOffset,
        .size   = size,
    };
    SDL_UploadToGPUBuffer(copyPass, &srcLoc, &dstRegion, cycle);
    SDL_EndGPUCopyPass(copyPass);
}

void SdlGpuBackend::downloadFromTexture(GpuCmdBufferHandle cmd,
                                         const GpuTextureRegion& src,
                                         const GpuTransferBufferRegion& dst) {
    auto* cmdBuf   = reinterpret_cast<SDL_GPUCommandBuffer*>(cmd);
    auto* copyPass = SDL_BeginGPUCopyPass(cmdBuf);

    SDL_GPUTextureRegion srcRegion{
        .texture   = reinterpret_cast<SDL_GPUTexture*>(src.texture),
        .mip_level = src.mipLevel,
        .layer     = src.layer,
        .x = src.x, .y = src.y, .z = src.z,
        .w = src.width, .h = src.height, .d = src.depth,
    };
    SDL_GPUTextureTransferInfo dstInfo{
        .transfer_buffer = reinterpret_cast<SDL_GPUTransferBuffer*>(dst.transferBuffer),
        .offset          = dst.offset,
        .pixels_per_row  = dst.pixels_per_row,
        .rows_per_layer  = dst.rows_per_layer,
    };
    SDL_DownloadFromGPUTexture(copyPass, &srcRegion, &dstInfo);
    SDL_EndGPUCopyPass(copyPass);
}

void SdlGpuBackend::blitTexture(GpuCmdBufferHandle cmd,
                                 GpuTextureHandle src, GpuTextureHandle dst,
                                 uint32_t srcX, uint32_t srcY, uint32_t srcW, uint32_t srcH,
                                 uint32_t dstX, uint32_t dstY, uint32_t dstW, uint32_t dstH,
                                 GpuFilter filter) {
    SDL_GPUBlitInfo blit{
        .source = {
            .texture  = reinterpret_cast<SDL_GPUTexture*>(src),
            .mip_level = 0, .layer_or_depth_plane = 0,
            .x = srcX, .y = srcY, .w = srcW, .h = srcH,
        },
        .destination = {
            .texture  = reinterpret_cast<SDL_GPUTexture*>(dst),
            .mip_level = 0, .layer_or_depth_plane = 0,
            .x = dstX, .y = dstY, .w = dstW, .h = dstH,
        },
        .load_op     = SDL_GPU_LOADOP_DONT_CARE,
        .clear_color = {},
        .flip_mode   = SDL_FLIP_NONE,
        .filter      = toSDL(filter),
        .cycle       = false,
    };
    SDL_BlitGPUTexture(reinterpret_cast<SDL_GPUCommandBuffer*>(cmd), &blit);
}

// ─────────────────────────────────────────────────────────────────────────────
// IBackendAccess
// ─────────────────────────────────────────────────────────────────────────────

void* SdlGpuBackend::getRawDevice() const {
    return m_device;
}

void* SdlGpuBackend::getRawSampler(int /*scaleModeInt*/) const {
    return nullptr; // Samplers are owned by Renderer in Phase 1.
}
