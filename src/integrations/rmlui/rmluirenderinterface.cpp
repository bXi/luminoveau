#ifdef LUMINOVEAU_WITH_RMLUI

#include "integrations/rmlui/rmluirenderinterface.h"

#include "assets/assethandler.h"
#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "gpu/presets.h"
#include "renderer/renderer.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>

#include <algorithm>
#include <cstring>

namespace RmlUI {

namespace {

constexpr const char *kVertShader = "assets/shaders/rmlui.vert";
constexpr const char *kFragShader = "assets/shaders/rmlui.frag";

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

RenderInterface_Lumi::RenderInterface_Lumi() {
    _projection = Rml::Matrix4f::Identity();
    _transform  = Rml::Matrix4f::Identity();
}

RenderInterface_Lumi::~RenderInterface_Lumi() { Shutdown(); }

bool RenderInterface_Lumi::Init(GpuTextureFormat targetFormat) {
    if (_ready) return true;

    IGpu &gpu = Renderer::GetGpu();

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

    _texturedPipeline = gpu.CreateGraphicsPipeline(info);
    if (_texturedPipeline == 0) {
        LOG_ERROR("RmlUI renderer: pipeline creation failed");
        return false;
    }

    // One pipeline serves both cases; the field is kept so the distinction stays visible at the
    // call site and a colour-only pipeline can be reintroduced without touching the replay.
    _colorPipeline = _texturedPipeline;
    return true;
}

void RenderInterface_Lumi::Shutdown() {
    if (!_ready) return;

    IGpu &gpu = Renderer::GetGpu();

    releaseBuffers();
    _geometries.clear();

    for (GpuTextureHandle texture : _textures) {
        if (texture != 0) gpu.ReleaseTexture(texture);
    }
    _textures.clear();

    if (_whiteTexture != 0) gpu.ReleaseTexture(_whiteTexture);
    if (_sampler != 0) gpu.ReleaseSampler(_sampler);
    if (_texturedPipeline != 0) gpu.ReleaseGraphicsPipeline(_texturedPipeline);

    _whiteTexture     = 0;
    _sampler          = 0;
    _texturedPipeline = 0;
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
                                      uint32_t width, uint32_t height) {
    _cmd    = cmd;
    _target = target;
    _width  = width;
    _height = height;

    updateProjection();

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

    auto found = std::find(_textures.begin(), _textures.end(), texture);
    if (found == _textures.end()) return; // not ours — a hooked texture, owned elsewhere

    Renderer::GetGpu().ReleaseTexture(texture);
    _textures.erase(found);
}

void RenderInterface_Lumi::EnableScissorRegion(bool enable) {
    Command command;
    command.kind   = Command::Kind::ScissorEnable;
    command.enable = enable;
    _commands.push_back(command);
}

void RenderInterface_Lumi::SetScissorRegion(Rml::Rectanglei region) {
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
        return;
    }

    IGpu &gpu = Renderer::GetGpu();

    GpuColorTargetInfo colorTarget{};
    colorTarget.texture = _target;
    // Loaded, never cleared: the UI draws over a finished frame.
    colorTarget.loadOp  = GpuLoadOp::Load;
    colorTarget.storeOp = GpuStoreOp::Store;

    GpuRenderPassHandle pass = gpu.BeginRenderPass(_cmd, &colorTarget, 1, nullptr);
    gpu.BindGraphicsPipeline(pass, _texturedPipeline);

    // Scissor state is tracked rather than set eagerly: RmlUi toggles it far more often than it
    // changes the rectangle, and a disabled scissor is the whole target rather than a separate
    // piece of state the backend has to remember how to undo.
    bool scissorDirty = true;

    for (const Command &command : _commands) {
        switch (command.kind) {
        case Command::Kind::ScissorEnable:
            _scissorEnabled = command.enable;
            scissorDirty    = true;
            break;

        case Command::Kind::Scissor:
            _scissorX    = command.x;
            _scissorY    = command.y;
            _scissorW    = command.w;
            _scissorH    = command.h;
            scissorDirty = true;
            break;

        case Command::Kind::Transform:
            _transform    = command.transform;
            _hasTransform = command.enable;
            break;

        case Command::Kind::Draw: {
            if (command.geometry == nullptr || command.geometry->indexCount == 0) break;

            if (scissorDirty) {
                // A zero-area rectangle means RmlUi enabled clipping before it said what to clip
                // to, and applying it literally throws the whole document away. Treated as "no
                // clip yet" instead — the next `SetScissorRegion` will narrow it properly.
                const bool clipping = _scissorEnabled && _scissorW > 0 && _scissorH > 0;

                if (clipping) {
                    // Clamped to the target. A document can scroll a region partly off screen and
                    // a negative origin or an oversized extent is a validation error on some
                    // backends rather than a clipped rectangle.
                    const int32_t x = std::max(_scissorX, 0);
                    const int32_t y = std::max(_scissorY, 0);
                    const int32_t w = std::min<int32_t>(_scissorW + std::min(_scissorX, 0),
                                                        static_cast<int32_t>(_width) - x);
                    const int32_t h = std::min<int32_t>(_scissorH + std::min(_scissorY, 0),
                                                        static_cast<int32_t>(_height) - y);

                    gpu.SetScissor(pass, x, y, static_cast<uint32_t>(std::max(w, 0)),
                                   static_cast<uint32_t>(std::max(h, 0)));
                } else {
                    gpu.SetScissor(pass, 0, 0, _width, _height);
                }
                scissorDirty = false;
            }

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
            gpu.BindVertexBuffers(pass, 0, &vertexBinding, 1);

            GpuBufferBinding indexBinding{command.geometry->indices->buffer, 0};
            // RmlUi indices are `int`, so 32-bit.
            gpu.BindIndexBuffer(pass, indexBinding, /*use16BitIndices=*/false);

            GpuTextureSamplerBinding samplerBinding{command.texture, _sampler};
            gpu.BindFragmentSamplers(pass, 0, &samplerBinding, 1);

            gpu.DrawIndexedPrimitives(pass, command.geometry->indexCount, 1, 0, 0, 0);
            break;
        }
        }
    }

    gpu.EndRenderPass(pass);
    _commands.clear();
}

} // namespace RmlUI

#endif // LUMINOVEAU_WITH_RMLUI
