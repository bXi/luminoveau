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
static constexpr const char* kSpriteVertWGSL = R"(
struct SpriteData {
    pos_xy   : u32,
    pos_z_rot: u32,
    tex_uv   : u32,
    tex_wh   : u32,
    color_rg : u32,
    color_ba : u32,
    size_wh  : u32,
    pivot_xy : u32,
}

struct UniformBlock {
    viewProjection : mat4x4<f32>,
}

struct InstanceOffset {
    baseInstance : u32,
    _pad         : vec3<u32>,
}

@group(0) @binding(0) var<uniform>       uniforms        : UniformBlock;
@group(0) @binding(1) var<uniform>       instOffset      : InstanceOffset;
@group(3) @binding(0) var<storage, read> spriteInstances : array<SpriteData>;

struct VertOut {
    @builtin(position)               position  : vec4<f32>,
    @location(0)                     texCoord  : vec2<f32>,
    @location(1)                     color     : vec4<f32>,
    @location(2) @interpolate(flat)  isSDF     : u32,
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

    let x        = unpackHalfLo(sprite.pos_xy);
    let y        = unpackHalfHi(sprite.pos_xy);
    let rotation = unpackHalfHi(sprite.pos_z_rot);
    let texUV    = unpackHalf2(sprite.tex_uv);
    let texWH    = unpackHalf2(sprite.tex_wh);
    let color    = vec4<f32>(
        unpackHalfLo(sprite.color_rg),
        unpackHalfHi(sprite.color_rg),
        unpackHalfLo(sprite.color_ba),
        unpackHalfHi(sprite.color_ba),
    );
    let scale = unpackHalf2(sprite.size_wh);

    let pivotPacked  = sprite.pivot_xy;
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
    return out;
}
)";

static constexpr const char* kSpriteFragWGSL = R"(
@group(2) @binding(0) var gSampler : sampler;
@group(2) @binding(1) var gTexture : texture_2d<f32>;

struct FragIn {
    @location(0)                     texCoord : vec2<f32>,
    @location(1)                     color    : vec4<f32>,
    @location(2) @interpolate(flat)  isSDF    : u32,
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
    let screenPxRange    = max(dot(msdfUnit, 0.5 / texDeriv), 1.0);
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

// ── release / init (WebGPU) ──────────────────────────────────────────────────

void SpriteRenderPass::release(bool logRelease) {
    IGpu& gpu = Renderer::GetGpu();

    for (auto& [key, pipeline] : m_effect_pipelines) {
        if (pipeline) gpu.releaseGraphicsPipeline(pipeline);
    }
    m_effect_pipelines.clear();

    if (m_effect_tex_a)    { gpu.releaseTexture(m_effect_tex_a);          m_effect_tex_a    = 0; }
    if (m_effect_tex_b)    { gpu.releaseTexture(m_effect_tex_b);          m_effect_tex_b    = 0; }
    if (m_effect_sampler)  { gpu.releaseSampler(m_effect_sampler);        m_effect_sampler  = 0; }
    if (m_effect_vbuf)     { gpu.releaseBuffer(m_effect_vbuf);            m_effect_vbuf     = 0; }
    if (m_effect_ibuf)     { gpu.releaseBuffer(m_effect_ibuf);            m_effect_ibuf     = 0; }
    if (m_quad_vertex_buf) { gpu.releaseBuffer(m_quad_vertex_buf);        m_quad_vertex_buf = 0; }
    if (m_quad_index_buf)  { gpu.releaseBuffer(m_quad_index_buf);         m_quad_index_buf  = 0; }
    if (m_quad_xfer_vert)  { gpu.releaseTransferBuffer(m_quad_xfer_vert); m_quad_xfer_vert  = 0; }
    if (m_quad_xfer_idx)   { gpu.releaseTransferBuffer(m_quad_xfer_idx);  m_quad_xfer_idx   = 0; }

    if (SpriteDataTransferBuffer) { gpu.releaseTransferBuffer(SpriteDataTransferBuffer); SpriteDataTransferBuffer = 0; }
    if (SpriteDataBuffer)         { gpu.releaseBuffer(SpriteDataBuffer);                 SpriteDataBuffer         = 0; }
    if (vertex_shader)            { gpu.releaseShader(vertex_shader);                    vertex_shader            = 0; }
    if (fragment_shader)          { gpu.releaseShader(fragment_shader);                  fragment_shader          = 0; }
    if (m_pipeline)               { gpu.releaseGraphicsPipeline(m_pipeline);             m_pipeline               = 0; }
    if (m_effect_sprite_pipeline) { gpu.releaseGraphicsPipeline(m_effect_sprite_pipeline); m_effect_sprite_pipeline = 0; }

    renderQueue = nullptr;

    if (logRelease) {
        LOG_INFO("Released graphics pipeline: {}", passname.c_str());
    }
}

bool SpriteRenderPass::init(
    GpuTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name, bool logInit,
    size_t capacity, bool forceNoMSAA) {
    m_noMSAA           = forceNoMSAA;
    passname           = std::move(name);
    m_surface_width    = surface_width;
    m_surface_height   = surface_height;
    m_swapchain_format = swapchain_texture_format;

    IGpu& gpu = Renderer::GetGpu();
    renderQueue = BufferManager::Create<Renderable>(passname + "_renderQueue", capacity > 0 ? capacity : MAX_SPRITES);

    createShaders();

    GpuVertexAttribute vertexAttributes[] = {
        { .location = 0, .binding = 0, .format = GpuVertexElementFormat::UInt, .offset = 0 },
        { .location = 1, .binding = 0, .format = GpuVertexElementFormat::UInt, .offset = 4 },
    };
    GpuVertexBinding vertexBinding = { .binding = 0, .stride = 8, .instanceStepping = false };

    GpuGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertexShader             = vertex_shader;
    pipelineInfo.fragmentShader           = fragment_shader;
    pipelineInfo.attributes               = vertexAttributes;
    pipelineInfo.attributeCount           = 2;
    pipelineInfo.bindings                 = &vertexBinding;
    pipelineInfo.bindingCount             = 1;
    pipelineInfo.fillMode                 = GpuFillMode::Fill;
    pipelineInfo.cullMode                 = GpuCullMode::None;
    pipelineInfo.frontFace                = GpuFrontFace::CounterClockwise;
    pipelineInfo.colorTargetFormat        = swapchain_texture_format;
    pipelineInfo.blend                    = renderPassBlendState;
    pipelineInfo.hasDepthTarget           = false;
    pipelineInfo.sampleCount              = GpuSampleCount::x1;
    pipelineInfo.vertexStorageBufferCount = 1;
    m_pipeline = gpu.createGraphicsPipeline(pipelineInfo);

    if (!m_pipeline) {
        LOG_CRITICAL("SpriteRenderPass: failed to create pipeline for {}", passname);
        return false;
    }

    // Second pipeline for the effect path's sprite-to-tempA draw. The primary m_pipeline uses
    // AlphaBlendKeepDstAlpha (preserves the framebuffer's existing alpha when compositing the
    // sprite layer), which is wrong when drawing into a freshly-cleared (alpha=0) effect
    // ping-pong texture — the sprite's RGB lands but the alpha stays 0, so the downstream
    // effect quad samples fully-transparent texels and produces no visible output. This
    // variant uses straight One/Zero replace blending so tempA ends up with the sprite's
    // own alpha. Mirrors SDL's effectSpritePipeline.
    {
        GpuColorTargetBlendState replaceBlend{};
        replaceBlend.blendEnabled    = true;
        replaceBlend.srcColorFactor  = GpuBlendFactor::One;
        replaceBlend.dstColorFactor  = GpuBlendFactor::Zero;
        replaceBlend.colorOp         = GpuBlendOp::Add;
        replaceBlend.srcAlphaFactor  = GpuBlendFactor::One;
        replaceBlend.dstAlphaFactor  = GpuBlendFactor::Zero;
        replaceBlend.alphaOp         = GpuBlendOp::Add;

        GpuGraphicsPipelineCreateInfo effSpritePci = pipelineInfo;
        effSpritePci.blend = replaceBlend;
        m_effect_sprite_pipeline = gpu.createGraphicsPipeline(effSpritePci);
        if (!m_effect_sprite_pipeline) {
            LOG_CRITICAL("SpriteRenderPass: failed to create effect-sprite pipeline for {}", passname);
            return false;
        }
    }

    SpriteDataTransferBuffer = gpu.createTransferBuffer({
        static_cast<uint32_t>(MAX_SPRITES * sizeof(CompactSpriteInstance)),
        GpuTransferUsage::Upload,
    });
    SpriteDataBuffer = gpu.createBuffer({
        static_cast<uint32_t>(MAX_SPRITES * sizeof(CompactSpriteInstance)),
        GpuBufferUsage::StorageRead,
    });

    // Unit quad geometry (CompactVertex2D — pos_xy and uv packed as uint32 half-floats).
    struct QuadVertex { uint32_t pos_xy; uint32_t uv; };
    auto packHalf = [](float a, float b) -> uint32_t {
        auto toHalf = [](float f) -> uint16_t {
            union { float f; uint32_t i; } u = {f};
            uint32_t bits = u.i;
            uint32_t sign = (bits >> 16) & 0x8000;
            int32_t  exp  = ((bits >> 23) & 0xFF) - 127 + 15;
            uint32_t mant = (bits >> 13) & 0x3FF;
            if (exp <= 0) return static_cast<uint16_t>(sign);
            if (exp >= 31) return static_cast<uint16_t>(sign | 0x7C00);
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

    m_quad_xfer_vert  = gpu.createTransferBuffer({ sizeof(quadVerts), GpuTransferUsage::Upload });
    m_quad_xfer_idx   = gpu.createTransferBuffer({ sizeof(quadIdx),   GpuTransferUsage::Upload });
    m_quad_vertex_buf = gpu.createBuffer({ sizeof(quadVerts), GpuBufferUsage::Vertex });
    m_quad_index_buf  = gpu.createBuffer({ sizeof(quadIdx),   GpuBufferUsage::Index  });

    memcpy(gpu.mapTransferBuffer(m_quad_xfer_vert, false), quadVerts, sizeof(quadVerts));
    gpu.unmapTransferBuffer(m_quad_xfer_vert);
    memcpy(gpu.mapTransferBuffer(m_quad_xfer_idx,  false), quadIdx,   sizeof(quadIdx));
    gpu.unmapTransferBuffer(m_quad_xfer_idx);

    GpuCmdBufferHandle uploadCmd = gpu.acquireCommandBuffer();
    gpu.uploadToBuffer(uploadCmd, m_quad_xfer_vert, 0, m_quad_vertex_buf, 0, sizeof(quadVerts));
    gpu.uploadToBuffer(uploadCmd, m_quad_xfer_idx,  0, m_quad_index_buf,  0, sizeof(quadIdx));
    gpu.submitCommandBuffer(uploadCmd);

    // Effect ping-pong textures — sized to the physical window, NOT the desktop-sized
    // surface. User-supplied effect fragment shaders typically assume input UV [0..1]
    // maps to the full populated area; if we sized these at surface_width the populated
    // sprite content would only occupy the top-left physW×physH fraction and any shader
    // sampling at offsets/derivatives would misbehave. Recreated in render() if the
    // physical window grows.
    // Cap effect-tex dims to the surface (FB size). For fixedSize FBs like LightToy's
    // hrc_scene (1348×783) we want UV [0..1] to span the actual usable area, not the full
    // physical window — otherwise the effect quad samples uninitialized pixels past the
    // surface's content region.
    {
        uint32_t pw = static_cast<uint32_t>(Window::GetPhysicalWidth());
        uint32_t ph = static_cast<uint32_t>(Window::GetPhysicalHeight());
        if (pw == 0 || ph == 0) { pw = surface_width; ph = surface_height; }
        m_effect_tex_w = std::min(pw, surface_width);
        m_effect_tex_h = std::min(ph, surface_height);
    }
    {
        GpuTextureCreateInfo texInfo{};
        texInfo.width       = m_effect_tex_w;
        texInfo.height      = m_effect_tex_h;
        texInfo.format      = swapchain_texture_format;
        texInfo.usage       = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
        texInfo.sampleCount = GpuSampleCount::x1;
        m_effect_tex_a = gpu.createTexture(texInfo);
        m_effect_tex_b = gpu.createTexture(texInfo);
    }

    {
        GpuSamplerCreateInfo si{};
        si.minFilter = GpuFilter::Nearest;
        si.magFilter = GpuFilter::Nearest;
        m_effect_sampler = gpu.createSampler(si);
    }

    // Effect fullscreen quad — position (0..1) + texcoord (0..1), 16 bytes per vertex.
    struct EffectVertex { float px, py, ux, uy; };
    EffectVertex effectVerts[4] = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
    };
    uint16_t effectIdx[6] = { 0, 1, 2, 2, 1, 3 };

    m_effect_vbuf = gpu.createBuffer({ sizeof(effectVerts), GpuBufferUsage::Vertex });
    m_effect_ibuf = gpu.createBuffer({ sizeof(effectIdx),   GpuBufferUsage::Index  });
    {
        GpuTransferBufferHandle tvb = gpu.createTransferBuffer({ sizeof(effectVerts), GpuTransferUsage::Upload });
        GpuTransferBufferHandle tib = gpu.createTransferBuffer({ sizeof(effectIdx),   GpuTransferUsage::Upload });
        memcpy(gpu.mapTransferBuffer(tvb, false), effectVerts, sizeof(effectVerts));
        gpu.unmapTransferBuffer(tvb);
        memcpy(gpu.mapTransferBuffer(tib, false), effectIdx, sizeof(effectIdx));
        gpu.unmapTransferBuffer(tib);
        GpuCmdBufferHandle effectUploadCmd = gpu.acquireCommandBuffer();
        gpu.uploadToBuffer(effectUploadCmd, tvb, 0, m_effect_vbuf, 0, sizeof(effectVerts));
        gpu.uploadToBuffer(effectUploadCmd, tib, 0, m_effect_ibuf, 0, sizeof(effectIdx));
        gpu.submitCommandBuffer(effectUploadCmd);
        gpu.releaseTransferBuffer(tvb);
        gpu.releaseTransferBuffer(tib);
    }

    if (logInit) {
        LOG_INFO("Render pass initialized: {}", passname.c_str());
    }
    return true;
}

// ── createShaders (WebGPU) ───────────────────────────────────────────────────

void SpriteRenderPass::createShaders() {
    IGpu& gpu = Renderer::GetGpu();

    GpuShaderCreateInfo vsi{};
    vsi.code                = reinterpret_cast<const uint8_t*>(kSpriteVertWGSL);
    vsi.codeSize            = strlen(kSpriteVertWGSL);
    vsi.entrypoint          = "vs_main";
    vsi.stage               = GpuShaderStage::Vertex;
    vsi.samplerCount        = 0;
    vsi.uniformBufferCount  = 2;
    vsi.storageBufferCount  = 1;
    vsi.storageTextureCount = 0;
    vertex_shader = gpu.createShader(vsi);

    GpuShaderCreateInfo fsi{};
    fsi.code                = reinterpret_cast<const uint8_t*>(kSpriteFragWGSL);
    fsi.codeSize            = strlen(kSpriteFragWGSL);
    fsi.entrypoint          = "fs_main";
    fsi.stage               = GpuShaderStage::Fragment;
    fsi.samplerCount        = 1;
    fsi.uniformBufferCount  = 0;
    fsi.storageBufferCount  = 0;
    fsi.storageTextureCount = 0;
    fragment_shader = gpu.createShader(fsi);

    if (!vertex_shader || !fragment_shader) {
        LOG_CRITICAL("SpriteRenderPass: failed to load WGSL shaders for {}", passname);
    }
}

// Resize fast-path counterpart of the SDL backend. The WebGPU sprite pass has no depth target
// and grows its effect ping-pong textures on demand inside render() (capped at the surface
// size), so resizing just needs to update the surface dims — no texture recreation here.
void SpriteRenderPass::onResize(uint32_t surfaceWidth, uint32_t surfaceHeight) {
    if (surfaceWidth == 0 || surfaceHeight == 0) return;
    m_surface_width  = surfaceWidth;
    m_surface_height = surfaceHeight;
}

// ── render (WebGPU) ──────────────────────────────────────────────────────────

void SpriteRenderPass::render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4& camera
) {
    auto& gpu = Renderer::GetGpu();

    // Resize effect ping-pong textures when the physical window grows past their current
    // dims so UV [0..1] keeps mapping to the fully-populated area. Shrinking is fine to
    // leave (the unused tail of the texture costs little; recreate only on grow to avoid
    // thrash). Capped at m_surface_width/height so we never allocate beyond the desktop.
    {
        uint32_t pw = static_cast<uint32_t>(std::min((float)Window::GetPhysicalWidth(),  (float)m_surface_width));
        uint32_t ph = static_cast<uint32_t>(std::min((float)Window::GetPhysicalHeight(), (float)m_surface_height));
        if (pw > m_effect_tex_w || ph > m_effect_tex_h) {
            if (m_effect_tex_a) { gpu.releaseTexture(m_effect_tex_a); m_effect_tex_a = 0; }
            if (m_effect_tex_b) { gpu.releaseTexture(m_effect_tex_b); m_effect_tex_b = 0; }
            m_effect_tex_w = std::max(pw, m_effect_tex_w);
            m_effect_tex_h = std::max(ph, m_effect_tex_h);
            GpuTextureCreateInfo texInfo{};
            texInfo.width       = m_effect_tex_w;
            texInfo.height      = m_effect_tex_h;
            texInfo.format      = m_swapchain_format;
            texInfo.usage       = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
            texInfo.sampleCount = GpuSampleCount::x1;
            m_effect_tex_a = gpu.createTexture(texInfo);
            m_effect_tex_b = gpu.createTexture(texInfo);
        }
    }

    if (!renderQueue || renderQueue->Count() == 0) {
        // Empty queue — just clear
        GpuColorTargetInfo ct{};
        ct.texture  = targetTexture;
        ct.loadOp   = color_target_info_loadop;
        ct.storeOp  = GpuStoreOp::Store;
        ct.clearR   = color_target_clear_r;
        ct.clearG   = color_target_clear_g;
        ct.clearB   = color_target_clear_b;
        ct.clearA   = color_target_clear_a;
        auto rp = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
        gpu.endRenderPass(rp);
        return;
    }

    size_t spriteCount = renderQueue->Count();

    // Pack sprite data into transfer buffer
    auto* dataPtr = static_cast<CompactSpriteInstance*>(gpu.mapTransferBuffer(SpriteDataTransferBuffer, false));
    for (size_t i = 0; i < spriteCount; ++i) {
        const auto& sprite = (*renderQueue)[i];
        float x       = sprite.x;
        float y       = sprite.y;
        float z       = sprite.z;
        float rotation = sprite.rotation;
        float tex_u   = fast_clamp(sprite.tex_u, 0.0f, 1.0f);
        float tex_v   = fast_clamp(sprite.tex_v, 0.0f, 1.0f);
        float tex_w   = fast_clamp(sprite.tex_w, -1.0f, 1.0f);
        float tex_h   = fast_clamp(sprite.tex_h, -1.0f, 1.0f);
        float r       = fast_clamp(sprite.r, 0.0f, 1.0f);
        float g       = fast_clamp(sprite.g, 0.0f, 1.0f);
        float b       = fast_clamp(sprite.b, 0.0f, 1.0f);
        float a       = fast_clamp(sprite.a, 0.0f, 1.0f);
        float w       = fast_max(sprite.w, 0.001f);
        float h       = fast_max(sprite.h, 0.001f);
        float pivot_x = sprite.pivot_x;
        float pivot_y = sprite.pivot_y;
        bool  isSDF   = sprite.isSDF;

        dataPtr[i].pos_xy    = pack_half2(x, y);
        dataPtr[i].pos_z_rot = pack_half2(z, rotation);
        dataPtr[i].tex_uv    = pack_half2(tex_u, tex_v);
        dataPtr[i].tex_wh    = pack_half2(tex_w, tex_h);
        dataPtr[i].color_rg  = pack_half2(r, g);
        dataPtr[i].color_ba  = pack_half2(b, a);
        dataPtr[i].size_wh   = pack_half2(w, h);
        uint32_t pivot_packed = pack_half2(pivot_x, pivot_y);
        if (isSDF) pivot_packed |= 0x80000000u;
        dataPtr[i].pivot_xy  = pivot_packed;
    }
    gpu.unmapTransferBuffer(SpriteDataTransferBuffer);

    // Upload to GPU buffer
    gpu.uploadToBuffer(cmdBuffer, SpriteDataTransferBuffer, 0, SpriteDataBuffer, 0,
                       static_cast<uint32_t>(spriteCount * sizeof(CompactSpriteInstance)));

    // Build batches
    std::vector<Batch> batches;
    batches.reserve(64);
    size_t currentOffset = 0;
    for (size_t i = 0; i < spriteCount; ++i) {
        const auto& cur = (*renderQueue)[i];
        bool geomChanged    = (i > 0 && cur.geometry != (*renderQueue)[i - 1].geometry);
        bool textureChanged = (i > 0 && cur.texture.gpuTexture != (*renderQueue)[i - 1].texture.gpuTexture);
        bool effectChanged  = (i > 0 && cur.effectIndex != (*renderQueue)[i - 1].effectIndex);

        if (i == 0 || geomChanged || textureChanged || effectChanged) {
            Batch batch;
            batch.offset  = currentOffset;
            batch.count   = 1;
            // Use per-renderable geometry when provided; otherwise unit quad.
            if (cur.geometry && cur.geometry->vertexBuffer && cur.geometry->indexBuffer) {
                batch.vertexBuffer = cur.geometry->vertexBuffer;
                batch.indexBuffer  = cur.geometry->indexBuffer;
                batch.indexCount   = static_cast<uint32_t>(cur.geometry->GetIndexCount());
            } else {
                batch.vertexBuffer = m_quad_vertex_buf;
                batch.indexBuffer  = m_quad_index_buf;
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
    bool hasAnyEffects = false;
    const auto& effectStore = Draw::GetEffectStore();
    for (size_t i = 0; i < batches.size(); ++i) {
        size_t spriteIdx = batches[i].offset;
        int32_t effectIdx = (*renderQueue)[spriteIdx].effectIndex;
        if (effectIdx >= 0 && effectIdx < (int32_t)effectStore.size() && !effectStore[effectIdx].empty()) {
            hasAnyEffects = true;
            break;
        }
    }

    if (!hasAnyEffects) {
        // Simple path: one render pass, all batches go directly to target
        GpuColorTargetInfo ct{};
        ct.texture  = targetTexture;
        ct.loadOp   = color_target_info_loadop;
        ct.storeOp  = GpuStoreOp::Store;
        ct.clearR   = color_target_clear_r;
        ct.clearG   = color_target_clear_g;
        ct.clearB   = color_target_clear_b;
        ct.clearA   = color_target_clear_a;
        auto rp = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
        // Restrict draws to the window's physical-pixel region of the desktop-sized primary FB.
        // For custom FBs sized smaller than the desktop, m_surface_width caps the viewport so
        // we don't over-clip a smaller intermediate.
        {
            float vpW = std::min((float)Window::GetPhysicalWidth(),  (float)m_surface_width);
            float vpH = std::min((float)Window::GetPhysicalHeight(), (float)m_surface_height);
            gpu.setViewport(rp, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
        }
        gpu.bindGraphicsPipeline(rp, m_pipeline);
        gpu.bindVertexStorageBuffers(rp, 0, &SpriteDataBuffer, 1);

        // Camera is identical for every batch; push once and let it persist across draws.
        gpu.pushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));

        for (const auto& batch : batches) {
            if (!batch.texture || !batch.sampler || !batch.vertexBuffer || !batch.indexBuffer) continue;
            GpuBufferBinding vb{ batch.vertexBuffer, 0 };
            gpu.bindVertexBuffers(rp, 0, &vb, 1);
            GpuBufferBinding ib{ batch.indexBuffer, 0 };
            gpu.bindIndexBuffer(rp, ib, true);
            GpuTextureSamplerBinding tsb{ batch.texture, batch.sampler };
            gpu.bindFragmentSamplers(rp, 0, &tsb, 1);
            uint32_t instOff[8] = {};
            instOff[0] = static_cast<uint32_t>(batch.offset);
            gpu.pushVertexUniformData(cmdBuffer, 1, instOff, 32);
            gpu.drawIndexedPrimitives(rp, batch.indexCount, static_cast<uint32_t>(batch.count), 0, 0, 0);
        }
        gpu.endRenderPass(rp);
    } else {
        // Effect path: iterate batches, switching between direct and effect rendering
        GpuRenderPassHandle currentPass = 0;
        bool passIsOpen = false;

        auto openSpritePass = [&](bool isFirst) {
            if (passIsOpen) return;
            GpuColorTargetInfo ct{};
            ct.texture  = targetTexture;
            ct.loadOp   = isFirst ? color_target_info_loadop : GpuLoadOp::Load;
            ct.storeOp  = GpuStoreOp::Store;
            ct.clearR   = color_target_clear_r;
            ct.clearG   = color_target_clear_g;
            ct.clearB   = color_target_clear_b;
            ct.clearA   = color_target_clear_a;
            currentPass = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
            gpu.bindGraphicsPipeline(currentPass, m_pipeline);
            gpu.bindVertexStorageBuffers(currentPass, 0, &SpriteDataBuffer, 1);
            // Push once per pass; camera is constant across the batches drawn into it.
            gpu.pushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));
            // Same viewport restriction as the direct (no-effect-batches) path — without
            // this, non-effect batches that happen to share a render() with an effect batch
            // render into the full FB texture instead of the window-physical region.
            {
                float vpW = std::min((float)Window::GetPhysicalWidth(),  (float)m_surface_width);
                float vpH = std::min((float)Window::GetPhysicalHeight(), (float)m_surface_height);
                gpu.setViewport(currentPass, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
            }
            passIsOpen = true;
        };
        auto closeSpritePass = [&]() {
            if (!passIsOpen) return;
            gpu.endRenderPass(currentPass);
            currentPass = 0;
            passIsOpen = false;
        };

        bool firstBatch = true;
        for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
            const auto& batch = batches[batchIdx];
            if (!batch.texture || !batch.sampler || !batch.vertexBuffer || !batch.indexBuffer) continue;

            size_t spriteIdx = batch.offset;
            int32_t effectIdx = (*renderQueue)[spriteIdx].effectIndex;
            bool batchHasEffect = (effectIdx >= 0 && effectIdx < (int32_t)effectStore.size()
                                   && !effectStore[effectIdx].empty());

            if (!batchHasEffect) {
                openSpritePass(firstBatch);
                firstBatch = false;

                GpuBufferBinding vb{ batch.vertexBuffer, 0 };
                gpu.bindVertexBuffers(currentPass, 0, &vb, 1);
                GpuBufferBinding ib{ batch.indexBuffer, 0 };
                gpu.bindIndexBuffer(currentPass, ib, true);
                GpuTextureSamplerBinding tsb{ batch.texture, batch.sampler };
                gpu.bindFragmentSamplers(currentPass, 0, &tsb, 1);
                uint32_t instOff[8] = {};
                instOff[0] = static_cast<uint32_t>(batch.offset);
                gpu.pushVertexUniformData(cmdBuffer, 1, instOff, 32);
                gpu.drawIndexedPrimitives(currentPass, batch.indexCount, static_cast<uint32_t>(batch.count), 0, 0, 0);
            } else {
                closeSpritePass();
                firstBatch = false;

                const auto& effects = effectStore[effectIdx];

                // Render sprite batch to effectTempA
                {
                    GpuColorTargetInfo ct{};
                    ct.texture  = m_effect_tex_a;
                    ct.loadOp   = GpuLoadOp::Clear;
                    ct.storeOp  = GpuStoreOp::Store;
                    ct.clearR = ct.clearG = ct.clearB = ct.clearA = 0.0f;
                    auto tmpRp = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
                    // Replace-blend pipeline so the sprite's alpha survives into tempA;
                    // m_pipeline preserves dst alpha which would zero it out.
                    gpu.bindGraphicsPipeline(tmpRp, m_effect_sprite_pipeline);
                    gpu.bindVertexStorageBuffers(tmpRp, 0, &SpriteDataBuffer, 1);
                    {
                        // Set viewport AFTER bindPipeline — some WebGPU impls reset dynamic state on pipeline binding.
                        float vpW = std::min((float)Window::GetPhysicalWidth(),  (float)m_surface_width);
                        float vpH = std::min((float)Window::GetPhysicalHeight(), (float)m_surface_height);
                        static bool s_loggedTmp = false;
                        if (!s_loggedTmp) {
                            s_loggedTmp = true;
                            LOG_INFO("effect sprite-to-tempA viewport: phys={}x{} surface={}x{} vp={}x{}",
                                     Window::GetPhysicalWidth(), Window::GetPhysicalHeight(),
                                     (int)m_surface_width, (int)m_surface_height, (int)vpW, (int)vpH);
                        }
                        gpu.setViewport(tmpRp, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
                    }
                    GpuBufferBinding vb{ batch.vertexBuffer, 0 };
                    gpu.bindVertexBuffers(tmpRp, 0, &vb, 1);
                    GpuBufferBinding ib{ batch.indexBuffer, 0 };
                    gpu.bindIndexBuffer(tmpRp, ib, true);
                    GpuTextureSamplerBinding tsb{ batch.texture, batch.sampler };
                    gpu.bindFragmentSamplers(tmpRp, 0, &tsb, 1);
                    gpu.pushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));
                    uint32_t instOff[8] = {};
                    instOff[0] = static_cast<uint32_t>(batch.offset);
                    gpu.pushVertexUniformData(cmdBuffer, 1, instOff, 32);
                    gpu.drawIndexedPrimitives(tmpRp, batch.indexCount, static_cast<uint32_t>(batch.count), 0, 0, 0);
                    gpu.endRenderPass(tmpRp);
                }

                // Apply effects: effectTempA → targetTexture
                const auto& effectTextureStore = Draw::GetEffectTextureStore();
                const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>> emptyExtraTex;
                const auto& storedTextures = (effectIdx < (int32_t)effectTextureStore.size())
                                              ? effectTextureStore[effectIdx] : emptyExtraTex;
                _applyEffectsWGPU(cmdBuffer, effects, m_effect_tex_a, targetTexture, storedTextures,
                                  batchIdx == 0 ? color_target_info_loadop : GpuLoadOp::Load,
                                  color_target_clear_r, color_target_clear_g,
                                  color_target_clear_b, color_target_clear_a);
            }
        }
        closeSpritePass();
    }
}

// ── Effect pipeline helpers (WebGPU) ─────────────────────────────────────────

GpuGraphicsPipelineHandle SpriteRenderPass::_getOrCreateEffectPipeline(
    const ShaderAsset& vertShader, const ShaderAsset& fragShader)
{
    auto it = m_effect_pipelines.find(fragShader.gpuShader);
    if (it != m_effect_pipelines.end()) return it->second;

    auto& gpu = Renderer::GetGpu();

    GpuVertexAttribute attrs[2] = {
        { .location = 0, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 0 },
        { .location = 1, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 8 },
    };
    GpuVertexBinding vbind{ .binding = 0, .stride = 16, .instanceStepping = false };

    GpuColorTargetBlendState blend{};
    blend.blendEnabled   = true;
    blend.srcColorFactor = GpuBlendFactor::SrcAlpha;
    blend.dstColorFactor = GpuBlendFactor::OneMinusSrcAlpha;
    blend.colorOp        = GpuBlendOp::Add;
    blend.srcAlphaFactor = GpuBlendFactor::One;
    blend.dstAlphaFactor = GpuBlendFactor::OneMinusSrcAlpha;
    blend.alphaOp        = GpuBlendOp::Add;

    GpuGraphicsPipelineCreateInfo pci{};
    pci.vertexShader      = vertShader.gpuShader;
    pci.fragmentShader    = fragShader.gpuShader;
    pci.attributes        = attrs;
    pci.attributeCount    = 2;
    pci.bindings          = &vbind;
    pci.bindingCount      = 1;
    pci.fillMode          = GpuFillMode::Fill;
    pci.cullMode          = GpuCullMode::None;
    pci.frontFace         = GpuFrontFace::CounterClockwise;
    pci.colorTargetFormat = m_swapchain_format;
    pci.blend             = blend;
    pci.hasDepthTarget    = false;
    pci.sampleCount       = GpuSampleCount::x1;
    pci.vertexStorageBufferCount = 0;

    GpuGraphicsPipelineHandle pipeline = gpu.createGraphicsPipeline(pci);
    m_effect_pipelines[fragShader.gpuShader] = pipeline;
    return pipeline;
}

void SpriteRenderPass::_applyEffectsWGPU(
    GpuCmdBufferHandle cmdBuffer,
    const std::vector<EffectAsset>& effects,
    GpuTextureHandle sourceTexture,
    GpuTextureHandle targetTexture,
    const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>>& extraTextures,
    GpuLoadOp targetLoadOp,
    float clearR, float clearG, float clearB, float clearA)
{
    auto& gpu = Renderer::GetGpu();

    // Effect ping-pong textures are sized to the physical window, so UV [0..1] is the
    // full populated area — no per-call quad rewrite needed (the init-time quad already
    // uses [0..1]).
    GpuTextureHandle readTex  = sourceTexture;
    GpuTextureHandle writeTex = m_effect_tex_b;

    for (size_t i = 0; i < effects.size(); ++i) {
        const auto& effect = effects[i];
        bool isLast = (i == effects.size() - 1);
        if (isLast) writeTex = targetTexture;

        GpuGraphicsPipelineHandle pipeline = _getOrCreateEffectPipeline(effect.vertShader, effect.fragShader);
        if (!pipeline) {
            LOG_ERROR("_applyEffectsWGPU: failed to get/create effect pipeline");
            continue;
        }

        GpuColorTargetInfo ct{};
        ct.texture  = writeTex;
        ct.loadOp   = isLast ? targetLoadOp : GpuLoadOp::Clear;
        ct.clearR   = clearR; ct.clearG = clearG; ct.clearB = clearB; ct.clearA = clearA;
        ct.storeOp  = GpuStoreOp::Store;
        auto rp = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
        gpu.bindGraphicsPipeline(rp, pipeline);
        {
            // Set viewport AFTER bindPipeline (see sprite-to-tempA note).
            float vpW = std::min((float)Window::GetPhysicalWidth(),  (float)m_surface_width);
            float vpH = std::min((float)Window::GetPhysicalHeight(), (float)m_surface_height);
            gpu.setViewport(rp, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
        }

        GpuBufferBinding vb{ m_effect_vbuf, 0 };
        gpu.bindVertexBuffers(rp, 0, &vb, 1);
        GpuBufferBinding ib{ m_effect_ibuf, 0 };
        gpu.bindIndexBuffer(rp, ib, true);

        const uint32_t pairCount = effect.fragShader.samplerCount > 0 ? effect.fragShader.samplerCount : 1;
        std::vector<GpuTextureSamplerBinding> tsbs(pairCount);
        for (uint32_t s = 0; s < pairCount; ++s) {
            auto it = extraTextures.find(s);
            if (it != extraTextures.end()) {
                tsbs[s].texture = it->second.first;
                tsbs[s].sampler = Renderer::GetSampler(it->second.second);
            } else {
                tsbs[s].texture = readTex;
                tsbs[s].sampler = m_effect_sampler;
            }
        }
        gpu.bindFragmentSamplers(rp, 0, tsbs.data(), pairCount);

        if (effect.uniforms && effect.uniforms->getBufferSize() > 0) {
            gpu.pushFragmentUniformData(cmdBuffer, 0,
                effect.uniforms->getBufferPointer(),
                static_cast<uint32_t>(effect.uniforms->getBufferSize()));
        }

        gpu.drawIndexedPrimitives(rp, 6, 1, 0, 0, 0);
        gpu.endRenderPass(rp);

        if (!isLast) {
            readTex  = writeTex;
            writeTex = (writeTex == m_effect_tex_b) ? m_effect_tex_a : m_effect_tex_b;
        }
    }
}
