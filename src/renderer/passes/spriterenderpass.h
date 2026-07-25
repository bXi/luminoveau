#pragma once

#include "gpu/halffloat.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "renderer/renderer.h"

#include "assets/texture/texture.h"
#include "assets/effect/effect.h"

#include "assets/assethandler.h"
#include "gpu/renderable.h"
#include "gpu/presets.h"

#include "gpu/renderpass.h"
#include "gpu/buffer/buffermanager.h"

#include "util/threadpool.h"

class SpriteRenderPass : public RenderPass {

    // Emscripten without -pthread can't spawn std::thread; the 3DS's appcore is a
    // single ARM11 where worker threads only add overhead. Default to single-threaded
    // sprite packing on those platforms. All other targets use one worker per core.
#if defined(__EMSCRIPTEN__) || defined(__3DS__)
    ThreadPool _threadPool = ThreadPool(0);
#else
    ThreadPool _threadPool = ThreadPool(std::max(1u, std::thread::hardware_concurrency()));
#endif

    // ── Shared pipeline + sprite-data buffers ─────────────────────────────────
    GpuGraphicsPipelineHandle _pipeline                 = 0;
    GpuGraphicsPipelineHandle _effectSpritePipeline     = 0; // WebGPU only — replace-blend variant for effect ping-pong draws
    GpuShaderHandle           _vertexShader             = 0;
    GpuShaderHandle           _fragmentShader           = 0;
    GpuTransferBufferHandle   _spriteDataTransferBuffer = 0;
    GpuBufferHandle           _spriteDataBuffer         = 0;
    GpuTextureFormat          _swapchainFormat          = GpuTextureFormat::B8G8R8A8_Unorm;
    bool                      _noMSAA                   = false;

    // SDL-only state — unused on WebGPU but cheap to carry unconditionally so the header
    // stays backend-neutral. MSAA + depth textures and per-binding extra-texture map.
    GpuTextureHandle                               _msaaColorTexture = 0;
    GpuTextureHandle                               _msaaDepthTexture = 0;
    TextureAsset                                   _depthTexture;
    std::unordered_map<uint32_t, GpuTextureHandle> _additionalEffectTextures;

    // WebGPU-only state — unused on SDL. Unit quad geometry + effect ping-pong resources
    // + per-shader effect pipeline cache.
    GpuBufferHandle                                                _quadVertexBuf = 0;
    GpuBufferHandle                                                _quadIndexBuf  = 0;
    GpuTransferBufferHandle                                        _quadXferVert  = 0;
    GpuTransferBufferHandle                                        _quadXferIdx   = 0;
    GpuTextureHandle                                               _effectTexA    = 0;
    GpuTextureHandle                                               _effectTexB    = 0;
    uint32_t                                                       _effectTexW    = 0; // physical-window dims at last create
    uint32_t                                                       _effectTexH    = 0; // (WebGPU only)
    GpuSamplerHandle                                               _effectSampler = 0;
    GpuBufferHandle                                                _effectVbuf    = 0;
    GpuBufferHandle                                                _effectIbuf    = 0;
    std::unordered_map<GpuShaderHandle, GpuGraphicsPipelineHandle> _effectPipelines;

    std::string _passname;

    // Surface dimensions (desktop size) for effect textures
    uint32_t _surfaceWidth  = 0;
    uint32_t _surfaceHeight = 0;

    // Compact half-precision struct (32 bytes)
    // Packed to match shader layout where each uint contains 2× uint16_t values
    struct CompactSpriteInstance {
        uint32_t posXy;   // x in low 16 bits, y in high 16 bits
        uint32_t posZRot; // z in low 16 bits, rotation in high 16 bits
        uint32_t texUv;   // tex_u in low 16 bits, tex_v in high 16 bits
        uint32_t texWh;   // tex_w in low 16 bits, tex_h in high 16 bits
        uint32_t colorRg; // r in low 16 bits, g in high 16 bits
        uint32_t colorBa; // b in low 16 bits, a in high 16 bits
        uint32_t sizeWh;  // w in low 16 bits, h in high 16 bits
        uint32_t pivotXy; // pivot_x in low 16 bits, pivot_y in high 15 bits, isSDF flag in highest bit
    };

    struct Batch {
        GpuBufferHandle  vertexBuffer = 0;
        GpuBufferHandle  indexBuffer  = 0;
        uint32_t         indexCount   = 0;
        GpuTextureHandle texture      = 0;
        GpuSamplerHandle sampler      = 0;
        size_t           offset       = 0; // offset in sprite buffer (instances)
        size_t           count        = 0; // number of sprites
    };

public:
    Buffer<Renderable> *renderQueue = nullptr;

    SpriteRenderPass(const SpriteRenderPass &) = delete;

    SpriteRenderPass &operator=(const SpriteRenderPass &) = delete;

    SpriteRenderPass(SpriteRenderPass &&) = delete;

    SpriteRenderPass &operator=(SpriteRenderPass &&) = delete;

    SpriteRenderPass()
        : RenderPass() { }

    bool Init(
        GpuTextureFormat swapchainTextureFormat, uint32_t surfaceWidth,
        uint32_t surfaceHeight, std::string name, bool logInit = true,
        size_t capacity = 0, bool forceNoMSAA = false) override;

    void Release(bool logRelease = true) override;

    void OnResize(uint32_t surfaceWidth, uint32_t surfaceHeight) override;

    void Render(
        GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4 &camera) override;

    void AddToRenderQueue(const Renderable &renderable) override {
        renderQueue->Add(renderable);
    }

    void ResetRenderQueue() override {
        renderQueue->Reset();
    }

    UniformBuffer uniformBuffer;

    UniformBuffer &GetUniformBuffer() override {
        return uniformBuffer;
    }

    // Blend state applied at pipeline creation time. Defaults to KeepDstAlpha for
    // framebuffer compositing; sprite passes drawing into intermediate targets typically
    // override this via UpdateRenderPassBlendState before Init().
    GpuColorTargetBlendState renderPassBlendState = GpuPresets::AlphaBlendKeepDstAlpha;

    void UpdateRenderPassBlendState(GpuColorTargetBlendState newstate) {
        renderPassBlendState = newstate;
    }

    // SDL-only extra-texture API — no-ops on WebGPU (which routes extras through draw-time arg).
    // Defined unconditionally so user code is backend-neutral.
    void SetAdditionalTexture(uint32_t binding, GpuTextureHandle texture) {
        _additionalEffectTextures[binding] = texture;
    }
    void ClearAdditionalTextures() {
        _additionalEffectTextures.clear();
    }

    // SDL-only effect ping-pong resources. WebGPU uses _applyEffectsWGPU instead.
    TextureAsset              effectTempA;
    TextureAsset              effectTempB;
    GpuGraphicsPipelineHandle effectPipeline       = 0;
    GpuGraphicsPipelineHandle effectSpritePipeline = 0;
    GpuShaderHandle           effectVertShader     = 0;

private:
    // WebGPU effect-pipeline helpers — defined only in the WebGPU backend .cpp.
    // Declared unconditionally so the header is backend-neutral; SDL builds never reference these symbols.
    GpuGraphicsPipelineHandle _getOrCreateEffectPipeline(const ShaderAsset &vertShader, const ShaderAsset &fragShader);
    void                      _applyEffectsWGPU(GpuCmdBufferHandle cmdBuffer, const std::vector<EffectAsset> &effects,
                             GpuTextureHandle sourceTexture, GpuTextureHandle targetTexture,
                             const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>> &extraTextures,
                             GpuLoadOp                                                                   targetLoadOp = GpuLoadOp::Load,
                             float clearR = 0, float clearG = 0, float clearB = 0, float clearA = 0);

    // Shared: each backend supplies its own implementation that writes _vertexShader + _fragmentShader.
    void _createShaders();

    // Cache of per-effect pipelines, keyed by the state that affects pipeline creation:
    // (vert shader, frag shader, isLast, target format, sample count). _noMSAA is fixed at
    // init so blend state is fully determined by isLast. Avoids recreating pipelines every frame.
    using EffectPipelineKey = std::tuple<GpuShaderHandle, GpuShaderHandle, bool,
        GpuTextureFormat, GpuSampleCount>;
    std::map<EffectPipelineKey, GpuGraphicsPipelineHandle> _effectPipelineCache;

    // Static fullscreen-quad geometry for the effect blit. Created once and reused across
    // frames (index data never changes); vertices re-uploaded only when the UV scale
    // (physical/surface ratio) changes on resize. Avoids 4 GPU buffer alloc+free per effect frame.
    GpuBufferHandle _effectQuadVbuf     = 0;
    GpuBufferHandle _effectQuadIbuf     = 0;
    float           _effectQuadUvScaleX = -1.0f;
    float           _effectQuadUvScaleY = -1.0f;

    void _createEffectResources();
    void _releaseEffectResources();
    void _applyEffects(GpuCmdBufferHandle cmdBuffer, const std::vector<EffectAsset> &effects,
        GpuTextureHandle sourceTexture, GpuTextureHandle targetTexture, const glm::mat4 &camera,
        GpuTextureFormat targetFormat, bool isFirstBatch,
        const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>> &effectTextures);
};
