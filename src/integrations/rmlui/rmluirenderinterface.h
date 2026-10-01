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
    /// `targetSampleable` says the target may also be read, which backdrop filters need.
    void BeginFrame(GpuCmdBufferHandle cmd, GpuTextureHandle target, uint32_t width,
                    uint32_t height, bool targetSampleable = false);

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

    // Clip masks, in a stencil buffer shared by every layer.
    void EnableClipMask(bool enable) override;
    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                          Rml::Vector2f translation) override;

    // Gradients, filters, layers.
    Rml::CompiledShaderHandle CompileShader(const Rml::String     &name,
                                            const Rml::Dictionary &parameters) override;
    void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry,
                      Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseShader(Rml::CompiledShaderHandle shader) override;

    Rml::CompiledFilterHandle CompileFilter(const Rml::String     &name,
                                            const Rml::Dictionary &parameters) override;
    void ReleaseFilter(Rml::CompiledFilterHandle filter) override;

    Rml::LayerHandle PushLayer() override;
    void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination,
                         Rml::BlendMode                         blend_mode,
                         Rml::Span<const Rml::CompiledFilterHandle> filters) override;
    void PopLayer() override;

    Rml::TextureHandle        SaveLayerAsTexture() override;
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;

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

    struct Shader;
    struct Filter;

    /// What RmlUi asked for, in the order it asked. Replayed by `EndFrame`.
    struct Command {
        enum class Kind {
            Draw, Scissor, ScissorEnable, Transform,
            PushLayer, PopLayer, Composite, SaveTexture, SaveMask,
            ClipEnable, ClipDraw,
        } kind = Kind::Draw;

        // ClipDraw
        Rml::ClipMaskOperation clipOperation = Rml::ClipMaskOperation::Set;

        // Draw; `shader` set means a gradient rather than a texture
        Geometry     *geometry    = nullptr;
        Rml::Vector2f translation{0.0f, 0.0f};
        GpuTextureHandle texture  = 0;
        const Shader    *shader   = nullptr;

        // Composite
        int                                    source      = 0;
        int                                    destination = 0;
        bool                                   replace     = false;
        std::vector<Rml::CompiledFilterHandle> filters;

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

    /// (Re)creates the layer and scratch textures at the target's size.
    void ensureSurfaces();
    void releaseSurfaces();

    /// Layer `index`: 0 is the target itself, so a backdrop filter sees what is under the UI.
    GpuTextureHandle layerTexture(int index);

    /// One fullscreen triangle from `source` into `destination`. Opens and closes its own pass.
    struct PostParams;
    /// `clipMask` attaches the stencil and tests it: only for a draw into a UI layer.
    void postPass(GpuTextureHandle destination, uint32_t width, uint32_t height,
                  GpuTextureHandle source, GpuTextureHandle mask, bool blend,
                  const PostParams &params, const float uvTransform[4], bool clipped,
                  bool clipMask = false);

    /// A scratch surface at the target's size, created on first use.
    GpuTextureHandle surface(GpuTextureHandle &slot);

    void runComposite(const Command &command);
    void runBlur(float sigma, GpuTextureHandle image, GpuTextureHandle scratch);

    /// The scissor as a pixel rectangle clamped to the target, or the whole target.
    void clippedRect(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const;

    /// The projection RmlUi's coordinates are drawn through, rebuilt when the target resizes.
    void updateProjection();

    GpuGraphicsPipelineHandle _texturedPipeline = 0;
    GpuGraphicsPipelineHandle _colorPipeline    = 0;
    GpuGraphicsPipelineHandle _gradientPipeline = 0;
    GpuGraphicsPipelineHandle _postReplace      = 0;
    GpuGraphicsPipelineHandle _postBlend        = 0;
    GpuSamplerHandle          _sampler          = 0;

    // The same two drawing pipelines, testing the clip mask; and the two that write it.
    GpuGraphicsPipelineHandle _texturedClipped = 0;
    GpuGraphicsPipelineHandle _gradientClipped = 0;
    GpuGraphicsPipelineHandle _maskReplace     = 0;
    GpuGraphicsPipelineHandle _maskIncrement   = 0;
    GpuGraphicsPipelineHandle _postBlendClipped   = 0;
    GpuGraphicsPipelineHandle _postReplaceClipped = 0;

    /// False when the device offers neither depth-stencil format; clipping is then off.
    bool             _hasStencil     = false;
    GpuTextureFormat _stencilFormat  = GpuTextureFormat::D24_Unorm_S8_Uint;
    GpuTextureHandle _stencilTexture = 0;

    // Replay: whether draws test the mask, and the value they test for.
    bool    _clipEnabled = false;
    uint8_t _stencilRef  = 1;

    /// Picks the depth-stencil format, by trying to create one.
    void probeStencilFormat();

    /// Releases between `BeginFrame` and the end of `EndFrame` wait until the replay is done.
    /// RmlUi frees transient geometry and filters straight after the call that used them, and the
    /// recorded commands still point at them.
    bool                          _inFrame = false;
    std::vector<Geometry *>       _deferredGeometry;
    std::vector<GpuTextureHandle> _deferredTextures;
    std::vector<Filter *>         _deferredFilters;
    std::vector<Shader *>         _deferredShaders;

    void destroyGeometry(Geometry *geometry);
    void destroyTexture(GpuTextureHandle texture);
    void flushDeferred();

    /// Layers above the base, then three scratch images for filters, then the mask image.
    std::vector<GpuTextureHandle> _layers;
    GpuTextureHandle              _scratch[3]     = {0, 0, 0};
    GpuTextureHandle              _maskTexture    = 0;
    uint32_t                      _surfaceWidth   = 0;
    uint32_t                      _surfaceHeight  = 0;
    GpuTextureFormat              _format         = GpuTextureFormat::R8G8B8A8_Unorm;
    bool                          _targetSampleable = false;

    /// Replay state: the open pass, what is bound in it, and which layer it draws to.
    GpuRenderPassHandle       _pass          = 0;
    GpuGraphicsPipelineHandle _boundPipeline = 0;
    bool                      _scissorDirty  = true;
    int                       _top           = 0;

    /// `stencilClear` of -1 keeps the mask; anything else clears it to that value.
    void openPass(GpuTextureHandle target, bool clear, int stencilClear = -1);
    void closePass();
    void applyScissor();

    /// Layer depth and scissor as RmlUi sees them while it records, for the calls that answer at once.
    int             _recordDepth = 0;
    bool            _recordScissorOn = false;
    Rml::Rectanglei _recordScissor;

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
