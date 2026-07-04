#include "WebGpuGpuBackend.h"
#include "WebGpuHandles.h"
#include "core/log/log.h"

#include <cstring>
#include <cstdlib>
#include <cassert>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#ifndef __EMSCRIPTEN__
// sdl3webgpu provides SDL_GetWGPUSurface for native builds
#include <sdl3webgpu.h>
#include <SDL3/SDL.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Async init helpers
// ─────────────────────────────────────────────────────────────────────────────

static inline WGPUStringView wgpuStr(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

static void pollUntil(bool& flag) {
#ifdef __EMSCRIPTEN__
    while (!flag) emscripten_sleep(10);
#else
    // Dawn native: request callbacks are typically called synchronously.
    // Spin without yielding — add platform yield if needed.
    while (!flag) { /* yield */ }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool WebGpuGpuBackend::init(void* windowHandle) {
    // 1. Instance
    WGPUInstanceDescriptor instDesc{};
    m_instance = wgpuCreateInstance(&instDesc);
    if (!m_instance) { LOG_WARNING("WebGpu init: wgpuCreateInstance returned null"); return false; }

    // 2. Surface
#ifdef __EMSCRIPTEN__
    // Emscripten: create surface from the HTML canvas element
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc{};
    canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasDesc.selector    = wgpuStr("#canvas");
    WGPUSurfaceDescriptor surfDesc{};
    surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc.chain);
    m_surface = wgpuInstanceCreateSurface(m_instance, &surfDesc);
#else
    // Native: surface from SDL3 window via sdl3webgpu
    m_surface = SDL_GetWGPUSurface(m_instance, static_cast<SDL_Window*>(windowHandle));
#endif
    if (!m_surface) { LOG_WARNING("WebGpu init: m_surface is null (canvas not found?)"); return false; }

    // 3. Adapter
    struct AdapterData { WGPUAdapter adapter = nullptr; bool done = false; };
    auto requestAdapter = [&](WGPURequestAdapterOptions& opts) -> WGPUAdapter {
        AdapterData ad;
        WGPURequestAdapterCallbackInfo cbi{};
        cbi.mode     = WGPUCallbackMode_AllowSpontaneous;
        cbi.callback = [](WGPURequestAdapterStatus, WGPUAdapter adapter, WGPUStringView, void* ud1, void*) {
            auto* d = static_cast<AdapterData*>(ud1);
            d->adapter = adapter;
            d->done    = true;
        };
        cbi.userdata1 = &ad;
        wgpuInstanceRequestAdapter(m_instance, &opts, cbi);
        pollUntil(ad.done);
        return ad.adapter;
    };

    {
        WGPURequestAdapterOptions opts{};
        opts.compatibleSurface = m_surface;
        m_adapter = requestAdapter(opts);
    }
    // Fallback 1: try high-performance preference (some Firefox builds need this for discrete GPU).
    if (!m_adapter) {
        WGPURequestAdapterOptions opts{};
        opts.compatibleSurface = m_surface;
        opts.powerPreference   = WGPUPowerPreference_HighPerformance;
        m_adapter = requestAdapter(opts);
    }
    // Fallback 2: no surface filter (browsers can refuse surface-bound requests after a prior
    // device used the canvas; the unbound request often succeeds).
    if (!m_adapter) {
        WGPURequestAdapterOptions opts{};
        m_adapter = requestAdapter(opts);
        if (m_adapter) LOG_WARNING("WebGpu init: adapter acquired without compatibleSurface filter");
    }
    if (!m_adapter) { LOG_WARNING("WebGpu init: requestAdapter returned null after fallbacks"); return false; }

    // 4. Device
    struct DeviceData { WGPUDevice device = nullptr; bool done = false; };
    DeviceData deviceData;

    // Request higher buffer/storage limits — particle buffer can reach ~320 MB at 5M particles
    // (64 B each), exceeding the default maxBufferSize (256 MB) and maxStorageBufferBindingSize
    // (128 MB). Clamp the request to the adapter's reported maximums so the device-request
    // doesn't fail outright on adapters with smaller caps (e.g. Firefox on integrated GPUs).
    WGPULimits adapterLimits = WGPU_LIMITS_INIT;
    wgpuAdapterGetLimits(m_adapter, &adapterLimits);

    auto clampMin = [](uint64_t want, uint64_t cap) { return cap > 0 && cap < want ? cap : want; };

    WGPULimits requiredLimits                        = WGPU_LIMITS_INIT;
    requiredLimits.maxBufferSize                     = clampMin(536870912ULL, adapterLimits.maxBufferSize);
    requiredLimits.maxStorageBufferBindingSize       = clampMin(536870912ULL, adapterLimits.maxStorageBufferBindingSize);
    // BSP data is packed into single-row data textures that can exceed the default 8192-wide
    // limit (e.g. 12288 on larger maps). Request the adapter's full maxTextureDimension2D
    // (commonly 16384) so those createTexture calls don't fail and leave null render targets.
    if (adapterLimits.maxTextureDimension2D > 0)
        requiredLimits.maxTextureDimension2D = adapterLimits.maxTextureDimension2D;

    // TextureFormatsTier1: unlocks read_write storage texture access on
    // rgba8unorm, rgba16float, rgba32float. Optional — only requested if adapter exposes it.
    std::vector<WGPUFeatureName> requiredFeatures;
    if (wgpuAdapterHasFeature(m_adapter, WGPUFeatureName_TextureFormatsTier1)) {
        requiredFeatures.push_back(WGPUFeatureName_TextureFormatsTier1);
        LOG_INFO("WebGPU: TextureFormatsTier1 supported, requesting feature");
    } else {
        LOG_WARNING("WebGPU: TextureFormatsTier1 not supported by adapter — "
                    "compute shaders using read_write rgba16f/rgba32f storage textures will fail");
    }
    if (wgpuAdapterHasFeature(m_adapter, WGPUFeatureName_Float32Filterable)) {
        requiredFeatures.push_back(WGPUFeatureName_Float32Filterable);
        m_float32Filterable = true;
        LOG_INFO("WebGPU: Float32Filterable supported, requesting feature");
    } else {
        m_float32Filterable = false;
        LOG_WARNING("WebGPU: Float32Filterable not supported — sampling rgba32f textures must use Nearest filter");
    }
    // TextureCompressionBC: lets us upload the HD pack's BC7 (KTX2) textures directly. Optional —
    // when absent (e.g. some mobile adapters) the KTX2 loader transcodes to RGBA8 instead.
    if (wgpuAdapterHasFeature(m_adapter, WGPUFeatureName_TextureCompressionBC)) {
        requiredFeatures.push_back(WGPUFeatureName_TextureCompressionBC);
        m_textureCompressionBC = true;
        LOG_INFO("WebGPU: TextureCompressionBC supported, requesting feature");
    } else {
        m_textureCompressionBC = false;
        LOG_WARNING("WebGPU: TextureCompressionBC not supported — KTX2 textures fall back to RGBA8");
    }

    WGPUDeviceDescriptor deviceDesc{};
    deviceDesc.requiredLimits       = &requiredLimits;
    if (!requiredFeatures.empty()) {
        deviceDesc.requiredFeatures     = requiredFeatures.data();
        deviceDesc.requiredFeatureCount = requiredFeatures.size();
    }
    // Surface validation errors via uncapturedErrorCallbackInfo in the device descriptor.
    deviceDesc.uncapturedErrorCallbackInfo.callback =
        [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
            std::string msg = (message.data && message.length > 0)
                ? std::string(message.data, message.length) : "(no message)";
            LOG_WARNING("WebGPU device error (type={}): {}", (int)type, msg);
        };
    WGPURequestDeviceCallbackInfo deviceCBI{};
    deviceCBI.mode     = WGPUCallbackMode_AllowSpontaneous;
    deviceCBI.callback = [](WGPURequestDeviceStatus, WGPUDevice device, WGPUStringView, void* ud1, void*) {
        auto* d = static_cast<DeviceData*>(ud1);
        d->device = device;
        d->done   = true;
    };
    deviceCBI.userdata1 = &deviceData;
    wgpuAdapterRequestDevice(m_adapter, &deviceDesc, deviceCBI);
    pollUntil(deviceData.done);
    m_device = deviceData.device;
    if (!m_device) { LOG_WARNING("WebGpu init: requestDevice returned null (limits/features may exceed adapter caps)"); return false; }

    m_queue = wgpuDeviceGetQueue(m_device);

    // 5. Configure surface
    int w = 0, h = 0;
#ifdef __EMSCRIPTEN__
    // Use window.innerWidth/Height as the target size for a fullscreen canvas.
    // clientWidth/clientHeight only reflects CSS layout (may be fixed/0); the pixel
    // buffer (canvas.width/height) and CSS display size (canvas.style.width/height)
    // must both be set explicitly to fill the viewport.
    w = EM_ASM_INT({ return window.innerWidth  | 0; });
    h = EM_ASM_INT({ return window.innerHeight | 0; });
    if (w <= 0 || h <= 0) { w = 1280; h = 720; }
    EM_ASM({
        var c = document.querySelector('#canvas');
        if (c) {
            c.width  = $0; c.height = $1;
            c.style.width  = $0 + 'px';
            c.style.height = $1 + 'px';
        }
    }, w, h);
#else
    SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(windowHandle), &w, &h);
#endif
    m_swapchainWidth  = static_cast<uint32_t>(w);
    m_swapchainHeight = static_cast<uint32_t>(h);
    m_initialCanvasWidth  = static_cast<uint32_t>(w);
    m_initialCanvasHeight = static_cast<uint32_t>(h);

    // Prefer BGRA8Unorm (most common); check capabilities if needed.
    m_swapchainFormat = WGPUTextureFormat_BGRA8Unorm;

    WGPUSurfaceConfiguration surfCfg{};
    surfCfg.device      = m_device;
    surfCfg.format      = m_swapchainFormat;
    surfCfg.usage       = WGPUTextureUsage_RenderAttachment;
    surfCfg.width       = m_swapchainWidth;
    surfCfg.height      = m_swapchainHeight;
    surfCfg.presentMode = WGPUPresentMode_Fifo;
    surfCfg.alphaMode   = WGPUCompositeAlphaMode_Auto;
    wgpuSurfaceConfigure(m_surface, &surfCfg);

#ifdef __EMSCRIPTEN__
    // Reset tracking so acquireSwapchainTexture reconfigures on the first frame.
    // A surface configured during init returns Outdated on the first getSurfaceTexture
    // call; reconfiguring in the same frame as getSurfaceTexture (as the resize path
    // does) is what makes it succeed.
    m_swapchainWidth  = 0;
    m_swapchainHeight = 0;
#endif

    // 6. Zero buffer (placeholder for empty uniform slots)
    static const uint8_t zeros[kZeroBufSize] = {};
    WGPUBufferDescriptor zeroDesc{};
    zeroDesc.size             = kZeroBufSize;
    zeroDesc.usage            = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    zeroDesc.mappedAtCreation = false;
    m_zeroBuffer = wgpuDeviceCreateBuffer(m_device, &zeroDesc);
    wgpuQueueWriteBuffer(m_queue, m_zeroBuffer, 0, zeros, kZeroBufSize);

    return true;
}

void WebGpuGpuBackend::shutdown() {
    waitIdle();

    if (m_currentSurfaceView) { wgpuTextureViewRelease(m_currentSurfaceView); m_currentSurfaceView = nullptr; }
    if (m_currentSurfaceTex)  { wgpuTextureRelease(m_currentSurfaceTex);      m_currentSurfaceTex  = nullptr; }
    if (m_zeroBuffer)         { wgpuBufferRelease(m_zeroBuffer);               m_zeroBuffer         = nullptr; }

    if (m_surface)  { wgpuSurfaceUnconfigure(m_surface); wgpuSurfaceRelease(m_surface);  m_surface  = nullptr; }
    if (m_queue)    { wgpuQueueRelease(m_queue);                                           m_queue    = nullptr; }
    if (m_device)   { wgpuDeviceRelease(m_device);                                         m_device   = nullptr; }
    if (m_adapter)  { wgpuAdapterRelease(m_adapter);                                       m_adapter  = nullptr; }
    if (m_instance) { wgpuInstanceRelease(m_instance);                                     m_instance = nullptr; }
}

WebGpuGpuBackend::~WebGpuGpuBackend() {
    shutdown();
}

WGPUBuffer WebGpuGpuBackend::_acquireUniformBuffer(uint32_t alignedSize) {
    auto it = m_uniformBufferPool.find(alignedSize);
    if (it != m_uniformBufferPool.end() && !it->second.empty()) {
        WGPUBuffer b = it->second.back();
        it->second.pop_back();
        return b;
    }
    WGPUBufferDescriptor bd{};
    bd.size  = alignedSize;
    bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    return wgpuDeviceCreateBuffer(m_device, &bd);
}

void WebGpuGpuBackend::_recycleUniformBuffer(WGPUBuffer buf, uint32_t alignedSize) {
    if (!buf) return;
    auto& bucket = m_uniformBufferPool[alignedSize];
    // Cap to avoid pathological growth — a few hundred per size is more than enough.
    if (bucket.size() >= 256) { wgpuBufferRelease(buf); return; }
    bucket.push_back(buf);
}

void WebGpuGpuBackend::waitIdle() {
    // Submitting an empty command buffer and waiting for its completion is
    // the portable WebGPU way to flush all pending GPU work.
    WGPUCommandEncoderDescriptor encDesc{};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(m_device, &encDesc);
    WGPUCommandBufferDescriptor cbDesc{};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cbDesc);
    wgpuCommandEncoderRelease(enc);

    struct WaitData { bool done = false; };
    WaitData wd;
    WGPUQueueWorkDoneCallbackInfo wdCBI{};
    wdCBI.mode     = WGPUCallbackMode_AllowSpontaneous;
    wdCBI.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* ud1, void*) {
        static_cast<WaitData*>(ud1)->done = true;
    };
    wdCBI.userdata1 = &wd;
    wgpuQueueOnSubmittedWorkDone(m_queue, wdCBI);
    wgpuQueueSubmit(m_queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    pollUntil(wd.done);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame management
// ─────────────────────────────────────────────────────────────────────────────

GpuCmdBufferHandle WebGpuGpuBackend::acquireCommandBuffer() {
    WGPUCommandEncoderDescriptor desc{};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(m_device, &desc);
    auto* cb = new WgpuCmdBuffer();
    cb->encoder = enc;
    return reinterpret_cast<GpuCmdBufferHandle>(cb);
}

void WebGpuGpuBackend::submitCommandBuffer(GpuCmdBufferHandle cmd) {
    auto* cb = reinterpret_cast<WgpuCmdBuffer*>(cmd);
    WGPUCommandBufferDescriptor desc{};
    WGPUCommandBuffer gpuCmd = wgpuCommandEncoderFinish(cb->encoder, &desc);
    wgpuCommandEncoderRelease(cb->encoder);

    wgpuQueueSubmit(m_queue, 1, &gpuCmd);
    wgpuCommandBufferRelease(gpuCmd);

    // Recycle per-frame uniform buffers into the pool instead of releasing them. WebGPU
    // implementations (Firefox in particular) don't reclaim buffer allocations fast enough
    // to keep up with per-draw uniform pushes, leading to "Not enough memory left" within
    // seconds of running.
    for (auto& bg : cb->cleanup.tempBindGroups)    wgpuBindGroupRelease(bg);
    for (auto  b  : cb->cleanup.tempBuffers)       wgpuBufferRelease(b);
    for (auto& p  : cb->cleanup.tempUniformBuffers) _recycleUniformBuffer(p.first, p.second);
    cb->cleanup.tempBuffers.clear();
    cb->cleanup.tempUniformBuffers.clear();
    cb->cleanup.tempBindGroups.clear();
    delete cb;

    // Frame-end present is done separately by the renderer via presentSwapchain() so that
    // mid-frame submits (e.g. texture uploads during scene init) don't release the
    // currently-acquired swapchain view.
}

void WebGpuGpuBackend::presentSwapchain() {
#ifndef __EMSCRIPTEN__
    if (m_currentSurfaceView) wgpuTextureViewRelease(m_currentSurfaceView);
    if (m_currentSurfaceTex)  { wgpuSurfacePresent(m_surface); wgpuTextureRelease(m_currentSurfaceTex); }
#else
    // Browser/emscripten: do NOT call wgpuTextureRelease on the surface texture here — the queue
    // submit is async and the swap-buffer provider may still be using it when this runs, which
    // triggers "Destroyed texture used in a submit". The browser cleans up automatically when
    // the next acquireSwapchainTexture is called.
    if (m_currentSurfaceView) wgpuTextureViewRelease(m_currentSurfaceView);
#endif
    m_currentSurfaceView = nullptr;
    m_currentSurfaceTex  = nullptr;
}

GpuTextureHandle WebGpuGpuBackend::acquireSwapchainTexture(GpuCmdBufferHandle /*cmd*/,
                                                            uint32_t& outWidth,
                                                            uint32_t& outHeight) {
    // Check window.innerWidth/Height each frame; resize canvas + surface when it changes.
#ifdef __EMSCRIPTEN__
    {
        int w = EM_ASM_INT({ return window.innerWidth  | 0; });
        int h = EM_ASM_INT({ return window.innerHeight | 0; });
        if (w > 0 && h > 0 &&
            (static_cast<uint32_t>(w) != m_swapchainWidth ||
             static_cast<uint32_t>(h) != m_swapchainHeight)) {
            m_swapchainWidth  = static_cast<uint32_t>(w);
            m_swapchainHeight = static_cast<uint32_t>(h);
            EM_ASM({
                var c = document.querySelector('#canvas');
                if (c) {
                    c.width  = $0; c.height = $1;
                    c.style.width  = $0 + 'px';
                    c.style.height = $1 + 'px';
                }
            }, w, h);
            WGPUSurfaceConfiguration cfg{};
            cfg.device      = m_device;
            cfg.format      = m_swapchainFormat;
            cfg.usage       = WGPUTextureUsage_RenderAttachment;
            cfg.width       = m_swapchainWidth;
            cfg.height      = m_swapchainHeight;
            cfg.presentMode = WGPUPresentMode_Fifo;
            cfg.alphaMode   = WGPUCompositeAlphaMode_Auto;
            wgpuSurfaceConfigure(m_surface, &cfg);
        }
    }
#endif

    WGPUSurfaceTexture surfTex{};
    wgpuSurfaceGetCurrentTexture(m_surface, &surfTex);

#ifdef __EMSCRIPTEN__
    // If the surface is outdated (e.g. first frame after init, or after canvas resize),
    // reconfigure with current dimensions and retry once.
    if (surfTex.status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
        surfTex.status == WGPUSurfaceGetCurrentTextureStatus_Lost) {
        if (m_swapchainWidth == 0 || m_swapchainHeight == 0) {
            m_swapchainWidth  = static_cast<uint32_t>(EM_ASM_INT({ return window.innerWidth  | 0; }));
            m_swapchainHeight = static_cast<uint32_t>(EM_ASM_INT({ return window.innerHeight | 0; }));
        }
        WGPUSurfaceConfiguration retryCfg{};
        retryCfg.device      = m_device;
        retryCfg.format      = m_swapchainFormat;
        retryCfg.usage       = WGPUTextureUsage_RenderAttachment;
        retryCfg.width       = m_swapchainWidth;
        retryCfg.height      = m_swapchainHeight;
        retryCfg.presentMode = WGPUPresentMode_Fifo;
        retryCfg.alphaMode   = WGPUCompositeAlphaMode_Auto;
        wgpuSurfaceConfigure(m_surface, &retryCfg);
        wgpuSurfaceGetCurrentTexture(m_surface, &surfTex);
    }
#endif

    if (surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        outWidth = outHeight = 0;
        return 0;
    }

    m_currentSurfaceTex  = surfTex.texture;
    m_currentSurfaceView = wgpuTextureCreateView(m_currentSurfaceTex, nullptr);

    outWidth  = m_swapchainWidth;
    outHeight = m_swapchainHeight;

    // Return the TextureView so it can be used as a color target.
    // The caller uses this as GpuTextureHandle — we use the view pointer.
    return reinterpret_cast<GpuTextureHandle>(m_currentSurfaceView);
}

GpuTextureFormat WebGpuGpuBackend::getSwapchainFormat() const {
    return fromWGPU(m_swapchainFormat);
}

// ─────────────────────────────────────────────────────────────────────────────
// Render pass
// ─────────────────────────────────────────────────────────────────────────────

GpuRenderPassHandle WebGpuGpuBackend::beginRenderPass(GpuCmdBufferHandle cmd,
                                                       const GpuColorTargetInfo* colorTargets,
                                                       uint32_t colorTargetCount,
                                                       const GpuDepthStencilTargetInfo* depthTarget) {
    auto* cb = reinterpret_cast<WgpuCmdBuffer*>(cmd);

    std::vector<WGPURenderPassColorAttachment> colors(colorTargetCount);
    for (uint32_t i = 0; i < colorTargetCount; ++i) {
        const auto& ct = colorTargets[i];

        // Texture handle is either a WGPUTextureView* (for swapchain) or WgpuTexture*
        WGPUTextureView view = nullptr;
        if (ct.texture == reinterpret_cast<GpuTextureHandle>(m_currentSurfaceView)) {
            view = m_currentSurfaceView;
        } else {
            auto* tex = reinterpret_cast<WgpuTexture*>(ct.texture);
            // For cube/array targets, render into the selected face/layer via its 2D view; the
            // default view is the (unrenderable) cube/array sampling view.
            if (tex && tex->layerCount > 1 && ct.layer < 6 && tex->faceViews[ct.layer]) {
                view = tex->faceViews[ct.layer];
            } else {
                view = tex ? tex->defaultView : nullptr;
            }
        }

        WGPUTextureView resolveView = nullptr;
        if (ct.resolveTexture) {
            auto* rTex = reinterpret_cast<WgpuTexture*>(ct.resolveTexture);
            resolveView = rTex ? rTex->defaultView : nullptr;
        }

        if (!view) {
            auto* tex = reinterpret_cast<WgpuTexture*>(ct.texture);
            LOG_WARNING("beginRenderPass: color target {} has null view (handle={:#x}, tex_field={:#x}, fmt={}, {}x{})",
                        i, (uintptr_t)ct.texture,
                        tex ? (uintptr_t)tex->texture : 0,
                        tex ? (int)tex->format : -1,
                        tex ? tex->width  : 0,
                        tex ? tex->height : 0);
        }
        colors[i] = WGPURenderPassColorAttachment{};
        colors[i].depthSlice    = WGPU_DEPTH_SLICE_UNDEFINED;
        colors[i].view          = view;
        colors[i].resolveTarget = resolveView;
        colors[i].loadOp        = toWGPU(ct.loadOp);
        colors[i].storeOp       = toWGPU(ct.storeOp);
        colors[i].clearValue    = {ct.clearR, ct.clearG, ct.clearB, ct.clearA};
    }

    WGPURenderPassDepthStencilAttachment depthAttach{};
    WGPURenderPassDepthStencilAttachment* pDepth = nullptr;
    if (depthTarget && depthTarget->texture) {
        auto* dTex = reinterpret_cast<WgpuTexture*>(depthTarget->texture);
        depthAttach.view              = dTex->defaultView;
        depthAttach.depthLoadOp       = toWGPU(depthTarget->loadOp);
        depthAttach.depthStoreOp      = toWGPU(depthTarget->storeOp);
        depthAttach.depthClearValue   = depthTarget->clearDepth;
        // Set stencil ops only when the format has a stencil aspect. Depth-only formats
        // (Depth32Float, Depth16Unorm) reject any non-Undefined stencil op.
        const bool hasStencil = (dTex->format == WGPUTextureFormat_Depth24PlusStencil8 ||
                                 dTex->format == WGPUTextureFormat_Depth32FloatStencil8 ||
                                 dTex->format == WGPUTextureFormat_Stencil8);
        if (hasStencil) {
            depthAttach.stencilLoadOp     = WGPULoadOp_Clear;
            depthAttach.stencilStoreOp    = WGPUStoreOp_Discard;
            depthAttach.stencilClearValue = depthTarget->clearStencil;
        }
        pDepth = &depthAttach;
    }

    WGPURenderPassDescriptor desc{};
    desc.colorAttachmentCount   = colorTargetCount;
    desc.colorAttachments       = colors.data();
    desc.depthStencilAttachment = pDepth;

    WGPURenderPassEncoder enc = wgpuCommandEncoderBeginRenderPass(cb->encoder, &desc);

    auto* rp  = new WgpuRenderPass();
    rp->encoder = enc;
    rp->cmdBuf  = cb;
    rp->device  = m_device;
    rp->queue   = m_queue;
    return reinterpret_cast<GpuRenderPassHandle>(rp);
}

void WebGpuGpuBackend::endRenderPass(GpuRenderPassHandle pass) {
    auto* rp = reinterpret_cast<WgpuRenderPass*>(pass);
    wgpuRenderPassEncoderEnd(rp->encoder);
    wgpuRenderPassEncoderRelease(rp->encoder);
    delete rp;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute pass
// ─────────────────────────────────────────────────────────────────────────────

GpuComputePassHandle WebGpuGpuBackend::beginComputePass(GpuCmdBufferHandle cmd,
                                                         const GpuStorageTextureBinding* rwTex, uint32_t rwTexCount,
                                                         const GpuStorageBufferBinding*  rwBuf, uint32_t rwBufCount) {
    auto* cb = reinterpret_cast<WgpuCmdBuffer*>(cmd);

    WGPUComputePassDescriptor desc{};
    WGPUComputePassEncoder enc = wgpuCommandEncoderBeginComputePass(cb->encoder, &desc);

    auto* cp  = new WgpuComputePass();
    cp->encoder = enc;
    cp->cmdBuf  = cb;
    cp->device  = m_device;
    cp->queue   = m_queue;

    // Save RW buffers — bound to group 2 just before dispatch once the pipeline is known.
    for (uint32_t i = 0; i < rwBufCount; ++i) {
        auto* b = reinterpret_cast<WgpuBuffer*>(rwBuf[i].buffer);
        if (b) cp->pendingRWBuffers.push_back({b->buffer, b->size});
    }
    // Save RW textures — also bound to group 2 if pipeline uses tex-only RW slot.
    for (uint32_t i = 0; i < rwTexCount; ++i) {
        auto* t = reinterpret_cast<WgpuTexture*>(rwTex[i].texture);
        if (t) cp->pendingRWTextures.push_back({t->defaultView});
    }

    return reinterpret_cast<GpuComputePassHandle>(cp);
}

void WebGpuGpuBackend::endComputePass(GpuComputePassHandle pass) {
    auto* cp = reinterpret_cast<WgpuComputePass*>(pass);
    wgpuComputePassEncoderEnd(cp->encoder);
    wgpuComputePassEncoderRelease(cp->encoder);
    delete cp;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline binding
// ─────────────────────────────────────────────────────────────────────────────

void WebGpuGpuBackend::bindGraphicsPipeline(GpuRenderPassHandle pass, GpuGraphicsPipelineHandle pipeline) {
    auto* rp = reinterpret_cast<WgpuRenderPass*>(pass);
    auto* pl = reinterpret_cast<WgpuGraphicsPipeline*>(pipeline);
    rp->currentPipeline = pl;
    rp->cmdBuf->vertexUniforms.clear();
    rp->cmdBuf->fragmentUniforms.clear();
    wgpuRenderPassEncoderSetPipeline(rp->encoder, pl->pipeline);
    // Cover any pipeline-layout slots whose BGL is empty so Firefox accepts the draw.
    for (int i = 0; i < 4; ++i) {
        if (pl->emptyBG[i]) {
            wgpuRenderPassEncoderSetBindGroup(rp->encoder, (uint32_t)i, pl->emptyBG[i], 0, nullptr);
        }
    }
}

void WebGpuGpuBackend::bindComputePipeline(GpuComputePassHandle pass, GpuComputePipelineHandle pipeline) {
    auto* cp = reinterpret_cast<WgpuComputePass*>(pass);
    auto* pl = reinterpret_cast<WgpuComputePipelineData*>(pipeline);
    cp->currentPipeline = pl;
    cp->cmdBuf->computeUniforms.clear();
    wgpuComputePassEncoderSetPipeline(cp->encoder, pl->pipeline);
}

// ─────────────────────────────────────────────────────────────────────────────
// Vertex / index binding
// ─────────────────────────────────────────────────────────────────────────────

void WebGpuGpuBackend::bindVertexBuffers(GpuRenderPassHandle pass, uint32_t firstSlot,
                                          const GpuBufferBinding* bindings, uint32_t count) {
    auto* rp = reinterpret_cast<WgpuRenderPass*>(pass);
    for (uint32_t i = 0; i < count; ++i) {
        auto* buf = reinterpret_cast<WgpuBuffer*>(bindings[i].buffer);
        wgpuRenderPassEncoderSetVertexBuffer(rp->encoder, firstSlot + i,
                                             buf->buffer, bindings[i].offset, buf->size);
    }
}

void WebGpuGpuBackend::bindIndexBuffer(GpuRenderPassHandle pass, GpuBufferBinding binding,
                                        bool use16BitIndices) {
    auto* rp  = reinterpret_cast<WgpuRenderPass*>(pass);
    auto* buf = reinterpret_cast<WgpuBuffer*>(binding.buffer);
    wgpuRenderPassEncoderSetIndexBuffer(rp->encoder, buf->buffer,
        use16BitIndices ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32,
        binding.offset, buf->size - binding.offset);
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler / texture binding helpers
// ─────────────────────────────────────────────────────────────────────────────

// Build and set a bind group for fragment sampler pairs (group 2).
void WebGpuGpuBackend::bindFragmentSamplers(GpuRenderPassHandle pass, uint32_t first,
                                             const GpuTextureSamplerBinding* bindings, uint32_t count) {
    auto* rp = reinterpret_cast<WgpuRenderPass*>(pass);
    if (!rp->currentPipeline || !rp->currentPipeline->bgLayouts[2]) return;

    // Fast path: single (texture, sampler) pair at slot 0 — covers ~all sprite draws.
    // Cached BGs are evicted on releaseTexture/releaseSampler so stale views are impossible.
    if (first == 0 && count == 1) {
        auto* tex = reinterpret_cast<WgpuTexture*>(bindings[0].texture);
        auto* smp = reinterpret_cast<WgpuSampler*>(bindings[0].sampler);
        WGPUTextureView v = tex ? tex->defaultView : nullptr;
        WGPUSampler     s = smp ? smp->sampler     : nullptr;
        SamplerBgKey key{ rp->currentPipeline->bgLayouts[2], v, s };
        auto it = m_samplerBgCache.find(key);
        WGPUBindGroup bg;
        if (it != m_samplerBgCache.end()) {
            bg = it->second;
        } else {
            WGPUBindGroupEntry e[2] = {};
            e[0].binding = 0; e[0].sampler     = s;
            e[1].binding = 1; e[1].textureView = v;
            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.layout     = rp->currentPipeline->bgLayouts[2];
            bgDesc.entryCount = 2;
            bgDesc.entries    = e;
            bg = wgpuDeviceCreateBindGroup(m_device, &bgDesc);
            m_samplerBgCache.emplace(key, bg);
        }
        wgpuRenderPassEncoderSetBindGroup(rp->encoder, 2, bg, 0, nullptr);
        return;
    }

    std::vector<WGPUBindGroupEntry> entries;
    entries.reserve(count * 2);
    for (uint32_t i = 0; i < count; ++i) {
        auto* tex = reinterpret_cast<WgpuTexture*>(bindings[i].texture);
        auto* smp = reinterpret_cast<WgpuSampler*>(bindings[i].sampler);
        uint32_t base = (first + i) * 2;

        WGPUBindGroupEntry smpEntry{};
        smpEntry.binding = base;
        smpEntry.sampler = smp ? smp->sampler : nullptr;
        entries.push_back(smpEntry);

        WGPUBindGroupEntry texEntry{};
        texEntry.binding     = base + 1;
        texEntry.textureView = tex ? tex->defaultView : nullptr;
        entries.push_back(texEntry);
    }

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout     = rp->currentPipeline->bgLayouts[2];
    bgDesc.entryCount = entries.size();
    bgDesc.entries    = entries.data();
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(m_device, &bgDesc);
    wgpuRenderPassEncoderSetBindGroup(rp->encoder, 2, bg, 0, nullptr);
    rp->cmdBuf->cleanup.tempBindGroups.push_back(bg);
}

void WebGpuGpuBackend::bindVertexSamplers(GpuRenderPassHandle pass, uint32_t first,
                                           const GpuTextureSamplerBinding* bindings, uint32_t count) {
    // Map to group 4 (vertex sampler+texture pairs) — same logic as fragment but group 4.
    auto* rp = reinterpret_cast<WgpuRenderPass*>(pass);
    if (!rp->currentPipeline || !rp->currentPipeline->bgLayouts[3]) return;

    (void)first; (void)bindings; (void)count;
    // Stub: vertex samplers are rare; add when needed.
}

void WebGpuGpuBackend::bindFragmentStorageTextures(GpuRenderPassHandle pass, uint32_t first,
                                                    const GpuTextureHandle* textures, uint32_t count) {
    auto* rp = reinterpret_cast<WgpuRenderPass*>(pass);
    if (!rp->currentPipeline || !rp->currentPipeline->bgLayouts[3]) return;

    std::vector<WGPUBindGroupEntry> entries(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* tex = reinterpret_cast<WgpuTexture*>(textures[i]);
        entries[i].binding     = first + i;
        entries[i].textureView = tex ? tex->defaultView : nullptr;
    }

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout     = rp->currentPipeline->bgLayouts[3];
    bgDesc.entryCount = entries.size();
    bgDesc.entries    = entries.data();
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(m_device, &bgDesc);
    wgpuRenderPassEncoderSetBindGroup(rp->encoder, 3, bg, 0, nullptr);
    rp->cmdBuf->cleanup.tempBindGroups.push_back(bg);
}

void WebGpuGpuBackend::bindVertexStorageBuffers(GpuRenderPassHandle pass, uint32_t first,
                                                 const GpuBufferHandle* buffers, uint32_t count) {
    auto* rp = reinterpret_cast<WgpuRenderPass*>(pass);
    if (!rp->currentPipeline || !rp->currentPipeline->bgLayouts[3]) return;

    std::vector<WGPUBindGroupEntry> entries(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* buf = reinterpret_cast<WgpuBuffer*>(buffers[first + i]);
        entries[i].binding = i;
        entries[i].buffer  = buf ? buf->buffer : nullptr;
        entries[i].offset  = 0;
        entries[i].size    = buf ? buf->size : 0;
    }

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout     = rp->currentPipeline->bgLayouts[3];
    bgDesc.entryCount = entries.size();
    bgDesc.entries    = entries.data();
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(m_device, &bgDesc);
    wgpuRenderPassEncoderSetBindGroup(rp->encoder, 3, bg, 0, nullptr);
    rp->cmdBuf->cleanup.tempBindGroups.push_back(bg);
}

void WebGpuGpuBackend::bindComputeSamplers(GpuComputePassHandle pass, uint32_t first,
                                            const GpuTextureSamplerBinding* bindings, uint32_t count) {
    auto* cp = reinterpret_cast<WgpuComputePass*>(pass);
    // Samplers live in group 1 (SDL set0 -> group 1 via the compute @group remap), with the
    // sampler+texture BGL built in createComputePipeline. (Group 3 is unused for compute.)
    if (!cp->currentPipeline || !cp->currentPipeline->bgLayouts[1]) return;

    std::vector<WGPUBindGroupEntry> entries;
    entries.reserve(count * 2);
    for (uint32_t i = 0; i < count; ++i) {
        auto* tex = reinterpret_cast<WgpuTexture*>(bindings[i].texture);
        auto* smp = reinterpret_cast<WgpuSampler*>(bindings[i].sampler);
        uint32_t base = (first + i) * 2;
        WGPUBindGroupEntry te{}; te.binding = base;     te.textureView = tex ? tex->defaultView : nullptr;
        WGPUBindGroupEntry se{}; se.binding = base + 1; se.sampler     = smp ? smp->sampler     : nullptr;
        entries.push_back(te); entries.push_back(se);
    }

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout     = cp->currentPipeline->bgLayouts[1];
    bgDesc.entryCount = entries.size();
    bgDesc.entries    = entries.data();
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(m_device, &bgDesc);
    wgpuComputePassEncoderSetBindGroup(cp->encoder, 1, bg, 0, nullptr);
    cp->cmdBuf->cleanup.tempBindGroups.push_back(bg);
}

void WebGpuGpuBackend::bindComputeStorageTextures(GpuComputePassHandle pass, uint32_t first,
                                                   const GpuTextureHandle* textures, uint32_t count) {
    auto* cp = reinterpret_cast<WgpuComputePass*>(pass);
    // RO storage textures occupy group 1 (same slot RO storage buffers would use for buf-only pipelines).
    if (!cp->currentPipeline || !cp->currentPipeline->bgLayouts[1]) return;

    std::vector<WGPUBindGroupEntry> entries(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* tex = reinterpret_cast<WgpuTexture*>(textures[i]);
        entries[i].binding     = first + i;
        entries[i].textureView = tex ? tex->defaultView : nullptr;
    }
    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout = cp->currentPipeline->bgLayouts[1];
    bgDesc.entryCount = entries.size(); bgDesc.entries = entries.data();
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(m_device, &bgDesc);
    wgpuComputePassEncoderSetBindGroup(cp->encoder, 1, bg, 0, nullptr);
    cp->cmdBuf->cleanup.tempBindGroups.push_back(bg);
}

void WebGpuGpuBackend::bindComputeStorageBuffers(GpuComputePassHandle pass, uint32_t first,
                                                  const GpuBufferHandle* buffers, uint32_t count) {
    auto* cp = reinterpret_cast<WgpuComputePass*>(pass);
    if (!cp->currentPipeline || !cp->currentPipeline->bgLayouts[1]) return;

    std::vector<WGPUBindGroupEntry> entries(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* buf = reinterpret_cast<WgpuBuffer*>(buffers[i]);
        entries[i].binding = first + i;
        entries[i].buffer  = buf->buffer;
        entries[i].offset  = 0;
        entries[i].size    = buf->size;
    }
    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout = cp->currentPipeline->bgLayouts[1];
    bgDesc.entryCount = entries.size(); bgDesc.entries = entries.data();
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(m_device, &bgDesc);
    wgpuComputePassEncoderSetBindGroup(cp->encoder, 1, bg, 0, nullptr);
    cp->cmdBuf->cleanup.tempBindGroups.push_back(bg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Push-constant emulation — cache data; flushed on draw/dispatch
// ─────────────────────────────────────────────────────────────────────────────

void WebGpuGpuBackend::pushVertexUniformData(GpuCmdBufferHandle cmd, uint32_t slot,
                                              const void* data, uint32_t size) {
    auto* cb = reinterpret_cast<WgpuCmdBuffer*>(cmd);
    cb->vertexUniforms.set(slot, data, size);
}

void WebGpuGpuBackend::pushFragmentUniformData(GpuCmdBufferHandle cmd, uint32_t slot,
                                                const void* data, uint32_t size) {
    auto* cb = reinterpret_cast<WgpuCmdBuffer*>(cmd);
    cb->fragmentUniforms.set(slot, data, size);
}

void WebGpuGpuBackend::pushComputeUniformData(GpuCmdBufferHandle cmd, uint32_t slot,
                                               const void* data, uint32_t size) {
    auto* cb = reinterpret_cast<WgpuCmdBuffer*>(cmd);
    cb->computeUniforms.set(slot, data, size);
}

// Creates a temporary uniform WGPUBuffer from cached data and builds a bind group.
WGPUBindGroup WebGpuGpuBackend::_makeUniformBindGroup(WGPUBindGroupLayout layout,
                                                        const WgpuUniformCache& cache,
                                                        uint32_t slotCount,
                                                        std::vector<std::pair<WGPUBuffer, uint32_t>>& uniformCleanup) {
    if (!layout || slotCount == 0) return nullptr;

    std::vector<WGPUBindGroupEntry> entries(slotCount);
    for (uint32_t i = 0; i < slotCount; ++i) {
        const auto& slot = cache.slots[i];
        WGPUBuffer buf = nullptr;

        if (!slot.data.empty()) {
            // Align to 16 bytes (WebGPU minimum uniform buffer binding size)
            uint32_t alignedSz = (static_cast<uint32_t>(slot.data.size()) + 15u) & ~15u;
            buf = _acquireUniformBuffer(alignedSz);
            wgpuQueueWriteBuffer(m_queue, buf, 0, slot.data.data(), slot.data.size());
            uniformCleanup.push_back({buf, alignedSz});
        } else {
            buf = m_zeroBuffer;
        }

        entries[i].binding = i;
        entries[i].buffer  = buf;
        entries[i].offset  = 0;
        entries[i].size    = buf == m_zeroBuffer ? kZeroBufSize
                           : (static_cast<uint32_t>(slot.data.size() + 15u) & ~15u);
    }

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout     = layout;
    bgDesc.entryCount = slotCount;
    bgDesc.entries    = entries.data();
    return wgpuDeviceCreateBindGroup(m_device, &bgDesc);
}

void WebGpuGpuBackend::_flushVertexUniforms(WgpuRenderPass* rp) {
    if (!rp->currentPipeline) return;
    const uint32_t count = rp->currentPipeline->vertexUniformCount;
    if (count == 0) return;

    WGPUBindGroup bg = _makeUniformBindGroup(
        rp->currentPipeline->bgLayouts[0],
        rp->cmdBuf->vertexUniforms, count,
        rp->cmdBuf->cleanup.tempUniformBuffers);
    if (!bg) return;
    wgpuRenderPassEncoderSetBindGroup(rp->encoder, 0, bg, 0, nullptr);
    rp->cmdBuf->cleanup.tempBindGroups.push_back(bg);
}

void WebGpuGpuBackend::_flushFragmentUniforms(WgpuRenderPass* rp) {
    if (!rp->currentPipeline) return;
    const uint32_t count = rp->currentPipeline->fragmentUniformCount;
    if (count == 0) return;

    WGPUBindGroup bg = _makeUniformBindGroup(
        rp->currentPipeline->bgLayouts[1],
        rp->cmdBuf->fragmentUniforms, count,
        rp->cmdBuf->cleanup.tempUniformBuffers);
    if (!bg) return;
    wgpuRenderPassEncoderSetBindGroup(rp->encoder, 1, bg, 0, nullptr);
    rp->cmdBuf->cleanup.tempBindGroups.push_back(bg);
}

void WebGpuGpuBackend::_flushComputeUniforms(WgpuComputePass* cp) {
    if (!cp->currentPipeline) return;
    const uint32_t count = cp->currentPipeline->uniformCount;
    if (count == 0) return;

    WGPUBindGroup bg = _makeUniformBindGroup(
        cp->currentPipeline->bgLayouts[0],
        cp->cmdBuf->computeUniforms, count,
        cp->cmdBuf->cleanup.tempUniformBuffers);
    if (!bg) return;
    wgpuComputePassEncoderSetBindGroup(cp->encoder, 0, bg, 0, nullptr);
    cp->cmdBuf->cleanup.tempBindGroups.push_back(bg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw calls
// ─────────────────────────────────────────────────────────────────────────────

void WebGpuGpuBackend::drawPrimitives(GpuRenderPassHandle pass, uint32_t vertexCount,
                                       uint32_t instanceCount, uint32_t firstVertex,
                                       uint32_t firstInstance) {
    auto* rp = reinterpret_cast<WgpuRenderPass*>(pass);
    _flushVertexUniforms(rp);
    _flushFragmentUniforms(rp);
    wgpuRenderPassEncoderDraw(rp->encoder, vertexCount, instanceCount, firstVertex, firstInstance);
}

void WebGpuGpuBackend::drawIndexedPrimitives(GpuRenderPassHandle pass, uint32_t indexCount,
                                              uint32_t instanceCount, uint32_t firstIndex,
                                              int32_t vertexOffset, uint32_t firstInstance) {
    auto* rp = reinterpret_cast<WgpuRenderPass*>(pass);
    _flushVertexUniforms(rp);
    _flushFragmentUniforms(rp);
    wgpuRenderPassEncoderDrawIndexed(rp->encoder, indexCount, instanceCount,
                                     firstIndex, vertexOffset, firstInstance);
}

void WebGpuGpuBackend::dispatchCompute(GpuComputePassHandle pass,
                                        uint32_t gX, uint32_t gY, uint32_t gZ) {
    auto* cp = reinterpret_cast<WgpuComputePass*>(pass);
    _flushComputeUniforms(cp);

    // Bind pending RW resources to group 2. Pipeline is either tex-only or buf-only on group 2;
    // pick whichever pending list matches the layout.
    if (cp->currentPipeline && cp->currentPipeline->bgLayouts[2]) {
        const bool wantTex = cp->currentPipeline->rwStorageTexCount > 0;
        std::vector<WGPUBindGroupEntry> entries;
        if (wantTex && !cp->pendingRWTextures.empty()) {
            entries.resize(cp->pendingRWTextures.size());
            for (uint32_t i = 0; i < entries.size(); ++i) {
                entries[i].binding     = i;
                entries[i].textureView = cp->pendingRWTextures[i].view;
            }
        } else if (!wantTex && !cp->pendingRWBuffers.empty()) {
            entries.resize(cp->pendingRWBuffers.size());
            for (uint32_t i = 0; i < entries.size(); ++i) {
                entries[i].binding = i;
                entries[i].buffer  = cp->pendingRWBuffers[i].buf;
                entries[i].offset  = 0;
                entries[i].size    = cp->pendingRWBuffers[i].size;
            }
        }
        if (!entries.empty()) {
            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.layout     = cp->currentPipeline->bgLayouts[2];
            bgDesc.entryCount = static_cast<uint32_t>(entries.size());
            bgDesc.entries    = entries.data();
            WGPUBindGroup bg = wgpuDeviceCreateBindGroup(cp->device, &bgDesc);
            wgpuComputePassEncoderSetBindGroup(cp->encoder, 2, bg, 0, nullptr);
            cp->cmdBuf->cleanup.tempBindGroups.push_back(bg);
        }
        cp->pendingRWBuffers.clear();
        cp->pendingRWTextures.clear();
    }

    wgpuComputePassEncoderDispatchWorkgroups(cp->encoder, gX, gY, gZ);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scissor / viewport
// ─────────────────────────────────────────────────────────────────────────────

void WebGpuGpuBackend::setScissor(GpuRenderPassHandle pass,
                                   int32_t x, int32_t y, uint32_t w, uint32_t h) {
    auto* rp = reinterpret_cast<WgpuRenderPass*>(pass);
    wgpuRenderPassEncoderSetScissorRect(rp->encoder,
        static_cast<uint32_t>(x), static_cast<uint32_t>(y), w, h);
}

void WebGpuGpuBackend::setViewport(GpuRenderPassHandle pass,
                                    float x, float y, float w, float h,
                                    float minDepth, float maxDepth) {
    auto* rp = reinterpret_cast<WgpuRenderPass*>(pass);
    wgpuRenderPassEncoderSetViewport(rp->encoder, x, y, w, h, minDepth, maxDepth);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bind group layout helpers
// ─────────────────────────────────────────────────────────────────────────────

WGPUBindGroupLayout WebGpuGpuBackend::_makeEmptyBGL() {
    WGPUBindGroupLayoutDescriptor desc{};
    desc.entryCount = 0;
    desc.entries    = nullptr;
    return wgpuDeviceCreateBindGroupLayout(m_device, &desc);
}

WGPUBindGroupLayout WebGpuGpuBackend::_makeUniformBGL(uint32_t count, WGPUShaderStageFlags vis) {
    if (count == 0) return nullptr;
    std::vector<WGPUBindGroupLayoutEntry> entries(count);
    for (uint32_t i = 0; i < count; ++i) {
        entries[i] = {};
        entries[i].binding              = i;
        entries[i].visibility           = vis;
        entries[i].buffer.type          = WGPUBufferBindingType_Uniform;
        entries[i].buffer.minBindingSize = 0;
    }
    WGPUBindGroupLayoutDescriptor desc{};
    desc.entryCount = count;
    desc.entries    = entries.data();
    return wgpuDeviceCreateBindGroupLayout(m_device, &desc);
}

WGPUBindGroupLayout WebGpuGpuBackend::_makeSamplerBGL(uint32_t pairCount, WGPUShaderStageFlags vis,
                                                      bool texFirst, uint32_t cubeMask) {
    if (pairCount == 0) return nullptr;
    // If Float32Filterable is not enabled, declare textures as UnfilterableFloat so rgba32f
    // textures bind without validation errors. Sampler is downgraded to NonFiltering to match.
    const WGPUTextureSampleType   texSampleType  = m_float32Filterable
        ? WGPUTextureSampleType_Float : WGPUTextureSampleType_UnfilterableFloat;
    const WGPUSamplerBindingType  samplerType    = m_float32Filterable
        ? WGPUSamplerBindingType_Filtering : WGPUSamplerBindingType_NonFiltering;
    std::vector<WGPUBindGroupLayoutEntry> entries(pairCount * 2);
    for (uint32_t i = 0; i < pairCount; ++i) {
        // Pair layout: graphics uses even=sampler/odd=texture (post pair-swap); compute keeps
        // Tint's native even=texture/odd=sampler (texFirst). Pick the bindings accordingly.
        uint32_t texBinding = texFirst ? (i * 2)     : (i * 2 + 1);
        uint32_t smpBinding = texFirst ? (i * 2 + 1) : (i * 2);

        entries[i * 2] = {};
        entries[i * 2].binding         = smpBinding;
        entries[i * 2].visibility      = vis;
        entries[i * 2].sampler.type    = samplerType;

        entries[i * 2 + 1] = {};
        entries[i * 2 + 1].binding               = texBinding;
        entries[i * 2 + 1].visibility            = vis;
        entries[i * 2 + 1].texture.sampleType    = texSampleType;
        entries[i * 2 + 1].texture.viewDimension = (cubeMask & (1u << i))
            ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_2D;
    }
    WGPUBindGroupLayoutDescriptor desc{};
    desc.entryCount = pairCount * 2;
    desc.entries    = entries.data();
    return wgpuDeviceCreateBindGroupLayout(m_device, &desc);
}

WGPUBindGroupLayout WebGpuGpuBackend::_makeStorageTexBGL(uint32_t count, WGPUShaderStageFlags vis,
                                                          WGPUStorageTextureAccess access,
                                                          const GpuTextureFormat* perBindingFormats,
                                                          const bool* perBindingWriteOnly) {
    if (count == 0) return nullptr;
    std::vector<WGPUBindGroupLayoutEntry> entries(count);
    for (uint32_t i = 0; i < count; ++i) {
        entries[i] = {};
        entries[i].binding                         = i;
        entries[i].visibility                      = vis;
        entries[i].storageTexture.access           =
            (perBindingWriteOnly && perBindingWriteOnly[i]) ? WGPUStorageTextureAccess_WriteOnly : access;
        entries[i].storageTexture.format           = perBindingFormats
            ? toWGPU(perBindingFormats[i])
            : WGPUTextureFormat_RGBA8Unorm;
        entries[i].storageTexture.viewDimension    = WGPUTextureViewDimension_2D;
    }
    WGPUBindGroupLayoutDescriptor desc{};
    desc.entryCount = count;
    desc.entries    = entries.data();
    return wgpuDeviceCreateBindGroupLayout(m_device, &desc);
}

WGPUBindGroupLayout WebGpuGpuBackend::_makeStorageBufBGL(uint32_t count, WGPUShaderStageFlags vis,
                                                          WGPUBufferBindingType type) {
    if (count == 0) return nullptr;
    std::vector<WGPUBindGroupLayoutEntry> entries(count);
    for (uint32_t i = 0; i < count; ++i) {
        entries[i] = {};
        entries[i].binding     = i;
        entries[i].visibility  = vis;
        entries[i].buffer.type = type;
    }
    WGPUBindGroupLayoutDescriptor desc{};
    desc.entryCount = count;
    desc.entries    = entries.data();
    return wgpuDeviceCreateBindGroupLayout(m_device, &desc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource creation
// ─────────────────────────────────────────────────────────────────────────────

GpuTextureHandle WebGpuGpuBackend::createTexture(const GpuTextureCreateInfo& info) {
    auto* t = new WgpuTexture();
    t->format = toWGPU(info.format);
    t->width  = info.width;
    t->height = info.height;

    const bool isCube = (info.type == GpuTextureType::TexCube);
    t->layerCount = isCube ? 6u : info.depthOrLayers;

    WGPUTextureDescriptor desc{};
    desc.usage         = toWGPUUsage(info.usage);
    desc.dimension     = WGPUTextureDimension_2D;   // cube is a 2D texture with 6 array layers
    desc.size          = { info.width, info.height, isCube ? 6u : info.depthOrLayers };
    desc.format        = t->format;
    desc.mipLevelCount = info.numLevels;
    desc.sampleCount   = static_cast<uint32_t>(info.sampleCount);
    t->texture = wgpuDeviceCreateTexture(m_device, &desc);

    // Sampling view: cube dimension for cube maps (so texture_cube<f32> binds), else 2D.
    WGPUTextureViewDescriptor vd{};
    vd.format          = t->format;
    vd.dimension       = isCube ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_2D;
    vd.mipLevelCount   = info.numLevels;
    vd.arrayLayerCount = t->layerCount;
    t->defaultView = wgpuTextureCreateView(t->texture, &vd);
    if (!t->defaultView) {
        LOG_WARNING("createTexture: defaultView is null (fmt={}, usage={}, size={}x{}, samples={}, tex_ptr={:#x})",
                    (int)t->format, (uint32_t)desc.usage, info.width, info.height,
                    (int)info.sampleCount, (uintptr_t)t);
    }

    // Per-layer 2D render views: cube/array color targets are rendered one face/layer at a time
    // (GpuColorTargetInfo.layer selects which). Only needed when the texture can be a render target.
    if (t->layerCount > 1 && (info.usage & GpuTextureUsage::ColorTarget)) {
        const uint32_t faces = (t->layerCount < 6u) ? t->layerCount : 6u;
        for (uint32_t f = 0; f < faces; ++f) {
            WGPUTextureViewDescriptor fv{};
            fv.format          = t->format;
            fv.dimension       = WGPUTextureViewDimension_2D;
            fv.baseArrayLayer  = f;
            fv.arrayLayerCount = 1;
            fv.mipLevelCount   = 1;
            t->faceViews[f] = wgpuTextureCreateView(t->texture, &fv);
        }
    }

    return reinterpret_cast<GpuTextureHandle>(t);
}

GpuBufferHandle WebGpuGpuBackend::createBuffer(const GpuBufferCreateInfo& info) {
    auto* b = new WgpuBuffer();
    b->size = info.size;

    WGPUBufferDescriptor desc{};
    // WebGPU requires buffer sizes (and writeBuffer sizes/offsets) to be 4-byte multiples; SDL_GPU's
    // native backends don't, so unaligned uploads (e.g. an odd uint16 index count) crash only here.
    desc.size  = (info.size + 3u) & ~3u;
    desc.usage = toWGPUUsage(info.usage) | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    b->buffer = wgpuDeviceCreateBuffer(m_device, &desc);

    return reinterpret_cast<GpuBufferHandle>(b);
}

GpuTransferBufferHandle WebGpuGpuBackend::createTransferBuffer(const GpuTransferBufferCreateInfo& info) {
    auto* tb = new WgpuTransferBuffer();
    tb->size       = info.size;
    tb->isDownload = (info.usage == GpuTransferUsage::Download);

    if (tb->isDownload) {
        // GPU-side buffer for readback, mapped after copy
        WGPUBufferDescriptor desc{};
        desc.size  = (info.size + 3u) & ~3u;
        desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        tb->downloadBuffer = wgpuDeviceCreateBuffer(m_device, &desc);
    } else {
        // Pad to a 4-byte multiple so a padded writeBuffer can't read past the staging data.
        tb->stagingData.resize((info.size + 3u) & ~3u);
    }

    return reinterpret_cast<GpuTransferBufferHandle>(tb);
}

GpuSamplerHandle WebGpuGpuBackend::createSampler(const GpuSamplerCreateInfo& info) {
    auto* s = new WgpuSampler();

    WGPUSamplerDescriptor desc{};
    desc.addressModeU  = toWGPU(info.addressU);
    desc.addressModeV  = toWGPU(info.addressV);
    desc.addressModeW  = toWGPU(info.addressW);
    desc.magFilter     = toWGPU(info.magFilter);
    desc.minFilter     = toWGPU(info.minFilter);
    desc.mipmapFilter  = toWGPU(info.mipFilter) == WGPUFilterMode_Linear
                         ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
    desc.lodMinClamp   = info.minLod;
    desc.lodMaxClamp   = info.maxLod;
    desc.maxAnisotropy = static_cast<uint16_t>(info.maxAniso > 1.0f ? info.maxAniso : 1);
    s->sampler = wgpuDeviceCreateSampler(m_device, &desc);

    return reinterpret_cast<GpuSamplerHandle>(s);
}

GpuShaderHandle WebGpuGpuBackend::createShader(const GpuShaderCreateInfo& info) {
    auto* sh = new WgpuShader();
    sh->entrypoint          = info.entrypoint ? info.entrypoint : "main";
    sh->stage               = info.stage;
    sh->samplerCount        = info.samplerCount;
    sh->uniformBufferCount  = info.uniformBufferCount;
    sh->storageBufferCount  = info.storageBufferCount;
    sh->storageTextureCount = info.storageTextureCount;
    sh->samplerCubeMask     = info.samplerCubeMask;

    // Code must be null-terminated WGSL text.
    WGPUShaderSourceWGSL wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code        = wgpuStr(reinterpret_cast<const char*>(info.code));

    WGPUShaderModuleDescriptor shDesc{};
    shDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc.chain);
    sh->module = wgpuDeviceCreateShaderModule(m_device, &shDesc);

    return reinterpret_cast<GpuShaderHandle>(sh);
}

GpuGraphicsPipelineHandle WebGpuGpuBackend::createGraphicsPipeline(const GpuGraphicsPipelineCreateInfo& info) {
    auto* sh_v = reinterpret_cast<WgpuShader*>(info.vertexShader);
    auto* sh_f = reinterpret_cast<WgpuShader*>(info.fragmentShader);
    auto* pl   = new WgpuGraphicsPipeline();

    pl->vertexUniformCount      = sh_v ? sh_v->uniformBufferCount  : 0;
    pl->fragmentUniformCount    = sh_f ? sh_f->uniformBufferCount  : 0;
    pl->fragmentSamplerCount    = sh_f ? sh_f->samplerCount        : 0;
    pl->fragmentSamplerCubeMask = sh_f ? sh_f->samplerCubeMask     : 0;
    pl->fragmentStorageTexCount = sh_f ? sh_f->storageTextureCount : 0;
    pl->vertexStorageBufCount   = info.vertexStorageBufferCount;

    // Build bind group layouts
    pl->bgLayouts[0] = _makeUniformBGL(pl->vertexUniformCount,   WGPUShaderStage_Vertex);
    pl->bgLayouts[1] = _makeUniformBGL(pl->fragmentUniformCount, WGPUShaderStage_Fragment);
    pl->bgLayouts[2] = _makeSamplerBGL(pl->fragmentSamplerCount, WGPUShaderStage_Fragment,
                                       /*texFirst=*/false, pl->fragmentSamplerCubeMask);
    if (pl->vertexStorageBufCount > 0) {
        pl->bgLayouts[3] = _makeStorageBufBGL(pl->vertexStorageBufCount, WGPUShaderStage_Vertex,
                                               WGPUBufferBindingType_ReadOnlyStorage);
    } else {
        pl->bgLayouts[3] = _makeStorageTexBGL(pl->fragmentStorageTexCount, WGPUShaderStage_Fragment,
                                              WGPUStorageTextureAccess_ReadWrite);
    }

    // Pipeline layout — preserve group indices by padding nulls with empty BGLs.
    // Null entries that trail after the last non-null are omitted entirely.
    int lastGroup = -1;
    for (int i = 3; i >= 0; --i) { if (pl->bgLayouts[i]) { lastGroup = i; break; } }
    if (lastGroup >= 0) pl->emptyBGL = _makeEmptyBGL();
    std::vector<WGPUBindGroupLayout> validLayouts;
    for (int i = 0; i <= lastGroup; ++i)
        validLayouts.push_back(pl->bgLayouts[i] ? pl->bgLayouts[i] : pl->emptyBGL);
    WGPUPipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayoutCount = validLayouts.size();
    plDesc.bindGroupLayouts     = validLayouts.data();
    pl->layout = wgpuDeviceCreatePipelineLayout(m_device, &plDesc);
    // Build persistent empty bind groups for any null slot before lastGroup. Firefox requires
    // every slot in the pipeline layout to have a bind group set at draw time, even empties.
    if (pl->emptyBGL) {
        WGPUBindGroupDescriptor ebgDesc{};
        ebgDesc.layout     = pl->emptyBGL;
        ebgDesc.entryCount = 0;
        ebgDesc.entries    = nullptr;
        for (int i = 0; i <= lastGroup; ++i) {
            if (!pl->bgLayouts[i]) {
                pl->emptyBG[i] = wgpuDeviceCreateBindGroup(m_device, &ebgDesc);
            }
        }
    }

    // Vertex buffer layouts
    std::vector<WGPUVertexAttribute> wattrs(info.attributeCount);
    for (uint32_t i = 0; i < info.attributeCount; ++i) {
        wattrs[i].format         = toWGPU(info.attributes[i].format);
        wattrs[i].offset         = info.attributes[i].offset;
        wattrs[i].shaderLocation = info.attributes[i].location;
    }

    std::vector<WGPUVertexBufferLayout> vbufs(info.bindingCount);
    for (uint32_t i = 0; i < info.bindingCount; ++i) {
        vbufs[i].arrayStride    = info.bindings[i].stride;
        vbufs[i].stepMode       = info.bindings[i].instanceStepping
                                  ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
        vbufs[i].attributeCount = 0;
        vbufs[i].attributes     = nullptr;
        // Assign attributes to their binding slots
        for (uint32_t a = 0; a < info.attributeCount; ++a) {
            if (info.attributes[a].binding == info.bindings[i].binding) {
                vbufs[i].attributeCount++;
            }
        }
    }
    // Re-assign attribute pointers correctly
    uint32_t attrBase = 0;
    for (uint32_t i = 0; i < info.bindingCount; ++i) {
        vbufs[i].attributes = wattrs.data() + attrBase;
        attrBase += vbufs[i].attributeCount;
    }

    // Color targets. colorTargetCount == 0 → the single colorTargetFormat/blend; otherwise MRT.
    uint32_t numColor = info.colorTargetCount ? info.colorTargetCount : 1u;
    std::vector<WGPUBlendState>       blends(numColor);
    std::vector<WGPUColorTargetState> colorTargets(numColor);
    for (uint32_t i = 0; i < numColor; ++i) {
        const GpuColorTargetBlendState &b = info.colorTargetCount ? info.colorTargetBlends[i] : info.blend;
        blends[i].color = { .operation = toWGPU(b.colorOp),
                            .srcFactor = toWGPU(b.srcColorFactor),
                            .dstFactor = toWGPU(b.dstColorFactor) };
        blends[i].alpha = { .operation = toWGPU(b.alphaOp),
                            .srcFactor = toWGPU(b.srcAlphaFactor),
                            .dstFactor = toWGPU(b.dstAlphaFactor) };
        colorTargets[i].format    = toWGPU(info.colorTargetCount ? info.colorTargetFormats[i] : info.colorTargetFormat);
        colorTargets[i].blend     = b.blendEnabled ? &blends[i] : nullptr;
        colorTargets[i].writeMask = WGPUColorWriteMask_All;
    }

    WGPUFragmentState fragState{};
    fragState.module      = sh_f ? sh_f->module : nullptr;
    fragState.entryPoint  = wgpuStr(sh_f ? sh_f->entrypoint.c_str() : "main");
    fragState.targetCount = numColor;
    fragState.targets     = colorTargets.data();

    WGPURenderPipelineDescriptor rpDesc{};
    rpDesc.layout = pl->layout;

    rpDesc.vertex.module      = sh_v ? sh_v->module : nullptr;
    rpDesc.vertex.entryPoint  = wgpuStr(sh_v ? sh_v->entrypoint.c_str() : "main");
    rpDesc.vertex.bufferCount = static_cast<uint32_t>(vbufs.size());
    rpDesc.vertex.buffers     = vbufs.empty() ? nullptr : vbufs.data();

    rpDesc.primitive.topology  = WGPUPrimitiveTopology_TriangleList;
    rpDesc.primitive.cullMode  = WGPUCullMode_None;
    rpDesc.primitive.frontFace = WGPUFrontFace_CCW;

    rpDesc.multisample.count = static_cast<uint32_t>(info.sampleCount);
    rpDesc.multisample.mask  = 0xFFFFFFFF;

    WGPUDepthStencilState ds{};
    if (info.hasDepthTarget) {
        ds.format              = depthFormatToWGPU(info.depthTargetFormat);
        ds.depthWriteEnabled   = WGPUOptionalBool_True;
        ds.depthCompare        = WGPUCompareFunction_Less;
        ds.stencilFront.compare = WGPUCompareFunction_Always;
        ds.stencilBack.compare  = WGPUCompareFunction_Always;
        rpDesc.depthStencil    = &ds;
    }

    rpDesc.fragment = &fragState;
    pl->pipeline = wgpuDeviceCreateRenderPipeline(m_device, &rpDesc);
    if (!pl->pipeline) {
        LOG_WARNING("wgpuDeviceCreateRenderPipeline returned null (check WGSL for errors)");
    }

    return reinterpret_cast<GpuGraphicsPipelineHandle>(pl);
}

GpuComputePipelineHandle WebGpuGpuBackend::createComputePipeline(const GpuComputePipelineCreateInfo& info) {
    auto* pl = new WgpuComputePipelineData();
    pl->uniformCount         = info.uniformBufferCount;
    pl->roStorageBufferCount = info.readonlyStorageBufferCount;
    pl->rwStorageBufferCount = info.readwriteStorageBufferCount;
    pl->samplerCount         = info.samplerCount;
    pl->roStorageTexCount    = info.readonlyStorageTextureCount;
    pl->rwStorageTexCount    = info.readwriteStorageTextureCount;

    // Compute bind groups: 0 = uniforms (SDL set2), 1 = the "read" group (SDL set0:
    // sampled textures+samplers OR RO storage texture OR RO storage buffer), 2 = RW (SDL set1:
    // buffer OR storage texture). A single shader's set0 is one resource kind, so group 1's BGL
    // type is chosen per pipeline: samplers take priority (matches probe_gi.comp's sampler2D
    // data textures), then RO storage texture, then RO storage buffer.
    pl->bgLayouts[0] = _makeUniformBGL(pl->uniformCount, WGPUShaderStage_Compute);
    if (pl->samplerCount > 0) {
        // texFirst=true: compute WGSL keeps Tint's even=texture/odd=sampler binding order.
        pl->bgLayouts[1] = _makeSamplerBGL(pl->samplerCount, WGPUShaderStage_Compute, /*texFirst=*/true);
    } else if (pl->roStorageTexCount > 0) {
        pl->bgLayouts[1] = _makeStorageTexBGL(pl->roStorageTexCount, WGPUShaderStage_Compute,
                                              WGPUStorageTextureAccess_ReadOnly,
                                              info.readonlyStorageTextureFormats);
    } else {
        pl->bgLayouts[1] = _makeStorageBufBGL(pl->roStorageBufferCount, WGPUShaderStage_Compute,
                                              WGPUBufferBindingType_ReadOnlyStorage);
    }
    if (pl->rwStorageTexCount > 0) {
        pl->bgLayouts[2] = _makeStorageTexBGL(pl->rwStorageTexCount, WGPUShaderStage_Compute,
                                              WGPUStorageTextureAccess_ReadWrite,
                                              info.readwriteStorageTextureFormats,
                                              info.readwriteStorageTextureWriteOnly);
    } else {
        pl->bgLayouts[2] = _makeStorageBufBGL(pl->rwStorageBufferCount, WGPUShaderStage_Compute,
                                              WGPUBufferBindingType_Storage);
    }
    pl->bgLayouts[3] = nullptr;
    pl->bgLayouts[4] = nullptr;

    int lastGroupC = -1;
    for (int i = 2; i >= 0; --i) { if (pl->bgLayouts[i]) { lastGroupC = i; break; } }
    WGPUBindGroupLayout emptyBGLC = (lastGroupC >= 0) ? _makeEmptyBGL() : nullptr;
    std::vector<WGPUBindGroupLayout> validLayouts;
    for (int i = 0; i <= lastGroupC; ++i)
        validLayouts.push_back(pl->bgLayouts[i] ? pl->bgLayouts[i] : emptyBGLC);
    WGPUPipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayoutCount = validLayouts.size();
    plDesc.bindGroupLayouts     = validLayouts.data();
    pl->layout = wgpuDeviceCreatePipelineLayout(m_device, &plDesc);
    if (emptyBGLC) wgpuBindGroupLayoutRelease(emptyBGLC);

    // Create shader module and pipeline inside a validation error scope so we can
    // detect failures synchronously and return 0 instead of an "error object" handle.
    wgpuDevicePushErrorScope(m_device, WGPUErrorFilter_Validation);

    WgpuShader tempShader{};
    {
        WGPUShaderSourceWGSL wgslDesc{};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code        = wgpuStr(reinterpret_cast<const char*>(info.code));
        WGPUShaderModuleDescriptor shDesc{};
        shDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc.chain);
        tempShader.module = wgpuDeviceCreateShaderModule(m_device, &shDesc);
    }
    tempShader.entrypoint = info.entrypoint ? info.entrypoint : "main";

    WGPUComputePipelineDescriptor cpDesc{};
    cpDesc.layout              = pl->layout;
    cpDesc.compute.module      = tempShader.module;
    cpDesc.compute.entryPoint  = wgpuStr(tempShader.entrypoint.c_str());
    pl->pipeline = wgpuDeviceCreateComputePipeline(m_device, &cpDesc);

    wgpuShaderModuleRelease(tempShader.module);

    // Pop error scope and check for compilation failure.
    struct ErrData { bool done = false; bool error = false; std::string msg; };
    ErrData errData;
    WGPUPopErrorScopeCallbackInfo errCBI{};
    errCBI.mode     = WGPUCallbackMode_AllowSpontaneous;
    errCBI.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type, WGPUStringView message, void* ud1, void*) {
        auto* d    = static_cast<ErrData*>(ud1);
        d->error   = (type != WGPUErrorType_NoError);
        d->msg     = (message.data && message.length > 0)
                       ? std::string(message.data, message.length) : std::string();
        d->done    = true;
    };
    errCBI.userdata1 = &errData;
    wgpuDevicePopErrorScope(m_device, errCBI);
    pollUntil(errData.done);

    if (errData.error) {
        LOG_WARNING("Compute pipeline validation error: {}", errData.msg.empty() ? "(no message)" : errData.msg);
        if (pl->pipeline) wgpuComputePipelineRelease(pl->pipeline);
        if (pl->layout)   wgpuPipelineLayoutRelease(pl->layout);
        for (auto& bgl : pl->bgLayouts) { if (bgl) wgpuBindGroupLayoutRelease(bgl); }
        delete pl;
        return 0;
    }

    return reinterpret_cast<GpuComputePipelineHandle>(pl);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource release
// ─────────────────────────────────────────────────────────────────────────────

void WebGpuGpuBackend::releaseTexture(GpuTextureHandle handle) {
    if (!handle) return;
    auto* t = reinterpret_cast<WgpuTexture*>(handle);
    if (t->defaultView) _evictSamplerBgsByView(t->defaultView);
    if (t->defaultView) wgpuTextureViewRelease(t->defaultView);
    for (auto& fv : t->faceViews) if (fv) wgpuTextureViewRelease(fv);
    if (t->texture)     wgpuTextureRelease(t->texture);
    delete t;
}

void WebGpuGpuBackend::releaseBuffer(GpuBufferHandle handle) {
    if (!handle) return;
    auto* b = reinterpret_cast<WgpuBuffer*>(handle);
    if (b->buffer) wgpuBufferRelease(b->buffer);
    delete b;
}

void WebGpuGpuBackend::releaseTransferBuffer(GpuTransferBufferHandle handle) {
    if (!handle) return;
    auto* tb = reinterpret_cast<WgpuTransferBuffer*>(handle);
    if (tb->downloadBuffer) wgpuBufferRelease(tb->downloadBuffer);
    delete tb;
}

void WebGpuGpuBackend::releaseSampler(GpuSamplerHandle handle) {
    if (!handle) return;
    auto* s = reinterpret_cast<WgpuSampler*>(handle);
    if (s->sampler) _evictSamplerBgsBySampler(s->sampler);
    if (s->sampler) wgpuSamplerRelease(s->sampler);
    delete s;
}

void WebGpuGpuBackend::_evictSamplerBgsByView(WGPUTextureView view) {
    for (auto it = m_samplerBgCache.begin(); it != m_samplerBgCache.end();) {
        if (it->first.view == view) {
            wgpuBindGroupRelease(it->second);
            it = m_samplerBgCache.erase(it);
        } else ++it;
    }
}
void WebGpuGpuBackend::_evictSamplerBgsBySampler(WGPUSampler sampler) {
    for (auto it = m_samplerBgCache.begin(); it != m_samplerBgCache.end();) {
        if (it->first.sampler == sampler) {
            wgpuBindGroupRelease(it->second);
            it = m_samplerBgCache.erase(it);
        } else ++it;
    }
}

void WebGpuGpuBackend::releaseShader(GpuShaderHandle handle) {
    if (!handle) return;
    auto* sh = reinterpret_cast<WgpuShader*>(handle);
    if (sh->module) wgpuShaderModuleRelease(sh->module);
    delete sh;
}

void WebGpuGpuBackend::releaseGraphicsPipeline(GpuGraphicsPipelineHandle handle) {
    if (!handle) return;
    auto* pl = reinterpret_cast<WgpuGraphicsPipeline*>(handle);
    if (pl->pipeline) wgpuRenderPipelineRelease(pl->pipeline);
    if (pl->layout)   wgpuPipelineLayoutRelease(pl->layout);
    for (auto& bg  : pl->emptyBG) { if (bg)  wgpuBindGroupRelease(bg); }
    if (pl->emptyBGL) wgpuBindGroupLayoutRelease(pl->emptyBGL);
    for (auto& bgl : pl->bgLayouts) { if (bgl) wgpuBindGroupLayoutRelease(bgl); }
    delete pl;
}

void WebGpuGpuBackend::releaseComputePipeline(GpuComputePipelineHandle handle) {
    if (!handle) return;
    auto* pl = reinterpret_cast<WgpuComputePipelineData*>(handle);
    if (pl->pipeline) wgpuComputePipelineRelease(pl->pipeline);
    if (pl->layout)   wgpuPipelineLayoutRelease(pl->layout);
    for (auto& bgl : pl->bgLayouts) { if (bgl) wgpuBindGroupLayoutRelease(bgl); }
    delete pl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Transfer buffer mapping
// ─────────────────────────────────────────────────────────────────────────────

void* WebGpuGpuBackend::mapTransferBuffer(GpuTransferBufferHandle handle, bool /*cycle*/) {
    auto* tb = reinterpret_cast<WgpuTransferBuffer*>(handle);
    if (tb->isDownload) {
        // Map the download buffer (must be called after GPU copy finishes)
        struct MapData { bool done = false; };
        MapData md;
        WGPUBufferMapCallbackInfo mapCBI{};
        mapCBI.mode     = WGPUCallbackMode_AllowSpontaneous;
        mapCBI.callback = [](WGPUMapAsyncStatus, WGPUStringView, void* ud1, void*) {
            static_cast<MapData*>(ud1)->done = true;
        };
        mapCBI.userdata1 = &md;
        wgpuBufferMapAsync(tb->downloadBuffer, WGPUMapMode_Read, 0, tb->size, mapCBI);
        pollUntil(md.done);
        return const_cast<void*>(wgpuBufferGetConstMappedRange(tb->downloadBuffer, 0, tb->size));
    }
    return tb->stagingData.data();
}

void WebGpuGpuBackend::unmapTransferBuffer(GpuTransferBufferHandle handle) {
    auto* tb = reinterpret_cast<WgpuTransferBuffer*>(handle);
    if (tb->isDownload && tb->downloadBuffer) {
        wgpuBufferUnmap(tb->downloadBuffer);
    }
    // Upload staging buffer: no unmap needed (CPU vector)
}

// ─────────────────────────────────────────────────────────────────────────────
// Upload / download
// ─────────────────────────────────────────────────────────────────────────────

void WebGpuGpuBackend::uploadToTexture(GpuCmdBufferHandle /*cmd*/,
                                        const GpuTransferBufferRegion& src,
                                        const GpuTextureRegion& dst,
                                        bool /*cycle*/) {
    auto* tb  = reinterpret_cast<WgpuTransferBuffer*>(src.transferBuffer);
    auto* tex = reinterpret_cast<WgpuTexture*>(dst.texture);

    WGPUTexelCopyTextureInfo dstCopy{};
    dstCopy.texture  = tex->texture;
    dstCopy.mipLevel = dst.mipLevel;
    dstCopy.origin   = { dst.x, dst.y, dst.z };
    dstCopy.aspect   = WGPUTextureAspect_All;

    // Bytes per pixel from the texture's actual format (don't assume 4/RGBA —
    // R8 lightmaps are 1 byte/pixel, otherwise bytesPerRow is 4x too large).
    uint32_t bpp;
    switch (tex->format) {
        case WGPUTextureFormat_R8Unorm:                          bpp = 1;  break;
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_R16Float:                         bpp = 2;  break;
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RG32Float:                        bpp = 8;  break;
        case WGPUTextureFormat_RGBA32Float:                      bpp = 16; break;
        default:                                                 bpp = 4;  break;  // RGBA8/BGRA8/R32F/RGB10A2/...
    }

    WGPUTexelCopyBufferLayout layout{};
    layout.offset       = src.offset;
    layout.bytesPerRow  = (src.pixels_per_row ? src.pixels_per_row : dst.width) * bpp;
    layout.rowsPerImage = src.rows_per_layer ? src.rows_per_layer : dst.height;

    WGPUExtent3D extent = { dst.width, dst.height, dst.depth };

    wgpuQueueWriteTexture(m_queue, &dstCopy, tb->stagingData.data() + src.offset,
                          tb->stagingData.size() - src.offset, &layout, &extent);
}

void WebGpuGpuBackend::uploadToBuffer(GpuCmdBufferHandle /*cmd*/,
                                       GpuTransferBufferHandle src, uint32_t srcOffset,
                                       GpuBufferHandle dst, uint32_t dstOffset,
                                       uint32_t size, bool /*cycle*/) {
    auto* tb  = reinterpret_cast<WgpuTransferBuffer*>(src);
    auto* buf = reinterpret_cast<WgpuBuffer*>(dst);
    // WebGPU rejects writeBuffer sizes that aren't a 4-byte multiple. Round up; the dst buffer is
    // allocated padded (createBuffer) so it has room. If the padded read would run past the staging
    // data (unaligned srcOffset), go through a zero-padded scratch instead.
    uint32_t paddedSize = (size + 3u) & ~3u;
    if (srcOffset + paddedSize <= tb->stagingData.size()) {
        wgpuQueueWriteBuffer(m_queue, buf->buffer, dstOffset,
                             tb->stagingData.data() + srcOffset, paddedSize);
    } else {
        static thread_local std::vector<uint8_t> scratch;
        scratch.assign(tb->stagingData.data() + srcOffset, tb->stagingData.data() + srcOffset + size);
        scratch.resize(paddedSize, 0);
        wgpuQueueWriteBuffer(m_queue, buf->buffer, dstOffset, scratch.data(), paddedSize);
    }
}

void WebGpuGpuBackend::downloadFromTexture(GpuCmdBufferHandle cmd,
                                            const GpuTextureRegion& src,
                                            const GpuTransferBufferRegion& dst) {
    auto* cb  = reinterpret_cast<WgpuCmdBuffer*>(cmd);
    auto* tex = reinterpret_cast<WgpuTexture*>(src.texture);
    auto* tb  = reinterpret_cast<WgpuTransferBuffer*>(dst.transferBuffer);

    WGPUTexelCopyTextureInfo srcCopy{};
    srcCopy.texture  = tex->texture;
    srcCopy.mipLevel = src.mipLevel;
    srcCopy.origin   = { src.x, src.y, src.z };
    srcCopy.aspect   = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dstCopy{};
    dstCopy.buffer  = tb->downloadBuffer;
    dstCopy.layout.offset       = dst.offset;
    dstCopy.layout.bytesPerRow  = dst.pixels_per_row * 4;
    dstCopy.layout.rowsPerImage = dst.rows_per_layer ? dst.rows_per_layer : src.height;

    WGPUExtent3D extent = { src.width, src.height, src.depth };
    wgpuCommandEncoderCopyTextureToBuffer(cb->encoder, &srcCopy, &dstCopy, &extent);
}

void WebGpuGpuBackend::blitTexture(GpuCmdBufferHandle /*cmd*/,
                                    GpuTextureHandle src, GpuTextureHandle dst,
                                    uint32_t srcX, uint32_t srcY, uint32_t srcW, uint32_t srcH,
                                    uint32_t dstX, uint32_t dstY, uint32_t dstW, uint32_t dstH,
                                    GpuFilter /*filter*/) {
    // WebGPU has no native blit; approximate with a copy when regions match.
    auto* srcTex = reinterpret_cast<WgpuTexture*>(src);
    auto* dstTex = reinterpret_cast<WgpuTexture*>(dst);
    (void)srcX; (void)srcY; (void)srcW; (void)srcH;
    (void)dstX; (void)dstY; (void)dstW; (void)dstH;
    (void)srcTex; (void)dstTex;
    // Full blit requires a render pass + fullscreen quad shader.
    // Add when needed.
}

// ─────────────────────────────────────────────────────────────────────────────
// Type mapping helpers
// ─────────────────────────────────────────────────────────────────────────────

WGPUTextureFormat WebGpuGpuBackend::toWGPU(GpuTextureFormat fmt) {
    switch (fmt) {
        case GpuTextureFormat::R8_Unorm:              return WGPUTextureFormat_R8Unorm;
        case GpuTextureFormat::R8G8_Unorm:            return WGPUTextureFormat_RG8Unorm;
        case GpuTextureFormat::R8G8B8A8_Unorm:        return WGPUTextureFormat_RGBA8Unorm;
        case GpuTextureFormat::B8G8R8A8_Unorm:        return WGPUTextureFormat_BGRA8Unorm;
        case GpuTextureFormat::R8G8B8A8_Unorm_SRGB:   return WGPUTextureFormat_RGBA8UnormSrgb;
        case GpuTextureFormat::B8G8R8A8_Unorm_SRGB:   return WGPUTextureFormat_BGRA8UnormSrgb;
        case GpuTextureFormat::R16_Float:              return WGPUTextureFormat_R16Float;
        case GpuTextureFormat::R16G16_Float:           return WGPUTextureFormat_RG16Float;
        case GpuTextureFormat::R16G16B16A16_Float:     return WGPUTextureFormat_RGBA16Float;
        case GpuTextureFormat::R32_Float:              return WGPUTextureFormat_R32Float;
        case GpuTextureFormat::R32G32_Float:           return WGPUTextureFormat_RG32Float;
        case GpuTextureFormat::R32G32B32A32_Float:     return WGPUTextureFormat_RGBA32Float;
        case GpuTextureFormat::R10G10B10A2_Unorm:      return WGPUTextureFormat_RGB10A2Unorm;
        case GpuTextureFormat::D16_Unorm:              return WGPUTextureFormat_Depth16Unorm;
        case GpuTextureFormat::D32_Float:              return WGPUTextureFormat_Depth32Float;
        case GpuTextureFormat::D24_Unorm_S8_Uint:      return WGPUTextureFormat_Depth24PlusStencil8;
        case GpuTextureFormat::D32_Float_S8_Uint:      return WGPUTextureFormat_Depth32FloatStencil8;
        case GpuTextureFormat::BC1_Unorm:              return WGPUTextureFormat_BC1RGBAUnorm;
        case GpuTextureFormat::BC2_Unorm:              return WGPUTextureFormat_BC2RGBAUnorm;
        case GpuTextureFormat::BC3_Unorm:              return WGPUTextureFormat_BC3RGBAUnorm;
        case GpuTextureFormat::BC4_Unorm:              return WGPUTextureFormat_BC4RUnorm;
        case GpuTextureFormat::BC5_Unorm:              return WGPUTextureFormat_BC5RGUnorm;
        case GpuTextureFormat::BC7_Unorm:              return WGPUTextureFormat_BC7RGBAUnorm;
        default:                                       return WGPUTextureFormat_RGBA8Unorm;
    }
}

WGPUTextureFormat WebGpuGpuBackend::depthFormatToWGPU(GpuTextureFormat fmt) {
    switch (fmt) {
        case GpuTextureFormat::D16_Unorm:          return WGPUTextureFormat_Depth16Unorm;
        case GpuTextureFormat::D32_Float:          return WGPUTextureFormat_Depth32Float;
        case GpuTextureFormat::D24_Unorm_S8_Uint:  return WGPUTextureFormat_Depth24PlusStencil8;
        case GpuTextureFormat::D32_Float_S8_Uint:  return WGPUTextureFormat_Depth32FloatStencil8;
        default:                                   return WGPUTextureFormat_Depth32Float;
    }
}

WGPUTextureUsageFlags WebGpuGpuBackend::toWGPUUsage(GpuTextureUsage usage) {
    WGPUTextureUsageFlags out = 0;
    if (usage & GpuTextureUsage::Sampler)            out |= WGPUTextureUsage_TextureBinding;
    if (usage & GpuTextureUsage::ColorTarget)        out |= WGPUTextureUsage_RenderAttachment;
    if (usage & GpuTextureUsage::DepthStencilTarget) out |= WGPUTextureUsage_RenderAttachment;
    if (usage & GpuTextureUsage::StorageRead)        out |= WGPUTextureUsage_StorageBinding;
    if (usage & GpuTextureUsage::StorageWrite)       out |= WGPUTextureUsage_StorageBinding;
    if (usage & GpuTextureUsage::Transfer)           out |= WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    return out;
}

WGPUBufferUsageFlags WebGpuGpuBackend::toWGPUUsage(GpuBufferUsage usage) {
    WGPUBufferUsageFlags out = 0;
    if (usage & GpuBufferUsage::Vertex)       out |= WGPUBufferUsage_Vertex;
    if (usage & GpuBufferUsage::Index)        out |= WGPUBufferUsage_Index;
    if (usage & GpuBufferUsage::StorageRead)  out |= WGPUBufferUsage_Storage;
    if (usage & GpuBufferUsage::StorageWrite) out |= WGPUBufferUsage_Storage;
    if (usage & GpuBufferUsage::Indirect)     out |= WGPUBufferUsage_Indirect;
    return out;
}

WGPUFilterMode WebGpuGpuBackend::toWGPU(GpuFilter f) {
    return f == GpuFilter::Linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
}

WGPUAddressMode WebGpuGpuBackend::toWGPU(GpuSamplerAddressMode m) {
    switch (m) {
        case GpuSamplerAddressMode::Repeat:         return WGPUAddressMode_Repeat;
        case GpuSamplerAddressMode::MirroredRepeat: return WGPUAddressMode_MirrorRepeat;
        case GpuSamplerAddressMode::ClampToEdge:    return WGPUAddressMode_ClampToEdge;
        default:                                    return WGPUAddressMode_ClampToEdge;
    }
}

WGPUVertexFormat WebGpuGpuBackend::toWGPU(GpuVertexElementFormat fmt) {
    switch (fmt) {
        case GpuVertexElementFormat::Float:      return WGPUVertexFormat_Float32;
        case GpuVertexElementFormat::Float2:     return WGPUVertexFormat_Float32x2;
        case GpuVertexElementFormat::Float3:     return WGPUVertexFormat_Float32x3;
        case GpuVertexElementFormat::Float4:     return WGPUVertexFormat_Float32x4;
        case GpuVertexElementFormat::Byte2Norm:  return WGPUVertexFormat_Snorm8x2;
        case GpuVertexElementFormat::Byte4Norm:  return WGPUVertexFormat_Snorm8x4;
        case GpuVertexElementFormat::UByte2Norm: return WGPUVertexFormat_Unorm8x2;
        case GpuVertexElementFormat::UByte4Norm: return WGPUVertexFormat_Unorm8x4;
        case GpuVertexElementFormat::Short2:     return WGPUVertexFormat_Sint16x2;
        case GpuVertexElementFormat::Short4:     return WGPUVertexFormat_Sint16x4;
        case GpuVertexElementFormat::Short2Norm: return WGPUVertexFormat_Snorm16x2;
        case GpuVertexElementFormat::Short4Norm: return WGPUVertexFormat_Snorm16x4;
        case GpuVertexElementFormat::Half2:      return WGPUVertexFormat_Float16x2;
        case GpuVertexElementFormat::Half4:      return WGPUVertexFormat_Float16x4;
        case GpuVertexElementFormat::UInt:       return WGPUVertexFormat_Uint32;
        case GpuVertexElementFormat::UInt2:      return WGPUVertexFormat_Uint32x2;
        case GpuVertexElementFormat::UInt4:      return WGPUVertexFormat_Uint32x4;
        case GpuVertexElementFormat::Int:        return WGPUVertexFormat_Sint32;
        case GpuVertexElementFormat::Int2:       return WGPUVertexFormat_Sint32x2;
        case GpuVertexElementFormat::Int4:       return WGPUVertexFormat_Sint32x4;
        default:                                 return WGPUVertexFormat_Float32x4;
    }
}

WGPUBlendFactor WebGpuGpuBackend::toWGPU(GpuBlendFactor f) {
    switch (f) {
        case GpuBlendFactor::Zero:                 return WGPUBlendFactor_Zero;
        case GpuBlendFactor::One:                  return WGPUBlendFactor_One;
        case GpuBlendFactor::SrcColor:             return WGPUBlendFactor_Src;
        case GpuBlendFactor::OneMinusSrcColor:     return WGPUBlendFactor_OneMinusSrc;
        case GpuBlendFactor::DstColor:             return WGPUBlendFactor_Dst;
        case GpuBlendFactor::OneMinusDstColor:     return WGPUBlendFactor_OneMinusDst;
        case GpuBlendFactor::SrcAlpha:             return WGPUBlendFactor_SrcAlpha;
        case GpuBlendFactor::OneMinusSrcAlpha:     return WGPUBlendFactor_OneMinusSrcAlpha;
        case GpuBlendFactor::DstAlpha:             return WGPUBlendFactor_DstAlpha;
        case GpuBlendFactor::OneMinusDstAlpha:     return WGPUBlendFactor_OneMinusDstAlpha;
        case GpuBlendFactor::SrcAlphaSaturate:     return WGPUBlendFactor_SrcAlphaSaturated;
        default:                                   return WGPUBlendFactor_One;
    }
}

WGPUBlendOperation WebGpuGpuBackend::toWGPU(GpuBlendOp op) {
    switch (op) {
        case GpuBlendOp::Add:             return WGPUBlendOperation_Add;
        case GpuBlendOp::Subtract:        return WGPUBlendOperation_Subtract;
        case GpuBlendOp::ReverseSubtract: return WGPUBlendOperation_ReverseSubtract;
        case GpuBlendOp::Min:             return WGPUBlendOperation_Min;
        case GpuBlendOp::Max:             return WGPUBlendOperation_Max;
        default:                          return WGPUBlendOperation_Add;
    }
}

WGPULoadOp WebGpuGpuBackend::toWGPU(GpuLoadOp op) {
    switch (op) {
        case GpuLoadOp::Load:     return WGPULoadOp_Load;
        case GpuLoadOp::Clear:    return WGPULoadOp_Clear;
        // WebGPU has no "don't care" load op, and Undefined(0) is rejected on a color
        // attachment that has a view. Our DontCare passes are fullscreen overwrites, so Clear
        // is a safe equivalent (the cleared contents are immediately overwritten).
        case GpuLoadOp::DontCare: return WGPULoadOp_Clear;
        default:                  return WGPULoadOp_Load;
    }
}

WGPUStoreOp WebGpuGpuBackend::toWGPU(GpuStoreOp op) {
    switch (op) {
        case GpuStoreOp::Store:    return WGPUStoreOp_Store;
        case GpuStoreOp::DontCare: return WGPUStoreOp_Discard;
        // Resolve: no native equivalent; store the resolve texture separately
        default:                   return WGPUStoreOp_Store;
    }
}

GpuTextureFormat WebGpuGpuBackend::fromWGPU(WGPUTextureFormat fmt) {
    switch (fmt) {
        case WGPUTextureFormat_RGBA8Unorm:       return GpuTextureFormat::R8G8B8A8_Unorm;
        case WGPUTextureFormat_BGRA8Unorm:       return GpuTextureFormat::B8G8R8A8_Unorm;
        case WGPUTextureFormat_RGBA8UnormSrgb:   return GpuTextureFormat::R8G8B8A8_Unorm_SRGB;
        case WGPUTextureFormat_BGRA8UnormSrgb:   return GpuTextureFormat::B8G8R8A8_Unorm_SRGB;
        case WGPUTextureFormat_Depth32Float:     return GpuTextureFormat::D32_Float;
        default:                                 return GpuTextureFormat::R8G8B8A8_Unorm;
    }
}
