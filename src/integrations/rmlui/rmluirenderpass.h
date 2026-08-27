#pragma once

#include "core/log/log.h"
#include "gpu/renderpass.h"
#include "integrations/rmlui/rmlui.h"
#include "integrations/rmlui/rmluibackend.h"

/**
 * @brief Draws the RmlUi contexts as an ordinary render pass.
 *
 * **Why this exists: so the UI can be post-processed.** The renderer's own end-of-frame path draws
 * RmlUi straight to the swapchain *after* `_renderFrameBuffer` has already blitted, which puts the
 * UI outside the framebuffer. Nothing in the framebuffer's pass list can see it — there is no
 * position a post-process could take, because at that point the UI has not been drawn yet.
 *
 * As a pass it joins the list, and its order becomes ordinary business for
 * `Renderer::InsertRenderPassIntoFrameBuffer`. A `ShaderRenderPass` placed after it post-processes
 * the UI along with everything else.
 *
 * **Opt-in and additive.** Installing it sets `RmlUI::SetRenderedByPass(true)`, which is the only
 * thing that makes the renderer skip its own draw, so exactly one of the two runs and a project
 * that never installs this sees no change at all.
 *
 * Usage:
 * @code
 * auto *ui = new RmlUIRenderPass();
 * ui->Init(Renderer::GetGpu().GetSwapchainFormat(), w, h, "rmlui");
 * Renderer::AttachRenderPassToFrameBuffer(ui, "rmlui", "primaryFramebuffer");
 * // then attach any post-process after it
 * @endcode
 */
class RmlUIRenderPass : public RenderPass {
public:
    bool Init(GpuTextureFormat /*swapchainFormat*/, uint32_t surfaceWidth, uint32_t surfaceHeight,
        std::string name, bool logInit = true, size_t /*capacity*/ = 0,
        bool /*forceNoMSAA*/ = false) override {
        _passname = std::move(name);
        _width    = surfaceWidth;
        _height   = surfaceHeight;

        // The UI is drawn on top of whatever the passes below it produced.
        colorTargetInfoLoadOp = GpuLoadOp::Load;

        RmlUI::SetRenderedByPass(true);

        if (logInit) {
            LOG_INFO("RmlUIRenderPass '{}' initialised at {}x{}", _passname, _width, _height);
        }
        return true;
    }

    void Release(bool logRelease = true) override {
        RmlUI::SetRenderedByPass(false);
        if (logRelease) {
            LOG_INFO("RmlUIRenderPass '{}' released", _passname);
        }
    }

    void Render(GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture,
        const glm::mat4 & /*camera*/) override {
        if (targetTexture == 0) {
            return;
        }

        // **The target's size, not the window's, and that is the whole trick.**
        //
        // `RenderInterface_SDL_GPU::BeginFrame` builds its orthographic projection straight from
        // the width and height given here — `ProjectOrtho(0, width, height, 0, ...)` — so those
        // decide how UI pixels map onto the target. The primary framebuffer is *desktop*-sized and
        // passes draw into its top-left window-sized region, with the blit sampling only that
        // region. Handing over the framebuffer's dimensions therefore lands a UI laid out in
        // window pixels exactly in that region, at 1:1.
        //
        // Passing the window size instead would stretch the UI across the whole desktop-sized
        // texture, and the blit would then show a magnified corner of it.
        RmlUI::Backend::BeginFrame(cmdBuffer, targetTexture, _width, _height);

        RmlUI::Render();
        RmlUI::Backend::EndFrame();
    }

    void OnResize(uint32_t surfaceWidth, uint32_t surfaceHeight) override {
        _width  = surfaceWidth;
        _height = surfaceHeight;
    }

    /// RmlUi keeps its own geometry; nothing is queued through the renderable path.
    void AddToRenderQueue(const Renderable & /*renderable*/) override { }

    /// Nothing is queued, so an abandoned frame has nothing to drop.
    void ResetRenderQueue() override { }

    UniformBuffer &GetUniformBuffer() override { return _uniformBuffer; }

    /// The UI has to composite over the scene rather than over a multisampled copy of it, so it
    /// wants the resolved texture when MSAA is on.
    bool NeedsResolvedInput() const override { return true; }

private:
    uint32_t      _width  = 0;
    uint32_t      _height = 0;
    UniformBuffer _uniformBuffer;
};
