#include "renderer.h"

#include <stdexcept>
#include <regex>
#include <chrono>
#include "platform/audio/audio.h"
#include "profiler/perf.h"

#include "assets/assethandler.h"
#include "assets/shaders_generated.h"

#include "util/helpers.h"
#include "core/log/log.h"
#include "core/enginestate/enginestate.h"

#include "gpu/renderpass.h"
#include "gpu/presets.h"

#include "renderer/passes/spriterenderpass.h"
#include "renderer/passes/model3drenderpass.h"
#include "renderer/passes/shaderrenderpass.h"

#include "compute.h"
#include "draw/particles.h"

#include "platform/input/input.h"

#include "draw/draw.h"
#include "renderer/shaders.h"        // Cross-backend Shaders::Init/Quit/Get*EntryPoint
#include "gpu/geometry/geometry2d.h" // shared on both backends
#ifdef LUMINOVEAU_WITH_RMLUI
// RmlUI's SDL3 backend takes raw SDL_GPU command buffer + texture pointers.
#include <SDL3/SDL_gpu.h>
#endif

#ifdef LUMINOVEAU_WITH_RMLUI
#include "integrations/rmlui/rmlui.h"
#include "integrations/rmlui/rmluibackend.h"
#endif

#ifdef LUMINOVEAU_WITH_IMGUI
#include "integrations/imgui/imgui_integration.h"
#endif

// _initRendering() is defined per-backend in renderer_init_sdl.cpp or renderer_init_webgpu.cpp.

void Renderer::_close() {
    if (!_gpu) {
        return;
    }

    LOG_INFO("Closing renderer");

    Particles::Quit();
    _gpu->WaitIdle();

    for (auto &[fbName, framebuffer] : _frameBuffers) {
        for (auto &[passname, renderpass] : framebuffer->renderpasses) {
            renderpass->Release(false);
            delete renderpass;
        }
        framebuffer->renderpasses.clear();

        if (framebuffer->fbContent)
            _gpu->ReleaseTexture(framebuffer->fbContent);
        if (framebuffer->fbContentMSAA)
            _gpu->ReleaseTexture(framebuffer->fbContentMSAA);
        if (framebuffer->fbDepthMSAA)
            _gpu->ReleaseTexture(framebuffer->fbDepthMSAA);
        delete framebuffer;
    }
    _frameBuffers.clear();

    for (auto &[mode, sampler] : _samplers) {
        if (sampler)
            _gpu->ReleaseSampler(sampler);
    }
    _samplers.clear();

    if (_renderToTexturePipeline)
        _gpu->ReleaseGraphicsPipeline(_renderToTexturePipeline);
    if (_renderToTexturePipelineAdditive)
        _gpu->ReleaseGraphicsPipeline(_renderToTexturePipelineAdditive);
    if (_rttVertexShader)
        _gpu->ReleaseShader(_rttVertexShader);
    if (_rttFragmentShader)
        _gpu->ReleaseShader(_rttFragmentShader);
    _renderToTexturePipeline = _renderToTexturePipelineAdditive = 0;
    _rttVertexShader = _rttFragmentShader = 0;

    Shaders::Quit();

    AssetHandler::Cleanup();

    Geometry2DFactory::ReleaseAll();
    _gpu->Shutdown();
    _device = nullptr; // SDL: ownership in SdlGpuBackend::shutdown(); WebGPU: always null

    _gpu.reset();
    _cmdbuf           = 0;
    _swapchainTexture = 0;

    LOG_INFO("Renderer closed");
}

void Renderer::_updateCameraProjection() {
    // In WebGpuScaleMode::Native the canvas may differ from Window::GetWidth/Height
    // (browser layout drives canvas size). Prefer the live canvas dimensions when
    // available so logical coords still cover the visible area.
    if (Window::GetWebGpuScaleMode() == WebGpuScaleMode::Native && _canvasWidth > 0 && _canvasHeight > 0) {
        // The integer window scale (Window::SetScale) is independent of the HiDPI scale
        // modes: it zooms the logical canvas. Divide the physical canvas by the scale
        // factor so logical coords still map across the visible area (web: scale==1, no-op).
        const float s = Window::GetScale() > 0.0f ? Window::GetScale() : 1.0f;
        _camera       = glm::ortho(0.0f, (float)_canvasWidth / s, (float)_canvasHeight / s, 0.0f);
        return;
    }
    _camera = glm::ortho(0.0f, (float)Window::GetWidth(), (float)Window::GetHeight(), 0.0f);
}

void Renderer::_onResize() {
    // Fast resize path. The framebuffer content + per-pass render targets are desktop-sized and
    // the render viewport is read per-frame, so they survive a resize untouched. Only the camera
    // projection and the (window-sized) MSAA textures actually depend on window size — recreate
    // just those. Crucially we do NOT Release()/Init() the passes, which would recompile every
    // pipeline (a Metal shader-compile storm) and re-upload all map geometry/textures for nothing.
    _updateCameraProjection();

    int w = Window::GetPhysicalWidth(), h = Window::GetPhysicalHeight();
    if (w <= 0 || h <= 0)
        return;

    // Let each pass refresh its surface-sized targets (e.g. the 2D sprite layer's depth +
    // effect temps) so 2D content rescales — without recompiling pipelines or re-uploading geo.
    for (auto &[fbName, framebuffer] : _frameBuffers)
        for (auto &[passname, renderpass] : framebuffer->renderpasses)
            renderpass->OnResize((uint32_t)w, (uint32_t)h);

    if (_currentSampleCount <= GpuSampleCount::X1)
        return; // no MSAA targets -> nothing more
    GpuTextureFormat swapchainFmt = _gpu->GetSwapchainFormat();

    for (auto &[fbName, framebuffer] : _frameBuffers) {
        if (framebuffer->noMSAA)
            continue;
        if (framebuffer->fbContentMSAA) {
            _gpu->ReleaseTexture(framebuffer->fbContentMSAA);
            framebuffer->fbContentMSAA = 0;
        }
        if (framebuffer->fbDepthMSAA) {
            _gpu->ReleaseTexture(framebuffer->fbDepthMSAA);
            framebuffer->fbDepthMSAA = 0;
        }

        GpuTextureCreateInfo colorInfo {
            .width         = (uint32_t)w,
            .height        = (uint32_t)h,
            .depthOrLayers = 1,
            .numLevels     = 1,
            .format        = swapchainFmt,
            .sampleCount   = _currentSampleCount,
            .usage         = GpuTextureUsage::ColorTarget,
        };
        framebuffer->fbContentMSAA = _gpu->CreateTexture(colorInfo);
        GpuTextureCreateInfo depthInfo {
            .width         = (uint32_t)w,
            .height        = (uint32_t)h,
            .depthOrLayers = 1,
            .numLevels     = 1,
            .format        = GpuTextureFormat::D32_Float,
            .sampleCount   = _currentSampleCount,
            .usage         = GpuTextureUsage::DepthStencilTarget,
        };
        framebuffer->fbDepthMSAA = _gpu->CreateTexture(depthInfo);
    }
    _gpu->WaitIdle();
}

void Renderer::_clearBackground(Color color) {
    LUMI_UNUSED(color);
    // can ignore this for now
}

void Renderer::_startFrame() const {
    if (!_gpu)
        return;

#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::NewFrame();
#endif
}

void Renderer::_endFrame() {
    if (!_gpu)
        return;

    Draw::FlushPixels();
    Input::GetVirtualControls().Render();
    _gpu->ProcessPendingScreenshots();

    // ── Acquire command buffer and swapchain (IGpu interface) ─────────────────
    _cmdbuf = _gpu->AcquireCommandBuffer();
    if (!_cmdbuf) {
        LOG_WARNING("Failed to acquire GPU command buffer");
#ifdef LUMINOVEAU_WITH_IMGUI
        ImGuiIntegration::EndFrame();
#endif
        return;
    }

    uint32_t scWidth = 0, scHeight = 0;
    _swapchainTexture = _gpu->AcquireSwapchainTexture(_cmdbuf, scWidth, scHeight);
    if (scWidth > 0 && scHeight > 0) {
        // Authoritative present size -> Window::GetPhysicalWidth/Height. Drives viewport + blit
        // UV so they match the swapchain even when SDL_GetWindowSizeInPixels lies (Wayland
        // fractional scaling), which otherwise renders the scene bigger than the window.
        EngineState::swapchainWidth  = (int)scWidth;
        EngineState::swapchainHeight = (int)scHeight;
    }
    if (scWidth > 0 && scHeight > 0 && (scWidth != _canvasWidth || scHeight != _canvasHeight)) {
        _canvasWidth  = scWidth;
        _canvasHeight = scHeight;
        // The swapchain can change size WITHOUT an SDL window-resize event — most importantly on
        // the first frame, when a HiDPI window realizes its true physical drawable (e.g. a 1280x720
        // logical window backed by a 2560x1440 Metal layer). Run the full resize path, not just the
        // camera: passes that cap their viewport to m_surface_width (SpriteRenderPass, used for the
        // HUD/console layer) would otherwise stay pinned to the pre-realization size and fill only
        // part of the screen, while passes that read GetPhysicalWidth live (the 3D world) fill it.
        // _onResize() reads GetPhysicalWidth/Height, which == the swapchain size set just above.
        _onResize();
    }

    if (!_swapchainTexture) {
        Compute::Reset();
        Draw::ResetEffectStore();
#ifdef LUMINOVEAU_WITH_IMGUI
        ImGuiIntegration::EndFrame();
#endif
        _gpu->SubmitCommandBuffer(_cmdbuf);
        for (auto &[fbName, framebuffer] : _frameBuffers) {
            for (auto &[passname, renderpass] : framebuffer->renderpasses) {
                renderpass->ResetRenderQueue();
            }
        }
        _cmdbuf = 0;
        return;
    }

    Draw::ReleaseFramePixelTextures();

    // ── MSAA-aware render-pass scheduling with pre/post compute split ──────────
    bool useMSAA = (_currentSampleCount > GpuSampleCount::X1);

    auto runPasses = [&](bool preCompute) {
        for (auto &[fbName, framebuffer] : _frameBuffers) {
            if (framebuffer->preComputeFlush != preCompute)
                continue;
            // Default: canvas-logical ortho via _camera. fixedSize FBs (e.g., the LightToy
            // hrc_scene RT) instead use an ortho sized to their actual pixel dims so callers
            // can draw in the RT's native coord space — without this, an FB sized 1348×783
            // would receive sprites projected against the 1598-wide window camera, so a draw
            // at "scene x=1336" ends up at RT pixel ~1127 (the canvas-x→NDC→viewport chain
            // shifts the content left by canvas_w/fb_w).
            glm::mat4 fbCamera = framebuffer->fixedSize
                ? glm::ortho(0.0f, (float)framebuffer->width, (float)framebuffer->height, 0.0f)
                : _camera;

            bool             useThisMSAA  = useMSAA && framebuffer->fbContentMSAA != 0;
            GpuTextureHandle renderTarget = useThisMSAA ? framebuffer->fbContentMSAA : framebuffer->fbContent;
            GpuTextureHandle depthTarget  = useThisMSAA ? framebuffer->fbDepthMSAA : 0;
            for (size_t i = 0; i < framebuffer->renderpasses.size(); i++) {
                auto &[passname, renderpass] = framebuffer->renderpasses[i];
                if (i > 0)
                    renderpass->colorTargetInfoLoadOp = GpuLoadOp::Load;
                renderpass->renderTargetDepth   = depthTarget;
                bool isLastPass                 = (i == framebuffer->renderpasses.size() - 1);
                bool nextNeedsResolved          = useThisMSAA && !isLastPass && framebuffer->renderpasses[i + 1].second->NeedsResolvedInput();
                renderpass->renderTargetResolve = (useThisMSAA && (isLastPass || nextNeedsResolved)) ? framebuffer->fbContent : 0;
                renderpass->Render(_cmdbuf, renderTarget, fbCamera);
            }
        }
    };

#ifdef LUMINOVEAU_WITH_RMLUI
    auto *sdlCmdBuf    = reinterpret_cast<SDL_GPUCommandBuffer *>(_cmdbuf);
    auto *sdlSwapchain = reinterpret_cast<SDL_GPUTexture *>(_swapchainTexture);
#endif

    runPasses(true);
    Particles::PrepareFrame(_cmdbuf);
    Compute::ExecuteQueued(_cmdbuf);
    Compute::Reset();
    runPasses(false);

    // ── Blit framebuffer to swapchain (IGpu interface) ────────────────────────
    _renderFrameBuffer(_cmdbuf);

    // ── UI overlays (RmlUI is SDL-only today; opt-in via cmake flag) ──────────
#ifdef LUMINOVEAU_WITH_RMLUI
    RmlUI::Backend::BeginFrame(sdlCmdBuf, sdlSwapchain,
        static_cast<uint32_t>(Window::GetWidth()),
        static_cast<uint32_t>(Window::GetHeight()));
    RmlUI::Render();
    RmlUI::Backend::EndFrame();
#endif

    // ── ImGui: both SDL and WebGPU ────────────────────────────────────────────
#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::RenderFrame(_cmdbuf, _swapchainTexture);
#endif

    if (Window::HasPendingScreenshot()) {
        std::string filename = Window::GetAndClearPendingScreenshot();
        if (filename.empty()) {
            auto now       = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            filename       = Helpers::TextFormat("screenshot_%lld.png", timestamp);
        }
        if (!filename.ends_with(".png")) {
            if (filename.ends_with(".bmp"))
                filename = filename.substr(0, filename.length() - 4) + ".png";
            else
                filename += ".png";
        }
        uint32_t width  = (uint32_t)Window::GetPhysicalWidth();
        uint32_t height = (uint32_t)Window::GetPhysicalHeight();
        _gpu->RequestScreenshot(_cmdbuf, _swapchainTexture, width, height, filename);
    }

    // Offscreen framebuffer captures (queued via CaptureFramebuffer). Their passes
    // have already rendered into fbContent above, so stage the download now.
    for (auto &[fbName, filename] : _pendingFbCaptures) {
        if (FrameBuffer *fb = _getFramebuffer(fbName); fb && fb->fbContent)
            _gpu->RequestScreenshot(_cmdbuf, fb->fbContent, fb->width, fb->height, filename);
    }
    _pendingFbCaptures.clear();

    // ── Submit and reset ──────────────────────────────────────────────────────
    // When the perf HUD is open, fence the final submit and wait for GPU completion to time
    // real GPU work. Near-free when vsync-bound (the CPU would idle for the GPU anyway); zero
    // cost when the HUD is hidden.
    if (Perf::Visible()) {
        auto           t0    = std::chrono::high_resolution_clock::now();
        GpuFenceHandle fence = _gpu->SubmitCommandBufferAndAcquireFence(_cmdbuf);
        if (fence) {
            _gpu->WaitFence(fence);
            double ms = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::high_resolution_clock::now() - t0)
                            .count()
                / 1.0e6;
            Perf::ReportGPUms(ms);
            _gpu->ReleaseFence(fence);
        }
    } else {
        _gpu->SubmitCommandBuffer(_cmdbuf);
    }
    _gpu->PresentSwapchain();

    // Increment the present counter — Window::GetFPS reads this over its caller-supplied
    // averaging window. Decoupled from _lastFrameTime so SDL_AppIterate's spin rate
    // doesn't poison the FPS report.
    EngineState::presentCount++;

    // Real per-present frame time -> perf HUD graph (matches the present-based FPS).
    {
        static auto lastPresent = std::chrono::high_resolution_clock::now();
        static bool have        = false;
        auto        nowP        = std::chrono::high_resolution_clock::now();
        if (have) {
            double dt = std::chrono::duration_cast<std::chrono::nanoseconds>(nowP - lastPresent).count() / 1.0e6;
            Perf::ReportFrameMs(dt);
        }
        lastPresent = nowP;
        have        = true;
    }

    // Per-frame draw stats -> perf HUD.
    if (Perf::Visible())
        Perf::ReportDraws(_gpu->FrameDrawCalls(), _gpu->FrameDrawVerts());
    _gpu->ResetFrameDrawStats();
    for (auto &[fbName, framebuffer] : _frameBuffers) {
        for (auto &[passname, renderpass] : framebuffer->renderpasses) {
            renderpass->ResetRenderQueue();
        }
    }
    Draw::ResetEffectStore();
    _cmdbuf = 0;
}

void Renderer::_reset() {
    LOG_INFO("Resetting render passes with MSAA={}", static_cast<int>(_currentSampleCount));

    // Render dimensions = current window in physical pixels. Render passes draw into
    // this many pixels (top-left region of each framebuffer's texture). The framebuffer
    // textures themselves are sized large enough at Init (desktop) so they survive
    // window resizes / fullscreen toggles without ever being recreated.
    int rpWidth  = Window::GetPhysicalWidth();
    int rpHeight = Window::GetPhysicalHeight();
    if (rpWidth <= 0 || rpHeight <= 0) {
        rpWidth  = 1280;
        rpHeight = 720;
    }

    bool             useMSAA      = (_currentSampleCount > GpuSampleCount::X1);
    GpuTextureFormat swapchainFmt = _gpu->GetSwapchainFormat();

    // Recreate framebuffer MSAA textures if needed
    for (auto &[fbName, framebuffer] : _frameBuffers) {
        // Release old MSAA textures
        if (framebuffer->fbContentMSAA) {
            _gpu->ReleaseTexture(framebuffer->fbContentMSAA);
            framebuffer->fbContentMSAA = 0;
        }
        if (framebuffer->fbDepthMSAA) {
            _gpu->ReleaseTexture(framebuffer->fbDepthMSAA);
            framebuffer->fbDepthMSAA = 0;
        }

        // Create new MSAA textures if MSAA is enabled and this framebuffer supports it.
        // Custom effect render targets (noMSAA=true) always render to plain 1x textures.
        if (useMSAA && !framebuffer->noMSAA) {
            GpuTextureCreateInfo msaaColorInfo {
                .width         = static_cast<uint32_t>(rpWidth),
                .height        = static_cast<uint32_t>(rpHeight),
                .depthOrLayers = 1,
                .numLevels     = 1,
                .format        = swapchainFmt,
                .sampleCount   = _currentSampleCount,
                .usage         = GpuTextureUsage::ColorTarget,
            };
            framebuffer->fbContentMSAA = _gpu->CreateTexture(msaaColorInfo);

            GpuTextureCreateInfo msaaDepthInfo {
                .width         = static_cast<uint32_t>(rpWidth),
                .height        = static_cast<uint32_t>(rpHeight),
                .depthOrLayers = 1,
                .numLevels     = 1,
                .format        = GpuTextureFormat::D32_Float,
                .sampleCount   = _currentSampleCount,
                .usage         = GpuTextureUsage::DepthStencilTarget,
            };
            framebuffer->fbDepthMSAA = _gpu->CreateTexture(msaaDepthInfo);
        }
    }

    for (auto &[fbName, framebuffer] : _frameBuffers) {
        for (auto &[passname, renderpass] : framebuffer->renderpasses) {

            renderpass->Release(false);

            _gpu->WaitIdle();
            bool initSuccess = renderpass->Init(swapchainFmt,
                rpWidth, rpHeight,
                passname, true, 0, framebuffer->noMSAA);
            if (!initSuccess) {
                LOG_ERROR("Renderpass ({}) failed to Init()", passname.c_str());
            }
        }
    }

    // Ensure all pipeline compilations are complete before rendering resumes.
    _gpu->WaitIdle();

    LOG_INFO("Reset complete");
}

void Renderer::_addToRenderQueue(const std::string &passname, const Renderable &renderable) {
    for (auto &[fbName, framebuffer] : _frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
            [&passname](const std::pair<std::string, RenderPass *> &entry) {
                return entry.first == passname;
            });

        if (it != framebuffer->renderpasses.end()) {
            it->second->AddToRenderQueue(renderable);
        }
    }
}

void Renderer::_addShaderPass(const std::string &passname, const ShaderAsset &vertShader, const ShaderAsset &fragShader,
    std::vector<std::string> targetBuffers) {
    auto shaderPass        = new ShaderRenderPass();
    shaderPass->vertShader = vertShader;
    shaderPass->fragShader = fragShader;

    uint32_t desktopWidth = 0, desktopHeight = 0;
    Window::GetDisplayBounds(desktopWidth, desktopHeight);

    bool succes = shaderPass->Init(_gpu->GetSwapchainFormat(),
        desktopWidth, desktopHeight,
        passname);
    if (succes) {
        if (targetBuffers.empty()) {
            targetBuffers.emplace_back("primaryFramebuffer");
        }

        for (auto &buffername : targetBuffers) {
            auto it = std::find_if(_frameBuffers.begin(), _frameBuffers.end(),
                [&buffername](const std::pair<std::string, FrameBuffer *> &entry) {
                    return entry.first == buffername;
                });

            if (it != _frameBuffers.end()) {
                it->second->renderpasses.emplace_back(passname, shaderPass);
            }
        }
    } else {
        LOG_ERROR("Failed to create shaderpass: {}", passname.c_str());
    }
}

void Renderer::_removeShaderPass(const std::string &passname) {
    bool        found        = false;
    RenderPass *passToDelete = nullptr;

    // Find and remove the pass from all framebuffers
    for (auto &[fbName, framebuffer] : _frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
            [&passname](const std::pair<std::string, RenderPass *> &entry) {
                return entry.first == passname;
            });

        if (it != framebuffer->renderpasses.end()) {
            passToDelete = it->second;
            framebuffer->renderpasses.erase(it);
            found = true;
            LOG_INFO("Removed shader pass '{}' from framebuffer '{}'", passname, fbName);
        }
    }

    // Release GPU resources and delete the pass
    if (passToDelete) {
        passToDelete->Release(true); // Log the release
        delete passToDelete;
    }

    if (!found) {
        LOG_WARNING("Shader pass '{}' not found for removal", passname);
    }
}

UniformBuffer &Renderer::_getUniformBuffer(const std::string &passname) {
    for (auto &[fbName, framebuffer] : _frameBuffers) {

        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
            [&passname](const std::pair<std::string, RenderPass *> &entry) {
                return entry.first == passname;
            });

        if (it != framebuffer->renderpasses.end()) {
            return it->second->GetUniformBuffer();
        }
    }

    // this section of code should never be hit because every renderpass has a buffer attached to it
    assert(false && "UniformBuffer not found");
    static UniformBuffer dummyBuffer;
    return dummyBuffer;
}

void Renderer::_renderFrameBuffer(GpuCmdBufferHandle cmdBuf) {

    auto *framebufferObj = Renderer::GetFramebuffer("primaryFramebuffer");

    struct UniformsPadded : Uniforms {
        float pad[2] = { 0.0f, 0.0f };
    };
    UniformsPadded rttUniforms {};

    // No reset on window resize: the framebuffer is sized at init to handle any future
    // window size up to the full desktop. Render-pass viewports + the blit's partial-UV
    // sample pick up the new window dimensions per frame without recreating textures.
    {
        const float fbW = (float)framebufferObj->width;
        const float fbH = (float)framebufferObj->height;
        const float scW = (float)(_canvasWidth > 0 ? _canvasWidth : (uint32_t)fbW);
        const float scH = (float)(_canvasHeight > 0 ? _canvasHeight : (uint32_t)fbH);

        float blitX = 0.0f, blitY = 0.0f, blitW = scW, blitH = scH;
        float uvX0 = 0.0f, uvY0 = 0.0f, uvX1 = 1.0f, uvY1 = 1.0f;

        // UV defaults: top-left phys-window region of the (possibly larger) framebuffer.
        // For framebuffers that exactly match the window in physical pixels this is the
        // full [0,1] quad; for desktop-sized buffers it samples only the rendered region.
        {
            const float physW = (float)Window::GetPhysicalWidth();
            const float physH = (float)Window::GetPhysicalHeight();
            uvX1              = std::min(1.0f, physW / fbW);
            uvY1              = std::min(1.0f, physH / fbH);
        }

        switch (Window::GetWebGpuScaleMode()) {
        case WebGpuScaleMode::Contain: {
            const float scale = std::min(scW / fbW, scH / fbH);
            blitW             = fbW * scale;
            blitH             = fbH * scale;
            blitX             = (scW - blitW) * 0.5f;
            blitY             = (scH - blitH) * 0.5f;
            break;
        }
        case WebGpuScaleMode::Fill: {
            const float scale = std::max(scW / fbW, scH / fbH);
            const float visW  = scW / scale;
            const float visH  = scH / scale;
            uvX0              = (fbW - visW) * 0.5f / fbW;
            uvY0              = (fbH - visH) * 0.5f / fbH;
            uvX1              = uvX0 + visW / fbW;
            uvY1              = uvY0 + visH / fbH;
            break;
        }
        default:
            break; // Stretch / Native: keep the phys-window UV defaults above
        }

        // Store inverse blit transform for mouse coordinate remapping.
        // Formula: logical = offset + canvas * invScale
        _blitInvScaleX      = 1.0f;
        _blitInvScaleY      = 1.0f;
        _blitLogicalOffsetX = 0.0f;
        _blitLogicalOffsetY = 0.0f;
        switch (Window::GetWebGpuScaleMode()) {
        case WebGpuScaleMode::Contain: {
            const float sc      = std::min(scW / fbW, scH / fbH);
            const float bX      = (scW - fbW * sc) * 0.5f;
            const float bY      = (scH - fbH * sc) * 0.5f;
            _blitInvScaleX      = 1.0f / sc;
            _blitInvScaleY      = 1.0f / sc;
            _blitLogicalOffsetX = -bX / sc;
            _blitLogicalOffsetY = -bY / sc;
            break;
        }
        case WebGpuScaleMode::Fill: {
            const float sc      = std::max(scW / fbW, scH / fbH);
            _blitInvScaleX      = 1.0f / sc;
            _blitInvScaleY      = 1.0f / sc;
            _blitLogicalOffsetX = (fbW - scW / sc) * 0.5f;
            _blitLogicalOffsetY = (fbH - scH / sc) * 0.5f;
            break;
        }
        case WebGpuScaleMode::Stretch:
            _blitInvScaleX = fbW / scW;
            _blitInvScaleY = fbH / scH;
            break;
        case WebGpuScaleMode::Native:
            break;
        }

        // Blit always uses canvas-space ortho so modes work independently of _camera
        rttUniforms.camera = glm::ortho(0.0f, scW, scH, 0.0f);
        rttUniforms.model  = glm::mat4(
            blitW, 0.0f, 0.0f, 0.0f,
            0.0f, blitH, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            blitX, blitY, 0.0f, 1.0f);
        rttUniforms.flipped = glm::vec2(1.0f, 1.0f);
        // Vertex positions (from WGSL): (1,1),(0,1),(1,0),(0,1),(0,0),(1,0)
        rttUniforms.uv0 = glm::vec2(uvX1, uvY1);
        rttUniforms.uv1 = glm::vec2(uvX0, uvY1);
        rttUniforms.uv2 = glm::vec2(uvX1, uvY0);
        rttUniforms.uv3 = glm::vec2(uvX0, uvY1);
        rttUniforms.uv4 = glm::vec2(uvX0, uvY0);
        rttUniforms.uv5 = glm::vec2(uvX1, uvY0);
    }

    GpuColorTargetInfo colorTarget {
        .texture = _swapchainTexture,
        .loadOp  = GpuLoadOp::Clear,
        .storeOp = GpuStoreOp::Store,
        .clearR  = 0.0f,
        .clearG  = 0.0f,
        .clearB  = 0.0f,
        .clearA  = 1.0f,
    };

    auto renderPass = _gpu->BeginRenderPass(cmdBuf, &colorTarget, 1, nullptr);
    _gpu->BindGraphicsPipeline(renderPass, _renderToTexturePipeline);
    _gpu->PushVertexUniformData(cmdBuf, 0, &rttUniforms, sizeof(rttUniforms));
    GpuTextureSamplerBinding binding { framebufferObj->fbContent, Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode()) };
    _gpu->BindFragmentSamplers(renderPass, 0, &binding, 1);
    _gpu->DrawPrimitives(renderPass, 6, 1, 0, 0);

    for (const auto &[fbName, fb] : _frameBuffers) {
        if (fb->renderToScreen) {
            _gpu->BindGraphicsPipeline(renderPass, fb->additiveBlend ? _renderToTexturePipelineAdditive : _renderToTexturePipeline);
            _gpu->PushVertexUniformData(cmdBuf, 0, &rttUniforms, sizeof(rttUniforms));
            GpuTextureSamplerBinding fbBinding { fb->fbContent, Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode()) };
            _gpu->BindFragmentSamplers(renderPass, 0, &fbBinding, 1);
            _gpu->DrawPrimitives(renderPass, 6, 1, 0, 0);
        }
    }

    _gpu->EndRenderPass(renderPass);
}

FrameBuffer *Renderer::_getFramebuffer(std::string fbname) {
    auto it = std::find_if(_frameBuffers.begin(), _frameBuffers.end(), [&fbname](const auto &pair) {
        return pair.first == fbname;
    });

    FrameBuffer *framebuffer = nullptr;

    if (it != _frameBuffers.end()) {
        framebuffer = it->second;
    }

    return framebuffer;
}

void Renderer::_createFrameBuffer(const std::string &fbname, uint32_t width, uint32_t height) {
    auto it = std::find_if(_frameBuffers.begin(), _frameBuffers.end(), [&fbname](const auto &pair) {
        return pair.first == fbname;
    });

    if (it == _frameBuffers.end()) {
        int fbWidth, fbHeight;
        if (width > 0 && height > 0) {
            fbWidth  = (int)width;
            fbHeight = (int)height;
        } else {
            uint32_t dw = 0, dh = 0;
            Window::GetDisplayBounds(dw, dh);
            fbWidth  = (int)dw;
            fbHeight = (int)dh;
        }

        auto *framebuffer      = new FrameBuffer;
        framebuffer->fbContent = AssetHandler::CreateEmptyTexture({ (float)fbWidth, (float)fbHeight }).gpuTexture;
        framebuffer->width     = fbWidth;
        framebuffer->height    = fbHeight;
        framebuffer->fixedSize = (width > 0 && height > 0);
        _frameBuffers.emplace_back(fbname, framebuffer);

        framebuffer->textureView.width      = fbWidth;
        framebuffer->textureView.height     = fbHeight;
        framebuffer->textureView.gpuTexture = framebuffer->fbContent;
        framebuffer->textureView.gpuSampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode());

        LOG_INFO("Created framebuffer: {} ({}x{})", fbname.c_str(), fbWidth, fbHeight);
    }
}

void Renderer::_createFrameBuffer(const std::string &fbname, uint32_t width, uint32_t height, GpuTextureFormat format) {
    auto it = std::find_if(_frameBuffers.begin(), _frameBuffers.end(), [&fbname](const auto &pair) {
        return pair.first == fbname;
    });

    if (it == _frameBuffers.end()) {
        int fbWidth, fbHeight;
        if (width > 0 && height > 0) {
            fbWidth  = (int)width;
            fbHeight = (int)height;
        } else {
            uint32_t dw = 0, dh = 0;
            Window::GetDisplayBounds(dw, dh);
            fbWidth  = (int)dw;
            fbHeight = (int)dh;
        }

        auto *framebuffer      = new FrameBuffer;
        framebuffer->fbContent = AssetHandler::CreateEmptyTexture({ (float)fbWidth, (float)fbHeight }, format).gpuTexture;
        framebuffer->width     = fbWidth;
        framebuffer->height    = fbHeight;
        framebuffer->fixedSize = (width > 0 && height > 0);
        _frameBuffers.emplace_back(fbname, framebuffer);

        framebuffer->textureView.width      = fbWidth;
        framebuffer->textureView.height     = fbHeight;
        framebuffer->textureView.gpuTexture = framebuffer->fbContent;
        framebuffer->textureView.gpuSampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode());

        LOG_INFO("Created framebuffer: {} ({}x{}, custom format)", fbname.c_str(), fbWidth, fbHeight);
    }
}

void Renderer::_setFramebufferRenderToScreen(const std::string &fbName, bool render) {
    auto *framebuffer = _getFramebuffer(fbName);
    if (framebuffer) {
        framebuffer->renderToScreen = render;
    } else {
        LOG_WARNING("Framebuffer not found: {}", fbName.c_str());
    }
}

void Renderer::_attachRenderPassToFrameBuffer(RenderPass *renderPass, const std::string &passname, const std::string &fbName) {
    auto it = std::find_if(_frameBuffers.begin(), _frameBuffers.end(), [&fbName](const auto &pair) {
        return pair.first == fbName;
    });

    if (it != _frameBuffers.end()) {
        it->second->renderpasses.emplace_back(passname, renderPass);

        LOG_INFO("Attached renderpass {} to framebuffer: {}", passname.c_str(), fbName.c_str());
    }
}

GpuSamplerHandle Renderer::_getSampler(ScaleMode scaleMode) {
    return _samplers[scaleMode];
}

Texture Renderer::_whitePixel() {
    return _whitePixelTexture;
}

Geometry2D *Renderer::_getQuadGeometry() {
    return Geometry2DFactory::CreateQuad();
}

Geometry2D *Renderer::_getCircleGeometry(int segments) {
    return Geometry2DFactory::CreateCircle(segments);
}

Geometry2D *Renderer::_getRoundedRectGeometry(float cornerRadiusX, float cornerRadiusY, int cornerSegments) {
    return Geometry2DFactory::CreateRoundedRect(cornerRadiusX, cornerRadiusY, cornerSegments);
}

GpuRenderPassHandle Renderer::_getRenderPass(const std::string &passname) {

    GpuRenderPassHandle foundPass = 0;

    for (auto &[fbName, framebuffer] : _frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
            [&passname](const std::pair<std::string, RenderPass *> &entry) {
                return entry.first == passname;
            });

        if (it != framebuffer->renderpasses.end()) {
            foundPass = it->second->renderPass;
        }
    }

    return foundPass;
}

RenderPass *Renderer::_findRenderPass(const std::string &passname) {
    for (auto &[fbName, framebuffer] : _frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
            [&passname](const std::pair<std::string, RenderPass *> &entry) {
                return entry.first == passname;
            });
        if (it != framebuffer->renderpasses.end()) {
            return it->second;
        }
    }
    return nullptr;
}

void Renderer::_setScissorMode(const std::string &passname, const rectf &cliprect) {

    for (auto &[fbName, framebuffer] : _frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
            [&passname](const std::pair<std::string, RenderPass *> &entry) {
                return entry.first == passname;
            });

        if (it != framebuffer->renderpasses.end()) {
            it->second->scissorEnabled = true;
            it->second->scissorX       = static_cast<int32_t>(cliprect.x);
            it->second->scissorY       = static_cast<int32_t>(cliprect.y);
            it->second->scissorW       = static_cast<uint32_t>(cliprect.w);
            it->second->scissorH       = static_cast<uint32_t>(cliprect.h);
        }
    }
}

void Renderer::_setSampleCount(GpuSampleCount sampleCount) {
    _currentSampleCount = sampleCount;

    _reset();
}

void Renderer::_createSpriteRenderTarget(const std::string &name, const SpriteRenderTargetConfig &config) {
    std::string framebufferName = name + "_framebuffer";
    if (config.format != GpuTextureFormat::Invalid)
        _createFrameBuffer(framebufferName, config.width, config.height, config.format);
    else
        _createFrameBuffer(framebufferName, config.width, config.height);
    _setFramebufferRenderToScreen(framebufferName, config.renderToScreen);

    // Custom render targets never use MSAA — intermediate effect buffers, plain 1x.
    auto *fb = _getFramebuffer(framebufferName);
    if (fb) {
        fb->noMSAA          = true;
        fb->preComputeFlush = config.preComputeFlush;
        if (config.renderToScreen && config.blendMode == BlendMode::Additive)
            fb->additiveBlend = true;
    }

    // Convert BlendMode enum to neutral blend state
    GpuColorTargetBlendState blendState {};
    switch (config.blendMode) {
    case BlendMode::Default:
        blendState = GpuPresets::AlphaBlendKeepDstAlpha;
        break;
    case BlendMode::SrcAlpha:
        blendState = GpuPresets::AlphaBlend;
        break;
    case BlendMode::Additive:
        blendState.blendEnabled   = true;
        blendState.srcColorFactor = GpuBlendFactor::SrcAlpha;
        blendState.dstColorFactor = GpuBlendFactor::One;
        blendState.colorOp        = GpuBlendOp::Add;
        blendState.srcAlphaFactor = GpuBlendFactor::One;
        blendState.dstAlphaFactor = GpuBlendFactor::One;
        blendState.alphaOp        = GpuBlendOp::Add;
        break;
    case BlendMode::None:
        blendState.blendEnabled   = false;
        blendState.srcColorFactor = GpuBlendFactor::One;
        blendState.dstColorFactor = GpuBlendFactor::Zero;
        blendState.colorOp        = GpuBlendOp::Add;
        blendState.srcAlphaFactor = GpuBlendFactor::One;
        blendState.dstAlphaFactor = GpuBlendFactor::Zero;
        blendState.alphaOp        = GpuBlendOp::Add;
        break;
    }

    auto *renderPass = new SpriteRenderPass();
    renderPass->UpdateRenderPassBlendState(blendState);

    // Render-pass size: explicit config size, else display bounds.
    int rpWidth, rpHeight;
    if (config.width > 0 && config.height > 0) {
        rpWidth  = (int)config.width;
        rpHeight = (int)config.height;
    } else {
        uint32_t dw = 0, dh = 0;
        Window::GetDisplayBounds(dw, dh);
        rpWidth  = (int)dw;
        rpHeight = (int)dh;
        if (rpWidth <= 0 || rpHeight <= 0) {
            rpWidth  = 1280;
            rpHeight = 720;
        }
    }

    GpuTextureFormat passFormat = (config.format != GpuTextureFormat::Invalid)
        ? config.format
        : _gpu->GetSwapchainFormat();
    renderPass->Init(passFormat, rpWidth, rpHeight, name, true,
        config.maxSprites, /*forceNoMSAA=*/true);

    renderPass->colorTargetInfoLoadOp = config.clearOnLoad ? GpuLoadOp::Clear : GpuLoadOp::Load;
    renderPass->colorTargetClearR     = config.clearColor.r / 255.0f;
    renderPass->colorTargetClearG     = config.clearColor.g / 255.0f;
    renderPass->colorTargetClearB     = config.clearColor.b / 255.0f;
    renderPass->colorTargetClearA     = config.clearColor.a / 255.0f;

    _attachRenderPassToFrameBuffer(renderPass, name, framebufferName);
    LOG_INFO("Created sprite render target: {}", name.c_str());
}

void Renderer::_removeSpriteRenderTarget(const std::string &name, bool removeFramebuffer) {
    std::string framebufferName = name + "_framebuffer";

    // Find and remove the render pass from the framebuffer
    auto fbIt = std::find_if(_frameBuffers.begin(), _frameBuffers.end(),
        [&framebufferName](const auto &pair) {
            return pair.first == framebufferName;
        });

    if (fbIt != _frameBuffers.end()) {
        auto &renderpasses = fbIt->second->renderpasses;
        auto  passIt       = std::find_if(renderpasses.begin(), renderpasses.end(),
                   [&name](const auto &pair) {
                return pair.first == name;
            });

        if (passIt != renderpasses.end()) {
            // Release GPU resources
            passIt->second->Release();

            // Delete the render pass object
            delete passIt->second;

            // Remove from vector
            renderpasses.erase(passIt);

            LOG_INFO("Removed sprite render target: {}", name.c_str());
        }

        // Optionally remove the framebuffer if requested
        if (removeFramebuffer) {
            // Release framebuffer GPU textures
            if (fbIt->second->fbContent)
                _gpu->ReleaseTexture(fbIt->second->fbContent);
            if (fbIt->second->fbContentMSAA)
                _gpu->ReleaseTexture(fbIt->second->fbContentMSAA);
            if (fbIt->second->fbDepthMSAA)
                _gpu->ReleaseTexture(fbIt->second->fbDepthMSAA);

            // Delete framebuffer object
            delete fbIt->second;

            // Remove from vector
            _frameBuffers.erase(fbIt);

            LOG_INFO("Removed framebuffer: {}", framebufferName.c_str());
        }
    } else {
        LOG_WARNING("Sprite render target not found: {}", name.c_str());
    }
}
