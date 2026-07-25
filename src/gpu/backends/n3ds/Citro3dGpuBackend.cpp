// Citro3dGpuBackend — IGpu over citro3d/PICA200. See the header for scope and
// conventions. Compiled only on LUMINOVEAU_N3DS_BACKEND (devkitPro toolchain).

#include "gpu/backends/n3ds/Citro3dGpuBackend.h"

#include "core/log/log.h"

#include <3ds.h>
#include <citro3d.h>

#include <glm/glm.hpp>

#include <cstdlib>
#include <cstring>

// Warn once per call site, then stay quiet — unsupported paths are expected to be
// hit every frame (compute passes, storage bindings) and must not flood the console.
#define CTR_WARN_ONCE(...)               \
    do {                                 \
        static bool _warned = false;     \
        if (!_warned) {                  \
            _warned = true;              \
            LOG_WARNING(__VA_ARGS__);    \
        }                                \
    } while (0)

namespace {

// ── Handle structs (cast to/from uintptr_t) ──────────────────────────────────

struct CtrTexture {
    C3D_Tex           tex {};
    C3D_RenderTarget *rt       = nullptr; // set when usable as a color target
    bool              isScreen = false;   // wraps the top-screen target; tex is unused
    bool              inVram   = false;   // VRAM textures can't take CPU uploads
    uint32_t          logicalW = 0, logicalH = 0;
    uint32_t          potW = 0, potH = 0;
    float             uvScaleX = 1.0f, uvScaleY = 1.0f; // logical/POT ratio
};

struct CtrBuffer {
    void    *data = nullptr; // linearAlloc'd — PICA-visible
    uint32_t size = 0;
};

struct CtrSampler {
    GPU_TEXTURE_FILTER_PARAM magFilter = GPU_LINEAR;
    GPU_TEXTURE_FILTER_PARAM minFilter = GPU_LINEAR;
    GPU_TEXTURE_WRAP_PARAM   wrapS     = GPU_CLAMP_TO_EDGE;
    GPU_TEXTURE_WRAP_PARAM   wrapT     = GPU_CLAMP_TO_EDGE;
};

struct CtrShader {
    bool            isVertex = false;
    DVLB_s         *dvlb     = nullptr;
    shaderProgram_s program {};
    int8_t          locProjection = -1;
    int8_t          locUvscale    = -1;
    CtrTevPreset    tev           = CtrTevPreset::Modulate; // fragment "shaders"
};

struct CtrPipeline {
    CtrShader   *vsh = nullptr;
    CtrTevPreset tev = CtrTevPreset::Modulate;

    struct Attr {
        GPU_FORMATS format = GPU_FLOAT;
        int         count  = 4;
    };
    Attr     attrs[4];
    int      attrCount   = 0;
    uint32_t stride      = 0;
    uint64_t permutation = 0; // BufInfo_Add attribute permutation (0x210 for 3 attrs)

    bool blendEnabled = false;
    GPU_BLENDEQUATION colorOp = GPU_BLEND_ADD, alphaOp = GPU_BLEND_ADD;
    GPU_BLENDFACTOR   srcColor = GPU_ONE, dstColor = GPU_ZERO;
    GPU_BLENDFACTOR   srcAlpha = GPU_ONE, dstAlpha = GPU_ZERO;

    // Pipelines with no vertex input are the shared renderer's fullscreen composite
    // (positions synthesized from the vertex index — impossible on PICA, emulated
    // in DrawPrimitives via immediate mode).
    bool isComposite = false;
};

// Mirror of Renderer::Uniforms / RenderPass::Uniforms (renderer.h / renderpass.h).
// The composite path receives this blob via PushVertexUniformData and re-emits the
// 6 quad vertices on the CPU. Any layout change there must be mirrored here.
struct CompositeUniforms {
    float camera[16];
    float model[16];
    float flipped[2];
    float uv[6][2];
    float tint[4];
};
static_assert(sizeof(CompositeUniforms) == 200,
    "CompositeUniforms must mirror Renderer::Uniforms (renderer.h)");

constexpr uint32_t SCREEN_LOGICAL_W = 400;
constexpr uint32_t SCREEN_LOGICAL_H = 240;

uint32_t nextPow2(uint32_t v) {
    uint32_t p = 8; // PICA minimum texture dimension
    while (p < v)
        p <<= 1;
    return p;
}

// Texel index inside a PICA 8x8-tiled texture (Morton order within tiles).
inline uint32_t mortonInterleave(uint32_t x, uint32_t y) {
    static const uint32_t lut[8] = { 0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15 };
    return lut[x & 7] | (lut[y & 7] << 1);
}
inline uint32_t tiledTexelIndex(uint32_t x, uint32_t y, uint32_t potW) {
    return mortonInterleave(x, y) + (x & ~7u) * 8 + (y & ~7u) * potW;
}

GPU_BLENDFACTOR mapBlendFactor(GpuBlendFactor f) {
    switch (f) {
    case GpuBlendFactor::Zero: return GPU_ZERO;
    case GpuBlendFactor::One: return GPU_ONE;
    case GpuBlendFactor::SrcColor: return GPU_SRC_COLOR;
    case GpuBlendFactor::OneMinusSrcColor: return GPU_ONE_MINUS_SRC_COLOR;
    case GpuBlendFactor::DstColor: return GPU_DST_COLOR;
    case GpuBlendFactor::OneMinusDstColor: return GPU_ONE_MINUS_DST_COLOR;
    case GpuBlendFactor::SrcAlpha: return GPU_SRC_ALPHA;
    case GpuBlendFactor::OneMinusSrcAlpha: return GPU_ONE_MINUS_SRC_ALPHA;
    case GpuBlendFactor::DstAlpha: return GPU_DST_ALPHA;
    case GpuBlendFactor::OneMinusDstAlpha: return GPU_ONE_MINUS_DST_ALPHA;
    case GpuBlendFactor::ConstantColor: return GPU_CONSTANT_COLOR;
    case GpuBlendFactor::OneMinusConstantColor: return GPU_ONE_MINUS_CONSTANT_COLOR;
    case GpuBlendFactor::SrcAlphaSaturate: return GPU_SRC_ALPHA_SATURATE;
    default:
        CTR_WARN_ONCE("citro3d: unsupported blend factor {}, using ONE", (int)f);
        return GPU_ONE;
    }
}

GPU_BLENDEQUATION mapBlendOp(GpuBlendOp op) {
    switch (op) {
    case GpuBlendOp::Add: return GPU_BLEND_ADD;
    case GpuBlendOp::Subtract: return GPU_BLEND_SUBTRACT;
    case GpuBlendOp::ReverseSubtract: return GPU_BLEND_REVERSE_SUBTRACT;
    case GpuBlendOp::Min: return GPU_BLEND_MIN;
    case GpuBlendOp::Max: return GPU_BLEND_MAX;
    }
    return GPU_BLEND_ADD;
}

bool mapTextureFormat(GpuTextureFormat f, GPU_TEXCOLOR &out) {
    switch (f) {
    case GpuTextureFormat::R8G8B8A8_Unorm:
    case GpuTextureFormat::R8G8B8A8_Unorm_SRGB:
    case GpuTextureFormat::B8G8R8A8_Unorm: // treated as RGBA8; engine only feeds RGBA data on 3DS
    case GpuTextureFormat::B8G8R8A8_Unorm_SRGB:
        out = GPU_RGBA8;
        return true;
    case GpuTextureFormat::R8_Unorm:
        out = GPU_L8;
        return true;
    case GpuTextureFormat::B5G6R5_Unorm:
        out = GPU_RGB565;
        return true;
    default:
        return false;
    }
}

} // namespace

// ── Backend state ────────────────────────────────────────────────────────────

struct Citro3dGpuBackend::State {
    C3D_RenderTarget *screenTarget = nullptr;
    CtrTexture        swapchain; // wrapper handed to the renderer each frame

    // Only one command buffer and one render pass exist at a time; these tokens
    // are what the opaque handles point at.
    int cmdToken  = 0;
    int passToken = 0;

    // Active render-pass state.
    struct {
        bool        active = false;
        CtrTexture *target = nullptr;
        bool        isScreen = false;
        uint32_t    fbW = 0, fbH = 0; // physical target dims (240x400 screen, POT offscreen)
        CtrPipeline *pipeline = nullptr;
        CtrBuffer   *vbuf     = nullptr;
        uint32_t     vbufOffset = 0;
        CtrBuffer   *ibuf       = nullptr;
        uint32_t     ibufOffset = 0;
        CtrTexture  *boundTex   = nullptr;
    } pass;

    // Composite uniform stash — refreshed by every PushVertexUniformData while the
    // composite pipeline is bound, consumed by the next DrawPrimitives.
    CompositeUniforms compositeStash {};
    bool              hasCompositeStash = false;
};

// ── Orientation fix-up ───────────────────────────────────────────────────────
// Converts an engine (GL-convention, glm column-major) matrix into a C3D_Mtx for
// the current target:
//  * depth: z'' = 0.5*z' - 0.5*w' remaps GL clip z [-1,1] into PICA's [-1,0].
//  * screen: the top screen's framebuffer is 240x400 rotated 90°; pre-rotate in
//    clip space (x''=y', y''=-x') — the same rotation citro3d's Mtx_OrthoTilt
//    bakes into its projection. If hardware testing shows a mirrored/rotated
//    image, this is the ONE place to flip signs.
static void buildFixedUpMatrix(const float *glmColMajor, bool isScreen, C3D_Mtx *out) {
    // Engine rows (row i of the matrix, gathered from column-major storage).
    float rows[4][4];
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 4; ++c)
            rows[i][c] = glmColMajor[c * 4 + i];

    float fixed[4][4];
    if (isScreen) {
        for (int c = 0; c < 4; ++c) {
            fixed[0][c] = rows[1][c];
            fixed[1][c] = -rows[0][c];
        }
    } else {
        for (int c = 0; c < 4; ++c) {
            fixed[0][c] = rows[0][c];
            fixed[1][c] = rows[1][c];
        }
    }
    for (int c = 0; c < 4; ++c) {
        fixed[2][c] = 0.5f * rows[2][c] - 0.5f * rows[3][c];
        fixed[3][c] = rows[3][c];
    }

    for (int i = 0; i < 4; ++i) {
        out->r[i].x = fixed[i][0];
        out->r[i].y = fixed[i][1];
        out->r[i].z = fixed[i][2];
        out->r[i].w = fixed[i][3];
    }
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

Citro3dGpuBackend::~Citro3dGpuBackend() {
    Shutdown();
}

bool Citro3dGpuBackend::Init(void * /*windowHandle*/) {
    _s = new State();

    // SDL's video init already ran gfxInitDefault-equivalent setup; take over the
    // bottom screen for the log console and bring up citro3d for the top screen.
    consoleInit(GFX_BOTTOM, nullptr);

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2)) {
        LOG_ERROR("citro3d: C3D_Init failed");
        return false;
    }

    // Top screen: physical framebuffer is 240x400 (rotated 90°).
    _s->screenTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, C3D_DEPTHTYPE(-1));
    if (!_s->screenTarget) {
        LOG_ERROR("citro3d: failed to create top-screen render target");
        return false;
    }
    C3D_RenderTargetSetOutput(_s->screenTarget, GFX_TOP, GFX_LEFT,
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));

    _s->swapchain.isScreen = true;
    _s->swapchain.rt       = _s->screenTarget;
    _s->swapchain.logicalW = SCREEN_LOGICAL_W;
    _s->swapchain.logicalH = SCREEN_LOGICAL_H;
    _s->swapchain.potW     = 240; // physical fb dims (rotated)
    _s->swapchain.potH     = 400;

    LOG_INFO("citro3d backend initialized (top screen {}x{})", SCREEN_LOGICAL_W, SCREEN_LOGICAL_H);
    return true;
}

void Citro3dGpuBackend::Shutdown() {
    if (!_s)
        return;
    if (_inFrame) {
        C3D_FrameEnd(0);
        _inFrame = false;
    }
    if (_s->screenTarget)
        C3D_RenderTargetDelete(_s->screenTarget);
    C3D_Fini();
    delete _s;
    _s = nullptr;
}

void Citro3dGpuBackend::WaitIdle() {
    // All GPU work is fenced by C3D_FrameEnd (blocks until the previous frame's
    // command list is consumed) and uploads run synchronously — nothing to wait on.
}

// ── Frame management ─────────────────────────────────────────────────────────

GpuCmdBufferHandle Citro3dGpuBackend::AcquireCommandBuffer() {
    // One conceptual command buffer. It only becomes "the frame" when the renderer
    // acquires the swapchain on it; upload-only acquisitions (asset/geometry
    // uploads between frames) execute their transfers immediately instead.
    return reinterpret_cast<GpuCmdBufferHandle>(&_s->cmdToken);
}

void Citro3dGpuBackend::SubmitCommandBuffer(GpuCmdBufferHandle /*cmd*/) {
    if (_inFrame) {
        C3D_FrameEnd(0);
        _inFrame = false;
    }
    // Upload-only command buffers already executed synchronously.
}

void Citro3dGpuBackend::PresentSwapchain() {
    // C3D_FrameEnd presents implicitly.
}

GpuTextureHandle Citro3dGpuBackend::AcquireSwapchainTexture(GpuCmdBufferHandle /*cmd*/,
    uint32_t &outWidth, uint32_t &outHeight) {
    if (!_inFrame) {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW); // waits for VBlank — caps at 60 fps
        _inFrame = true;
    }
    outWidth  = SCREEN_LOGICAL_W;
    outHeight = SCREEN_LOGICAL_H;
    return reinterpret_cast<GpuTextureHandle>(&_s->swapchain);
}

GpuTextureFormat Citro3dGpuBackend::GetSwapchainFormat() const {
    return GpuTextureFormat::R8G8B8A8_Unorm;
}

// ── Render pass ──────────────────────────────────────────────────────────────

GpuRenderPassHandle Citro3dGpuBackend::BeginRenderPass(GpuCmdBufferHandle /*cmd*/,
    const GpuColorTargetInfo *colorTargets, uint32_t colorTargetCount,
    const GpuDepthStencilTargetInfo * /*depthTarget*/) {
    if (!_inFrame) {
        CTR_WARN_ONCE("citro3d: BeginRenderPass outside a frame — ignored");
        return 0;
    }
    if (colorTargetCount == 0 || !colorTargets || !colorTargets[0].texture)
        return 0;
    if (colorTargetCount > 1)
        CTR_WARN_ONCE("citro3d: multiple render targets unsupported; using the first");

    auto *target = reinterpret_cast<CtrTexture *>(colorTargets[0].texture);
    if (!target->rt) {
        CTR_WARN_ONCE("citro3d: BeginRenderPass on a non-renderable texture");
        return 0;
    }

    if (colorTargets[0].loadOp == GpuLoadOp::Clear) {
        const auto     &ct    = colorTargets[0];
        const uint32_t  color = ((uint32_t)(ct.clearR * 255.0f + 0.5f) << 24)
            | ((uint32_t)(ct.clearG * 255.0f + 0.5f) << 16)
            | ((uint32_t)(ct.clearB * 255.0f + 0.5f) << 8)
            | (uint32_t)(ct.clearA * 255.0f + 0.5f);
        C3D_RenderTargetClear(target->rt, C3D_CLEAR_COLOR, color, 0);
    }

    C3D_FrameDrawOn(target->rt);

    _s->pass          = {};
    _s->pass.active   = true;
    _s->pass.target   = target;
    _s->pass.isScreen = target->isScreen;
    _s->pass.fbW      = target->isScreen ? 240 : target->potW;
    _s->pass.fbH      = target->isScreen ? 400 : target->potH;

    // 2D defaults: no depth, no culling. (Re-applied per pass; citro3d state is global.)
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_CullFace(GPU_CULL_NONE);

    return reinterpret_cast<GpuRenderPassHandle>(&_s->passToken);
}

void Citro3dGpuBackend::EndRenderPass(GpuRenderPassHandle /*pass*/) {
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    _s->pass.active = false;
}

// ── Compute stubs ────────────────────────────────────────────────────────────

GpuComputePassHandle Citro3dGpuBackend::BeginComputePass(GpuCmdBufferHandle,
    const GpuStorageTextureBinding *, uint32_t, const GpuStorageBufferBinding *, uint32_t) {
    CTR_WARN_ONCE("citro3d: compute is not supported on PICA200");
    return 0;
}
void Citro3dGpuBackend::EndComputePass(GpuComputePassHandle) { }
void Citro3dGpuBackend::BindComputePipeline(GpuComputePassHandle, GpuComputePipelineHandle) { }
void Citro3dGpuBackend::BindComputeSamplers(GpuComputePassHandle, uint32_t, const GpuTextureSamplerBinding *, uint32_t) { }
void Citro3dGpuBackend::BindComputeStorageTextures(GpuComputePassHandle, uint32_t, const GpuTextureHandle *, uint32_t) { }
void Citro3dGpuBackend::BindComputeStorageBuffers(GpuComputePassHandle, uint32_t, const GpuBufferHandle *, uint32_t) { }
void Citro3dGpuBackend::PushComputeUniformData(GpuCmdBufferHandle, uint32_t, const void *, uint32_t) { }
void Citro3dGpuBackend::DispatchCompute(GpuComputePassHandle, uint32_t, uint32_t, uint32_t) { }

// ── Pipeline / resource binding ──────────────────────────────────────────────

void Citro3dGpuBackend::BindGraphicsPipeline(GpuRenderPassHandle /*pass*/,
    GpuGraphicsPipelineHandle pipeline) {
    auto *p = reinterpret_cast<CtrPipeline *>(pipeline);
    if (!_s->pass.active)
        return;
    _s->pass.pipeline = p; // null clears the binding so stale draws can't misfire
    if (!p)
        return;

    if (p->vsh)
        C3D_BindProgram(&p->vsh->program);

    C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
    AttrInfo_Init(attrInfo);
    for (int i = 0; i < p->attrCount; ++i)
        AttrInfo_AddLoader(attrInfo, i, p->attrs[i].format, p->attrs[i].count);

    if (p->blendEnabled) {
        C3D_AlphaBlend(p->colorOp, p->alphaOp, p->srcColor, p->dstColor, p->srcAlpha, p->dstAlpha);
    } else {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    }

    // TEV stage 0: the "fragment shader". Modulate = texture x vertex color.
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
}

void Citro3dGpuBackend::BindVertexBuffers(GpuRenderPassHandle /*pass*/, uint32_t firstBinding,
    const GpuBufferBinding *bindings, uint32_t count) {
    if (!_s->pass.active || !_s->pass.pipeline || count == 0 || firstBinding != 0)
        return;
    auto *p          = _s->pass.pipeline;
    auto *buf        = reinterpret_cast<CtrBuffer *>(bindings[0].buffer);
    _s->pass.vbuf       = buf;
    _s->pass.vbufOffset = bindings[0].offset;
    if (!buf)
        return;

    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, (uint8_t *)buf->data + bindings[0].offset,
        (ptrdiff_t)p->stride, p->attrCount, p->permutation);
}

void Citro3dGpuBackend::BindIndexBuffer(GpuRenderPassHandle /*pass*/, GpuBufferBinding binding,
    bool use16BitIndices) {
    if (!use16BitIndices)
        CTR_WARN_ONCE("citro3d: 32-bit indices unsupported; treating as 16-bit");
    _s->pass.ibuf       = reinterpret_cast<CtrBuffer *>(binding.buffer);
    _s->pass.ibufOffset = binding.offset;
}

void Citro3dGpuBackend::BindVertexSamplers(GpuRenderPassHandle, uint32_t,
    const GpuTextureSamplerBinding *, uint32_t) {
    CTR_WARN_ONCE("citro3d: vertex samplers unsupported");
}

void Citro3dGpuBackend::BindFragmentSamplers(GpuRenderPassHandle /*pass*/, uint32_t firstBinding,
    const GpuTextureSamplerBinding *bindings, uint32_t count) {
    if (!_s->pass.active || count == 0 || firstBinding != 0)
        return;
    if (count > 1)
        CTR_WARN_ONCE("citro3d: only one fragment sampler supported; extras ignored");

    auto *tex     = reinterpret_cast<CtrTexture *>(bindings[0].texture);
    auto *sampler = reinterpret_cast<CtrSampler *>(bindings[0].sampler);
    if (!tex || tex->isScreen)
        return;

    // PICA sampler state lives on the texture — apply the engine sampler here.
    if (sampler) {
        C3D_TexSetFilter(&tex->tex, sampler->magFilter, sampler->minFilter);
        C3D_TexSetWrap(&tex->tex, sampler->wrapS, sampler->wrapT);
    }
    C3D_TexBind(0, &tex->tex);
    _s->pass.boundTex = tex;

    // Per-texture UV transform: logical -> POT, with the t-axis flip (see header).
    if (_s->pass.pipeline && _s->pass.pipeline->vsh && _s->pass.pipeline->vsh->locUvscale >= 0) {
        C3D_FVUnifSet(GPU_VERTEX_SHADER, _s->pass.pipeline->vsh->locUvscale,
            tex->uvScaleX, -tex->uvScaleY, 0.0f, 1.0f);
    }
}

void Citro3dGpuBackend::BindFragmentStorageTextures(GpuRenderPassHandle, uint32_t,
    const GpuTextureHandle *, uint32_t) {
    CTR_WARN_ONCE("citro3d: fragment storage textures unsupported");
}

void Citro3dGpuBackend::BindVertexStorageBuffers(GpuRenderPassHandle, uint32_t,
    const GpuBufferHandle *, uint32_t) {
    CTR_WARN_ONCE("citro3d: vertex storage buffers unsupported");
}

// ── Uniforms ─────────────────────────────────────────────────────────────────

void Citro3dGpuBackend::PushVertexUniformData(GpuCmdBufferHandle /*cmd*/, uint32_t slotIndex,
    const void *data, uint32_t size) {
    if (!data || slotIndex != 0)
        return;

    // Composite pipeline: stash the full Uniforms blob; DrawPrimitives decodes it.
    if (_s->pass.active && _s->pass.pipeline && _s->pass.pipeline->isComposite) {
        if (size < sizeof(CompositeUniforms)) {
            CTR_WARN_ONCE("citro3d: composite uniform push too small ({} bytes)", size);
            return;
        }
        std::memcpy(&_s->compositeStash, data, sizeof(CompositeUniforms));
        _s->hasCompositeStash = true;
        return;
    }

    // Normal pipelines: first 64 bytes are the camera matrix.
    if (size < sizeof(float) * 16)
        return;
    if (!_s->pass.active || !_s->pass.pipeline || !_s->pass.pipeline->vsh
        || _s->pass.pipeline->vsh->locProjection < 0)
        return;

    C3D_Mtx m;
    buildFixedUpMatrix(static_cast<const float *>(data), _s->pass.isScreen, &m);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _s->pass.pipeline->vsh->locProjection, &m);
}

void Citro3dGpuBackend::PushFragmentUniformData(GpuCmdBufferHandle, uint32_t, const void *, uint32_t) {
    // No fragment shaders on PICA — fragment uniforms have no meaning here.
}

// ── Draws ────────────────────────────────────────────────────────────────────

void Citro3dGpuBackend::DrawPrimitives(GpuRenderPassHandle /*pass*/, uint32_t vertexCount,
    uint32_t instanceCount, uint32_t firstVertex, uint32_t /*firstInstance*/) {
    if (!_s->pass.active || !_s->pass.pipeline)
        return;
    if (instanceCount > 1)
        CTR_WARN_ONCE("citro3d: instancing unsupported; drawing one instance");

    CtrPipeline *p = _s->pass.pipeline;

    if (p->isComposite) {
        // The shared renderer's fullscreen blit: 6 vertices synthesized from the
        // vertex index in the desktop shaders. Recreate them on the CPU from the
        // stashed Uniforms blob and submit via immediate mode.
        if (!_s->hasCompositeStash) {
            CTR_WARN_ONCE("citro3d: composite draw without pushed uniforms");
            return;
        }
        const CompositeUniforms &u = _s->compositeStash;

        if (p->vsh && p->vsh->locProjection >= 0) {
            C3D_Mtx m;
            buildFixedUpMatrix(u.camera, _s->pass.isScreen, &m);
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, p->vsh->locProjection, &m);
        }

        glm::mat4 model;
        std::memcpy(&model, u.model, sizeof(model));

        // Vertex order matches the desktop shaders (webgpu/init.cpp kRttVertWGSL).
        static const float quad[6][2] = {
            { 1, 1 }, { 0, 1 }, { 1, 0 }, { 0, 1 }, { 0, 0 }, { 1, 0 }
        };
        const CtrTexture *tex = _s->pass.boundTex;
        const float       su  = tex ? tex->uvScaleX : 1.0f;
        const float       sv  = tex ? tex->uvScaleY : 1.0f;

        // The composite pipeline declares no vertex input (bindingCount==0), so the
        // immediate-mode attribute layout is configured here instead of at bind time.
        C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
        AttrInfo_Init(attrInfo);
        AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0 = position
        AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2); // v1 = uv
        AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 4); // v2 = color

        C3D_ImmDrawBegin(GPU_TRIANGLES);
        for (int i = 0; i < 6; ++i) {
            glm::vec4 world = model * glm::vec4(quad[i][0], quad[i][1], 0.0f, 1.0f);
            C3D_ImmSendAttrib(world.x, world.y, world.z, 1.0f);
            // Engine UVs (v=0 top) -> POT texture coords (t flipped).
            C3D_ImmSendAttrib(u.uv[i][0] * su, 1.0f - u.uv[i][1] * sv, 0.0f, 0.0f);
            C3D_ImmSendAttrib(u.tint[0], u.tint[1], u.tint[2], u.tint[3]);
        }
        C3D_ImmDrawEnd();

        _frameDrawCalls++;
        _frameDrawVerts += 6;
        return;
    }

    C3D_DrawArrays(GPU_TRIANGLES, firstVertex, vertexCount);
    _frameDrawCalls++;
    _frameDrawVerts += vertexCount;
}

void Citro3dGpuBackend::DrawIndexedPrimitives(GpuRenderPassHandle /*pass*/, uint32_t indexCount,
    uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t /*firstInstance*/) {
    if (!_s->pass.active || !_s->pass.pipeline || !_s->pass.ibuf || !_s->pass.vbuf)
        return;
    if (instanceCount > 1)
        CTR_WARN_ONCE("citro3d: instancing unsupported; drawing one instance");

    CtrPipeline *p = _s->pass.pipeline;

    if (vertexOffset != 0) {
        // PICA has no base-vertex draw; rebase the vertex buffer pointer instead.
        C3D_BufInfo *bufInfo = C3D_GetBufInfo();
        BufInfo_Init(bufInfo);
        BufInfo_Add(bufInfo,
            (uint8_t *)_s->pass.vbuf->data + _s->pass.vbufOffset + (ptrdiff_t)vertexOffset * p->stride,
            (ptrdiff_t)p->stride, p->attrCount, p->permutation);
    }

    const void *indices = (uint8_t *)_s->pass.ibuf->data + _s->pass.ibufOffset
        + (size_t)firstIndex * sizeof(uint16_t);
    C3D_DrawElements(GPU_TRIANGLES, indexCount, C3D_UNSIGNED_SHORT, indices);

    if (vertexOffset != 0) {
        // Restore the unbased binding for subsequent draws.
        C3D_BufInfo *bufInfo = C3D_GetBufInfo();
        BufInfo_Init(bufInfo);
        BufInfo_Add(bufInfo, (uint8_t *)_s->pass.vbuf->data + _s->pass.vbufOffset,
            (ptrdiff_t)p->stride, p->attrCount, p->permutation);
    }

    _frameDrawCalls++;
    _frameDrawVerts += indexCount;
}

// ── Scissor / viewport ───────────────────────────────────────────────────────
// Engine coordinates are top-left-origin in the target's logical space. PICA is
// bottom-left-origin, and the screen target is additionally rotated 90° (fb x runs
// along screen-vertical). Both conversions live here and nowhere else.

void Citro3dGpuBackend::SetScissor(GpuRenderPassHandle /*pass*/, int32_t x, int32_t y,
    uint32_t w, uint32_t h) {
    if (!_s->pass.active)
        return;
    if (_s->pass.isScreen) {
        const uint32_t left   = 240 > (uint32_t)(y + (int32_t)h) ? 240 - (y + h) : 0;
        const uint32_t top    = 400 > (uint32_t)(x + (int32_t)w) ? 400 - (x + w) : 0;
        const uint32_t right  = 240 > (uint32_t)y ? 240 - y : 0;
        const uint32_t bottom = 400 > (uint32_t)x ? 400 - x : 0;
        C3D_SetScissor(GPU_SCISSOR_NORMAL, left, top, right, bottom);
    } else {
        const uint32_t fbH = _s->pass.fbH;
        const uint32_t yLo = fbH > (uint32_t)(y + (int32_t)h) ? fbH - (y + h) : 0;
        C3D_SetScissor(GPU_SCISSOR_NORMAL, (uint32_t)x, yLo, (uint32_t)x + w, yLo + h);
    }
}

void Citro3dGpuBackend::SetViewport(GpuRenderPassHandle /*pass*/, float x, float y,
    float w, float h, float /*minDepth*/, float /*maxDepth*/) {
    if (!_s->pass.active)
        return;
    if (_s->pass.isScreen) {
        C3D_SetViewport((uint32_t)(240.0f - (y + h)), (uint32_t)(400.0f - (x + w)),
            (uint32_t)h, (uint32_t)w);
    } else {
        C3D_SetViewport((uint32_t)x, (uint32_t)(_s->pass.fbH - (y + h)),
            (uint32_t)w, (uint32_t)h);
    }
}

// ── Resource creation ────────────────────────────────────────────────────────

GpuTextureHandle Citro3dGpuBackend::CreateTexture(const GpuTextureCreateInfo &info) {
    GPU_TEXCOLOR fmt;
    if (!mapTextureFormat(info.format, fmt)) {
        // Depth/MSAA/compressed formats are requested by shared code paths that are
        // inert on 3DS (MSAA off, no shadow maps) — fail softly.
        CTR_WARN_ONCE("citro3d: unsupported texture format {}", (int)info.format);
        return 0;
    }
    if (info.type != GpuTextureType::Tex2D) {
        CTR_WARN_ONCE("citro3d: only 2D textures supported");
        return 0;
    }

    auto *t     = new CtrTexture();
    t->logicalW = info.width;
    t->logicalH = info.height;
    t->potW     = nextPow2(info.width);
    t->potH     = nextPow2(info.height);
    if (t->potW > 1024 || t->potH > 1024) {
        LOG_WARNING("citro3d: texture {}x{} exceeds the PICA 1024 limit", info.width, info.height);
        delete t;
        return 0;
    }
    t->uvScaleX = (float)t->logicalW / (float)t->potW;
    t->uvScaleY = (float)t->logicalH / (float)t->potH;

    const bool isTarget = info.usage & GpuTextureUsage::ColorTarget;
    // Pure render targets go to VRAM (fast). Targets that also take CPU uploads
    // (Transfer — e.g. the engine's white pixel) must stay in the linear heap,
    // since VRAM is not CPU-writable; the GPU can render to linear memory too.
    const bool wantVram = isTarget && !(info.usage & GpuTextureUsage::Transfer);
    bool       ok;
    if (wantVram)
        ok = C3D_TexInitVRAM(&t->tex, (u16)t->potW, (u16)t->potH, fmt);
    else
        ok = C3D_TexInit(&t->tex, (u16)t->potW, (u16)t->potH, fmt);
    t->inVram = wantVram;
    if (ok && isTarget) {
        t->rt = C3D_RenderTargetCreateFromTex(&t->tex, GPU_TEXFACE_2D, 0, C3D_DEPTHTYPE(-1));
        if (!t->rt)
            ok = false;
    }
    if (!ok) {
        LOG_WARNING("citro3d: texture allocation failed ({}x{} POT {}x{})",
            info.width, info.height, t->potW, t->potH);
        delete t;
        return 0;
    }

    C3D_TexSetFilter(&t->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&t->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    return reinterpret_cast<GpuTextureHandle>(t);
}

GpuBufferHandle Citro3dGpuBackend::CreateBuffer(const GpuBufferCreateInfo &info) {
    auto *b = new CtrBuffer();
    b->size = info.size;
    b->data = linearAlloc(info.size);
    if (!b->data) {
        LOG_WARNING("citro3d: linearAlloc({}) failed for buffer", info.size);
        delete b;
        return 0;
    }
    return reinterpret_cast<GpuBufferHandle>(b);
}

GpuTransferBufferHandle Citro3dGpuBackend::CreateTransferBuffer(const GpuTransferBufferCreateInfo &info) {
    auto *b = new CtrBuffer();
    b->size = info.size;
    b->data = linearAlloc(info.size);
    if (!b->data) {
        LOG_WARNING("citro3d: linearAlloc({}) failed for transfer buffer", info.size);
        delete b;
        return 0;
    }
    return reinterpret_cast<GpuTransferBufferHandle>(b);
}

GpuSamplerHandle Citro3dGpuBackend::CreateSampler(const GpuSamplerCreateInfo &info) {
    auto mapWrap = [](GpuSamplerAddressMode m) -> GPU_TEXTURE_WRAP_PARAM {
        switch (m) {
        case GpuSamplerAddressMode::Repeat: return GPU_REPEAT;
        case GpuSamplerAddressMode::MirroredRepeat: return GPU_MIRRORED_REPEAT;
        case GpuSamplerAddressMode::ClampToBorder: return GPU_CLAMP_TO_BORDER;
        case GpuSamplerAddressMode::ClampToEdge:
        default: return GPU_CLAMP_TO_EDGE;
        }
    };
    auto *s      = new CtrSampler();
    s->magFilter = info.magFilter == GpuFilter::Nearest ? GPU_NEAREST : GPU_LINEAR;
    s->minFilter = info.minFilter == GpuFilter::Nearest ? GPU_NEAREST : GPU_LINEAR;
    s->wrapS     = mapWrap(info.addressU);
    s->wrapT     = mapWrap(info.addressV);
    return reinterpret_cast<GpuSamplerHandle>(s);
}

GpuShaderHandle Citro3dGpuBackend::CreateShader(const GpuShaderCreateInfo &info) {
    auto *sh = new CtrShader();

    if (info.stage == GpuShaderStage::Fragment) {
        // "Fragment shaders" are TEV presets — code points at a CtrTevPreset value.
        sh->isVertex = false;
        if (info.code && info.codeSize >= sizeof(uint32_t))
            std::memcpy(&sh->tev, info.code, sizeof(uint32_t));
        return reinterpret_cast<GpuShaderHandle>(sh);
    }
    if (info.stage != GpuShaderStage::Vertex || !info.code || info.codeSize == 0) {
        delete sh;
        return 0;
    }

    sh->isVertex = true;
    sh->dvlb     = DVLB_ParseFile(
        // DVLB_ParseFile wants a mutable u32*; the embedded shbin is const data it
        // never actually writes through, so the cast is safe by libctru convention.
        const_cast<u32 *>(reinterpret_cast<const u32 *>(info.code)), (u32)info.codeSize);
    if (!sh->dvlb) {
        LOG_ERROR("citro3d: DVLB_ParseFile failed ({} bytes)", info.codeSize);
        delete sh;
        return 0;
    }
    shaderProgramInit(&sh->program);
    shaderProgramSetVsh(&sh->program, &sh->dvlb->DVLE[0]);
    sh->locProjection = (int8_t)shaderInstanceGetUniformLocation(sh->program.vertexShader, "projection");
    sh->locUvscale    = (int8_t)shaderInstanceGetUniformLocation(sh->program.vertexShader, "uvscale");
    return reinterpret_cast<GpuShaderHandle>(sh);
}

GpuGraphicsPipelineHandle Citro3dGpuBackend::CreateGraphicsPipeline(const GpuGraphicsPipelineCreateInfo &info) {
    auto *vsh = reinterpret_cast<CtrShader *>(info.vertexShader);
    auto *fsh = reinterpret_cast<CtrShader *>(info.fragmentShader);
    if (!vsh || !vsh->isVertex) {
        LOG_WARNING("citro3d: CreateGraphicsPipeline without a vertex shader");
        return 0;
    }

    auto *p = new CtrPipeline();
    p->vsh  = vsh;
    p->tev  = fsh ? fsh->tev : CtrTevPreset::Modulate;

    p->isComposite = (info.bindingCount == 0);
    p->stride      = info.bindingCount > 0 ? info.bindings[0].stride : 0;
    p->attrCount   = 0;
    for (uint32_t i = 0; i < info.attributeCount && p->attrCount < 4; ++i) {
        const auto &a = info.attributes[i];
        auto       &o = p->attrs[p->attrCount];
        switch (a.format) {
        case GpuVertexElementFormat::Float: o = { GPU_FLOAT, 1 }; break;
        case GpuVertexElementFormat::Float2: o = { GPU_FLOAT, 2 }; break;
        case GpuVertexElementFormat::Float3: o = { GPU_FLOAT, 3 }; break;
        case GpuVertexElementFormat::Float4: o = { GPU_FLOAT, 4 }; break;
        case GpuVertexElementFormat::UByte4Norm:
        case GpuVertexElementFormat::UByte4: o = { GPU_UNSIGNED_BYTE, 4 }; break;
        default:
            CTR_WARN_ONCE("citro3d: unsupported vertex attribute format {}", (int)a.format);
            o = { GPU_FLOAT, 4 };
            break;
        }
        p->attrCount++;
    }
    // Attribute permutation for BufInfo: attribute i comes from input reg i.
    p->permutation = 0;
    for (int i = 0; i < p->attrCount; ++i)
        p->permutation |= (uint64_t)i << (i * 4);

    p->blendEnabled = info.blend.blendEnabled;
    p->colorOp      = mapBlendOp(info.blend.colorOp);
    p->alphaOp      = mapBlendOp(info.blend.alphaOp);
    p->srcColor     = mapBlendFactor(info.blend.srcColorFactor);
    p->dstColor     = mapBlendFactor(info.blend.dstColorFactor);
    p->srcAlpha     = mapBlendFactor(info.blend.srcAlphaFactor);
    p->dstAlpha     = mapBlendFactor(info.blend.dstAlphaFactor);

    return reinterpret_cast<GpuGraphicsPipelineHandle>(p);
}

GpuComputePipelineHandle Citro3dGpuBackend::CreateComputePipeline(const GpuComputePipelineCreateInfo &) {
    return 0;
}

GpuShaderHandle Citro3dGpuBackend::CreateShaderFromSPIRV(const GpuShaderCreateInfo &) {
    return 0; // no SPIRV toolchain on 3DS (same contract as WebGPU: null handle)
}

GpuComputePipelineHandle Citro3dGpuBackend::CreateComputePipelineFromSPIRV(const uint8_t *, size_t,
    const char *, GpuComputeReflection *) {
    return 0;
}

// ── Resource release ─────────────────────────────────────────────────────────

void Citro3dGpuBackend::ReleaseTexture(GpuTextureHandle handle) {
    auto *t = reinterpret_cast<CtrTexture *>(handle);
    if (!t || t->isScreen) // the swapchain wrapper is backend-owned
        return;
    if (t->rt)
        C3D_RenderTargetDelete(t->rt);
    C3D_TexDelete(&t->tex);
    delete t;
}

void Citro3dGpuBackend::ReleaseBuffer(GpuBufferHandle handle) {
    auto *b = reinterpret_cast<CtrBuffer *>(handle);
    if (!b)
        return;
    linearFree(b->data);
    delete b;
}

void Citro3dGpuBackend::ReleaseTransferBuffer(GpuTransferBufferHandle handle) {
    ReleaseBuffer((GpuBufferHandle)handle);
}

void Citro3dGpuBackend::ReleaseSampler(GpuSamplerHandle handle) {
    delete reinterpret_cast<CtrSampler *>(handle);
}

void Citro3dGpuBackend::ReleaseShader(GpuShaderHandle handle) {
    auto *sh = reinterpret_cast<CtrShader *>(handle);
    if (!sh)
        return;
    if (sh->isVertex && sh->dvlb) {
        shaderProgramFree(&sh->program);
        DVLB_Free(sh->dvlb);
    }
    delete sh;
}

void Citro3dGpuBackend::ReleaseGraphicsPipeline(GpuGraphicsPipelineHandle handle) {
    delete reinterpret_cast<CtrPipeline *>(handle);
}

void Citro3dGpuBackend::ReleaseComputePipeline(GpuComputePipelineHandle) { }

// ── Transfer ─────────────────────────────────────────────────────────────────

void *Citro3dGpuBackend::MapTransferBuffer(GpuTransferBufferHandle handle, bool /*cycle*/) {
    auto *b = reinterpret_cast<CtrBuffer *>(handle);
    return b ? b->data : nullptr;
}

void Citro3dGpuBackend::UnmapTransferBuffer(GpuTransferBufferHandle /*handle*/) { }

void Citro3dGpuBackend::UploadToTexture(GpuCmdBufferHandle /*cmd*/, const GpuTransferBufferRegion &src,
    const GpuTextureRegion &dst, bool /*cycle*/) {
    auto *buf = reinterpret_cast<CtrBuffer *>(src.transferBuffer);
    auto *tex = reinterpret_cast<CtrTexture *>(dst.texture);
    if (!buf || !tex || tex->isScreen)
        return;
    if (tex->inVram) {
        CTR_WARN_ONCE("citro3d: CPU upload to a VRAM render-target texture is unsupported");
        return;
    }
    if (tex->tex.fmt != GPU_RGBA8) {
        CTR_WARN_ONCE("citro3d: texture upload only implemented for RGBA8");
        return;
    }

    // CPU swizzle: linear top-down RGBA rows -> PICA 8x8 Morton tiles, with the
    // engine's RGBA byte order repacked into the PICA's ABGR word order and the
    // vertical flip that puts image row 0 at t≈1 (see uvscale in the header).
    // This is the reference path; a GX_DisplayTransfer fast path can replace it
    // later once validated against this output on hardware.
    const uint32_t rowPixels = src.pixelsPerRow ? src.pixelsPerRow : dst.width;
    const uint8_t *srcBytes  = (const uint8_t *)buf->data + src.offset;
    uint32_t      *texels    = (uint32_t *)tex->tex.data;

    for (uint32_t y = 0; y < dst.height; ++y) {
        const uint8_t *row  = srcBytes + (size_t)y * rowPixels * 4;
        const uint32_t yTop = dst.y + y;
        if (yTop >= tex->potH)
            break;
        const uint32_t ty = tex->potH - 1 - yTop; // bottom-origin flip
        for (uint32_t x = 0; x < dst.width; ++x) {
            const uint32_t tx = dst.x + x;
            if (tx >= tex->potW)
                break;
            const uint8_t r = row[x * 4 + 0], g = row[x * 4 + 1];
            const uint8_t b = row[x * 4 + 2], a = row[x * 4 + 3];
            texels[tiledTexelIndex(tx, ty, tex->potW)] =
                ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
        }
    }
    GSPGPU_FlushDataCache(tex->tex.data, tex->tex.size);
}

void Citro3dGpuBackend::UploadToBuffer(GpuCmdBufferHandle /*cmd*/, GpuTransferBufferHandle src,
    uint32_t srcOffset, GpuBufferHandle dst, uint32_t dstOffset, uint32_t size, bool /*cycle*/) {
    auto *s = reinterpret_cast<CtrBuffer *>(src);
    auto *d = reinterpret_cast<CtrBuffer *>(dst);
    if (!s || !d || !size)
        return;
    std::memcpy((uint8_t *)d->data + dstOffset, (const uint8_t *)s->data + srcOffset, size);
    GSPGPU_FlushDataCache((uint8_t *)d->data + dstOffset, size);
}

void Citro3dGpuBackend::DownloadFromTexture(GpuCmdBufferHandle, const GpuTextureRegion &,
    const GpuTransferBufferRegion &) {
    CTR_WARN_ONCE("citro3d: DownloadFromTexture unsupported (screenshots will be blank)");
}

void Citro3dGpuBackend::BlitTexture(GpuCmdBufferHandle, GpuTextureHandle, GpuTextureHandle,
    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, GpuFilter) {
    CTR_WARN_ONCE("citro3d: BlitTexture unsupported");
}
