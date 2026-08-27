/*
 * RmlUi render interface, written against the engine's own IGpu abstraction.
 *
 * **Replaces `RenderInterface_SDL_GPU`, rather than sitting beside it.** RmlUi ships backends for
 * OpenGL, Vulkan, DirectX and SDL_GPU, and none for WebGPU — so on the WebGPU backend the
 * integration had no renderer at all and `RmlUI::Backend::Initialize` bailed with "Invalid device
 * or window", taking every menu, the track editor and the piece lab with it.
 *
 * Writing a WebGPU-specific one would have made a third implementation of the same few hundred
 * lines. `IGpu` is already the engine's answer to "one renderer, two backends" — it is what the
 * game's own custom passes are written against — so this is written once against that and works
 * wherever the engine does. The SDL_GPU-shaped coupling in the backend's `Initialize(SDL_GPUDevice*,
 * SDL_Window*)` signature goes away with it.
 *
 * **Ported from RmlUi's own `RenderInterface_SDL_GPU`**, whose structure is worth keeping: SDL_GPU
 * and IGpu are close enough that the mapping is nearly one-to-one, and its two decisions both
 * matter here for the same reasons they did there.
 *
 *   - **Commands are recorded, not executed.** RmlUi calls into a render interface while it walks
 *     its own display list, which is not when a render pass is open. Geometry, scissor and
 *     transform changes are queued in submission order and replayed inside one pass in `EndFrame`.
 *   - **Vertex and index buffers come from a pool keyed on capacity.** A document re-renders every
 *     frame and would otherwise create and destroy a buffer per element per frame.
 */

#pragma once

#ifdef LUMINOVEAU_WITH_RMLUI

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>

#include "gpu/types.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace RmlUI {

class RenderInterface_Lumi : public Rml::RenderInterface {
public:
    RenderInterface_Lumi();
    ~RenderInterface_Lumi() override;

    /// Builds the pipelines and the sampler. Separate from the constructor so a failure is
    /// reportable — the GPU may not be up when the interface is constructed.
    bool Init(GpuTextureFormat targetFormat);

    void Shutdown();

    /// Opens a frame. `target` is what the UI draws onto and `width`/`height` size the
    /// orthographic projection, so the caller decides whether that is the swapchain or an
    /// offscreen texture — which is what lets `RmlUiRenderPass` put the UI inside the framebuffer.
    void BeginFrame(GpuCmdBufferHandle cmd, GpuTextureHandle target, uint32_t width,
                    uint32_t height);

    /// Replays everything RmlUi queued since `BeginFrame`, in one render pass.
    void EndFrame();

    // ── Rml::RenderInterface ─────────────────────────────────────────────────
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int>        indices) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions,
                                   const Rml::String &source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i              source_dimensions) override;
    void               ReleaseTexture(Rml::TextureHandle texture_handle) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    void SetTransform(const Rml::Matrix4f *new_transform) override;

private:
    /// One pooled buffer. `capacity` is in bytes; a request takes the smallest free buffer that
    /// fits and grows a new one when none does.
    struct Buffer {
        GpuBufferHandle         buffer   = 0;
        GpuTransferBufferHandle transfer = 0;
        uint32_t                capacity = 0;
        GpuBufferUsage          usage    = GpuBufferUsage::Vertex;
        bool                    inUse    = false;
    };

    /// A compiled mesh: the handle RmlUi holds on to between frames.
    struct Geometry {
        Buffer  *vertices   = nullptr;
        Buffer  *indices    = nullptr;
        uint32_t indexCount = 0;
    };

    /// What RmlUi asked for, in the order it asked. Replayed by `EndFrame`.
    struct Command {
        enum class Kind { Draw, Scissor, ScissorEnable, Transform } kind = Kind::Draw;

        // Draw
        Geometry     *geometry    = nullptr;
        Rml::Vector2f translation{0.0f, 0.0f};
        GpuTextureHandle texture  = 0;

        // Scissor
        int32_t x = 0, y = 0;
        int32_t w = 0, h = 0;
        bool    enable = false;

        // Transform
        Rml::Matrix4f transform;
    };

    Buffer *requestBuffer(uint32_t capacity, GpuBufferUsage usage);
    void    releaseBuffers();
    bool    createPipelines(GpuTextureFormat targetFormat);

    /// The projection RmlUi's coordinates are drawn through, rebuilt when the target resizes.
    void updateProjection();

    GpuGraphicsPipelineHandle _texturedPipeline = 0;
    GpuGraphicsPipelineHandle _colorPipeline    = 0;
    GpuSamplerHandle          _sampler          = 0;

    /// A 1x1 opaque white texel, bound when geometry has no texture of its own.
    ///
    /// **Cheaper than a second pipeline switch.** The colour-only path could bind nothing and use
    /// a shader without a sampler, but then every alternation between textured and untextured
    /// geometry — which is most of a document, text against panels — rebinds a pipeline. One
    /// texture that multiplies to identity keeps the whole frame on one pipeline.
    GpuTextureHandle _whiteTexture = 0;

    GpuCmdBufferHandle _cmd    = 0;
    GpuTextureHandle   _target = 0;
    uint32_t           _width  = 0;
    uint32_t           _height = 0;

    Rml::Matrix4f _projection;
    Rml::Matrix4f _transform;
    bool          _hasTransform = false;

    bool    _scissorEnabled = false;
    int32_t _scissorX = 0, _scissorY = 0, _scissorW = 0, _scissorH = 0;

    std::vector<Command>                   _commands;
    std::vector<std::unique_ptr<Buffer>>   _buffers;
    std::vector<std::unique_ptr<Geometry>> _geometries;
    std::vector<GpuTextureHandle>          _textures;

    bool _ready = false;
};

} // namespace RmlUI

#endif // LUMINOVEAU_WITH_RMLUI
