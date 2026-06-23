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
#include "renderer/shaders.h"          // Cross-backend Shaders::Init/Quit/Get*EntryPoint
#include "gpu/geometry/geometry2d.h"  // shared on both backends
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
    if (!m_gpu) {
        return;
    }

    LOG_INFO("Closing renderer");

    Particles::Quit();
    m_gpu->waitIdle();


    for (auto &[fbName, framebuffer]: frameBuffers) {
        for (auto &[passname, renderpass]: framebuffer->renderpasses) {
            renderpass->release(false);
            delete renderpass;
        }
        framebuffer->renderpasses.clear();

        if (framebuffer->fbContent)     m_gpu->releaseTexture(framebuffer->fbContent);
        if (framebuffer->fbContentMSAA) m_gpu->releaseTexture(framebuffer->fbContentMSAA);
        if (framebuffer->fbDepthMSAA)   m_gpu->releaseTexture(framebuffer->fbDepthMSAA);
        delete framebuffer;
    }
    frameBuffers.clear();

    for (auto& [mode, sampler] : _samplers) {
        if (sampler) m_gpu->releaseSampler(sampler);
    }
    _samplers.clear();

    if (m_rendertotexturepipeline)          m_gpu->releaseGraphicsPipeline(m_rendertotexturepipeline);
    if (m_rendertotexturepipeline_additive) m_gpu->releaseGraphicsPipeline(m_rendertotexturepipeline_additive);
    if (rtt_vertex_shader)                  m_gpu->releaseShader(rtt_vertex_shader);
    if (rtt_fragment_shader)                m_gpu->releaseShader(rtt_fragment_shader);
    m_rendertotexturepipeline = m_rendertotexturepipeline_additive = 0;
    rtt_vertex_shader = rtt_fragment_shader = 0;

    Shaders::Quit();

    AssetHandler::Cleanup();

    Geometry2DFactory::ReleaseAll();
    m_gpu->shutdown();
    m_device = nullptr;  // SDL: ownership in SdlGpuBackend::shutdown(); WebGPU: always null

    m_gpu.reset();
    m_cmdbuf = 0;
    swapchain_texture = 0;

    LOG_INFO("Renderer closed");
}


void Renderer::_updateCameraProjection() {
    // In WebGpuScaleMode::Native the canvas may differ from Window::GetWidth/Height
    // (browser layout drives canvas size). Prefer the live canvas dimensions when
    // available so logical coords still cover the visible area.
    if (Window::GetWebGpuScaleMode() == WebGpuScaleMode::Native &&
        m_canvasWidth > 0 && m_canvasHeight > 0) {
        // The integer window scale (Window::SetScale) is independent of the HiDPI scale
        // modes: it zooms the logical canvas. Divide the physical canvas by the scale
        // factor so logical coords still map across the visible area (web: scale==1, no-op).
        const float s = Window::GetScale() > 0.0f ? Window::GetScale() : 1.0f;
        m_camera = glm::ortho(0.0f, (float)m_canvasWidth / s, (float)m_canvasHeight / s, 0.0f);
        return;
    }
    m_camera = glm::ortho(0.0f, (float)Window::GetWidth(), (float)Window::GetHeight(), 0.0f);
}

void Renderer::_onResize() {
    // Fast resize path. The framebuffer content + per-pass render targets are desktop-sized and
    // the render viewport is read per-frame, so they survive a resize untouched. Only the camera
    // projection and the (window-sized) MSAA textures actually depend on window size — recreate
    // just those. Crucially we do NOT release()/init() the passes, which would recompile every
    // pipeline (a Metal shader-compile storm) and re-upload all map geometry/textures for nothing.
    _updateCameraProjection();

    int w = Window::GetPhysicalWidth(), h = Window::GetPhysicalHeight();
    if (w <= 0 || h <= 0) return;

    // Let each pass refresh its surface-sized targets (e.g. the 2D sprite layer's depth +
    // effect temps) so 2D content rescales — without recompiling pipelines or re-uploading geo.
    for (auto &[fbName, framebuffer] : frameBuffers)
        for (auto &[passname, renderpass] : framebuffer->renderpasses)
            renderpass->onResize((uint32_t)w, (uint32_t)h);

    if (currentSampleCount <= GpuSampleCount::x1) return;   // no MSAA targets -> nothing more
    GpuTextureFormat swapchainFmt = m_gpu->getSwapchainFormat();

    for (auto &[fbName, framebuffer] : frameBuffers) {
        if (framebuffer->noMSAA) continue;
        if (framebuffer->fbContentMSAA) { m_gpu->releaseTexture(framebuffer->fbContentMSAA); framebuffer->fbContentMSAA = 0; }
        if (framebuffer->fbDepthMSAA)   { m_gpu->releaseTexture(framebuffer->fbDepthMSAA);   framebuffer->fbDepthMSAA   = 0; }

        GpuTextureCreateInfo colorInfo{
            .width = (uint32_t)w, .height = (uint32_t)h, .depthOrLayers = 1, .numLevels = 1,
            .format = swapchainFmt, .sampleCount = currentSampleCount, .usage = GpuTextureUsage::ColorTarget,
        };
        framebuffer->fbContentMSAA = m_gpu->createTexture(colorInfo);
        GpuTextureCreateInfo depthInfo{
            .width = (uint32_t)w, .height = (uint32_t)h, .depthOrLayers = 1, .numLevels = 1,
            .format = GpuTextureFormat::D32_Float, .sampleCount = currentSampleCount, .usage = GpuTextureUsage::DepthStencilTarget,
        };
        framebuffer->fbDepthMSAA = m_gpu->createTexture(depthInfo);
    }
    m_gpu->waitIdle();
}

void Renderer::_clearBackground(Color color) {
    LUMI_UNUSED(color);
    // can ignore this for now
}

void Renderer::_startFrame() const {
    if (!m_gpu) return;

#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::NewFrame();
#endif
}

void Renderer::_endFrame() {
    if (!m_gpu) return;

    Draw::FlushPixels();
    Input::GetVirtualControls().Render();
    m_gpu->processPendingScreenshots();

    // ── Acquire command buffer and swapchain (IGpu interface) ─────────────────
    m_cmdbuf = m_gpu->acquireCommandBuffer();
    if (!m_cmdbuf) {
        LOG_WARNING("Failed to acquire GPU command buffer");
#ifdef LUMINOVEAU_WITH_IMGUI
        ImGuiIntegration::EndFrame();
#endif
        return;
    }

    uint32_t scWidth = 0, scHeight = 0;
    swapchain_texture = m_gpu->acquireSwapchainTexture(m_cmdbuf, scWidth, scHeight);
    if (scWidth > 0 && scHeight > 0) {
        // Authoritative present size -> Window::GetPhysicalWidth/Height. Drives viewport + blit
        // UV so they match the swapchain even when SDL_GetWindowSizeInPixels lies (Wayland
        // fractional scaling), which otherwise renders the scene bigger than the window.
        EngineState::_swapchainWidth  = (int)scWidth;
        EngineState::_swapchainHeight = (int)scHeight;
    }
    if (scWidth > 0 && scHeight > 0 && (scWidth != m_canvasWidth || scHeight != m_canvasHeight)) {
        m_canvasWidth  = scWidth;
        m_canvasHeight = scHeight;
        // The swapchain can change size WITHOUT an SDL window-resize event — most importantly on
        // the first frame, when a HiDPI window realizes its true physical drawable (e.g. a 1280x720
        // logical window backed by a 2560x1440 Metal layer). Run the full resize path, not just the
        // camera: passes that cap their viewport to m_surface_width (SpriteRenderPass, used for the
        // HUD/console layer) would otherwise stay pinned to the pre-realization size and fill only
        // part of the screen, while passes that read GetPhysicalWidth live (the 3D world) fill it.
        // _onResize() reads GetPhysicalWidth/Height, which == the swapchain size set just above.
        _onResize();
    }

    if (!swapchain_texture) {
        Compute::_Reset();
        Draw::ResetEffectStore();
#ifdef LUMINOVEAU_WITH_IMGUI
        ImGuiIntegration::EndFrame();
#endif
        m_gpu->submitCommandBuffer(m_cmdbuf);
        for (auto &[fbName, framebuffer]: frameBuffers) {
            for (auto &[passname, renderpass]: framebuffer->renderpasses) {
                renderpass->resetRenderQueue();
            }
        }
        m_cmdbuf = 0;
        return;
    }

    Draw::ReleaseFramePixelTextures();

    // ── MSAA-aware render-pass scheduling with pre/post compute split ──────────
    bool useMSAA = (currentSampleCount > GpuSampleCount::x1);

    auto runPasses = [&](bool preCompute) {
        for (auto &[fbName, framebuffer]: frameBuffers) {
            if (framebuffer->preComputeFlush != preCompute) continue;
            // Default: canvas-logical ortho via m_camera. fixedSize FBs (e.g., the LightToy
            // hrc_scene RT) instead use an ortho sized to their actual pixel dims so callers
            // can draw in the RT's native coord space — without this, an FB sized 1348×783
            // would receive sprites projected against the 1598-wide window camera, so a draw
            // at "scene x=1336" ends up at RT pixel ~1127 (the canvas-x→NDC→viewport chain
            // shifts the content left by canvas_w/fb_w).
            glm::mat4 fbCamera = framebuffer->fixedSize
                ? glm::ortho(0.0f, (float)framebuffer->width, (float)framebuffer->height, 0.0f)
                : m_camera;

            bool useThisMSAA = useMSAA && framebuffer->fbContentMSAA != 0;
            GpuTextureHandle renderTarget = useThisMSAA ? framebuffer->fbContentMSAA : framebuffer->fbContent;
            GpuTextureHandle depthTarget  = useThisMSAA ? framebuffer->fbDepthMSAA   : 0;
            for (size_t i = 0; i < framebuffer->renderpasses.size(); i++) {
                auto& [passname, renderpass] = framebuffer->renderpasses[i];
                if (i > 0) renderpass->color_target_info_loadop = GpuLoadOp::Load;
                renderpass->renderTargetDepth = depthTarget;
                bool isLastPass = (i == framebuffer->renderpasses.size() - 1);
                bool nextNeedsResolved = useThisMSAA && !isLastPass &&
                    framebuffer->renderpasses[i + 1].second->needsResolvedInput();
                renderpass->renderTargetResolve = (useThisMSAA && (isLastPass || nextNeedsResolved)) ? framebuffer->fbContent : 0;
                renderpass->render(m_cmdbuf, renderTarget, fbCamera);
            }
        }
    };

#ifdef LUMINOVEAU_WITH_RMLUI
    auto* sdlCmdBuf    = reinterpret_cast<SDL_GPUCommandBuffer*>(m_cmdbuf);
    auto* sdlSwapchain = reinterpret_cast<SDL_GPUTexture*>(swapchain_texture);
#endif

    runPasses(true);
    Particles::_PrepareFrame(m_cmdbuf);
    Compute::_ExecuteQueued(m_cmdbuf);
    Compute::_Reset();
    runPasses(false);

    // ── Blit framebuffer to swapchain (IGpu interface) ────────────────────────
    renderFrameBuffer(m_cmdbuf);

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
    ImGuiIntegration::RenderFrame(m_cmdbuf, swapchain_texture);
#endif

    if (Window::HasPendingScreenshot()) {
        std::string filename = Window::GetAndClearPendingScreenshot();
        if (filename.empty()) {
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            filename = Helpers::TextFormat("screenshot_%lld.png", timestamp);
        }
        if (!filename.ends_with(".png")) {
            if (filename.ends_with(".bmp")) filename = filename.substr(0, filename.length() - 4) + ".png";
            else                            filename += ".png";
        }
        uint32_t width  = (uint32_t)Window::GetPhysicalWidth();
        uint32_t height = (uint32_t)Window::GetPhysicalHeight();
        m_gpu->requestScreenshot(m_cmdbuf, swapchain_texture, width, height, filename);
    }

    // Offscreen framebuffer captures (queued via CaptureFramebuffer). Their passes
    // have already rendered into fbContent above, so stage the download now.
    for (auto& [fbName, filename] : m_pendingFbCaptures) {
        if (FrameBuffer* fb = _getFramebuffer(fbName); fb && fb->fbContent)
            m_gpu->requestScreenshot(m_cmdbuf, fb->fbContent, fb->width, fb->height, filename);
    }
    m_pendingFbCaptures.clear();

    // ── Submit and reset ──────────────────────────────────────────────────────
    // When the perf HUD is open, fence the final submit and wait for GPU completion to time
    // real GPU work. Near-free when vsync-bound (the CPU would idle for the GPU anyway); zero
    // cost when the HUD is hidden.
    if (Perf::Visible()) {
        auto t0 = std::chrono::high_resolution_clock::now();
        GpuFenceHandle fence = m_gpu->submitCommandBufferAndAcquireFence(m_cmdbuf);
        if (fence) {
            m_gpu->waitFence(fence);
            double ms = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::high_resolution_clock::now() - t0).count() / 1.0e6;
            Perf::ReportGPUms(ms);
            m_gpu->releaseFence(fence);
        }
    } else {
        m_gpu->submitCommandBuffer(m_cmdbuf);
    }
    m_gpu->presentSwapchain();

    // Increment the present counter — Window::GetFPS reads this over its caller-supplied
    // averaging window. Decoupled from _lastFrameTime so SDL_AppIterate's spin rate
    // doesn't poison the FPS report.
    EngineState::_presentCount++;

    // Real per-present frame time -> perf HUD graph (matches the present-based FPS).
    {
        static auto s_lastPresent = std::chrono::high_resolution_clock::now();
        static bool s_have = false;
        auto nowP = std::chrono::high_resolution_clock::now();
        if (s_have) {
            double dt = std::chrono::duration_cast<std::chrono::nanoseconds>(nowP - s_lastPresent).count() / 1.0e6;
            Perf::ReportFrameMs(dt);
        }
        s_lastPresent = nowP; s_have = true;
    }

    // Per-frame draw stats -> perf HUD.
    if (Perf::Visible()) Perf::ReportDraws(m_gpu->frameDrawCalls(), m_gpu->frameDrawVerts());
    m_gpu->resetFrameDrawStats();
    for (auto &[fbName, framebuffer]: frameBuffers) {
        for (auto &[passname, renderpass]: framebuffer->renderpasses) {
            renderpass->resetRenderQueue();
        }
    }
    Draw::ResetEffectStore();
    m_cmdbuf = 0;
}

void Renderer::_reset() {
    LOG_INFO("Resetting render passes with MSAA={}", static_cast<int>(currentSampleCount));

    // Render dimensions = current window in physical pixels. Render passes draw into
    // this many pixels (top-left region of each framebuffer's texture). The framebuffer
    // textures themselves are sized large enough at init (desktop) so they survive
    // window resizes / fullscreen toggles without ever being recreated.
    int rpWidth  = Window::GetPhysicalWidth();
    int rpHeight = Window::GetPhysicalHeight();
    if (rpWidth <= 0 || rpHeight <= 0) { rpWidth = 1280; rpHeight = 720; }

    bool useMSAA = (currentSampleCount > GpuSampleCount::x1);
    GpuTextureFormat swapchainFmt = m_gpu->getSwapchainFormat();

    // Recreate framebuffer MSAA textures if needed
    for (auto &[fbName, framebuffer]: frameBuffers) {
        // Release old MSAA textures
        if (framebuffer->fbContentMSAA) {
            m_gpu->releaseTexture(framebuffer->fbContentMSAA);
            framebuffer->fbContentMSAA = 0;
        }
        if (framebuffer->fbDepthMSAA) {
            m_gpu->releaseTexture(framebuffer->fbDepthMSAA);
            framebuffer->fbDepthMSAA = 0;
        }

        // Create new MSAA textures if MSAA is enabled and this framebuffer supports it.
        // Custom effect render targets (noMSAA=true) always render to plain 1x textures.
        if (useMSAA && !framebuffer->noMSAA) {
            GpuTextureCreateInfo msaaColorInfo{
                .width         = static_cast<uint32_t>(rpWidth),
                .height        = static_cast<uint32_t>(rpHeight),
                .depthOrLayers = 1, .numLevels = 1,
                .format        = swapchainFmt,
                .sampleCount   = currentSampleCount,
                .usage         = GpuTextureUsage::ColorTarget,
            };
            framebuffer->fbContentMSAA = m_gpu->createTexture(msaaColorInfo);

            GpuTextureCreateInfo msaaDepthInfo{
                .width         = static_cast<uint32_t>(rpWidth),
                .height        = static_cast<uint32_t>(rpHeight),
                .depthOrLayers = 1, .numLevels = 1,
                .format        = GpuTextureFormat::D32_Float,
                .sampleCount   = currentSampleCount,
                .usage         = GpuTextureUsage::DepthStencilTarget,
            };
            framebuffer->fbDepthMSAA = m_gpu->createTexture(msaaDepthInfo);
        }
    }

    for (auto &[fbName, framebuffer]: frameBuffers) {
        for (auto &[passname, renderpass]: framebuffer->renderpasses) {

            renderpass->release(false);

            m_gpu->waitIdle();
            bool initSuccess = renderpass->init(swapchainFmt,
                                  rpWidth, rpHeight,
                                  passname, true, 0, framebuffer->noMSAA);
            if (!initSuccess) {
                LOG_ERROR("Renderpass ({}) failed to init()", passname.c_str());
            }
        }
    }

    // Ensure all pipeline compilations are complete before rendering resumes.
    m_gpu->waitIdle();

    LOG_INFO("Reset complete");
}

void Renderer::_addToRenderQueue(const std::string &passname, const Renderable &renderable) {
    for (auto &[fbName, framebuffer]: frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
                               [&passname](const std::pair<std::string, RenderPass *> &entry) {
                                   return entry.first == passname;
                               });

        if (it != framebuffer->renderpasses.end()) {
            it->second->addToRenderQueue(renderable);
        }
    }
}

void Renderer::_addShaderPass(const std::string &passname, const ShaderAsset &vertShader, const ShaderAsset &fragShader,
                              std::vector<std::string> targetBuffers) {
    auto shaderPass = new ShaderRenderPass();
    shaderPass->vertShader = vertShader;
    shaderPass->fragShader = fragShader;

    uint32_t desktopWidth = 0, desktopHeight = 0;
    Window::GetDisplayBounds(desktopWidth, desktopHeight);

    bool succes = shaderPass->init(m_gpu->getSwapchainFormat(),
                                   desktopWidth, desktopHeight,
                                   passname);
    if (succes) {
        if (targetBuffers.empty()) {
            targetBuffers.emplace_back("primaryFramebuffer");
        }

        for (auto& buffername : targetBuffers) {
            auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(),
                                   [&buffername](const std::pair<std::string, FrameBuffer *> &entry) {
                                       return entry.first == buffername;
                                   });

            if (it != frameBuffers.end()) {
                it->second->renderpasses.emplace_back(passname, shaderPass);
            }
        }
    } else {
        LOG_ERROR("Failed to create shaderpass: {}", passname.c_str());
    }
}

void Renderer::_removeShaderPass(const std::string &passname) {
    bool found = false;
    RenderPass* passToDelete = nullptr;

    // Find and remove the pass from all framebuffers
    for (auto &[fbName, framebuffer]: frameBuffers) {
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
        passToDelete->release(true);  // Log the release
        delete passToDelete;
    }

    if (!found) {
        LOG_WARNING("Shader pass '{}' not found for removal", passname);
    }
}

UniformBuffer &Renderer::_getUniformBuffer(const std::string &passname) {
    for (auto &[fbName, framebuffer]: frameBuffers) {

        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
                               [&passname](const std::pair<std::string, RenderPass *> &entry) {
                                   return entry.first == passname;
                               });

        if (it != framebuffer->renderpasses.end()) {
            return it->second->getUniformBuffer();
        }
    }

    //this section of code should never be hit because every renderpass has a buffer attached to it
    assert(false && "UniformBuffer not found");
    static UniformBuffer dummyBuffer;
    return dummyBuffer;
}

void Renderer::renderFrameBuffer(GpuCmdBufferHandle cmdBuf) {

    auto *framebufferObj = Renderer::GetFramebuffer("primaryFramebuffer");

    struct UniformsPadded : Uniforms { float _pad[2] = {0.0f, 0.0f}; };
    UniformsPadded rtt_uniforms{};

    // No reset on window resize: the framebuffer is sized at init to handle any future
    // window size up to the full desktop. Render-pass viewports + the blit's partial-UV
    // sample pick up the new window dimensions per frame without recreating textures.
    {
        const float fbW = (float)framebufferObj->width;
        const float fbH = (float)framebufferObj->height;
        const float scW = (float)(m_canvasWidth  > 0 ? m_canvasWidth  : (uint32_t)fbW);
        const float scH = (float)(m_canvasHeight > 0 ? m_canvasHeight : (uint32_t)fbH);

        float blitX = 0.0f, blitY = 0.0f, blitW = scW, blitH = scH;
        float uvX0 = 0.0f,  uvY0 = 0.0f,  uvX1 = 1.0f, uvY1 = 1.0f;

        // UV defaults: top-left phys-window region of the (possibly larger) framebuffer.
        // For framebuffers that exactly match the window in physical pixels this is the
        // full [0,1] quad; for desktop-sized buffers it samples only the rendered region.
        {
            const float physW = (float)Window::GetPhysicalWidth();
            const float physH = (float)Window::GetPhysicalHeight();
            uvX1 = std::min(1.0f, physW / fbW);
            uvY1 = std::min(1.0f, physH / fbH);
        }

        switch (Window::GetWebGpuScaleMode()) {
        case WebGpuScaleMode::Contain: {
            const float scale = std::min(scW / fbW, scH / fbH);
            blitW = fbW * scale;
            blitH = fbH * scale;
            blitX = (scW - blitW) * 0.5f;
            blitY = (scH - blitH) * 0.5f;
            break;
        }
        case WebGpuScaleMode::Fill: {
            const float scale = std::max(scW / fbW, scH / fbH);
            const float visW  = scW / scale;
            const float visH  = scH / scale;
            uvX0 = (fbW - visW) * 0.5f / fbW;
            uvY0 = (fbH - visH) * 0.5f / fbH;
            uvX1 = uvX0 + visW / fbW;
            uvY1 = uvY0 + visH / fbH;
            break;
        }
        default: break; // Stretch / Native: keep the phys-window UV defaults above
        }

        // Store inverse blit transform for mouse coordinate remapping.
        // Formula: logical = offset + canvas * invScale
        m_blitInvScaleX      = 1.0f;
        m_blitInvScaleY      = 1.0f;
        m_blitLogicalOffsetX = 0.0f;
        m_blitLogicalOffsetY = 0.0f;
        switch (Window::GetWebGpuScaleMode()) {
        case WebGpuScaleMode::Contain: {
            const float sc = std::min(scW / fbW, scH / fbH);
            const float bX = (scW - fbW * sc) * 0.5f;
            const float bY = (scH - fbH * sc) * 0.5f;
            m_blitInvScaleX      = 1.0f / sc;
            m_blitInvScaleY      = 1.0f / sc;
            m_blitLogicalOffsetX = -bX / sc;
            m_blitLogicalOffsetY = -bY / sc;
            break;
        }
        case WebGpuScaleMode::Fill: {
            const float sc = std::max(scW / fbW, scH / fbH);
            m_blitInvScaleX      = 1.0f / sc;
            m_blitInvScaleY      = 1.0f / sc;
            m_blitLogicalOffsetX = (fbW - scW / sc) * 0.5f;
            m_blitLogicalOffsetY = (fbH - scH / sc) * 0.5f;
            break;
        }
        case WebGpuScaleMode::Stretch:
            m_blitInvScaleX = fbW / scW;
            m_blitInvScaleY = fbH / scH;
            break;
        case WebGpuScaleMode::Native:
            break;
        }

        // Blit always uses canvas-space ortho so modes work independently of m_camera
        rtt_uniforms.camera  = glm::ortho(0.0f, scW, scH, 0.0f);
        rtt_uniforms.model   = glm::mat4(
            blitW, 0.0f,  0.0f, 0.0f,
            0.0f,  blitH, 0.0f, 0.0f,
            0.0f,  0.0f,  1.0f, 0.0f,
            blitX, blitY, 0.0f, 1.0f
        );
        rtt_uniforms.flipped = glm::vec2(1.0f, 1.0f);
        // Vertex positions (from WGSL): (1,1),(0,1),(1,0),(0,1),(0,0),(1,0)
        rtt_uniforms.uv0 = glm::vec2(uvX1, uvY1);
        rtt_uniforms.uv1 = glm::vec2(uvX0, uvY1);
        rtt_uniforms.uv2 = glm::vec2(uvX1, uvY0);
        rtt_uniforms.uv3 = glm::vec2(uvX0, uvY1);
        rtt_uniforms.uv4 = glm::vec2(uvX0, uvY0);
        rtt_uniforms.uv5 = glm::vec2(uvX1, uvY0);
    }

    GpuColorTargetInfo colorTarget{
        .texture = swapchain_texture,
        .loadOp  = GpuLoadOp::Clear,
        .storeOp = GpuStoreOp::Store,
        .clearR  = 0.0f, .clearG = 0.0f, .clearB = 0.0f, .clearA = 1.0f,
    };

    auto renderPass = m_gpu->beginRenderPass(cmdBuf, &colorTarget, 1, nullptr);
    m_gpu->bindGraphicsPipeline(renderPass, m_rendertotexturepipeline);
    m_gpu->pushVertexUniformData(cmdBuf, 0, &rtt_uniforms, sizeof(rtt_uniforms));
    GpuTextureSamplerBinding binding{ framebufferObj->fbContent, Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode()) };
    m_gpu->bindFragmentSamplers(renderPass, 0, &binding, 1);
    m_gpu->drawPrimitives(renderPass, 6, 1, 0, 0);

    for (const auto& [fbName, fb] : frameBuffers) {
        if (fb->renderToScreen) {
            m_gpu->bindGraphicsPipeline(renderPass, fb->additiveBlend
                ? m_rendertotexturepipeline_additive : m_rendertotexturepipeline);
            m_gpu->pushVertexUniformData(cmdBuf, 0, &rtt_uniforms, sizeof(rtt_uniforms));
            GpuTextureSamplerBinding fbBinding{ fb->fbContent, Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode()) };
            m_gpu->bindFragmentSamplers(renderPass, 0, &fbBinding, 1);
            m_gpu->drawPrimitives(renderPass, 6, 1, 0, 0);
        }
    }

    m_gpu->endRenderPass(renderPass);
}

FrameBuffer *Renderer::_getFramebuffer(std::string fbname) {
    auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(), [&fbname](const auto &pair) {
        return pair.first == fbname;
    });

    FrameBuffer *framebuffer = nullptr;

    if (it != frameBuffers.end()) {
        framebuffer = it->second;
    }

    return framebuffer;
}

void Renderer::_createFrameBuffer(const std::string &fbname, uint32_t width, uint32_t height) {
    auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(), [&fbname](const auto &pair) {
        return pair.first == fbname;
    });

    if (it == frameBuffers.end()) {
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

        auto *framebuffer = new FrameBuffer;
        framebuffer->fbContent = AssetHandler::CreateEmptyTexture({(float)fbWidth, (float)fbHeight}).gpuTexture;
        framebuffer->width = fbWidth;
        framebuffer->height = fbHeight;
        framebuffer->fixedSize = (width > 0 && height > 0);
        frameBuffers.emplace_back(fbname, framebuffer);

        framebuffer->textureView.width  = fbWidth;
        framebuffer->textureView.height = fbHeight;
        framebuffer->textureView.gpuTexture = framebuffer->fbContent;
        framebuffer->textureView.gpuSampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode());

        LOG_INFO("Created framebuffer: {} ({}x{})", fbname.c_str(), fbWidth, fbHeight);
    }
}

void Renderer::_createFrameBuffer(const std::string &fbname, uint32_t width, uint32_t height, GpuTextureFormat format) {
    auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(), [&fbname](const auto &pair) {
        return pair.first == fbname;
    });

    if (it == frameBuffers.end()) {
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

        auto *framebuffer = new FrameBuffer;
        framebuffer->fbContent = AssetHandler::CreateEmptyTexture({(float)fbWidth, (float)fbHeight}, format).gpuTexture;
        framebuffer->width = fbWidth;
        framebuffer->height = fbHeight;
        framebuffer->fixedSize = (width > 0 && height > 0);
        frameBuffers.emplace_back(fbname, framebuffer);

        framebuffer->textureView.width  = fbWidth;
        framebuffer->textureView.height = fbHeight;
        framebuffer->textureView.gpuTexture = framebuffer->fbContent;
        framebuffer->textureView.gpuSampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode());

        LOG_INFO("Created framebuffer: {} ({}x{}, custom format)", fbname.c_str(), fbWidth, fbHeight);
    }
}

void Renderer::_setFramebufferRenderToScreen(const std::string& fbName, bool render) {
    auto* framebuffer = _getFramebuffer(fbName);
    if (framebuffer) {
        framebuffer->renderToScreen = render;
    } else {
        LOG_WARNING("Framebuffer not found: {}", fbName.c_str());
    }
}

void Renderer::_attachRenderPassToFrameBuffer(RenderPass *renderPass, const std::string &passname, const std::string &fbName) {
    auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(), [&fbName](const auto &pair) {
        return pair.first == fbName;
    });

    if (it != frameBuffers.end()) {
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

Geometry2D* Renderer::_getQuadGeometry() {
    return Geometry2DFactory::CreateQuad();
}

Geometry2D* Renderer::_getCircleGeometry(int segments) {
    return Geometry2DFactory::CreateCircle(segments);
}

Geometry2D* Renderer::_getRoundedRectGeometry(float cornerRadiusX, float cornerRadiusY, int cornerSegments) {
    return Geometry2DFactory::CreateRoundedRect(cornerRadiusX, cornerRadiusY, cornerSegments);
}

GpuRenderPassHandle Renderer::_getRenderPass(const std::string& passname) {

    GpuRenderPassHandle foundPass = 0;

    for (auto &[fbName, framebuffer]: frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
                               [&passname](const std::pair<std::string, RenderPass *> &entry) {
                                   return entry.first == passname;
                               });

        if (it != framebuffer->renderpasses.end()) {
            foundPass = it->second->render_pass;
        }
    }

    return foundPass;

}

RenderPass* Renderer::_findRenderPass(const std::string& passname) {
    for (auto &[fbName, framebuffer]: frameBuffers) {
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

void Renderer::_setScissorMode(const std::string& passname, const rectf& cliprect) {

        for (auto &[fbName, framebuffer]: frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
                               [&passname](const std::pair<std::string, RenderPass *> &entry) {
                                   return entry.first == passname;
                               });

        if (it != framebuffer->renderpasses.end()) {
                it->second->_scissorEnabled = true;
                it->second->_scissorX = static_cast<int32_t>(cliprect.x);
                it->second->_scissorY = static_cast<int32_t>(cliprect.y);
                it->second->_scissorW = static_cast<uint32_t>(cliprect.w);
                it->second->_scissorH = static_cast<uint32_t>(cliprect.h);

        }
    }

}

void Renderer::_setSampleCount(GpuSampleCount sampleCount) {
    currentSampleCount = sampleCount;

    _reset();
}

void Renderer::_createSpriteRenderTarget(const std::string& name, const SpriteRenderTargetConfig& config) {
    std::string framebufferName = name + "_framebuffer";
    if (config.format != GpuTextureFormat::Invalid)
        _createFrameBuffer(framebufferName, config.width, config.height, config.format);
    else
        _createFrameBuffer(framebufferName, config.width, config.height);
    _setFramebufferRenderToScreen(framebufferName, config.renderToScreen);

    // Custom render targets never use MSAA — intermediate effect buffers, plain 1x.
    auto* fb = _getFramebuffer(framebufferName);
    if (fb) {
        fb->noMSAA          = true;
        fb->preComputeFlush = config.preComputeFlush;
        if (config.renderToScreen && config.blendMode == BlendMode::Additive)
            fb->additiveBlend = true;
    }

    // Convert BlendMode enum to neutral blend state
    GpuColorTargetBlendState blendState{};
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

    auto* renderPass = new SpriteRenderPass();
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
        if (rpWidth <= 0 || rpHeight <= 0) { rpWidth = 1280; rpHeight = 720; }
    }

    GpuTextureFormat passFormat = (config.format != GpuTextureFormat::Invalid)
                                  ? config.format
                                  : m_gpu->getSwapchainFormat();
    renderPass->init(passFormat, rpWidth, rpHeight, name, true,
                     config.maxSprites, /*forceNoMSAA=*/true);

    renderPass->color_target_info_loadop = config.clearOnLoad ? GpuLoadOp::Clear : GpuLoadOp::Load;
    renderPass->color_target_clear_r = config.clearColor.r / 255.0f;
    renderPass->color_target_clear_g = config.clearColor.g / 255.0f;
    renderPass->color_target_clear_b = config.clearColor.b / 255.0f;
    renderPass->color_target_clear_a = config.clearColor.a / 255.0f;

    _attachRenderPassToFrameBuffer(renderPass, name, framebufferName);
    LOG_INFO("Created sprite render target: {}", name.c_str());
}

void Renderer::_removeSpriteRenderTarget(const std::string& name, bool removeFramebuffer) {
    std::string framebufferName = name + "_framebuffer";

    // Find and remove the render pass from the framebuffer
    auto fbIt = std::find_if(frameBuffers.begin(), frameBuffers.end(),
        [&framebufferName](const auto& pair) {
            return pair.first == framebufferName;
        });

    if (fbIt != frameBuffers.end()) {
        auto& renderpasses = fbIt->second->renderpasses;
        auto passIt = std::find_if(renderpasses.begin(), renderpasses.end(),
            [&name](const auto& pair) {
                return pair.first == name;
            });

        if (passIt != renderpasses.end()) {
            // Release GPU resources
            passIt->second->release();

            // Delete the render pass object
            delete passIt->second;

            // Remove from vector
            renderpasses.erase(passIt);

            LOG_INFO("Removed sprite render target: {}", name.c_str());
        }

        // Optionally remove the framebuffer if requested
        if (removeFramebuffer) {
            // Release framebuffer GPU textures
            if (fbIt->second->fbContent)     m_gpu->releaseTexture(fbIt->second->fbContent);
            if (fbIt->second->fbContentMSAA) m_gpu->releaseTexture(fbIt->second->fbContentMSAA);
            if (fbIt->second->fbDepthMSAA)   m_gpu->releaseTexture(fbIt->second->fbDepthMSAA);

            // Delete framebuffer object
            delete fbIt->second;

            // Remove from vector
            frameBuffers.erase(fbIt);

            LOG_INFO("Removed framebuffer: {}", framebufferName.c_str());
        }
    } else {
        LOG_WARNING("Sprite render target not found: {}", name.c_str());
    }
}
