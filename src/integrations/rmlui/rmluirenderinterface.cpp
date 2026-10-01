#ifdef LUMINOVEAU_WITH_RMLUI

#include "integrations/rmlui/rmluirenderinterface.h"

#include "assets/assethandler.h"
#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "gpu/presets.h"
#include "renderer/renderer.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Math.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace RmlUI {

namespace {

constexpr const char *kVertShader     = "assets/shaders/rmlui.vert";
constexpr const char *kFragShader     = "assets/shaders/rmlui.frag";
constexpr const char *kGradientShader = "assets/shaders/rmlui_gradient.frag";
constexpr const char *kPostVertShader = "assets/shaders/rmlui_post.vert";
constexpr const char *kPostFragShader = "assets/shaders/rmlui_post.frag";

constexpr int kMaxGradientStops = 16;

/// Mirrors `GradientParams` in rmlui_gradient.frag.
struct GradientParams {
    float   stopColors[kMaxGradientStops][4];
    float   stopPositions[kMaxGradientStops];
    float   p[2];
    float   v[2];
    int32_t func;
    int32_t numStops;
    float   pad[2];
};

/// Mirrors the `MODE_` defines in rmlui_post.frag.
enum PostMode : int32_t { PostCopy = 0, PostColorMatrix = 1, PostMask = 2, PostBlur = 3, PostShadow = 4 };

constexpr float kIdentityUv[4] = {1.0f, 1.0f, 0.0f, 0.0f};

void CopyMatrix(float out[16], const Rml::Matrix4f &m) {
    // The shader wants column-major; RmlUi's default already is.
    if constexpr (std::is_same_v<Rml::Matrix4f, Rml::RowMajorMatrix4f>) {
        std::memcpy(out, m.Transpose().data(), sizeof(float) * 16);
    } else {
        std::memcpy(out, m.data(), sizeof(float) * 16);
    }
}

/// `Rml::Vertex` is a 2D position, a packed premultiplied RGBA8 colour and a texture coordinate.
/// Twenty bytes, and the layout is fixed by RmlUi — the offsets below are it, not a choice.
constexpr uint32_t kVertexStride = sizeof(Rml::Vertex);

/// Mirrors `VertexParams` in rmlui.vert: a combined matrix then a translation, padded to a
/// sixteen-byte boundary because that is what std140 does to a trailing vec2.
struct VertexParams {
    float transform[16];
    float translation[2];
    float pad[2];
};

} // namespace

/// Mirrors `PostParams` in rmlui_post.frag.
struct RenderInterface_Lumi::PostParams {
    float   colorMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float   color[4]        = {0, 0, 0, 0};
    float   region[4]       = {0, 0, 1, 1};
    float   texelStep[2]    = {0, 0};
    float   factor          = 1.0f;
    float   sigma           = 1.0f;
    int32_t mode            = PostCopy;
    int32_t taps            = 0;
    float   pad[2]          = {0, 0};
};

struct RenderInterface_Lumi::Shader {
    GradientParams params{};
};

struct RenderInterface_Lumi::Filter {
    enum class Type { Opacity, Blur, DropShadow, ColorMatrix, Mask } type = Type::Opacity;
    float         value = 1.0f;
    float         sigma = 0.0f;
    Rml::Vector2f offset{0.0f, 0.0f};
    float         color[4] = {0, 0, 0, 0};
    float         matrix[16] = {};
};

RenderInterface_Lumi::RenderInterface_Lumi() {
    _projection = Rml::Matrix4f::Identity();
    _transform  = Rml::Matrix4f::Identity();
}

RenderInterface_Lumi::~RenderInterface_Lumi() { Shutdown(); }

bool RenderInterface_Lumi::Init(GpuTextureFormat targetFormat) {
    if (_ready) return true;

    IGpu &gpu = Renderer::GetGpu();

    _format = targetFormat;
    probeStencilFormat();
    if (!createPipelines(targetFormat)) return false;

    GpuSamplerCreateInfo sampler{};
    // Linear, clamped. RmlUi's font atlas is sampled at whatever scale the document asks for and
    // a nearest filter makes text crawl; clamping stops a glyph bleeding into its neighbour at
    // the atlas edge.
    sampler.minFilter = GpuFilter::Linear;
    sampler.magFilter = GpuFilter::Linear;
    sampler.mipFilter = GpuFilter::Linear;
    sampler.addressU  = GpuSamplerAddressMode::ClampToEdge;
    sampler.addressV  = GpuSamplerAddressMode::ClampToEdge;

    _sampler = gpu.CreateSampler(sampler);
    if (_sampler == 0) {
        LOG_ERROR("RmlUI renderer: sampler creation failed");
        return false;
    }

    // The stand-in for untextured geometry. Opaque white, so a multiply leaves the vertex colour
    // exactly as it was — see the note in the header on why this beats a second pipeline.
    const uint8_t white[4] = {255, 255, 255, 255};

    GpuTextureCreateInfo texInfo{};
    texInfo.width  = 1;
    texInfo.height = 1;
    texInfo.format = GpuTextureFormat::R8G8B8A8_Unorm;

    // `Transfer` alongside `Sampler`, because this is uploaded into. SDL_gpu infers that from the
    // copy itself; WebGPU wants `CopyDst` declared up front and rejects the write otherwise —
    // "Usage (TextureBinding) ... does not include TextureUsage::CopyDst".
    texInfo.usage = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer;
    _whiteTexture = gpu.CreateTexture(texInfo);

    if (_whiteTexture == 0) {
        LOG_ERROR("RmlUI renderer: white texture creation failed");
        return false;
    }

    GpuTransferBufferCreateInfo staging{};
    staging.size  = sizeof(white);
    staging.usage = GpuTransferUsage::Upload;

    GpuTransferBufferHandle transfer = gpu.CreateTransferBuffer(staging);
    std::memcpy(gpu.MapTransferBuffer(transfer, false), white, sizeof(white));
    gpu.UnmapTransferBuffer(transfer);

    GpuTransferBufferRegion src{};
    src.transferBuffer = transfer;
    src.pixelsPerRow   = 1;
    src.rowsPerLayer   = 1;

    GpuTextureRegion dst{};
    dst.texture = _whiteTexture;
    dst.width   = 1;
    dst.height  = 1;
    dst.depth   = 1;

    GpuCmdBufferHandle cmd = gpu.AcquireCommandBuffer();
    gpu.UploadToTexture(cmd, src, dst);
    gpu.SubmitCommandBuffer(cmd);

    // This one runs before the first frame, so a wait would be harmless — dropped anyway so all
    // three uploads in this file follow the same rule and none of them can drift back.
    gpu.ReleaseTransferBuffer(transfer);

    _ready = true;
    LOG_INFO("RmlUI renderer ready (IGpu)");
    return true;
}

bool RenderInterface_Lumi::createPipelines(GpuTextureFormat targetFormat) {
    IGpu &gpu = Renderer::GetGpu();

    ShaderAsset &vert = AssetHandler::GetShader(kVertShader);
    ShaderAsset &frag = AssetHandler::GetShader(kFragShader);

    if (!vert.gpuShader || !frag.gpuShader) {
        LOG_ERROR("RmlUI renderer: could not load {} / {}", kVertShader, kFragShader);
        return false;
    }

    // Fixed by `Rml::Vertex`; see kVertexStride.
    GpuVertexAttribute attributes[3] = {
        {0, 0, GpuVertexElementFormat::Float2, offsetof(Rml::Vertex, position)},
        {1, 0, GpuVertexElementFormat::UByte4Norm, offsetof(Rml::Vertex, colour)},
        {2, 0, GpuVertexElementFormat::Float2, offsetof(Rml::Vertex, tex_coord)},
    };
    GpuVertexBinding binding{0, kVertexStride, false};

    GpuGraphicsPipelineCreateInfo info{};
    info.vertexShader     = vert.gpuShader;
    info.fragmentShader   = frag.gpuShader;
    info.attributes       = attributes;
    info.attributeCount   = 3;
    info.bindings         = &binding;
    info.bindingCount     = 1;
    info.fillMode         = GpuFillMode::Fill;

    // No culling: RmlUi emits both windings depending on how a transform mirrors an element, and
    // a culled UI drops half of a rotated panel.
    info.cullMode         = GpuCullMode::None;
    info.frontFace        = GpuFrontFace::CounterClockwise;
    info.colorTargetFormat = targetFormat;

    // RmlUi's colours and textures are both premultiplied — `Rml::Vertex::colour` says so — so
    // the source is added whole rather than scaled by its own alpha again.
    info.blend            = GpuPresets::PremultipliedAlpha;

    // The UI draws over a finished frame and has no depth of its own. Ordering is RmlUi's, in
    // submission order, which is exactly what a depth test would break.
    info.hasDepthTarget   = false;
    info.sampleCount      = GpuSampleCount::X1;

    // Filter passes draw into their own scratch targets, which have no stencil attached.
    ShaderAsset &postVert = AssetHandler::GetShader(kPostVertShader);
    ShaderAsset &postFrag = AssetHandler::GetShader(kPostFragShader);
    const bool   hasPost  = postVert.gpuShader && postFrag.gpuShader;

    GpuGraphicsPipelineCreateInfo post = info;
    post.vertexShader   = postVert.gpuShader;
    post.fragmentShader = postFrag.gpuShader;
    post.attributes     = nullptr;
    post.attributeCount = 0;
    post.bindings       = nullptr;
    post.bindingCount   = 0;

    if (hasPost) {
        _postBlend = gpu.CreateGraphicsPipeline(post);

        GpuGraphicsPipelineCreateInfo replace = post;
        replace.blend = GpuColorTargetBlendState{};
        _postReplace  = gpu.CreateGraphicsPipeline(replace);
    }

    // Every UI pass carries the clip mask, so every pipeline drawn in one declares it: stencil
    // only, no depth test.
    if (_hasStencil) {
        info.hasDepthTarget    = true;
        info.depthTest         = false;
        info.depthWrite        = false;
        info.depthTargetFormat = _stencilFormat;
    }

    GpuStencilState testMask{};
    testMask.enabled   = true;
    testMask.compare   = GpuCompareOp::Equal;
    testMask.writeMask = 0;

    _texturedPipeline = gpu.CreateGraphicsPipeline(info);
    if (_texturedPipeline == 0) {
        LOG_ERROR("RmlUI renderer: pipeline creation failed");
        return false;
    }

    // One pipeline serves both cases; the field is kept so the distinction stays visible at the
    // call site and a colour-only pipeline can be reintroduced without touching the replay.
    _colorPipeline = _texturedPipeline;

    if (_hasStencil) {
        GpuGraphicsPipelineCreateInfo clipped = info;
        clipped.stencil  = testMask;
        _texturedClipped = gpu.CreateGraphicsPipeline(clipped);

        // Writing the mask leaves colour untouched: nothing of the source, all of the destination.
        GpuGraphicsPipelineCreateInfo mask = info;
        mask.blend.blendEnabled   = true;
        mask.blend.srcColorFactor = GpuBlendFactor::Zero;
        mask.blend.dstColorFactor = GpuBlendFactor::One;
        mask.blend.srcAlphaFactor = GpuBlendFactor::Zero;
        mask.blend.dstAlphaFactor = GpuBlendFactor::One;

        mask.stencil         = GpuStencilState{};
        mask.stencil.enabled = true;
        mask.stencil.compare = GpuCompareOp::Always;
        mask.stencil.passOp  = GpuStencilOp::Replace;
        _maskReplace         = gpu.CreateGraphicsPipeline(mask);

        mask.stencil.compare = GpuCompareOp::Equal;
        mask.stencil.passOp  = GpuStencilOp::IncrementClamp;
        _maskIncrement       = gpu.CreateGraphicsPipeline(mask);

        // A composite's last draw lands in a UI layer under whatever clip is active — which is how
        // a backdrop blur takes its element's shape rather than its bounding box.
        if (hasPost) {
            GpuGraphicsPipelineCreateInfo clippedPost = post;
            clippedPost.hasDepthTarget    = true;
            clippedPost.depthTest         = false;
            clippedPost.depthWrite        = false;
            clippedPost.depthTargetFormat = _stencilFormat;
            clippedPost.stencil           = testMask;
            _postBlendClipped             = gpu.CreateGraphicsPipeline(clippedPost);

            clippedPost.blend    = GpuColorTargetBlendState{};
            _postReplaceClipped  = gpu.CreateGraphicsPipeline(clippedPost);
        }
    }

    // The effect pipelines are optional: without them the UI still draws, only plainer.
    ShaderAsset &gradient = AssetHandler::GetShader(kGradientShader);
    if (gradient.gpuShader) {
        info.fragmentShader = gradient.gpuShader;
        _gradientPipeline   = gpu.CreateGraphicsPipeline(info);
        if (_hasStencil) {
            info.stencil     = testMask;
            _gradientClipped = gpu.CreateGraphicsPipeline(info);
        }
    }

    if (_gradientPipeline == 0) LOG_WARNING("RmlUI renderer: no gradient pipeline; gradients will not draw");
    if (_postBlend == 0 || _postReplace == 0) {
        LOG_WARNING("RmlUI renderer: no post pipelines; filters and layers will not draw");
    }
    if (_hasStencil && (_texturedClipped == 0 || _maskReplace == 0 || _maskIncrement == 0)) {
        // Built again without a stencil target, since passes and pipelines have to agree on it.
        LOG_WARNING("RmlUI renderer: no clip mask pipelines; clipping falls back to scissors");
        for (GpuGraphicsPipelineHandle *pipeline :
             {&_texturedPipeline, &_texturedClipped, &_gradientPipeline, &_gradientClipped,
              &_maskReplace, &_maskIncrement, &_postBlend, &_postReplace, &_postBlendClipped,
              &_postReplaceClipped}) {
            if (*pipeline != 0) gpu.ReleaseGraphicsPipeline(*pipeline);
            *pipeline = 0;
        }
        _hasStencil = false;
        return createPipelines(targetFormat);
    }
    return true;
}

void RenderInterface_Lumi::probeStencilFormat() {
    IGpu &gpu = Renderer::GetGpu();

    // Not every device has D24S8 (some Apple and AMD setups lack it); D32S8 is the fallback.
    for (GpuTextureFormat format : {GpuTextureFormat::D24_Unorm_S8_Uint, GpuTextureFormat::D32_Float_S8_Uint}) {
        GpuTextureCreateInfo info{};
        info.width  = 1;
        info.height = 1;
        info.format = format;
        info.usage  = GpuTextureUsage::DepthStencilTarget;

        if (const GpuTextureHandle probe = gpu.CreateTexture(info)) {
            gpu.ReleaseTexture(probe);
            _stencilFormat = format;
            _hasStencil    = true;
            return;
        }
    }

    _hasStencil = false;
    LOG_WARNING("RmlUI renderer: no depth-stencil format; clip masks disabled");
}

GpuTextureHandle RenderInterface_Lumi::surface(GpuTextureHandle &slot) {
    if (slot != 0) return slot;

    GpuTextureCreateInfo info{};
    info.width  = _surfaceWidth;
    info.height = _surfaceHeight;
    info.format = _format;
    info.usage  = GpuTextureUsage::ColorTarget | GpuTextureUsage::Sampler;
    slot        = Renderer::GetGpu().CreateTexture(info);
    if (slot == 0) LOG_ERROR("RmlUI renderer: could not create a {}x{} layer", _surfaceWidth, _surfaceHeight);
    return slot;
}

void RenderInterface_Lumi::ensureSurfaces() {
    if (_surfaceWidth == _width && _surfaceHeight == _height) return;
    releaseSurfaces();
    _surfaceWidth  = _width;
    _surfaceHeight = _height;

    if (_hasStencil && _width > 0 && _height > 0) {
        GpuTextureCreateInfo info{};
        info.width      = _width;
        info.height     = _height;
        info.format     = _stencilFormat;
        info.usage      = GpuTextureUsage::DepthStencilTarget;
        _stencilTexture = Renderer::GetGpu().CreateTexture(info);
    }
}

void RenderInterface_Lumi::releaseSurfaces() {
    IGpu &gpu = Renderer::GetGpu();
    for (GpuTextureHandle &texture : _layers) {
        if (texture != 0) gpu.ReleaseTexture(texture);
    }
    _layers.clear();
    for (GpuTextureHandle &texture : _scratch) {
        if (texture != 0) gpu.ReleaseTexture(texture);
        texture = 0;
    }
    if (_maskTexture != 0) gpu.ReleaseTexture(_maskTexture);
    _maskTexture = 0;
    if (_stencilTexture != 0) gpu.ReleaseTexture(_stencilTexture);
    _stencilTexture = 0;
}

GpuTextureHandle RenderInterface_Lumi::layerTexture(int index) {
    if (index <= 0) return _target;
    while ((int) _layers.size() < index) _layers.push_back(0);
    return surface(_layers[(size_t) index - 1]);
}

void RenderInterface_Lumi::Shutdown() {
    if (!_ready) return;

    IGpu &gpu = Renderer::GetGpu();

    flushDeferred();
    releaseBuffers();
    releaseSurfaces();
    _geometries.clear();

    for (GpuTextureHandle texture : _textures) {
        if (texture != 0) gpu.ReleaseTexture(texture);
    }
    _textures.clear();

    if (_whiteTexture != 0) gpu.ReleaseTexture(_whiteTexture);
    if (_sampler != 0) gpu.ReleaseSampler(_sampler);
    if (_texturedPipeline != 0) gpu.ReleaseGraphicsPipeline(_texturedPipeline);
    if (_gradientPipeline != 0) gpu.ReleaseGraphicsPipeline(_gradientPipeline);
    if (_postBlend != 0) gpu.ReleaseGraphicsPipeline(_postBlend);
    if (_postReplace != 0) gpu.ReleaseGraphicsPipeline(_postReplace);
    if (_texturedClipped != 0) gpu.ReleaseGraphicsPipeline(_texturedClipped);
    if (_gradientClipped != 0) gpu.ReleaseGraphicsPipeline(_gradientClipped);
    if (_maskReplace != 0) gpu.ReleaseGraphicsPipeline(_maskReplace);
    if (_maskIncrement != 0) gpu.ReleaseGraphicsPipeline(_maskIncrement);
    if (_postBlendClipped != 0) gpu.ReleaseGraphicsPipeline(_postBlendClipped);
    if (_postReplaceClipped != 0) gpu.ReleaseGraphicsPipeline(_postReplaceClipped);
    _postBlendClipped   = 0;
    _postReplaceClipped = 0;
    _texturedClipped = 0;
    _gradientClipped = 0;
    _maskReplace     = 0;
    _maskIncrement   = 0;

    _whiteTexture     = 0;
    _sampler          = 0;
    _texturedPipeline = 0;
    _gradientPipeline = 0;
    _postBlend        = 0;
    _postReplace      = 0;
    _surfaceWidth     = 0;
    _surfaceHeight    = 0;
    _colorPipeline    = 0;
    _ready            = false;
}

void RenderInterface_Lumi::releaseBuffers() {
    IGpu &gpu = Renderer::GetGpu();

    for (std::unique_ptr<Buffer> &buffer : _buffers) {
        if (buffer->buffer != 0) gpu.ReleaseBuffer(buffer->buffer);
        if (buffer->transfer != 0) gpu.ReleaseTransferBuffer(buffer->transfer);
    }
    _buffers.clear();
}

void RenderInterface_Lumi::updateProjection() {
    // RmlUi's origin is top-left with y running down, which is what `ProjectOrtho` produces when
    // top and bottom are given in that order. The depth range is wide and arbitrary: nothing here
    // tests depth, but a degenerate range makes the matrix singular.
    _projection = Rml::Matrix4f::ProjectOrtho(0.0f, static_cast<float>(_width),
                                              static_cast<float>(_height), 0.0f, -10000.0f,
                                              10000.0f);
}

void RenderInterface_Lumi::BeginFrame(GpuCmdBufferHandle cmd, GpuTextureHandle target,
                                      uint32_t width, uint32_t height, bool targetSampleable) {
    _cmd              = cmd;
    _target           = target;
    _width            = width;
    _height           = height;
    _targetSampleable = targetSampleable;
    _recordDepth      = 0;

    // A frame that was begun and never ended still owes its releases.
    flushDeferred();
    _inFrame = true;

    updateProjection();
    ensureSurfaces();

    // Cleared *first*: the two calls below queue commands of their own, and clearing afterwards
    // threw them away.
    _commands.clear();

    // Both reset per frame, because RmlUi only calls them when it wants a change — a scissor left
    // over from the previous frame would clip this one to a region nobody asked for.
    SetTransform(nullptr);
    EnableScissorRegion(false);

    // **Buffers are emphatically not freed here.** They belong to the `Geometry` that requested
    // them until `ReleaseGeometry` hands them back, and RmlUi keeps compiled geometry alive for
    // as long as the element does — that is the entire point of `CompileGeometry` returning a
    // handle rather than drawing immediately.
    //
    // Freeing them per frame meant the next `CompileGeometry` — one hover restyling one button is
    // enough — handed a live buffer to a second element, which overwrote it. The symptom was a
    // menu that rendered but wrong: a panel's fill drawn from its border's vertices, glyphs from
    // some other element's, everything shifted by however far apart the two happened to be.
}

RenderInterface_Lumi::Buffer *RenderInterface_Lumi::requestBuffer(uint32_t capacity,
                                                                 GpuBufferUsage usage) {
    // Smallest free buffer that fits, so a large one is not consumed by a small request and then
    // missing when the large one comes round again.
    Buffer *best = nullptr;
    for (std::unique_ptr<Buffer> &buffer : _buffers) {
        if (buffer->inUse || buffer->usage != usage || buffer->capacity < capacity) continue;
        if (best == nullptr || buffer->capacity < best->capacity) best = buffer.get();
    }

    if (best != nullptr) {
        best->inUse = true;
        return best;
    }

    IGpu &gpu = Renderer::GetGpu();

    auto created      = std::make_unique<Buffer>();
    created->capacity = capacity;
    created->usage    = usage;
    created->inUse    = true;

    GpuBufferCreateInfo info{};
    info.size        = capacity;
    info.usage       = usage;
    created->buffer  = gpu.CreateBuffer(info);

    GpuTransferBufferCreateInfo staging{};
    staging.size      = capacity;
    staging.usage     = GpuTransferUsage::Upload;
    created->transfer = gpu.CreateTransferBuffer(staging);

    if (created->buffer == 0 || created->transfer == 0) {
        LOG_ERROR("RmlUI renderer: could not allocate a {} byte buffer", capacity);
        return nullptr;
    }

    _buffers.push_back(std::move(created));
    return _buffers.back().get();
}

Rml::CompiledGeometryHandle RenderInterface_Lumi::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
    if (!_ready || vertices.empty() || indices.empty()) return 0;

    IGpu &gpu = Renderer::GetGpu();

    const auto vertexBytes = static_cast<uint32_t>(vertices.size() * sizeof(Rml::Vertex));
    const auto indexBytes  = static_cast<uint32_t>(indices.size() * sizeof(int));

    Buffer *vertexBuffer = requestBuffer(vertexBytes, GpuBufferUsage::Vertex);
    Buffer *indexBuffer  = requestBuffer(indexBytes, GpuBufferUsage::Index);
    if (vertexBuffer == nullptr || indexBuffer == nullptr) return 0;

    std::memcpy(gpu.MapTransferBuffer(vertexBuffer->transfer, true), vertices.data(), vertexBytes);
    gpu.UnmapTransferBuffer(vertexBuffer->transfer);

    std::memcpy(gpu.MapTransferBuffer(indexBuffer->transfer, true), indices.data(), indexBytes);
    gpu.UnmapTransferBuffer(indexBuffer->transfer);

    // Uploaded on its own command buffer rather than the frame's. `CompileGeometry` is called
    // while RmlUi walks its document, which is outside `BeginFrame`/`EndFrame` for anything
    // compiled once and reused — and a copy recorded into a command buffer that has already been
    // submitted goes nowhere.
    GpuCmdBufferHandle cmd = gpu.AcquireCommandBuffer();
    gpu.UploadToBuffer(cmd, vertexBuffer->transfer, 0, vertexBuffer->buffer, 0, vertexBytes);
    gpu.UploadToBuffer(cmd, indexBuffer->transfer, 0, indexBuffer->buffer, 0, indexBytes);
    gpu.SubmitCommandBuffer(cmd);

    // **No wait here, and on the web it must not be.** Submissions to one queue complete in the
    // order they were made, so this copy is done before the frame that draws from it — which is
    // why RmlUi's own backend does not fence either.
    //
    // A `WaitIdle` was here first, defensively. Under Emscripten it becomes an ASYNCIFY yield to
    // the browser's event loop, and the canvas texture handed out by `getCurrentTexture()` is
    // only valid until the end of the current task. Yielding mid-frame therefore expired the
    // swapchain texture the frame's own command buffer was still holding, and the frame died on
    // submit with "Destroyed texture ... used in a submit" — a failure with nothing whatsoever
    // to do with the geometry being uploaded.

    auto geometry        = std::make_unique<Geometry>();
    geometry->vertices   = vertexBuffer;
    geometry->indices    = indexBuffer;
    geometry->indexCount = static_cast<uint32_t>(indices.size());

    _geometries.push_back(std::move(geometry));
    return reinterpret_cast<Rml::CompiledGeometryHandle>(_geometries.back().get());
}

void RenderInterface_Lumi::ReleaseGeometry(Rml::CompiledGeometryHandle handle) {
    auto *geometry = reinterpret_cast<Geometry *>(handle);
    if (geometry == nullptr) return;

    if (_inFrame) {
        _deferredGeometry.push_back(geometry);
        return;
    }
    destroyGeometry(geometry);
}

void RenderInterface_Lumi::destroyGeometry(Geometry *geometry) {
    // The buffers go back to the pool rather than to the driver: another element will want the
    // same size next frame.
    if (geometry->vertices != nullptr) geometry->vertices->inUse = false;
    if (geometry->indices != nullptr) geometry->indices->inUse = false;

    _geometries.erase(std::remove_if(_geometries.begin(), _geometries.end(),
                                     [geometry](const std::unique_ptr<Geometry> &held) {
                                         return held.get() == geometry;
                                     }),
                      _geometries.end());
}

void RenderInterface_Lumi::RenderGeometry(Rml::CompiledGeometryHandle handle,
                                          Rml::Vector2f translation, Rml::TextureHandle texture) {
    auto *geometry = reinterpret_cast<Geometry *>(handle);
    if (geometry == nullptr) return;

    Command command;
    command.kind        = Command::Kind::Draw;
    command.geometry    = geometry;
    command.translation = translation;
    command.texture     = texture != 0 ? static_cast<GpuTextureHandle>(texture) : _whiteTexture;

    _commands.push_back(command);
}

Rml::TextureHandle RenderInterface_Lumi::LoadTexture(Rml::Vector2i     &texture_dimensions,
                                                     const Rml::String &source) {
    // Deliberately unsupported. Every image this engine's documents reference is served through
    // `RmlUI::SetRenderInterfaceHook` — the piece lab's generated thumbnails are the reason that
    // hook exists — and decoding a file here would need an image library this interface has no
    // business owning. A document that asks for one gets no texture and draws untextured, which
    // is visible rather than silent.
    LOG_WARNING("RmlUI renderer: LoadTexture('{}') is not supported; use the render-interface hook",
                source.c_str());
    texture_dimensions = {0, 0};
    return 0;
}

Rml::TextureHandle RenderInterface_Lumi::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                         Rml::Vector2i source_dimensions) {
    if (!_ready || source.empty()) return 0;

    IGpu &gpu = Renderer::GetGpu();

    const auto width  = static_cast<uint32_t>(source_dimensions.x);
    const auto height = static_cast<uint32_t>(source_dimensions.y);
    if (width == 0 || height == 0) return 0;

    GpuTextureCreateInfo info{};
    info.width  = width;
    info.height = height;
    info.format = GpuTextureFormat::R8G8B8A8_Unorm;

    // Sampled and uploaded into — see the note on the white texel in `Init`.
    info.usage = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer;

    GpuTextureHandle texture = gpu.CreateTexture(info);
    if (texture == 0) {
        LOG_ERROR("RmlUI renderer: could not create a {}x{} texture", width, height);
        return 0;
    }

    const auto bytes = static_cast<uint32_t>(width * height * 4);

    GpuTransferBufferCreateInfo staging{};
    staging.size  = bytes;
    staging.usage = GpuTransferUsage::Upload;

    GpuTransferBufferHandle transfer = gpu.CreateTransferBuffer(staging);
    std::memcpy(gpu.MapTransferBuffer(transfer, false), source.data(), bytes);
    gpu.UnmapTransferBuffer(transfer);

    GpuTransferBufferRegion src{};
    src.transferBuffer = transfer;
    src.pixelsPerRow   = width;
    src.rowsPerLayer   = height;

    GpuTextureRegion dst{};
    dst.texture = texture;
    dst.width   = width;
    dst.height  = height;
    dst.depth   = 1;

    GpuCmdBufferHandle cmd = gpu.AcquireCommandBuffer();
    gpu.UploadToTexture(cmd, src, dst);
    gpu.SubmitCommandBuffer(cmd);

    // Not waited on — see `CompileGeometry`. A font atlas is generated while a document loads,
    // which can be inside a frame, and yielding there expires the swapchain texture.
    gpu.ReleaseTransferBuffer(transfer);

    _textures.push_back(texture);
    return static_cast<Rml::TextureHandle>(texture);
}

void RenderInterface_Lumi::ReleaseTexture(Rml::TextureHandle texture_handle) {
    auto texture = static_cast<GpuTextureHandle>(texture_handle);
    if (texture == 0) return;

    if (_inFrame) {
        _deferredTextures.push_back(texture);
        return;
    }
    destroyTexture(texture);
}

void RenderInterface_Lumi::destroyTexture(GpuTextureHandle texture) {
    auto found = std::find(_textures.begin(), _textures.end(), texture);
    if (found == _textures.end()) return; // not ours — a hooked texture, owned elsewhere

    Renderer::GetGpu().ReleaseTexture(texture);
    _textures.erase(found);
}

void RenderInterface_Lumi::EnableScissorRegion(bool enable) {
    _recordScissorOn = enable;

    Command command;
    command.kind   = Command::Kind::ScissorEnable;
    command.enable = enable;
    _commands.push_back(command);
}

void RenderInterface_Lumi::SetScissorRegion(Rml::Rectanglei region) {
    _recordScissor = region;

    Command command;
    command.kind = Command::Kind::Scissor;
    command.x    = region.Left();
    command.y    = region.Top();
    command.w    = region.Width();
    command.h    = region.Height();
    _commands.push_back(command);
}

void RenderInterface_Lumi::SetTransform(const Rml::Matrix4f *new_transform) {
    Command command;
    command.kind      = Command::Kind::Transform;
    command.transform = new_transform != nullptr ? *new_transform : Rml::Matrix4f::Identity();
    command.enable    = new_transform != nullptr;
    _commands.push_back(command);
}

void RenderInterface_Lumi::EndFrame() {
    if (!_ready || _cmd == 0 || _target == 0) {
        _commands.clear();
        flushDeferred();
        return;
    }

    IGpu &gpu = Renderer::GetGpu();

    // Loaded, never cleared: the UI draws over a finished frame. The mask starts empty.
    _top         = 0;
    _clipEnabled = false;
    _stencilRef  = 1;
    openPass(_target, false, 0);

    for (const Command &command : _commands) {
        switch (command.kind) {
        case Command::Kind::ScissorEnable:
            _scissorEnabled = command.enable;
            _scissorDirty   = true;
            break;

        case Command::Kind::Scissor:
            _scissorX     = command.x;
            _scissorY     = command.y;
            _scissorW     = command.w;
            _scissorH     = command.h;
            _scissorDirty = true;
            break;

        case Command::Kind::Transform:
            _transform    = command.transform;
            _hasTransform = command.enable;
            break;

        case Command::Kind::ClipEnable:
            _clipEnabled = command.enable && _stencilTexture != 0;
            break;

        case Command::Kind::ClipDraw: {
            if (_stencilTexture == 0 || command.geometry == nullptr) break;

            // As RmlUi's GL3 renderer: Set clears to 0 and marks 1, SetInverse clears to 1 and
            // marks 0, both then passing where the mask is 1; Intersect bumps what already
            // passes and raises the bar by one.
            GpuGraphicsPipelineHandle pipeline = _maskReplace;
            uint8_t                   write    = 1;

            switch (command.clipOperation) {
            case Rml::ClipMaskOperation::Set:
            case Rml::ClipMaskOperation::SetInverse: {
                const bool inverse = command.clipOperation == Rml::ClipMaskOperation::SetInverse;
                closePass();
                _stencilRef = 1;
                openPass(layerTexture(_top), false, inverse ? 1 : 0);
                write = inverse ? 0 : 1;
                break;
            }
            case Rml::ClipMaskOperation::Intersect:
                pipeline = _maskIncrement;
                write    = _stencilRef;
                break;
            }

            gpu.BindGraphicsPipeline(_pass, pipeline);
            _boundPipeline = pipeline;
            applyScissor();
            gpu.SetStencilReference(_pass, write);

            const Rml::Matrix4f combined = _hasTransform ? (_projection * _transform) : _projection;
            VertexParams params{};
            std::memcpy(params.transform, combined.data(), sizeof(params.transform));
            params.translation[0] = command.translation.x;
            params.translation[1] = command.translation.y;
            gpu.PushVertexUniformData(_cmd, 0, &params, sizeof(params));

            GpuBufferBinding vertexBinding{command.geometry->vertices->buffer, 0};
            gpu.BindVertexBuffers(_pass, 0, &vertexBinding, 1);
            GpuBufferBinding indexBinding{command.geometry->indices->buffer, 0};
            gpu.BindIndexBuffer(_pass, indexBinding, false);
            GpuTextureSamplerBinding samplerBinding{_whiteTexture, _sampler};
            gpu.BindFragmentSamplers(_pass, 0, &samplerBinding, 1);
            gpu.DrawIndexedPrimitives(_pass, command.geometry->indexCount, 1, 0, 0, 0);

            if (command.clipOperation == Rml::ClipMaskOperation::Intersect) ++_stencilRef;
            gpu.SetStencilReference(_pass, _stencilRef);
            break;
        }

        case Command::Kind::PushLayer:
            closePass();
            ++_top;
            openPass(layerTexture(_top), true);
            break;

        case Command::Kind::PopLayer:
            closePass();
            _top = std::max(_top - 1, 0);
            openPass(layerTexture(_top), false);
            break;

        case Command::Kind::Composite:
            closePass();
            runComposite(command);
            openPass(layerTexture(_top), false);
            break;

        case Command::Kind::SaveTexture:
        case Command::Kind::SaveMask: {
            closePass();
            const GpuTextureHandle source = layerTexture(_top);
            if (_postReplace != 0 && (_top > 0 || _targetSampleable)) {
                PostParams copy;
                if (command.kind == Command::Kind::SaveMask) {
                    postPass(surface(_maskTexture), _surfaceWidth, _surfaceHeight, source,
                             _whiteTexture, false, copy, kIdentityUv, false);
                } else {
                    // The scissored region of the layer, moved to the origin of its own texture.
                    const float uv[4] = {float(command.w) / float(_width),
                                         float(command.h) / float(_height),
                                         float(command.x) / float(_width),
                                         float(command.y) / float(_height)};
                    postPass(command.texture, (uint32_t) command.w, (uint32_t) command.h, source,
                             _whiteTexture, false, copy, uv, false);
                }
            }
            openPass(layerTexture(_top), false);
            break;
        }

        case Command::Kind::Draw: {
            if (command.geometry == nullptr || command.geometry->indexCount == 0) break;

            const GpuGraphicsPipelineHandle pipeline =
                command.shader != nullptr ? (_clipEnabled ? _gradientClipped : _gradientPipeline)
                                          : (_clipEnabled ? _texturedClipped : _texturedPipeline);
            if (pipeline == 0) break;
            if (pipeline != _boundPipeline) {
                gpu.BindGraphicsPipeline(_pass, pipeline);
                _boundPipeline = pipeline;
            }
            applyScissor();

            // Projection and RmlUi's transform combined here rather than in the shader; see the
            // note in rmlui.vert.
            const Rml::Matrix4f combined =
                _hasTransform ? (_projection * _transform) : _projection;

            VertexParams params{};

            // **Copied raw — do not transpose.** `Rml::Matrix4f` is whatever `RMLUI_MATRIX4_TYPE`
            // aliases, and RmlUi's own SDL_GPU backend pushes `.data()` straight at a SPIR-V
            // shader expecting a mat4 with no transpose at all. That backend runs in exactly the
            // shader environment this one does, so matching it is the only claim about storage
            // order worth making.
            //
            // Transposing here was the first version, and it put every vertex somewhere off
            // screen — a document that loaded, laid out and drew, and was invisible.
            std::memcpy(params.transform, combined.data(), sizeof(params.transform));
            params.translation[0] = command.translation.x;
            params.translation[1] = command.translation.y;

            gpu.PushVertexUniformData(_cmd, 0, &params, sizeof(params));

            GpuBufferBinding vertexBinding{command.geometry->vertices->buffer, 0};
            gpu.BindVertexBuffers(_pass, 0, &vertexBinding, 1);

            GpuBufferBinding indexBinding{command.geometry->indices->buffer, 0};
            // RmlUi indices are `int`, so 32-bit.
            gpu.BindIndexBuffer(_pass, indexBinding, /*use16BitIndices=*/false);

            if (command.shader != nullptr) {
                gpu.PushFragmentUniformData(_cmd, 0, &command.shader->params,
                                            sizeof(command.shader->params));
            } else {
                GpuTextureSamplerBinding samplerBinding{command.texture, _sampler};
                gpu.BindFragmentSamplers(_pass, 0, &samplerBinding, 1);
            }

            gpu.DrawIndexedPrimitives(_pass, command.geometry->indexCount, 1, 0, 0, 0);
            break;
        }
        }
    }

    closePass();
    _commands.clear();
    flushDeferred();
}

// ── Replay helpers ───────────────────────────────────────────────────────────

void RenderInterface_Lumi::openPass(GpuTextureHandle target, bool clear, int stencilClear) {
    IGpu &gpu = Renderer::GetGpu();

    GpuColorTargetInfo colorTarget{};
    colorTarget.texture = target;
    colorTarget.loadOp  = clear ? GpuLoadOp::Clear : GpuLoadOp::Load;
    colorTarget.storeOp = GpuStoreOp::Store;

    // The mask outlives the pass: layers and filters close and reopen it mid-clip.
    GpuDepthStencilTargetInfo stencil{};
    stencil.texture        = _stencilTexture;
    stencil.loadOp         = GpuLoadOp::Clear;
    stencil.storeOp        = GpuStoreOp::DontCare;
    stencil.stencilLoadOp  = stencilClear >= 0 ? GpuLoadOp::Clear : GpuLoadOp::Load;
    stencil.stencilStoreOp = GpuStoreOp::Store;
    stencil.clearStencil   = (uint8_t) std::max(stencilClear, 0);

    _pass = gpu.BeginRenderPass(_cmd, &colorTarget, 1, _stencilTexture != 0 ? &stencil : nullptr);
    _boundPipeline = 0;
    _scissorDirty  = true;
    if (_stencilTexture != 0) gpu.SetStencilReference(_pass, _stencilRef);
}

void RenderInterface_Lumi::closePass() {
    if (_pass != 0) Renderer::GetGpu().EndRenderPass(_pass);
    _pass = 0;
}

void RenderInterface_Lumi::clippedRect(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const {
    // A zero-area rectangle means RmlUi enabled clipping before it said what to clip to, and
    // applying it literally throws the whole document away, so it counts as no clip.
    if (!(_scissorEnabled && _scissorW > 0 && _scissorH > 0)) {
        x = 0;
        y = 0;
        w = (int32_t) _width;
        h = (int32_t) _height;
        return;
    }

    // Clamped to the target: a negative origin or an oversized extent is a validation error on
    // some backends rather than a clipped rectangle.
    x = std::max(_scissorX, 0);
    y = std::max(_scissorY, 0);
    w = std::max(std::min<int32_t>(_scissorW + std::min(_scissorX, 0), (int32_t) _width - x), 0);
    h = std::max(std::min<int32_t>(_scissorH + std::min(_scissorY, 0), (int32_t) _height - y), 0);
}

void RenderInterface_Lumi::applyScissor() {
    if (!_scissorDirty || _pass == 0) return;
    int32_t x, y, w, h;
    clippedRect(x, y, w, h);
    Renderer::GetGpu().SetScissor(_pass, x, y, (uint32_t) w, (uint32_t) h);
    _scissorDirty = false;
}

void RenderInterface_Lumi::postPass(GpuTextureHandle destination, uint32_t width, uint32_t height,
                                    GpuTextureHandle source, GpuTextureHandle mask, bool blend,
                                    const PostParams &params, const float uvTransform[4],
                                    bool clipped, bool clipMask) {
    if (destination == 0 || source == 0) return;
    IGpu &gpu = Renderer::GetGpu();

    const GpuGraphicsPipelineHandle clippedPipeline = blend ? _postBlendClipped : _postReplaceClipped;
    clipMask = clipMask && _stencilTexture != 0 && clippedPipeline != 0;

    GpuColorTargetInfo colorTarget{};
    colorTarget.texture = destination;
    colorTarget.loadOp  = GpuLoadOp::Load;
    colorTarget.storeOp = GpuStoreOp::Store;

    GpuDepthStencilTargetInfo stencil{};
    stencil.texture        = _stencilTexture;
    stencil.loadOp         = GpuLoadOp::Clear;
    stencil.storeOp        = GpuStoreOp::DontCare;
    stencil.stencilLoadOp  = GpuLoadOp::Load;
    stencil.stencilStoreOp = GpuStoreOp::Store;

    GpuRenderPassHandle pass = gpu.BeginRenderPass(_cmd, &colorTarget, 1, clipMask ? &stencil : nullptr);
    if (clipMask) {
        gpu.BindGraphicsPipeline(pass, clippedPipeline);
        gpu.SetStencilReference(pass, _stencilRef);
    } else {
        gpu.BindGraphicsPipeline(pass, blend ? _postBlend : _postReplace);
    }

    if (clipped) {
        int32_t x, y, w, h;
        clippedRect(x, y, w, h);
        gpu.SetScissor(pass, x, y, (uint32_t) w, (uint32_t) h);
    } else {
        gpu.SetScissor(pass, 0, 0, width, height);
    }

    gpu.PushVertexUniformData(_cmd, 0, uvTransform, sizeof(float) * 4);
    gpu.PushFragmentUniformData(_cmd, 0, &params, sizeof(params));

    const GpuTextureSamplerBinding samplers[2] = {{source, _sampler}, {mask, _sampler}};
    gpu.BindFragmentSamplers(pass, 0, samplers, 2);

    gpu.DrawPrimitives(pass, 3, 1, 0, 0);
    gpu.EndRenderPass(pass);
}

void RenderInterface_Lumi::runBlur(float sigma, GpuTextureHandle image, GpuTextureHandle scratch) {
    if (sigma < 0.5f) return;

    // Wide blurs step over texels rather than visiting each, keeping a pass to 65 taps; the
    // linear sampler averages what is stepped over.
    const float step      = std::max(1.0f, sigma * 3.0f / 32.0f);
    const float tapSigma  = sigma / step;

    int32_t x, y, w, h;
    clippedRect(x, y, w, h);

    PostParams params;
    params.mode      = PostBlur;
    params.sigma     = tapSigma;
    params.taps      = std::max(1, (int32_t) std::ceil(tapSigma * 3.0f));
    params.region[0] = (float(x) + 0.5f) / float(_width);
    params.region[1] = (float(y) + 0.5f) / float(_height);
    params.region[2] = (float(x + w) - 0.5f) / float(_width);
    params.region[3] = (float(y + h) - 0.5f) / float(_height);

    params.texelStep[0] = step / float(_width);
    params.texelStep[1] = 0.0f;
    postPass(scratch, _surfaceWidth, _surfaceHeight, image, _whiteTexture, false, params,
             kIdentityUv, true);

    params.texelStep[0] = 0.0f;
    params.texelStep[1] = step / float(_height);
    postPass(image, _surfaceWidth, _surfaceHeight, scratch, _whiteTexture, false, params,
             kIdentityUv, true);
}

void RenderInterface_Lumi::runComposite(const Command &command) {
    if (_postReplace == 0 || _postBlend == 0) return;

    // Reading the base layer means reading the target, which a swapchain image cannot be.
    if (command.source == 0 && !_targetSampleable) {
        static bool warned = false;
        if (!warned) LOG_WARNING("RmlUI renderer: backdrop filters need a sampleable target; skipped");
        warned = true;
        return;
    }

    GpuTextureHandle a = surface(_scratch[0]);
    GpuTextureHandle b = surface(_scratch[1]);
    if (a == 0 || b == 0) return;

    const uint32_t W = _surfaceWidth, H = _surfaceHeight;
    const PostParams copy;

    postPass(a, W, H, layerTexture(command.source), _whiteTexture, false, copy, kIdentityUv, true);

    for (Rml::CompiledFilterHandle handle : command.filters) {
        const auto *filter = reinterpret_cast<const Filter *>(handle);
        if (filter == nullptr) continue;

        switch (filter->type) {
        case Filter::Type::Opacity: {
            PostParams params;
            params.factor = filter->value;
            postPass(b, W, H, a, _whiteTexture, false, params, kIdentityUv, true);
            std::swap(a, b);
            break;
        }
        case Filter::Type::ColorMatrix: {
            PostParams params;
            params.mode = PostColorMatrix;
            std::memcpy(params.colorMatrix, filter->matrix, sizeof(params.colorMatrix));
            postPass(b, W, H, a, _whiteTexture, false, params, kIdentityUv, true);
            std::swap(a, b);
            break;
        }
        case Filter::Type::Mask: {
            PostParams params;
            params.mode = PostMask;
            postPass(b, W, H, a, surface(_maskTexture), false, params, kIdentityUv, true);
            std::swap(a, b);
            break;
        }
        case Filter::Type::Blur:
            runBlur(filter->sigma, a, b);
            break;
        case Filter::Type::DropShadow: {
            int32_t x, y, w, h;
            clippedRect(x, y, w, h);

            PostParams shadow;
            shadow.mode      = PostShadow;
            shadow.region[0] = (float(x) + 0.5f) / float(_width);
            shadow.region[1] = (float(y) + 0.5f) / float(_height);
            shadow.region[2] = (float(x + w) - 0.5f) / float(_width);
            shadow.region[3] = (float(y + h) - 0.5f) / float(_height);
            std::memcpy(shadow.color, filter->color, sizeof(shadow.color));

            // Sampled back along the offset, so the shadow lands along it.
            const float uv[4] = {1.0f, 1.0f, -filter->offset.x / float(_width),
                                 -filter->offset.y / float(_height)};
            postPass(b, W, H, a, _whiteTexture, false, shadow, uv, true);

            if (filter->sigma >= 0.5f) runBlur(filter->sigma, b, surface(_scratch[2]));

            postPass(b, W, H, a, _whiteTexture, true, copy, kIdentityUv, true);
            std::swap(a, b);
            break;
        }
        }
    }

    postPass(layerTexture(command.destination), W, H, a, _whiteTexture, !command.replace, copy,
             kIdentityUv, true, _clipEnabled);
}

// ── Gradients, filters and layers, as RmlUi records them ─────────────────────

Rml::CompiledShaderHandle RenderInterface_Lumi::CompileShader(const Rml::String     &name,
                                                              const Rml::Dictionary &parameters) {
    enum { Linear, Radial, Conic, RepeatingLinear, RepeatingRadial, RepeatingConic };

    auto shader                 = std::make_unique<Shader>();
    GradientParams &params      = shader->params;
    const bool      repeating   = Rml::Get(parameters, "repeating", false);
    Rml::Vector2f   p{0.0f, 0.0f}, v{0.0f, 0.0f};

    if (name == "linear-gradient") {
        params.func = repeating ? RepeatingLinear : Linear;
        p           = Rml::Get(parameters, "p0", Rml::Vector2f(0.0f));
        v           = Rml::Get(parameters, "p1", Rml::Vector2f(0.0f)) - p;
    } else if (name == "radial-gradient") {
        params.func = repeating ? RepeatingRadial : Radial;
        p           = Rml::Get(parameters, "center", Rml::Vector2f(0.0f));
        v           = Rml::Vector2f(1.0f) / Rml::Get(parameters, "radius", Rml::Vector2f(1.0f));
    } else if (name == "conic-gradient") {
        params.func       = repeating ? RepeatingConic : Conic;
        p                 = Rml::Get(parameters, "center", Rml::Vector2f(0.0f));
        const float angle = Rml::Get(parameters, "angle", 0.0f);
        v                 = {std::cos(angle), std::sin(angle)};
    } else {
        LOG_WARNING("RmlUI renderer: unsupported shader '{}'", name.c_str());
        return {};
    }

    params.p[0] = p.x;
    params.p[1] = p.y;
    params.v[0] = v.x;
    params.v[1] = v.y;

    auto stops = parameters.find("color_stop_list");
    if (stops == parameters.end() || stops->second.GetType() != Rml::Variant::COLORSTOPLIST) return {};

    const Rml::ColorStopList &list = stops->second.GetReference<Rml::ColorStopList>();
    params.numStops                = std::min((int) list.size(), kMaxGradientStops);
    for (int i = 0; i < params.numStops; ++i) {
        params.stopPositions[i] = list[(size_t) i].position.number;
        for (int c = 0; c < 4; ++c) params.stopColors[i][c] = float(list[(size_t) i].color[c]) / 255.0f;
    }

    return reinterpret_cast<Rml::CompiledShaderHandle>(shader.release());
}

void RenderInterface_Lumi::RenderShader(Rml::CompiledShaderHandle shader,
                                        Rml::CompiledGeometryHandle geometry,
                                        Rml::Vector2f translation, Rml::TextureHandle /*texture*/) {
    if (shader == 0 || geometry == 0) return;

    Command command;
    command.kind        = Command::Kind::Draw;
    command.geometry    = reinterpret_cast<Geometry *>(geometry);
    command.translation = translation;
    command.shader      = reinterpret_cast<const Shader *>(shader);
    _commands.push_back(command);
}

void RenderInterface_Lumi::ReleaseShader(Rml::CompiledShaderHandle shader) {
    if (_inFrame) {
        _deferredShaders.push_back(reinterpret_cast<Shader *>(shader));
        return;
    }
    delete reinterpret_cast<Shader *>(shader);
}

Rml::CompiledFilterHandle RenderInterface_Lumi::CompileFilter(const Rml::String     &name,
                                                              const Rml::Dictionary &parameters) {
    auto filter = std::make_unique<Filter>();
    const float value = Rml::Get(parameters, "value", 1.0f);
    Rml::Matrix4f matrix = Rml::Matrix4f::Identity();

    // Ported from RmlUi's GL3 renderer, which follows the CSS filter-effects definitions.
    if (name == "opacity") {
        filter->type  = Filter::Type::Opacity;
        filter->value = value;
    } else if (name == "blur") {
        filter->type  = Filter::Type::Blur;
        filter->sigma = Rml::Get(parameters, "sigma", 1.0f);
    } else if (name == "drop-shadow") {
        filter->type   = Filter::Type::DropShadow;
        filter->sigma  = Rml::Get(parameters, "sigma", 0.0f);
        filter->offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.0f));
        const Rml::ColourbPremultiplied c = Rml::Get(parameters, "color", Rml::Colourb()).ToPremultiplied();
        for (int i = 0; i < 4; ++i) filter->color[i] = float(c[i]) / 255.0f;
    } else {
        filter->type = Filter::Type::ColorMatrix;

        if (name == "brightness") {
            matrix = Rml::Matrix4f::Diag(value, value, value, 1.0f);
        } else if (name == "contrast") {
            const float grey = 0.5f - 0.5f * value;
            matrix = Rml::Matrix4f::Diag(value, value, value, 1.0f);
            matrix.SetColumn(3, Rml::Vector4f(grey, grey, grey, 1.0f));
        } else if (name == "invert") {
            const float amount   = Rml::Math::Clamp(value, 0.0f, 1.0f);
            const float inverted = 1.0f - 2.0f * amount;
            matrix = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.0f);
            matrix.SetColumn(3, Rml::Vector4f(amount, amount, amount, 1.0f));
        } else if (name == "grayscale") {
            const float         rest = 1.0f - value;
            const Rml::Vector3f g    = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
            matrix = Rml::Matrix4f::FromRows({g.x + rest, g.y, g.z, 0.0f}, {g.x, g.y + rest, g.z, 0.0f},
                                             {g.x, g.y, g.z + rest, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f});
        } else if (name == "sepia") {
            const float         rest = 1.0f - value;
            const Rml::Vector3f r    = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
            const Rml::Vector3f g    = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
            const Rml::Vector3f b    = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);
            matrix = Rml::Matrix4f::FromRows({r.x + rest, r.y, r.z, 0.0f}, {g.x, g.y + rest, g.z, 0.0f},
                                             {b.x, b.y, b.z + rest, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f});
        } else if (name == "hue-rotate") {
            const float s = std::sin(value);
            const float c = std::cos(value);
            matrix = Rml::Matrix4f::FromRows(
                {0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s, 0.0f},
                {0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s, 0.0f},
                {0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s, 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f});
        } else if (name == "saturate") {
            matrix = Rml::Matrix4f::FromRows(
                {0.213f + 0.787f * value, 0.715f - 0.715f * value, 0.072f - 0.072f * value, 0.0f},
                {0.213f - 0.213f * value, 0.715f + 0.285f * value, 0.072f - 0.072f * value, 0.0f},
                {0.213f - 0.213f * value, 0.715f - 0.715f * value, 0.072f + 0.928f * value, 0.0f},
                {0.0f, 0.0f, 0.0f, 1.0f});
        } else {
            LOG_WARNING("RmlUI renderer: unsupported filter '{}'", name.c_str());
            return {};
        }
        CopyMatrix(filter->matrix, matrix);
    }

    return reinterpret_cast<Rml::CompiledFilterHandle>(filter.release());
}

void RenderInterface_Lumi::ReleaseFilter(Rml::CompiledFilterHandle filter) {
    if (_inFrame) {
        _deferredFilters.push_back(reinterpret_cast<Filter *>(filter));
        return;
    }
    delete reinterpret_cast<Filter *>(filter);
}

void RenderInterface_Lumi::flushDeferred() {
    _inFrame = false;
    for (Geometry *geometry : _deferredGeometry) destroyGeometry(geometry);
    for (GpuTextureHandle texture : _deferredTextures) destroyTexture(texture);
    for (Filter *filter : _deferredFilters) delete filter;
    for (Shader *shader : _deferredShaders) delete shader;
    _deferredGeometry.clear();
    _deferredTextures.clear();
    _deferredFilters.clear();
    _deferredShaders.clear();
}

void RenderInterface_Lumi::EnableClipMask(bool enable) {
    Command command;
    command.kind   = Command::Kind::ClipEnable;
    command.enable = enable;
    _commands.push_back(command);
}

void RenderInterface_Lumi::RenderToClipMask(Rml::ClipMaskOperation      operation,
                                            Rml::CompiledGeometryHandle geometry,
                                            Rml::Vector2f               translation) {
    Command command;
    command.kind          = Command::Kind::ClipDraw;
    command.clipOperation = operation;
    command.geometry      = reinterpret_cast<Geometry *>(geometry);
    command.translation   = translation;
    _commands.push_back(command);
}

Rml::LayerHandle RenderInterface_Lumi::PushLayer() {
    Command command;
    command.kind = Command::Kind::PushLayer;
    _commands.push_back(command);
    return (Rml::LayerHandle) ++_recordDepth;
}

void RenderInterface_Lumi::PopLayer() {
    Command command;
    command.kind = Command::Kind::PopLayer;
    _commands.push_back(command);
    _recordDepth = std::max(_recordDepth - 1, 0);
}

void RenderInterface_Lumi::CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination,
                                           Rml::BlendMode                             blend_mode,
                                           Rml::Span<const Rml::CompiledFilterHandle> filters) {
    Command command;
    command.kind        = Command::Kind::Composite;
    command.source      = (int) source;
    command.destination = (int) destination;
    command.replace     = blend_mode == Rml::BlendMode::Replace;
    command.filters.assign(filters.begin(), filters.end());
    _commands.push_back(std::move(command));
}

Rml::TextureHandle RenderInterface_Lumi::SaveLayerAsTexture() {
    if (!_ready) return 0;

    // The scissored region, which RmlUi sets to what it wants kept.
    Rml::Rectanglei region = _recordScissorOn ? _recordScissor
                                              : Rml::Rectanglei::FromSize({(int) _width, (int) _height});
    const int x = std::max(region.Left(), 0);
    const int y = std::max(region.Top(), 0);
    const int w = std::min(region.Right(), (int) _width) - x;
    const int h = std::min(region.Bottom(), (int) _height) - y;
    if (w <= 0 || h <= 0) return 0;

    GpuTextureCreateInfo info{};
    info.width  = (uint32_t) w;
    info.height = (uint32_t) h;
    info.format = _format;
    info.usage  = GpuTextureUsage::ColorTarget | GpuTextureUsage::Sampler;

    const GpuTextureHandle texture = Renderer::GetGpu().CreateTexture(info);
    if (texture == 0) return 0;
    _textures.push_back(texture);

    Command command;
    command.kind    = Command::Kind::SaveTexture;
    command.texture = texture;
    command.x       = x;
    command.y       = y;
    command.w       = w;
    command.h       = h;
    _commands.push_back(command);

    return static_cast<Rml::TextureHandle>(texture);
}

Rml::CompiledFilterHandle RenderInterface_Lumi::SaveLayerAsMaskImage() {
    Command command;
    command.kind = Command::Kind::SaveMask;
    _commands.push_back(command);

    auto filter  = std::make_unique<Filter>();
    filter->type = Filter::Type::Mask;
    return reinterpret_cast<Rml::CompiledFilterHandle>(filter.release());
}

} // namespace RmlUI

#endif // LUMINOVEAU_WITH_RMLUI
