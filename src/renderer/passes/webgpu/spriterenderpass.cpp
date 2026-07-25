// WebGPU-backend implementation for SpriteRenderPass — render path + effect helpers + shaders.
// Compiled only on LUMINOVEAU_WEBGPU_BACKEND. Shared init/release/blend lives in
// ../spriterenderpass.cpp.

#include "renderer/passes/spriterenderpass.h"

#include <algorithm>
#include <utility>
#include <cstring>
#include <vector>

#include "core/log/log.h"
#include "platform/window/window.h"
#include "draw/draw.h"

// ── Embedded WGSL: sprite vertex + fragment shaders ──────────────────────────
static constexpr const char *SPRITE_VERT_WGSL = R"(
struct SpriteData {
    posXy   : u32,
    posZRot: u32,
    texUv   : u32,
    texWh   : u32,
    colorRg : u32,
    colorBa : u32,
    sizeWh  : u32,
    pivotXy : u32,
}

struct UniformBlock {
    viewProjection : mat4x4<f32>,
}

struct InstanceOffset {
    baseInstance : u32,
    renderScale  : f32,
    _pad         : vec2<u32>,
}

@group(0) @binding(0) var<uniform>       uniforms        : UniformBlock;
@group(0) @binding(1) var<uniform>       instOffset      : InstanceOffset;
@group(3) @binding(0) var<storage, read> spriteInstances : array<SpriteData>;

struct VertOut {
    @builtin(position)               position  : vec4<f32>,
    @location(0)                     texCoord  : vec2<f32>,
    @location(1)                     color     : vec4<f32>,
    @location(2) @interpolate(flat)  isSDF     : u32,
    @location(3) @interpolate(flat)  sdfScale  : f32,
}

fn unpackHalfLo(packed: u32) -> f32 { return unpack2x16float(packed).x; }
fn unpackHalfHi(packed: u32) -> f32 { return unpack2x16float(packed).y; }
fn unpackHalf2(packed: u32) -> vec2<f32> { return unpack2x16float(packed); }

@vertex
fn vs_main(
    @location(0) vertPosXY : u32,
    @location(1) vertUV    : u32,
    @builtin(instance_index) instanceIndex : u32,
) -> VertOut {
    let sprite = spriteInstances[instanceIndex + instOffset.baseInstance];

    let x        = unpackHalfLo(sprite.posXy);
    let y        = unpackHalfHi(sprite.posXy);
    let rotation = unpackHalfHi(sprite.posZRot);
    let texUV    = unpackHalf2(sprite.texUv);
    let texWH    = unpackHalf2(sprite.texWh);
    let color    = vec4<f32>(
        unpackHalfLo(sprite.colorRg),
        unpackHalfHi(sprite.colorRg),
        unpackHalfLo(sprite.colorBa),
        unpackHalfHi(sprite.colorBa),
    );
    let scale = unpackHalf2(sprite.sizeWh);

    let pivotPacked  = sprite.pivotXy;
    let isSDF        = (pivotPacked >> 31u) & 1u;
    let pivotCleared = pivotPacked & 0x7FFFFFFFu;
    let pivot        = unpackHalf2(pivotCleared);

    let vertexPos = unpackHalf2(vertPosXY);
    let vertexUV  = unpackHalf2(vertUV);

    var coord = vertexPos;

    let texcoord = vec2<f32>(
        texUV.x + vertexUV.x * texWH.x,
        texUV.y + vertexUV.y * texWH.y,
    );

    if rotation != 0.0 {
        coord -= pivot;
    }

    coord *= scale;

    if rotation != 0.0 {
        let c = cos(rotation);
        let s = sin(rotation);
        coord = vec2<f32>(c * coord.x - s * coord.y,
                          s * coord.x + c * coord.y);
        coord += pivot * scale;
    }

    let worldPos = vec3<f32>(coord + vec2<f32>(x, y), 0.0);

    var out : VertOut;
    out.position = uniforms.viewProjection * vec4<f32>(worldPos, 1.0);
    out.texCoord = texcoord;
    out.color    = color;
    out.isSDF    = isSDF;
    out.sdfScale = max(instOffset.renderScale, 1.0);
    return out;
}
)";

static constexpr const char *SPRITE_FRAG_WGSL = R"(
@group(2) @binding(0) var gSampler : sampler;
@group(2) @binding(1) var gTexture : texture_2d<f32>;

struct FragIn {
    @location(0)                     texCoord : vec2<f32>,
    @location(1)                     color    : vec4<f32>,
    @location(2) @interpolate(flat)  isSDF    : u32,
    @location(3) @interpolate(flat)  sdfScale : f32,
}

fn median(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

@fragment
fn fs_main(in : FragIn) -> @location(0) vec4<f32> {
    // Sample and compute derivatives in uniform control flow (required by WebGPU).
    let texColor = textureSample(gTexture, gSampler, in.texCoord);
    let dim      = textureDimensions(gTexture, 0);
    let msdfUnit = 4.0 / vec2<f32>(f32(dim.x), f32(dim.y));
    let texDeriv = fwidth(in.texCoord);

    // SDF path
    let sd               = median(texColor.r, texColor.g, texColor.b);
    let screenPxRange    = max(dot(msdfUnit, 0.5 / texDeriv) * in.sdfScale, 1.0);
    let screenPxDistance = screenPxRange * (sd - 0.5);
    let sdfAlpha         = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    let sdfColor         = vec4<f32>(in.color.rgb, sdfAlpha * in.color.a);

    // Regular path
    let regularColor = texColor * in.color;

    let outColor = select(regularColor, sdfColor, in.isSDF != 0u);

    if outColor.a == 0.0 { discard; }
    return outColor;
}
)";

// ── release / Init (WebGPU) ──────────────────────────────────────────────────

void SpriteRenderPass::Release(bool logRelease) {
    IGpu &gpu = Renderer::GetGpu();

    for (auto &[key, pipeline] : _effectPipelines) {
        if (pipeline)
            gpu.ReleaseGraphicsPipeline(pipeline);
    }
    _effectPipelines.clear();

    if (_effectTexA) {
        gpu.ReleaseTexture(_effectTexA);
        _effectTexA = 0;
    }
    if (_effectTexB) {
        gpu.ReleaseTexture(_effectTexB);
        _effectTexB = 0;
    }
    if (_effectSampler) {
        gpu.ReleaseSampler(_effectSampler);
        _effectSampler = 0;
    }
    if (_effectVbuf) {
        gpu.ReleaseBuffer(_effectVbuf);
        _effectVbuf = 0;
    }
    if (_effectIbuf) {
        gpu.ReleaseBuffer(_effectIbuf);
        _effectIbuf = 0;
    }
    if (_quadVertexBuf) {
        gpu.ReleaseBuffer(_quadVertexBuf);
        _quadVertexBuf = 0;
    }
    if (_quadIndexBuf) {
        gpu.ReleaseBuffer(_quadIndexBuf);
        _quadIndexBuf = 0;
    }
    if (_quadXferVert) {
        gpu.ReleaseTransferBuffer(_quadXferVert);
        _quadXferVert = 0;
    }
    if (_quadXferIdx) {
        gpu.ReleaseTransferBuffer(_quadXferIdx);
        _quadXferIdx = 0;
    }

    if (_spriteDataTransferBuffer) {
        gpu.ReleaseTransferBuffer(_spriteDataTransferBuffer);
        _spriteDataTransferBuffer = 0;
    }
    if (_spriteDataBuffer) {
        gpu.ReleaseBuffer(_spriteDataBuffer);
        _spriteDataBuffer = 0;
    }
    if (_vertexShader) {
        gpu.ReleaseShader(_vertexShader);
        _vertexShader = 0;
    }
    if (_fragmentShader) {
        gpu.ReleaseShader(_fragmentShader);
        _fragmentShader = 0;
    }
    if (_pipeline) {
        gpu.ReleaseGraphicsPipeline(_pipeline);
        _pipeline = 0;
    }
    if (_effectSpritePipeline) {
        gpu.ReleaseGraphicsPipeline(_effectSpritePipeline);
        _effectSpritePipeline = 0;
    }

    renderQueue = nullptr;

    if (logRelease) {
        LOG_INFO("Released graphics pipeline: {}", _passname.c_str());
    }
}

bool SpriteRenderPass::Init(
    GpuTextureFormat swapchainTextureFormat, uint32_t surfaceWidth, uint32_t surfaceHeight, std::string name, bool logInit,
    size_t capacity, bool forceNoMSAA) {
    _noMSAA          = forceNoMSAA;
    _passname        = std::move(name);
    _surfaceWidth    = surfaceWidth;
    _surfaceHeight   = surfaceHeight;
    _swapchainFormat = swapchainTextureFormat;

    IGpu &gpu   = Renderer::GetGpu();
    renderQueue = BufferManager::Create<Renderable>(_passname + "_renderQueue", capacity > 0 ? capacity : MAX_SPRITES);

    _createShaders();

    GpuVertexAttribute vertexAttributes[] = {
        { .location = 0, .binding = 0, .format = GpuVertexElementFormat::UInt, .offset = 0 },
        { .location = 1, .binding = 0, .format = GpuVertexElementFormat::UInt, .offset = 4 },
    };
    GpuVertexBinding vertexBinding = { .binding = 0, .stride = 8, .instanceStepping = false };

    GpuGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.vertexShader             = _vertexShader;
    pipelineInfo.fragmentShader           = _fragmentShader;
    pipelineInfo.attributes               = vertexAttributes;
    pipelineInfo.attributeCount           = 2;
    pipelineInfo.bindings                 = &vertexBinding;
    pipelineInfo.bindingCount             = 1;
    pipelineInfo.fillMode                 = GpuFillMode::Fill;
    pipelineInfo.cullMode                 = GpuCullMode::None;
    pipelineInfo.frontFace                = GpuFrontFace::CounterClockwise;
    pipelineInfo.colorTargetFormat        = swapchainTextureFormat;
    pipelineInfo.blend                    = renderPassBlendState;
    pipelineInfo.hasDepthTarget           = false;
    pipelineInfo.sampleCount              = GpuSampleCount::X1;
    pipelineInfo.vertexStorageBufferCount = 1;
    _pipeline                             = gpu.CreateGraphicsPipeline(pipelineInfo);

    if (!_pipeline) {
        LOG_CRITICAL("SpriteRenderPass: failed to create pipeline for {}", _passname);
        return false;
    }

    // Second pipeline for the effect path's sprite-to-tempA draw. The primary _pipeline uses
    // AlphaBlendKeepDstAlpha (preserves the framebuffer's existing alpha when compositing the
    // sprite layer), which is wrong when drawing into a freshly-cleared (alpha=0) effect
    // ping-pong texture — the sprite's RGB lands but the alpha stays 0, so the downstream
    // effect quad samples fully-transparent texels and produces no visible output. This
    // variant uses straight One/Zero replace blending so tempA ends up with the sprite's
    // own alpha. Mirrors SDL's effectSpritePipeline.
    {
        GpuColorTargetBlendState replaceBlend {};
        replaceBlend.blendEnabled   = true;
        replaceBlend.srcColorFactor = GpuBlendFactor::One;
        replaceBlend.dstColorFactor = GpuBlendFactor::Zero;
        replaceBlend.colorOp        = GpuBlendOp::Add;
        replaceBlend.srcAlphaFactor = GpuBlendFactor::One;
        replaceBlend.dstAlphaFactor = GpuBlendFactor::Zero;
        replaceBlend.alphaOp        = GpuBlendOp::Add;

        GpuGraphicsPipelineCreateInfo effSpritePci = pipelineInfo;
        effSpritePci.blend                         = replaceBlend;
        _effectSpritePipeline                      = gpu.CreateGraphicsPipeline(effSpritePci);
        if (!_effectSpritePipeline) {
            LOG_CRITICAL("SpriteRenderPass: failed to create effect-sprite pipeline for {}", _passname);
            return false;
        }
    }

    _spriteDataTransferBuffer = gpu.CreateTransferBuffer({
        static_cast<uint32_t>(MAX_SPRITES * sizeof(CompactSpriteInstance)),
        GpuTransferUsage::Upload,
    });
    _spriteDataBuffer         = gpu.CreateBuffer({
        static_cast<uint32_t>(MAX_SPRITES * sizeof(CompactSpriteInstance)),
        GpuBufferUsage::StorageRead,
    });

    // Unit quad geometry (CompactVertex2D — posXy and uv packed as uint32 half-floats).
    struct QuadVertex {
        uint32_t posXy;
        uint32_t uv;
    };
    auto packHalf = [](float a, float b) -> uint32_t {
        auto toHalf = [](float f) -> uint16_t {
            union {
                float    f;
                uint32_t i;
            } u           = { f };
            uint32_t bits = u.i;
            uint32_t sign = (bits >> 16) & 0x8000;
            int32_t  exp  = ((bits >> 23) & 0xFF) - 127 + 15;
            uint32_t mant = (bits >> 13) & 0x3FF;
            if (exp <= 0)
                return static_cast<uint16_t>(sign);
            if (exp >= 31)
                return static_cast<uint16_t>(sign | 0x7C00);
            return static_cast<uint16_t>(sign | (exp << 10) | mant);
        };
        return static_cast<uint32_t>(toHalf(a)) | (static_cast<uint32_t>(toHalf(b)) << 16);
    };
    QuadVertex quadVerts[4] = {
        { packHalf(0.0f, 0.0f), packHalf(0.0f, 0.0f) },
        { packHalf(1.0f, 0.0f), packHalf(1.0f, 0.0f) },
        { packHalf(0.0f, 1.0f), packHalf(0.0f, 1.0f) },
        { packHalf(1.0f, 1.0f), packHalf(1.0f, 1.0f) },
    };
    uint16_t quadIdx[6] = { 0, 1, 2, 2, 1, 3 };

    _quadXferVert  = gpu.CreateTransferBuffer({ sizeof(quadVerts), GpuTransferUsage::Upload });
    _quadXferIdx   = gpu.CreateTransferBuffer({ sizeof(quadIdx), GpuTransferUsage::Upload });
    _quadVertexBuf = gpu.CreateBuffer({ sizeof(quadVerts), GpuBufferUsage::Vertex });
    _quadIndexBuf  = gpu.CreateBuffer({ sizeof(quadIdx), GpuBufferUsage::Index });

    memcpy(gpu.MapTransferBuffer(_quadXferVert, false), quadVerts, sizeof(quadVerts));
    gpu.UnmapTransferBuffer(_quadXferVert);
    memcpy(gpu.MapTransferBuffer(_quadXferIdx, false), quadIdx, sizeof(quadIdx));
    gpu.UnmapTransferBuffer(_quadXferIdx);

    GpuCmdBufferHandle uploadCmd = gpu.AcquireCommandBuffer();
    gpu.UploadToBuffer(uploadCmd, _quadXferVert, 0, _quadVertexBuf, 0, sizeof(quadVerts));
    gpu.UploadToBuffer(uploadCmd, _quadXferIdx, 0, _quadIndexBuf, 0, sizeof(quadIdx));
    gpu.SubmitCommandBuffer(uploadCmd);

    // Effect ping-pong textures — sized to the physical window, NOT the desktop-sized
    // surface. User-supplied effect fragment shaders typically assume input UV [0..1]
    // maps to the full populated area; if we sized these at surfaceWidth the populated
    // sprite content would only occupy the top-left physW×physH fraction and any shader
    // sampling at offsets/derivatives would misbehave. Recreated in Render() if the
    // physical window grows.
    // Cap effect-tex dims to the surface (FB size). For fixedSize FBs like LightToy's
    // hrc_scene (1348×783) we want UV [0..1] to span the actual usable area, not the full
    // physical window — otherwise the effect quad samples uninitialized pixels past the
    // surface's content region.
    {
        uint32_t pw = static_cast<uint32_t>(Window::GetPhysicalWidth());
        uint32_t ph = static_cast<uint32_t>(Window::GetPhysicalHeight());
        if (pw == 0 || ph == 0) {
            pw = surfaceWidth;
            ph = surfaceHeight;
        }
        _effectTexW = std::min(pw, surfaceWidth);
        _effectTexH = std::min(ph, surfaceHeight);
    }
    {
        GpuTextureCreateInfo texInfo {};
        texInfo.width       = _effectTexW;
        texInfo.height      = _effectTexH;
        texInfo.format      = swapchainTextureFormat;
        texInfo.usage       = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
        texInfo.sampleCount = GpuSampleCount::X1;
        _effectTexA         = gpu.CreateTexture(texInfo);
        _effectTexB         = gpu.CreateTexture(texInfo);
    }

    {
        GpuSamplerCreateInfo si {};
        si.minFilter   = GpuFilter::Nearest;
        si.magFilter   = GpuFilter::Nearest;
        _effectSampler = gpu.CreateSampler(si);
    }

    // Effect fullscreen quad — position (0..1) + texcoord (0..1), 16 bytes per vertex.
    struct EffectVertex {
        float px, py, ux, uy;
    };
    EffectVertex effectVerts[4] = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
    };
    uint16_t effectIdx[6] = { 0, 1, 2, 2, 1, 3 };

    _effectVbuf = gpu.CreateBuffer({ sizeof(effectVerts), GpuBufferUsage::Vertex });
    _effectIbuf = gpu.CreateBuffer({ sizeof(effectIdx), GpuBufferUsage::Index });
    {
        GpuTransferBufferHandle tvb = gpu.CreateTransferBuffer({ sizeof(effectVerts), GpuTransferUsage::Upload });
        GpuTransferBufferHandle tib = gpu.CreateTransferBuffer({ sizeof(effectIdx), GpuTransferUsage::Upload });
        memcpy(gpu.MapTransferBuffer(tvb, false), effectVerts, sizeof(effectVerts));
        gpu.UnmapTransferBuffer(tvb);
        memcpy(gpu.MapTransferBuffer(tib, false), effectIdx, sizeof(effectIdx));
        gpu.UnmapTransferBuffer(tib);
        GpuCmdBufferHandle effectUploadCmd = gpu.AcquireCommandBuffer();
        gpu.UploadToBuffer(effectUploadCmd, tvb, 0, _effectVbuf, 0, sizeof(effectVerts));
        gpu.UploadToBuffer(effectUploadCmd, tib, 0, _effectIbuf, 0, sizeof(effectIdx));
        gpu.SubmitCommandBuffer(effectUploadCmd);
        gpu.ReleaseTransferBuffer(tvb);
        gpu.ReleaseTransferBuffer(tib);
    }

    if (logInit) {
        LOG_INFO("Render pass initialized: {}", _passname.c_str());
    }
    return true;
}

// ── _createShaders (WebGPU) ───────────────────────────────────────────────────

void SpriteRenderPass::_createShaders() {
    IGpu &gpu = Renderer::GetGpu();

    GpuShaderCreateInfo vsi {};
    vsi.code                = reinterpret_cast<const uint8_t *>(SPRITE_VERT_WGSL);
    vsi.codeSize            = strlen(SPRITE_VERT_WGSL);
    vsi.entrypoint          = "vs_main";
    vsi.stage               = GpuShaderStage::Vertex;
    vsi.samplerCount        = 0;
    vsi.uniformBufferCount  = 2;
    vsi.storageBufferCount  = 1;
    vsi.storageTextureCount = 0;
    _vertexShader           = gpu.CreateShader(vsi);

    GpuShaderCreateInfo fsi {};
    fsi.code                = reinterpret_cast<const uint8_t *>(SPRITE_FRAG_WGSL);
    fsi.codeSize            = strlen(SPRITE_FRAG_WGSL);
    fsi.entrypoint          = "fs_main";
    fsi.stage               = GpuShaderStage::Fragment;
    fsi.samplerCount        = 1;
    fsi.uniformBufferCount  = 0;
    fsi.storageBufferCount  = 0;
    fsi.storageTextureCount = 0;
    _fragmentShader         = gpu.CreateShader(fsi);

    if (!_vertexShader || !_fragmentShader) {
        LOG_CRITICAL("SpriteRenderPass: failed to load WGSL shaders for {}", _passname);
    }
}

// Resize fast-path counterpart of the SDL backend. The WebGPU sprite pass has no depth target
// and grows its effect ping-pong textures on demand inside Render() (capped at the surface
// size), so resizing just needs to update the surface dims — no texture recreation here.
void SpriteRenderPass::OnResize(uint32_t surfaceWidth, uint32_t surfaceHeight) {
    if (surfaceWidth == 0 || surfaceHeight == 0)
        return;
    _surfaceWidth  = surfaceWidth;
    _surfaceHeight = surfaceHeight;
}

// ── Render (WebGPU) ──────────────────────────────────────────────────────────

void SpriteRenderPass::Render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4 &camera) {
    auto &gpu = Renderer::GetGpu();

    // Resize effect ping-pong textures when the physical window grows past their current
    // dims so UV [0..1] keeps mapping to the fully-populated area. Shrinking is fine to
    // leave (the unused tail of the texture costs little; recreate only on grow to avoid
    // thrash). Capped at _surfaceWidth/height so we never allocate beyond the desktop.
    {
        uint32_t pw = static_cast<uint32_t>(std::min((float)Window::GetPhysicalWidth(), (float)_surfaceWidth));
        uint32_t ph = static_cast<uint32_t>(std::min((float)Window::GetPhysicalHeight(), (float)_surfaceHeight));
        if (pw > _effectTexW || ph > _effectTexH) {
            if (_effectTexA) {
                gpu.ReleaseTexture(_effectTexA);
                _effectTexA = 0;
            }
            if (_effectTexB) {
                gpu.ReleaseTexture(_effectTexB);
                _effectTexB = 0;
            }
            _effectTexW = std::max(pw, _effectTexW);
            _effectTexH = std::max(ph, _effectTexH);
            GpuTextureCreateInfo texInfo {};
            texInfo.width       = _effectTexW;
            texInfo.height      = _effectTexH;
            texInfo.format      = _swapchainFormat;
            texInfo.usage       = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
            texInfo.sampleCount = GpuSampleCount::X1;
            _effectTexA         = gpu.CreateTexture(texInfo);
            _effectTexB         = gpu.CreateTexture(texInfo);
        }
    }

    if (!renderQueue || renderQueue->Count() == 0) {
        // Empty queue — just clear
        GpuColorTargetInfo ct {};
        ct.texture = targetTexture;
        ct.loadOp  = colorTargetInfoLoadOp;
        ct.storeOp = GpuStoreOp::Store;
        ct.clearR  = colorTargetClearR;
        ct.clearG  = colorTargetClearG;
        ct.clearB  = colorTargetClearB;
        ct.clearA  = colorTargetClearA;
        auto rp    = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
        gpu.EndRenderPass(rp);
        return;
    }

    size_t spriteCount = renderQueue->Count();

    // Pack sprite data into transfer buffer
    auto *dataPtr = static_cast<CompactSpriteInstance *>(gpu.MapTransferBuffer(_spriteDataTransferBuffer, false));
    for (size_t i = 0; i < spriteCount; ++i) {
        const auto &sprite   = (*renderQueue)[i];
        float       x        = sprite.x;
        float       y        = sprite.y;
        float       z        = sprite.z;
        float       rotation = sprite.rotation;
        float       texU     = fastClamp(sprite.texU, 0.0f, 1.0f);
        float       texV     = fastClamp(sprite.texV, 0.0f, 1.0f);
        float       texW     = fastClamp(sprite.texW, -1.0f, 1.0f);
        float       texH     = fastClamp(sprite.texH, -1.0f, 1.0f);
        float       r        = fastClamp(sprite.r, 0.0f, 1.0f);
        float       g        = fastClamp(sprite.g, 0.0f, 1.0f);
        float       b        = fastClamp(sprite.b, 0.0f, 1.0f);
        float       a        = fastClamp(sprite.a, 0.0f, 1.0f);
        float       w        = fastMax(sprite.w, 0.001f);
        float       h        = fastMax(sprite.h, 0.001f);
        float       pivotX   = sprite.pivotX;
        float       pivotY   = sprite.pivotY;
        bool        isSDF    = sprite.isSDF;

        dataPtr[i].posXy     = packHalf2(x, y);
        dataPtr[i].posZRot   = packHalf2(z, rotation);
        dataPtr[i].texUv     = packHalf2(texU, texV);
        dataPtr[i].texWh     = packHalf2(texW, texH);
        dataPtr[i].colorRg   = packHalf2(r, g);
        dataPtr[i].colorBa   = packHalf2(b, a);
        dataPtr[i].sizeWh    = packHalf2(w, h);
        uint32_t pivotPacked = packHalf2(pivotX, pivotY);
        if (isSDF)
            pivotPacked |= 0x80000000u;
        dataPtr[i].pivotXy = pivotPacked;
    }
    gpu.UnmapTransferBuffer(_spriteDataTransferBuffer);

    // Upload to GPU buffer
    gpu.UploadToBuffer(cmdBuffer, _spriteDataTransferBuffer, 0, _spriteDataBuffer, 0,
        static_cast<uint32_t>(spriteCount * sizeof(CompactSpriteInstance)));

    // Build batches
    std::vector<Batch> batches;
    batches.reserve(64);
    size_t currentOffset = 0;
    for (size_t i = 0; i < spriteCount; ++i) {
        const auto &cur            = (*renderQueue)[i];
        bool        geomChanged    = (i > 0 && cur.geometry != (*renderQueue)[i - 1].geometry);
        bool        textureChanged = (i > 0 && cur.texture.gpuTexture != (*renderQueue)[i - 1].texture.gpuTexture);
        bool        effectChanged  = (i > 0 && cur.effectIndex != (*renderQueue)[i - 1].effectIndex);

        if (i == 0 || geomChanged || textureChanged || effectChanged) {
            Batch batch;
            batch.offset = currentOffset;
            batch.count  = 1;
            // Use per-renderable geometry when provided; otherwise unit quad.
            if (cur.geometry && cur.geometry->vertexBuffer && cur.geometry->indexBuffer) {
                batch.vertexBuffer = cur.geometry->vertexBuffer;
                batch.indexBuffer  = cur.geometry->indexBuffer;
                batch.indexCount   = static_cast<uint32_t>(cur.geometry->GetIndexCount());
            } else {
                batch.vertexBuffer = _quadVertexBuf;
                batch.indexBuffer  = _quadIndexBuf;
                batch.indexCount   = 6;
            }
            batch.texture = cur.texture.gpuTexture;
            batch.sampler = cur.texture.gpuSampler;
            batches.push_back(batch);
        } else {
            batches.back().count++;
        }
        currentOffset++;
    }

    // Determine if any batch uses effects
    bool        hasAnyEffects = false;
    const auto &effectStore   = Draw::GetEffectStore();
    for (size_t i = 0; i < batches.size(); ++i) {
        size_t  spriteIdx = batches[i].offset;
        int32_t effectIdx = (*renderQueue)[spriteIdx].effectIndex;
        if (effectIdx >= 0 && effectIdx < (int32_t)effectStore.size() && !effectStore[effectIdx].empty()) {
            hasAnyEffects = true;
            break;
        }
    }

    if (!hasAnyEffects) {
        // Simple path: one render pass, all batches go directly to target
        GpuColorTargetInfo ct {};
        ct.texture = targetTexture;
        ct.loadOp  = colorTargetInfoLoadOp;
        ct.storeOp = GpuStoreOp::Store;
        ct.clearR  = colorTargetClearR;
        ct.clearG  = colorTargetClearG;
        ct.clearB  = colorTargetClearB;
        ct.clearA  = colorTargetClearA;
        auto rp    = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
        // Restrict draws to the window's physical-pixel region of the desktop-sized primary FB.
        // For custom FBs sized smaller than the desktop, _surfaceWidth caps the viewport so
        // we don't over-clip a smaller intermediate.
        {
            float vpW = std::min((float)Window::GetPhysicalWidth(), (float)_surfaceWidth);
            float vpH = std::min((float)Window::GetPhysicalHeight(), (float)_surfaceHeight);
            gpu.SetViewport(rp, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
        }
        gpu.BindGraphicsPipeline(rp, _pipeline);
        gpu.BindVertexStorageBuffers(rp, 0, &_spriteDataBuffer, 1);

        // Camera is identical for every batch; push once and let it persist across draws.
        gpu.PushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));

        for (const auto &batch : batches) {
            if (!batch.texture || !batch.sampler || !batch.vertexBuffer || !batch.indexBuffer)
                continue;
            GpuBufferBinding vb { batch.vertexBuffer, 0 };
            gpu.BindVertexBuffers(rp, 0, &vb, 1);
            GpuBufferBinding ib { batch.indexBuffer, 0 };
            gpu.BindIndexBuffer(rp, ib, true);
            GpuTextureSamplerBinding tsb { batch.texture, batch.sampler };
            gpu.BindFragmentSamplers(rp, 0, &tsb, 1);
            uint32_t instOff[8] = {};
            instOff[0]          = static_cast<uint32_t>(batch.offset);
            float instScale     = Window::GetScale();
            std::memcpy(&instOff[1], &instScale, sizeof(float)); // render scale -> MSDF AA sizing
            gpu.PushVertexUniformData(cmdBuffer, 1, instOff, 32);
            gpu.DrawIndexedPrimitives(rp, batch.indexCount, static_cast<uint32_t>(batch.count), 0, 0, 0);
        }
        gpu.EndRenderPass(rp);
    } else {
        // Effect path: iterate batches, switching between direct and effect rendering
        GpuRenderPassHandle currentPass = 0;
        bool                passIsOpen  = false;

        auto openSpritePass = [&](bool isFirst) {
            if (passIsOpen)
                return;
            GpuColorTargetInfo ct {};
            ct.texture  = targetTexture;
            ct.loadOp   = isFirst ? colorTargetInfoLoadOp : GpuLoadOp::Load;
            ct.storeOp  = GpuStoreOp::Store;
            ct.clearR   = colorTargetClearR;
            ct.clearG   = colorTargetClearG;
            ct.clearB   = colorTargetClearB;
            ct.clearA   = colorTargetClearA;
            currentPass = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
            gpu.BindGraphicsPipeline(currentPass, _pipeline);
            gpu.BindVertexStorageBuffers(currentPass, 0, &_spriteDataBuffer, 1);
            // Push once per pass; camera is constant across the batches drawn into it.
            gpu.PushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));
            // Same viewport restriction as the direct (no-effect-batches) path — without
            // this, non-effect batches that happen to share a Render() with an effect batch
            // render into the full FB texture instead of the window-physical region.
            {
                float vpW = std::min((float)Window::GetPhysicalWidth(), (float)_surfaceWidth);
                float vpH = std::min((float)Window::GetPhysicalHeight(), (float)_surfaceHeight);
                gpu.SetViewport(currentPass, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
            }
            passIsOpen = true;
        };
        auto closeSpritePass = [&]() {
            if (!passIsOpen)
                return;
            gpu.EndRenderPass(currentPass);
            currentPass = 0;
            passIsOpen  = false;
        };

        bool firstBatch = true;
        for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
            const auto &batch = batches[batchIdx];
            if (!batch.texture || !batch.sampler || !batch.vertexBuffer || !batch.indexBuffer)
                continue;

            size_t  spriteIdx      = batch.offset;
            int32_t effectIdx      = (*renderQueue)[spriteIdx].effectIndex;
            bool    batchHasEffect = (effectIdx >= 0 && effectIdx < (int32_t)effectStore.size()
                && !effectStore[effectIdx].empty());

            if (!batchHasEffect) {
                openSpritePass(firstBatch);
                firstBatch = false;

                GpuBufferBinding vb { batch.vertexBuffer, 0 };
                gpu.BindVertexBuffers(currentPass, 0, &vb, 1);
                GpuBufferBinding ib { batch.indexBuffer, 0 };
                gpu.BindIndexBuffer(currentPass, ib, true);
                GpuTextureSamplerBinding tsb { batch.texture, batch.sampler };
                gpu.BindFragmentSamplers(currentPass, 0, &tsb, 1);
                uint32_t instOff[8] = {};
                instOff[0]          = static_cast<uint32_t>(batch.offset);
                float instScale     = Window::GetScale();
                std::memcpy(&instOff[1], &instScale, sizeof(float)); // render scale -> MSDF AA sizing
                gpu.PushVertexUniformData(cmdBuffer, 1, instOff, 32);
                gpu.DrawIndexedPrimitives(currentPass, batch.indexCount, static_cast<uint32_t>(batch.count), 0, 0, 0);
            } else {
                closeSpritePass();
                firstBatch = false;

                const auto &effects = effectStore[effectIdx];

                // Render sprite batch to effectTempA
                {
                    GpuColorTargetInfo ct {};
                    ct.texture = _effectTexA;
                    ct.loadOp  = GpuLoadOp::Clear;
                    ct.storeOp = GpuStoreOp::Store;
                    ct.clearR = ct.clearG = ct.clearB = ct.clearA = 0.0f;
                    auto tmpRp                                    = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
                    // Replace-blend pipeline so the sprite's alpha survives into tempA;
                    // _pipeline preserves dst alpha which would zero it out.
                    gpu.BindGraphicsPipeline(tmpRp, _effectSpritePipeline);
                    gpu.BindVertexStorageBuffers(tmpRp, 0, &_spriteDataBuffer, 1);
                    {
                        // Set viewport AFTER bindPipeline — some WebGPU impls reset dynamic state on pipeline binding.
                        float       vpW       = std::min((float)Window::GetPhysicalWidth(), (float)_surfaceWidth);
                        float       vpH       = std::min((float)Window::GetPhysicalHeight(), (float)_surfaceHeight);
                        static bool loggedTmp = false;
                        if (!loggedTmp) {
                            loggedTmp = true;
                            LOG_INFO("effect sprite-to-tempA viewport: phys={}x{} surface={}x{} vp={}x{}",
                                Window::GetPhysicalWidth(), Window::GetPhysicalHeight(),
                                (int)_surfaceWidth, (int)_surfaceHeight, (int)vpW, (int)vpH);
                        }
                        gpu.SetViewport(tmpRp, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
                    }
                    GpuBufferBinding vb { batch.vertexBuffer, 0 };
                    gpu.BindVertexBuffers(tmpRp, 0, &vb, 1);
                    GpuBufferBinding ib { batch.indexBuffer, 0 };
                    gpu.BindIndexBuffer(tmpRp, ib, true);
                    GpuTextureSamplerBinding tsb { batch.texture, batch.sampler };
                    gpu.BindFragmentSamplers(tmpRp, 0, &tsb, 1);
                    gpu.PushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));
                    uint32_t instOff[8] = {};
                    instOff[0]          = static_cast<uint32_t>(batch.offset);
                    float instScale     = Window::GetScale();
                    std::memcpy(&instOff[1], &instScale, sizeof(float)); // render scale -> MSDF AA sizing
                    gpu.PushVertexUniformData(cmdBuffer, 1, instOff, 32);
                    gpu.DrawIndexedPrimitives(tmpRp, batch.indexCount, static_cast<uint32_t>(batch.count), 0, 0, 0);
                    gpu.EndRenderPass(tmpRp);
                }

                // Apply effects: effectTempA → targetTexture
                const auto                                                                &effectTextureStore = Draw::GetEffectTextureStore();
                const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>> emptyExtraTex;
                const auto                                                                &storedTextures = (effectIdx < (int32_t)effectTextureStore.size())
                                                                                   ? effectTextureStore[effectIdx]
                                                                                   : emptyExtraTex;
                _applyEffectsWGPU(cmdBuffer, effects, _effectTexA, targetTexture, storedTextures,
                    batchIdx == 0 ? colorTargetInfoLoadOp : GpuLoadOp::Load,
                    colorTargetClearR, colorTargetClearG,
                    colorTargetClearB, colorTargetClearA);
            }
        }
        closeSpritePass();
    }
}

// ── Effect pipeline helpers (WebGPU) ─────────────────────────────────────────

GpuGraphicsPipelineHandle SpriteRenderPass::_getOrCreateEffectPipeline(
    const ShaderAsset &vertShader, const ShaderAsset &fragShader) {
    auto it = _effectPipelines.find(fragShader.gpuShader);
    if (it != _effectPipelines.end())
        return it->second;

    auto &gpu = Renderer::GetGpu();

    GpuVertexAttribute attrs[2] = {
        { .location = 0, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 0 },
        { .location = 1, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 8 },
    };
    GpuVertexBinding vbind { .binding = 0, .stride = 16, .instanceStepping = false };

    GpuColorTargetBlendState blend {};
    blend.blendEnabled   = true;
    blend.srcColorFactor = GpuBlendFactor::SrcAlpha;
    blend.dstColorFactor = GpuBlendFactor::OneMinusSrcAlpha;
    blend.colorOp        = GpuBlendOp::Add;
    blend.srcAlphaFactor = GpuBlendFactor::One;
    blend.dstAlphaFactor = GpuBlendFactor::OneMinusSrcAlpha;
    blend.alphaOp        = GpuBlendOp::Add;

    GpuGraphicsPipelineCreateInfo pci {};
    pci.vertexShader             = vertShader.gpuShader;
    pci.fragmentShader           = fragShader.gpuShader;
    pci.attributes               = attrs;
    pci.attributeCount           = 2;
    pci.bindings                 = &vbind;
    pci.bindingCount             = 1;
    pci.fillMode                 = GpuFillMode::Fill;
    pci.cullMode                 = GpuCullMode::None;
    pci.frontFace                = GpuFrontFace::CounterClockwise;
    pci.colorTargetFormat        = _swapchainFormat;
    pci.blend                    = blend;
    pci.hasDepthTarget           = false;
    pci.sampleCount              = GpuSampleCount::X1;
    pci.vertexStorageBufferCount = 0;

    GpuGraphicsPipelineHandle pipeline     = gpu.CreateGraphicsPipeline(pci);
    _effectPipelines[fragShader.gpuShader] = pipeline;
    return pipeline;
}

void SpriteRenderPass::_applyEffectsWGPU(
    GpuCmdBufferHandle                                                          cmdBuffer,
    const std::vector<EffectAsset>                                             &effects,
    GpuTextureHandle                                                            sourceTexture,
    GpuTextureHandle                                                            targetTexture,
    const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>> &extraTextures,
    GpuLoadOp                                                                   targetLoadOp,
    float clearR, float clearG, float clearB, float clearA) {
    auto &gpu = Renderer::GetGpu();

    // Effect ping-pong textures are sized to the physical window, so UV [0..1] is the
    // full populated area — no per-call quad rewrite needed (the init-time quad already
    // uses [0..1]).
    GpuTextureHandle readTex  = sourceTexture;
    GpuTextureHandle writeTex = _effectTexB;

    for (size_t i = 0; i < effects.size(); ++i) {
        const auto &effect = effects[i];
        bool        isLast = (i == effects.size() - 1);
        if (isLast)
            writeTex = targetTexture;

        GpuGraphicsPipelineHandle pipeline = _getOrCreateEffectPipeline(effect.vertShader, effect.fragShader);
        if (!pipeline) {
            LOG_ERROR("_applyEffectsWGPU: failed to get/create effect pipeline");
            continue;
        }

        GpuColorTargetInfo ct {};
        ct.texture = writeTex;
        ct.loadOp  = isLast ? targetLoadOp : GpuLoadOp::Clear;
        ct.clearR  = clearR;
        ct.clearG  = clearG;
        ct.clearB  = clearB;
        ct.clearA  = clearA;
        ct.storeOp = GpuStoreOp::Store;
        auto rp    = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
        gpu.BindGraphicsPipeline(rp, pipeline);
        {
            // Set viewport AFTER bindPipeline (see sprite-to-tempA note).
            float vpW = std::min((float)Window::GetPhysicalWidth(), (float)_surfaceWidth);
            float vpH = std::min((float)Window::GetPhysicalHeight(), (float)_surfaceHeight);
            gpu.SetViewport(rp, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
        }

        GpuBufferBinding vb { _effectVbuf, 0 };
        gpu.BindVertexBuffers(rp, 0, &vb, 1);
        GpuBufferBinding ib { _effectIbuf, 0 };
        gpu.BindIndexBuffer(rp, ib, true);

        const uint32_t                        pairCount = effect.fragShader.samplerCount > 0 ? effect.fragShader.samplerCount : 1;
        std::vector<GpuTextureSamplerBinding> tsbs(pairCount);
        for (uint32_t s = 0; s < pairCount; ++s) {
            auto it = extraTextures.find(s);
            if (it != extraTextures.end()) {
                tsbs[s].texture = it->second.first;
                tsbs[s].sampler = Renderer::GetSampler(it->second.second);
            } else {
                tsbs[s].texture = readTex;
                tsbs[s].sampler = _effectSampler;
            }
        }
        gpu.BindFragmentSamplers(rp, 0, tsbs.data(), pairCount);

        if (effect.uniforms && effect.uniforms->GetBufferSize() > 0) {
            gpu.PushFragmentUniformData(cmdBuffer, 0,
                effect.uniforms->GetBufferPointer(),
                static_cast<uint32_t>(effect.uniforms->GetBufferSize()));
        }

        gpu.DrawIndexedPrimitives(rp, 6, 1, 0, 0, 0);
        gpu.EndRenderPass(rp);

        if (!isLast) {
            readTex  = writeTex;
            writeTex = (writeTex == _effectTexB) ? _effectTexA : _effectTexB;
        }
    }
}
