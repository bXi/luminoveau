// 3DS SpriteRenderPass: PICA200 has no storage buffers or instancing, so this pass CPU-expands each
// renderable into a per-frame vertex/index ring and draws one C3D_DrawElements per texture batch.
// Per-sprite effect shaders don't exist on PICA; effect batches render plain.

#include "renderer/passes/spriterenderpass.h"

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "core/log/log.h"
#include "draw/draw.h"
#include "platform/window/window.h"

// Embedded picasso shbin (cmake/N3dsShaders.cmake). bin2s generates this header with the
// shbin array (extern) and its size as a constexpr constant — do not hand-declare the size
// as an extern symbol, it isn't one.
#include "lumi_sprite_shbin.h"

// TEV preset enum lives in the backend header.
#include "gpu/backends/n3ds/Citro3dGpuBackend.h"

namespace {

// Vertex layout — must match the pipeline attributes below and sprite.v.pica:
// v0 = pos float3, v1 = uv float2, v2 = color u8x4 (0..255, normalized in-shader).
struct SpriteVertex {
    float   x, y, z;
    float   u, v;
    uint8_t rgba[4];
};
static_assert(sizeof(SpriteVertex) == 24, "pipeline stride is 24");

// Per-frame ring capacity. MAX_SPRITES quads worth of vertices/indices; non-quad
// geometry (circles ~34 verts) eats through it faster, which the fill loop guards.
constexpr size_t RING_VERTS = (size_t)MAX_SPRITES * 4;
constexpr size_t RING_INDS  = (size_t)MAX_SPRITES * 6;
// Ring depth 2: while the GPU consumes frame N's buffers after C3D_FrameEnd, the
// CPU fills frame N+1's.
constexpr int RING_DEPTH = 2;

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace

// Reuses the shared pass members: _quad*/_effect* buffers are the two ring slots, _quadXfer* the
// CPU staging buffers, _effectTexW the ring parity counter.

void SpriteRenderPass::_createShaders() {
    IGpu &gpu = Renderer::GetGpu();

    GpuShaderCreateInfo vsi {};
    vsi.code      = lumi_sprite_shbin;
    vsi.codeSize  = lumi_sprite_shbin_size;
    vsi.stage     = GpuShaderStage::Vertex;
    _vertexShader = gpu.CreateShader(vsi);

    const CtrTevPreset  tev = CtrTevPreset::Modulate;
    GpuShaderCreateInfo fsi {};
    fsi.code        = reinterpret_cast<const uint8_t *>(&tev);
    fsi.codeSize    = sizeof(tev);
    fsi.stage       = GpuShaderStage::Fragment;
    _fragmentShader = gpu.CreateShader(fsi);
}

bool SpriteRenderPass::Init(
    GpuTextureFormat swapchainTextureFormat, uint32_t surfaceWidth, uint32_t surfaceHeight,
    std::string name, bool logInit, size_t capacity, bool forceNoMSAA) {
    _noMSAA          = forceNoMSAA;
    _passname        = std::move(name);
    _surfaceWidth    = surfaceWidth;
    _surfaceHeight   = surfaceHeight;
    _swapchainFormat = swapchainTextureFormat;

    IGpu &gpu   = Renderer::GetGpu();
    renderQueue = BufferManager::Create<Renderable>(_passname + "_renderQueue",
        capacity > 0 ? capacity : MAX_SPRITES);

    _createShaders();
    if (!_vertexShader || !_fragmentShader) {
        LOG_CRITICAL("SpriteRenderPass (3DS): failed to create shaders for {}", _passname);
        return false;
    }

    GpuVertexAttribute vertexAttributes[] = {
        { .location = 0, .binding = 0, .format = GpuVertexElementFormat::Float3, .offset = 0 },
        { .location = 1, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 12 },
        { .location = 2, .binding = 0, .format = GpuVertexElementFormat::UByte4Norm, .offset = 20 },
    };
    GpuVertexBinding vertexBinding = { .binding = 0, .stride = sizeof(SpriteVertex), .instanceStepping = false };

    GpuGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.vertexShader      = _vertexShader;
    pipelineInfo.fragmentShader    = _fragmentShader;
    pipelineInfo.attributes        = vertexAttributes;
    pipelineInfo.attributeCount    = 3;
    pipelineInfo.bindings          = &vertexBinding;
    pipelineInfo.bindingCount      = 1;
    pipelineInfo.colorTargetFormat = swapchainTextureFormat;
    pipelineInfo.blend             = renderPassBlendState;
    pipelineInfo.hasDepthTarget    = false;
    _pipeline                      = gpu.CreateGraphicsPipeline(pipelineInfo);
    if (!_pipeline) {
        LOG_CRITICAL("SpriteRenderPass (3DS): failed to create pipeline for {}", _passname);
        return false;
    }

    // CPU-written staging + double-buffered device rings.
    _quadXferVert = gpu.CreateTransferBuffer({ (uint32_t)(RING_VERTS * sizeof(SpriteVertex)), GpuTransferUsage::Upload });
    _quadXferIdx  = gpu.CreateTransferBuffer({ (uint32_t)(RING_INDS * sizeof(uint16_t)), GpuTransferUsage::Upload });
    _quadVertexBuf = gpu.CreateBuffer({ (uint32_t)(RING_VERTS * sizeof(SpriteVertex)), GpuBufferUsage::Vertex });
    _quadIndexBuf  = gpu.CreateBuffer({ (uint32_t)(RING_INDS * sizeof(uint16_t)), GpuBufferUsage::Index });
    _effectVbuf    = gpu.CreateBuffer({ (uint32_t)(RING_VERTS * sizeof(SpriteVertex)), GpuBufferUsage::Vertex });
    _effectIbuf    = gpu.CreateBuffer({ (uint32_t)(RING_INDS * sizeof(uint16_t)), GpuBufferUsage::Index });

    if (!_quadXferVert || !_quadXferIdx || !_quadVertexBuf || !_quadIndexBuf || !_effectVbuf || !_effectIbuf) {
        LOG_CRITICAL("SpriteRenderPass (3DS): failed to allocate vertex rings for {} "
                     "(MAX_SPRITES={} — lower it if linear memory is exhausted)",
            _passname, (int)MAX_SPRITES);
        return false;
    }

    if (logInit)
        LOG_INFO("SpriteRenderPass (3DS) initialized: {}", _passname);
    return true;
}

void SpriteRenderPass::Release(bool logRelease) {
    if (!Renderer::HasGpu()) {
        renderQueue = nullptr;
        return;
    }
    IGpu &gpu = Renderer::GetGpu();

    auto releaseBuf = [&](GpuBufferHandle &h) {
        if (h) {
            gpu.ReleaseBuffer(h);
            h = 0;
        }
    };
    releaseBuf(_quadVertexBuf);
    releaseBuf(_quadIndexBuf);
    releaseBuf(_effectVbuf);
    releaseBuf(_effectIbuf);
    if (_quadXferVert) {
        gpu.ReleaseTransferBuffer(_quadXferVert);
        _quadXferVert = 0;
    }
    if (_quadXferIdx) {
        gpu.ReleaseTransferBuffer(_quadXferIdx);
        _quadXferIdx = 0;
    }
    if (_pipeline) {
        gpu.ReleaseGraphicsPipeline(_pipeline);
        _pipeline = 0;
    }
    if (_vertexShader) {
        gpu.ReleaseShader(_vertexShader);
        _vertexShader = 0;
    }
    if (_fragmentShader) {
        gpu.ReleaseShader(_fragmentShader);
        _fragmentShader = 0;
    }
    renderQueue = nullptr;

    if (logRelease)
        LOG_INFO("Released sprite render pass: {}", _passname.c_str());
}

void SpriteRenderPass::OnResize(uint32_t surfaceWidth, uint32_t surfaceHeight) {
    // The 3DS display is fixed; only custom render targets change size, and those
    // carry their dims through Init. Just track for the viewport cap.
    _surfaceWidth  = surfaceWidth;
    _surfaceHeight = surfaceHeight;
}

void SpriteRenderPass::Render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4 &camera) {
    IGpu &gpu = Renderer::GetGpu();

    const size_t spriteCount = renderQueue ? renderQueue->Count() : 0;
    if (spriteCount == 0) {
        // Still open/clear the pass so the clear color takes effect.
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

    // ── CPU expansion into the staging ring ───────────────────────────────────
    auto *verts = static_cast<SpriteVertex *>(gpu.MapTransferBuffer(_quadXferVert, false));
    auto *inds  = static_cast<uint16_t *>(gpu.MapTransferBuffer(_quadXferIdx, false));
    if (!verts || !inds)
        return;

    struct Batch {
        GpuTextureHandle texture    = 0;
        GpuSamplerHandle sampler    = 0;
        uint32_t         firstIndex = 0;
        uint32_t         indexCount = 0;
    };
    std::vector<Batch> batches;
    batches.reserve(32);

    size_t vertCount = 0, indCount = 0;
    bool   sawEffect = false;

    static const Vertex2D quadVerts[4] = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 1.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.0f, 1.0f, 0.0f, 1.0f }
    };
    static const uint16_t quadInds[6] = { 0, 1, 2, 0, 2, 3 };

    for (size_t i = 0; i < spriteCount; ++i) {
        const Renderable &spr = (*renderQueue)[i];

        const Vertex2D *srcVerts;
        const uint16_t *srcInds;
        size_t          nv, ni;
        if (spr.geometry && !spr.geometry->vertices.empty() && !spr.geometry->indices.empty()) {
            srcVerts = spr.geometry->vertices.data();
            srcInds  = spr.geometry->indices.data();
            nv       = spr.geometry->vertices.size();
            ni       = spr.geometry->indices.size();
        } else {
            srcVerts = quadVerts;
            srcInds  = quadInds;
            nv       = 4;
            ni       = 6;
        }

        if (vertCount + nv > RING_VERTS || indCount + ni > RING_INDS || vertCount + nv > 0xFFFF) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                LOG_WARNING("SpriteRenderPass (3DS): vertex ring full, dropping sprites (frame had {})", spriteCount);
            }
            break;
        }

        if (spr.effectIndex >= 0)
            sawEffect = true;

        // Same math as shaders/sprite.vert.hlsl (and the same input clamps as the
        // desktop packers).
        const float x      = spr.x, y = spr.y;
        const float rot    = spr.rotation;
        const float texU   = clampf(spr.texU, 0.0f, 1.0f);
        const float texV   = clampf(spr.texV, 0.0f, 1.0f);
        const float texW   = clampf(spr.texW, -1.0f, 1.0f);
        const float texH   = clampf(spr.texH, -1.0f, 1.0f);
        const float w      = spr.w > 0.001f ? spr.w : 0.001f;
        const float h      = spr.h > 0.001f ? spr.h : 0.001f;
        const float pivotX = spr.pivotX, pivotY = spr.pivotY;

        const uint8_t r8 = (uint8_t)(clampf(spr.r, 0.0f, 1.0f) * 255.0f + 0.5f);
        const uint8_t g8 = (uint8_t)(clampf(spr.g, 0.0f, 1.0f) * 255.0f + 0.5f);
        const uint8_t b8 = (uint8_t)(clampf(spr.b, 0.0f, 1.0f) * 255.0f + 0.5f);
        const uint8_t a8 = (uint8_t)(clampf(spr.a, 0.0f, 1.0f) * 255.0f + 0.5f);

        const bool  rotated = rot != 0.0f;
        const float c       = rotated ? std::cos(rot) : 1.0f;
        const float s       = rotated ? std::sin(rot) : 0.0f;

        for (size_t v = 0; v < nv; ++v) {
            float cx = srcVerts[v].x;
            float cy = srcVerts[v].y;

            if (rotated) {
                cx -= pivotX;
                cy -= pivotY;
            }
            cx *= w;
            cy *= h;
            if (rotated) {
                // HLSL: coord = mul(coord, float2x2(c, s, -s, c))
                const float rx = cx * c - cy * s;
                const float ry = cx * s + cy * c;
                cx             = rx + pivotX * w;
                cy             = ry + pivotY * h;
            }

            SpriteVertex &out = verts[vertCount + v];
            out.x             = cx + x;
            out.y             = cy + y;
            out.z             = 0.0f;
            out.u             = texU + srcVerts[v].u * texW;
            out.v             = texV + srcVerts[v].v * texH;
            out.rgba[0]       = r8;
            out.rgba[1]       = g8;
            out.rgba[2]       = b8;
            out.rgba[3]       = a8;
        }
        for (size_t k = 0; k < ni; ++k)
            inds[indCount + k] = (uint16_t)(srcInds[k] + vertCount);

        // Batch break on texture change only — vertices are pre-transformed, so
        // geometry switches don't need one (fewer draws than the desktop path).
        const bool needNewBatch = batches.empty()
            || batches.back().texture != spr.texture.gpuTexture
            || batches.back().sampler != spr.texture.gpuSampler;
        if (needNewBatch) {
            batches.push_back({ spr.texture.gpuTexture, spr.texture.gpuSampler,
                (uint32_t)indCount, (uint32_t)ni });
        } else {
            batches.back().indexCount += (uint32_t)ni;
        }

        vertCount += nv;
        indCount += ni;
    }

    gpu.UnmapTransferBuffer(_quadXferVert);
    gpu.UnmapTransferBuffer(_quadXferIdx);

    if (sawEffect) {
        static bool warnedEffects = false;
        if (!warnedEffects) {
            warnedEffects = true;
            LOG_WARNING("SpriteRenderPass (3DS): sprite effects are not supported; drawing plain");
        }
    }

    // ── Upload into this frame's device ring slot (per-instance parity) ───────
    _effectTexW                 = (_effectTexW + 1) % RING_DEPTH;
    const bool      parity      = _effectTexW != 0;
    GpuBufferHandle vbuf        = parity ? _effectVbuf : _quadVertexBuf;
    GpuBufferHandle ibuf        = parity ? _effectIbuf : _quadIndexBuf;

    gpu.UploadToBuffer(cmdBuffer, _quadXferVert, 0, vbuf, 0, (uint32_t)(vertCount * sizeof(SpriteVertex)));
    gpu.UploadToBuffer(cmdBuffer, _quadXferIdx, 0, ibuf, 0, (uint32_t)(indCount * sizeof(uint16_t)));

    // ── Draw ──────────────────────────────────────────────────────────────────
    GpuColorTargetInfo ct {};
    ct.texture = targetTexture;
    ct.loadOp  = colorTargetInfoLoadOp;
    ct.storeOp = GpuStoreOp::Store;
    ct.clearR  = colorTargetClearR;
    ct.clearG  = colorTargetClearG;
    ct.clearB  = colorTargetClearB;
    ct.clearA  = colorTargetClearA;
    auto rp    = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
    if (!rp)
        return;

    {
        // Restrict draws to the logical content region of the (POT-padded) target.
        float vpW = std::min((float)Window::GetPhysicalWidth(), (float)_surfaceWidth);
        float vpH = std::min((float)Window::GetPhysicalHeight(), (float)_surfaceHeight);
        gpu.SetViewport(rp, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
    }
    if (scissorEnabled) {
        gpu.SetScissor(rp, scissorX, scissorY, scissorW, scissorH);
        scissorEnabled = false;
    }

    gpu.BindGraphicsPipeline(rp, _pipeline);
    gpu.PushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));

    GpuBufferBinding vb { vbuf, 0 };
    gpu.BindVertexBuffers(rp, 0, &vb, 1);
    GpuBufferBinding ib { ibuf, 0 };
    gpu.BindIndexBuffer(rp, ib, true);

    for (const Batch &batch : batches) {
        if (!batch.texture || !batch.sampler || batch.indexCount == 0)
            continue;
        GpuTextureSamplerBinding tsb { batch.texture, batch.sampler };
        gpu.BindFragmentSamplers(rp, 0, &tsb, 1);
        gpu.DrawIndexedPrimitives(rp, batch.indexCount, 1, batch.firstIndex, 0, 0);
    }

    gpu.EndRenderPass(rp);
}
