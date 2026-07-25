// SDL-backend implementation for SpriteRenderPass — render path + effect helpers + shaders.
// Compiled only when LUMINOVEAU_WEBGPU_BACKEND is NOT set. Shared init/release/blend lives
// in ../spriterenderpass.cpp.

#include "renderer/passes/spriterenderpass.h"

#include <utility>
#include <cstring>
#include <vector>
#include <algorithm>

#include "core/log/log.h"
#include "platform/window/window.h"
#include "assets/shaders_generated.h"
#include "draw/draw.h"
#include "math/constants.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

// ── release / Init (SDL) ─────────────────────────────────────────────────────

void SpriteRenderPass::Release(bool logRelease) {
    IGpu &gpu = Renderer::GetGpu();

    _releaseEffectResources();

    if (_msaaColorTexture) {
        gpu.ReleaseTexture(_msaaColorTexture);
        _msaaColorTexture = 0;
    }
    if (_msaaDepthTexture) {
        gpu.ReleaseTexture(_msaaDepthTexture);
        _msaaDepthTexture = 0;
    }
    if (_depthTexture.gpuTexture) {
        gpu.ReleaseTexture(_depthTexture.gpuTexture);
        _depthTexture.gpuTexture = 0;
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

    // Local depth texture (D32_FLOAT) to match pipeline; MSAA color/depth come from
    // the shared framebuffer when MSAA is enabled.
    {
        GpuTextureCreateInfo depthInfo {};
        depthInfo.width          = surfaceWidth;
        depthInfo.height         = surfaceHeight;
        depthInfo.format         = GpuTextureFormat::D32_Float;
        depthInfo.usage          = GpuTextureUsage::DepthStencilTarget;
        depthInfo.sampleCount    = GpuSampleCount::X1;
        _depthTexture.gpuTexture = gpu.CreateTexture(depthInfo);
    }
    _createEffectResources();
    GpuSampleCount sampleCount = _noMSAA ? GpuSampleCount::X1 : Renderer::GetSampleCount();

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
    pipelineInfo.sampleCount              = sampleCount;
    pipelineInfo.vertexStorageBufferCount = 1;
    _pipeline                             = gpu.CreateGraphicsPipeline(pipelineInfo);

    if (!_pipeline) {
        LOG_CRITICAL("SpriteRenderPass: failed to create pipeline for {}", _passname);
        return false;
    }

    _spriteDataTransferBuffer = gpu.CreateTransferBuffer({
        static_cast<uint32_t>(MAX_SPRITES * sizeof(CompactSpriteInstance)),
        GpuTransferUsage::Upload,
    });
    _spriteDataBuffer         = gpu.CreateBuffer({
        static_cast<uint32_t>(MAX_SPRITES * sizeof(CompactSpriteInstance)),
        GpuBufferUsage::StorageRead,
    });

    if (logInit) {
        LOG_INFO("Render pass initialized: {}", _passname.c_str());
    }
    return true;
}

// ── _createShaders (SDL) ──────────────────────────────────────────────────────

void SpriteRenderPass::_createShaders() {
    IGpu &gpu = Renderer::GetGpu();

// spirv-cross renames "main" to "main0" in MSL (reserved keyword)
#if defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
    const char *entryPoint = "main0";
#else
    const char *entryPoint = "main";
#endif

    GpuShaderCreateInfo vsi {};
    vsi.code                = Lumi::Shaders::SPRITE_VERT;
    vsi.codeSize            = Lumi::Shaders::SPRITE_VERT_SIZE;
    vsi.entrypoint          = entryPoint;
    vsi.stage               = GpuShaderStage::Vertex;
    vsi.samplerCount        = 0;
    vsi.uniformBufferCount  = 2; // ViewProjection + InstanceOffset
    vsi.storageBufferCount  = 1;
    vsi.storageTextureCount = 0;
    _vertexShader           = gpu.CreateShader(vsi);
    if (!_vertexShader) {
        LOG_CRITICAL("failed to create vertex shader for: {} ({})", _passname.c_str(), SDL_GetError());
    }

    GpuShaderCreateInfo fsi {};
    fsi.code                = Lumi::Shaders::SPRITE_FRAG;
    fsi.codeSize            = Lumi::Shaders::SPRITE_FRAG_SIZE;
    fsi.entrypoint          = entryPoint;
    fsi.stage               = GpuShaderStage::Fragment;
    fsi.samplerCount        = 1;
    fsi.uniformBufferCount  = 0;
    fsi.storageBufferCount  = 0;
    fsi.storageTextureCount = 0;
    _fragmentShader         = gpu.CreateShader(fsi);
    if (!_fragmentShader) {
        LOG_CRITICAL("failed to create fragment shader for: {} ({})", _passname.c_str(), SDL_GetError());
    }
}

// ── Render (SDL) ─────────────────────────────────────────────────────────────

void SpriteRenderPass::Render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4 &camera) {
#ifdef LUMIDEBUG
    SDL_PushGPUDebugGroup(reinterpret_cast<SDL_GPUCommandBuffer *>(cmdBuffer), CURRENT_METHOD());
#endif

    // Check if ANY sprite in the queue has effects
    bool hasAnyEffects = false;
    for (uint32_t i = 0; i < renderQueue->Count(); i++) {
        if ((*renderQueue)[i].effectIndex >= 0) {
            hasAnyEffects = true;
            break;
        }
    }

    // sets up transfer - map the transfer buffer directly
    auto *dataPtr = static_cast<CompactSpriteInstance *>(
        Renderer::GetGpu().MapTransferBuffer(_spriteDataTransferBuffer, false));

    // Copy and compress from renderQueue to transfer buffer
    // Convert float32 to float16 and pack pairs into uint32

    size_t spriteCount = renderQueue->Count();
    size_t threadCount = _threadPool.GetThreadCount();
    size_t chunkSize   = spriteCount / threadCount + 1;

    for (size_t start = 0; start < spriteCount; start += chunkSize) {
        size_t end = std::min(start + chunkSize, spriteCount);
        _threadPool.Enqueue([this, dataPtr, start, end]() {
            for (size_t i = start; i < end; ++i) {
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

                dataPtr[i].posXy   = packHalf2(x, y);
                dataPtr[i].posZRot = packHalf2(z, rotation);
                dataPtr[i].texUv   = packHalf2(texU, texV);
                dataPtr[i].texWh   = packHalf2(texW, texH);
                dataPtr[i].colorRg = packHalf2(r, g);
                dataPtr[i].colorBa = packHalf2(b, a);
                dataPtr[i].sizeWh  = packHalf2(w, h);

                uint32_t pivotPacked = packHalf2(pivotX, pivotY);
                if (isSDF) {
                    pivotPacked |= 0x80000000u;
                }
                dataPtr[i].pivotXy = pivotPacked;
            }
        });
    }
    _threadPool.WaitAll();

    // Build batches respecting z-order, geometry, and texture changes
    std::vector<Batch> batches;
    batches.reserve(64);
    std::vector<bool> batchHasEffects;
    batchHasEffects.reserve(64);

    size_t currentOffset = 0;
    for (size_t i = 0; i < spriteCount; ++i) {
        const auto &cur             = (*renderQueue)[i];
        bool        geometryChanged = (i > 0 && cur.geometry != (*renderQueue)[i - 1].geometry);
        bool        textureChanged  = (i > 0 && cur.texture.gpuTexture != (*renderQueue)[i - 1].texture.gpuTexture);
        bool        effectsChanged  = (i > 0 && cur.effectIndex != (*renderQueue)[i - 1].effectIndex);

        if (i == 0 || geometryChanged || textureChanged || effectsChanged) {
            Batch batch;
            batch.offset = currentOffset;
            batch.count  = 1;
            if (cur.geometry) {
                batch.vertexBuffer = cur.geometry->vertexBuffer;
                batch.indexBuffer  = cur.geometry->indexBuffer;
                batch.indexCount   = static_cast<uint32_t>(cur.geometry->GetIndexCount());
            }
            batch.texture = cur.texture.gpuTexture;
            batch.sampler = cur.texture.gpuSampler;
            batches.push_back(batch);
            batchHasEffects.push_back(cur.effectIndex >= 0);
        } else {
            batches.back().count++;
        }
        currentOffset++;
    }
    Renderer::GetGpu().UnmapTransferBuffer(_spriteDataTransferBuffer);

    if (spriteCount > 0) {
        Renderer::GetGpu().UploadToBuffer(
            cmdBuffer,
            _spriteDataTransferBuffer, 0,
            _spriteDataBuffer, 0,
            static_cast<uint32_t>(spriteCount * sizeof(CompactSpriteInstance)),
            false);
    }

    bool shouldResolve = (renderTargetResolve != 0);

    if (!hasAnyEffects) {
        IGpu &gpu = Renderer::GetGpu();

        GpuColorTargetInfo ct {};
        ct.texture        = targetTexture;
        ct.resolveTexture = renderTargetResolve;
        ct.loadOp         = colorTargetInfoLoadOp;
        ct.storeOp        = shouldResolve ? GpuStoreOp::Resolve : GpuStoreOp::Store;
        ct.clearR         = colorTargetClearR;
        ct.clearG         = colorTargetClearG;
        ct.clearB         = colorTargetClearB;
        ct.clearA         = colorTargetClearA;

        GpuRenderPassHandle rp = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
        renderPass             = rp;

        {
            // Cap viewport to the surface dims so fixedSize FBs (e.g. LightToy's hrc_scene)
            // get the FB-sized viewport their FB-sized camera projects against. Without the
            // cap, a 1348-wide FB would receive a 1598-wide viewport and sprites projected
            // through ortho(0,1348,…) would land outside the FB's pixel range.
            float vpW = std::min((float)Window::GetPhysicalWidth(), (float)_surfaceWidth);
            float vpH = std::min((float)Window::GetPhysicalHeight(), (float)_surfaceHeight);
            gpu.SetViewport(rp, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
        }

        if (scissorEnabled) {
            gpu.SetScissor(rp, scissorX, scissorY, scissorW, scissorH);
            scissorEnabled = false;
        }

        gpu.BindGraphicsPipeline(rp, _pipeline);
        gpu.BindVertexStorageBuffers(rp, 0, &_spriteDataBuffer, 1);

        // Camera is identical for every batch; push once and let it persist across draws.
        gpu.PushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));

        for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
            const auto &batch = batches[batchIdx];
            if (!batch.texture || !batch.sampler || !batch.vertexBuffer || !batch.indexBuffer)
                continue;

            GpuBufferBinding vb { batch.vertexBuffer, 0 };
            gpu.BindVertexBuffers(rp, 0, &vb, 1);

            GpuBufferBinding ib { batch.indexBuffer, 0 };
            gpu.BindIndexBuffer(rp, ib, true);

            GpuTextureSamplerBinding tsb { batch.texture, batch.sampler };
            gpu.BindFragmentSamplers(rp, 0, &tsb, 1);

            uint32_t instOff[2] = { static_cast<uint32_t>(batch.offset), 0u };
            float    instScale  = Window::GetScale();
            std::memcpy(&instOff[1], &instScale, sizeof(float)); // render scale -> MSDF AA sizing
            gpu.PushVertexUniformData(cmdBuffer, 1, instOff, sizeof(instOff));

            gpu.DrawIndexedPrimitives(rp,
                batch.indexCount,
                static_cast<uint32_t>(batch.count), 0, 0, 0);
        }

        gpu.EndRenderPass(rp);
    } else {
        IGpu               &gpu         = Renderer::GetGpu();
        GpuRenderPassHandle currentPass = 0;

        for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
            const auto &batch = batches[batchIdx];
            if (!batch.texture || !batch.sampler || !batch.vertexBuffer || !batch.indexBuffer)
                continue;

            if (!batchHasEffects[batchIdx]) {
                if (!currentPass) {
                    GpuColorTargetInfo ct {};
                    ct.texture  = targetTexture;
                    ct.loadOp   = (batchIdx == 0) ? colorTargetInfoLoadOp : GpuLoadOp::Load;
                    ct.storeOp  = GpuStoreOp::Store;
                    ct.clearR   = colorTargetClearR;
                    ct.clearG   = colorTargetClearG;
                    ct.clearB   = colorTargetClearB;
                    ct.clearA   = colorTargetClearA;
                    currentPass = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);

                    {
                        float vpW = std::min((float)Window::GetPhysicalWidth(), (float)_surfaceWidth);
                        float vpH = std::min((float)Window::GetPhysicalHeight(), (float)_surfaceHeight);
                        gpu.SetViewport(currentPass, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
                    }
                    if (scissorEnabled) {
                        gpu.SetScissor(currentPass, scissorX, scissorY, scissorW, scissorH);
                        scissorEnabled = false;
                    }
                    gpu.BindGraphicsPipeline(currentPass, _pipeline);
                    gpu.BindVertexStorageBuffers(currentPass, 0, &_spriteDataBuffer, 1);
                    // Push once per pass; camera is constant across the batches drawn into it.
                    gpu.PushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));
                }

                GpuBufferBinding vb { batch.vertexBuffer, 0 };
                gpu.BindVertexBuffers(currentPass, 0, &vb, 1);
                GpuBufferBinding ib { batch.indexBuffer, 0 };
                gpu.BindIndexBuffer(currentPass, ib, true);

                GpuTextureSamplerBinding tsb { batch.texture, batch.sampler };
                gpu.BindFragmentSamplers(currentPass, 0, &tsb, 1);

                uint32_t instOff[2] = { static_cast<uint32_t>(batch.offset), 0u };
                float    instScale  = Window::GetScale();
                std::memcpy(&instOff[1], &instScale, sizeof(float)); // render scale -> MSDF AA sizing
                gpu.PushVertexUniformData(cmdBuffer, 1, instOff, sizeof(instOff));

                gpu.DrawIndexedPrimitives(currentPass,
                    batch.indexCount,
                    static_cast<uint32_t>(batch.count), 0, 0, 0);
            } else {
                if (currentPass) {
                    gpu.EndRenderPass(currentPass);
                    currentPass = 0;
                }

                size_t  spriteIdx = batch.offset;
                int32_t effectIdx = (*renderQueue)[spriteIdx].effectIndex;
                if (spriteIdx >= spriteCount || effectIdx < 0)
                    continue;
                const auto &effectStore = Draw::GetEffectStore();
                if (effectIdx >= (int32_t)effectStore.size())
                    continue;
                const auto &effects = effectStore[effectIdx];

                // Step 1: Render this batch to temp texture
                GpuColorTargetInfo tempCT {};
                tempCT.texture               = effectTempA.gpuTexture;
                tempCT.loadOp                = GpuLoadOp::Clear;
                tempCT.storeOp               = GpuStoreOp::Store;
                GpuRenderPassHandle tempPass = gpu.BeginRenderPass(cmdBuffer, &tempCT, 1, nullptr);

                {
                    float vpW = std::min((float)Window::GetPhysicalWidth(), (float)_surfaceWidth);
                    float vpH = std::min((float)Window::GetPhysicalHeight(), (float)_surfaceHeight);
                    gpu.SetViewport(tempPass, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
                }
                gpu.BindGraphicsPipeline(tempPass, effectSpritePipeline);
                gpu.BindVertexStorageBuffers(tempPass, 0, &_spriteDataBuffer, 1);

                GpuBufferBinding vb { batch.vertexBuffer, 0 };
                gpu.BindVertexBuffers(tempPass, 0, &vb, 1);
                GpuBufferBinding ib { batch.indexBuffer, 0 };
                gpu.BindIndexBuffer(tempPass, ib, true);

                GpuTextureSamplerBinding tsb { batch.texture, batch.sampler };
                gpu.BindFragmentSamplers(tempPass, 0, &tsb, 1);
                gpu.PushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));

                uint32_t instOff[2] = { static_cast<uint32_t>(batch.offset), 0u };
                float    instScale  = Window::GetScale();
                std::memcpy(&instOff[1], &instScale, sizeof(float)); // render scale -> MSDF AA sizing
                gpu.PushVertexUniformData(cmdBuffer, 1, instOff, sizeof(instOff));

                gpu.DrawIndexedPrimitives(tempPass,
                    batch.indexCount,
                    static_cast<uint32_t>(batch.count), 0, 0, 0);
                gpu.EndRenderPass(tempPass);

                const auto                                                                &effectTextureStore = Draw::GetEffectTextureStore();
                const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>> emptyTextures;
                const auto                                                                &storedTextures = (effectIdx < (int32_t)effectTextureStore.size()) ? effectTextureStore[effectIdx] : emptyTextures;
                _applyEffects(cmdBuffer, effects, effectTempA.gpuTexture, targetTexture, camera, _swapchainFormat, batchIdx == 0, storedTextures);
            }
        }

        if (currentPass) {
            gpu.EndRenderPass(currentPass);
        }

        if (shouldResolve) {
            GpuColorTargetInfo resolveCT {};
            resolveCT.texture               = targetTexture;
            resolveCT.resolveTexture        = renderTargetResolve;
            resolveCT.loadOp                = GpuLoadOp::Load;
            resolveCT.storeOp               = GpuStoreOp::Resolve;
            GpuRenderPassHandle resolvePass = gpu.BeginRenderPass(cmdBuffer, &resolveCT, 1, nullptr);
            gpu.EndRenderPass(resolvePass);
        }
    }

#ifdef LUMIDEBUG
    SDL_PopGPUDebugGroup(reinterpret_cast<SDL_GPUCommandBuffer *>(cmdBuffer));
#endif
}

// ── Effect resources (SDL) ───────────────────────────────────────────────────

void SpriteRenderPass::OnResize(uint32_t surfaceWidth, uint32_t surfaceHeight) {
    if (surfaceWidth == 0 || surfaceHeight == 0)
        return;
    if (surfaceWidth == _surfaceWidth && surfaceHeight == _surfaceHeight)
        return;

    IGpu &gpu = Renderer::GetGpu();
    gpu.WaitIdle();

    // Recreate only the surface-sized targets (depth + effect temps). Pipelines, shaders and
    // the render queue are untouched — no recompile.
    if (_depthTexture.gpuTexture) {
        gpu.ReleaseTexture(_depthTexture.gpuTexture);
        _depthTexture.gpuTexture = 0;
    }
    if (effectTempA.gpuTexture) {
        gpu.ReleaseTexture(effectTempA.gpuTexture);
        effectTempA.gpuTexture = 0;
    }
    if (effectTempB.gpuTexture) {
        gpu.ReleaseTexture(effectTempB.gpuTexture);
        effectTempB.gpuTexture = 0;
    }

    _surfaceWidth  = surfaceWidth;
    _surfaceHeight = surfaceHeight;

    GpuTextureCreateInfo depthInfo {};
    depthInfo.width          = surfaceWidth;
    depthInfo.height         = surfaceHeight;
    depthInfo.format         = GpuTextureFormat::D32_Float;
    depthInfo.usage          = GpuTextureUsage::DepthStencilTarget;
    depthInfo.sampleCount    = GpuSampleCount::X1;
    _depthTexture.gpuTexture = gpu.CreateTexture(depthInfo);

    _createEffectResources();
}

void SpriteRenderPass::_createEffectResources() {
    IGpu &gpu = Renderer::GetGpu();

    GpuTextureCreateInfo tempTexInfo {};
    tempTexInfo.width       = _surfaceWidth;
    tempTexInfo.height      = _surfaceHeight;
    tempTexInfo.format      = GpuTextureFormat::R8G8B8A8_Unorm;
    tempTexInfo.usage       = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
    tempTexInfo.sampleCount = GpuSampleCount::X1;

    effectTempA.gpuTexture = gpu.CreateTexture(tempTexInfo);
    effectTempA.gpuSampler = Renderer::GetSampler(ScaleMode::Nearest);
    effectTempA.width      = _surfaceWidth;
    effectTempA.height     = _surfaceHeight;
    effectTempA.filename   = "[Lumi]EffectTempA";

    effectTempB.gpuTexture = gpu.CreateTexture(tempTexInfo);
    effectTempB.gpuSampler = Renderer::GetSampler(ScaleMode::Nearest);
    effectTempB.width      = _surfaceWidth;
    effectTempB.height     = _surfaceHeight;
    effectTempB.filename   = "[Lumi]EffectTempB";

    if (!effectTempA.gpuTexture || !effectTempB.gpuTexture) {
        LOG_ERROR("Failed to create effect temp textures: {}", SDL_GetError());
        return;
    }

    GpuColorTargetBlendState noBlend {};
    noBlend.blendEnabled   = true;
    noBlend.srcColorFactor = GpuBlendFactor::One;
    noBlend.dstColorFactor = GpuBlendFactor::Zero;
    noBlend.colorOp        = GpuBlendOp::Add;
    noBlend.srcAlphaFactor = GpuBlendFactor::One;
    noBlend.dstAlphaFactor = GpuBlendFactor::Zero;
    noBlend.alphaOp        = GpuBlendOp::Add;

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
    pipelineInfo.colorTargetFormat        = GpuTextureFormat::R8G8B8A8_Unorm;
    pipelineInfo.blend                    = noBlend;
    pipelineInfo.hasDepthTarget           = false;
    pipelineInfo.sampleCount              = GpuSampleCount::X1;
    pipelineInfo.vertexStorageBufferCount = 1;

    effectSpritePipeline = gpu.CreateGraphicsPipeline(pipelineInfo);
    if (!effectSpritePipeline) {
        LOG_ERROR("Failed to create effect sprite pipeline: {}", SDL_GetError());
    }
}

void SpriteRenderPass::_releaseEffectResources() {
    IGpu &gpu = Renderer::GetGpu();
    if (effectTempA.gpuTexture) {
        gpu.ReleaseTexture(effectTempA.gpuTexture);
        effectTempA.gpuTexture = 0;
    }
    if (effectTempB.gpuTexture) {
        gpu.ReleaseTexture(effectTempB.gpuTexture);
        effectTempB.gpuTexture = 0;
    }
    if (effectPipeline) {
        gpu.ReleaseGraphicsPipeline(effectPipeline);
        effectPipeline = 0;
    }
    if (effectSpritePipeline) {
        gpu.ReleaseGraphicsPipeline(effectSpritePipeline);
        effectSpritePipeline = 0;
    }
    if (effectVertShader) {
        gpu.ReleaseShader(effectVertShader);
        effectVertShader = 0;
    }
    for (auto &[key, pipeline] : _effectPipelineCache) {
        if (pipeline)
            gpu.ReleaseGraphicsPipeline(pipeline);
    }
    _effectPipelineCache.clear();
    if (_effectQuadVbuf) {
        gpu.ReleaseBuffer(_effectQuadVbuf);
        _effectQuadVbuf = 0;
    }
    if (_effectQuadIbuf) {
        gpu.ReleaseBuffer(_effectQuadIbuf);
        _effectQuadIbuf = 0;
    }
    _effectQuadUvScaleX = -1.0f;
    _effectQuadUvScaleY = -1.0f;
}

void SpriteRenderPass::_applyEffects(GpuCmdBufferHandle cmdBuffer, const std::vector<EffectAsset> &effects,
    GpuTextureHandle sourceTexture, GpuTextureHandle targetTexture, const glm::mat4 &camera,
    GpuTextureFormat targetFormat, bool isFirstBatch,
    const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>> &effectTextures) {
    (void)camera;
    if (effects.empty())
        return;

    IGpu &gpu = Renderer::GetGpu();

    // Fullscreen quad geometry (position + texcoord). Temp textures are surface-sized
    // but only the physical-pixel area was drawn, so scale UVs to compensate.
    struct Vertex {
        float x, y, u, v;
    };
    float uvScaleX = (float)Window::GetPhysicalWidth() / (float)_surfaceWidth;
    float uvScaleY = (float)Window::GetPhysicalHeight() / (float)_surfaceHeight;

    // Lazily create the static quad buffers once; index data never changes.
    if (!_effectQuadVbuf) {
        _effectQuadVbuf = gpu.CreateBuffer({ sizeof(Vertex) * 4, GpuBufferUsage::Vertex });
        _effectQuadIbuf = gpu.CreateBuffer({ sizeof(uint16_t) * 6, GpuBufferUsage::Index });

        uint16_t                quadIndices[] = { 0, 1, 2, 2, 1, 3 };
        GpuTransferBufferHandle idxXfer       = gpu.CreateTransferBuffer({ sizeof(quadIndices), GpuTransferUsage::Upload });
        memcpy(gpu.MapTransferBuffer(idxXfer, false), quadIndices, sizeof(quadIndices));
        gpu.UnmapTransferBuffer(idxXfer);
        gpu.UploadToBuffer(cmdBuffer, idxXfer, 0, _effectQuadIbuf, 0, sizeof(quadIndices));
        gpu.ReleaseTransferBuffer(idxXfer);
        _effectQuadUvScaleX = -1.0f; // force the vertex upload below
    }

    // Re-upload vertices only when the UV scale changed (resize); otherwise the persistent
    // buffer already holds the right geometry from a previous frame.
    if (uvScaleX != _effectQuadUvScaleX || uvScaleY != _effectQuadUvScaleY) {
        Vertex quadVertices[] = {
            { 0.0f, 0.0f, 0.0f, uvScaleY },
            { 1.0f, 0.0f, uvScaleX, uvScaleY },
            { 0.0f, 1.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, uvScaleX, 0.0f },
        };
        GpuTransferBufferHandle vtxXfer = gpu.CreateTransferBuffer({ sizeof(quadVertices), GpuTransferUsage::Upload });
        memcpy(gpu.MapTransferBuffer(vtxXfer, false), quadVertices, sizeof(quadVertices));
        gpu.UnmapTransferBuffer(vtxXfer);
        gpu.UploadToBuffer(cmdBuffer, vtxXfer, 0, _effectQuadVbuf, 0, sizeof(quadVertices));
        gpu.ReleaseTransferBuffer(vtxXfer);
        _effectQuadUvScaleX = uvScaleX;
        _effectQuadUvScaleY = uvScaleY;
    }

    GpuTextureHandle readTex  = sourceTexture;
    GpuTextureHandle writeTex = (effects.size() == 1) ? targetTexture : effectTempB.gpuTexture;

    GpuVertexAttribute vertexAttribs[] = {
        { .location = 0, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 0 },
        { .location = 1, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 8 },
    };
    GpuVertexBinding vertBinding = { .binding = 0, .stride = 16, .instanceStepping = false };

    for (size_t i = 0; i < effects.size(); ++i) {
        const auto &effect = effects[i];
        bool        isLast = (i == effects.size() - 1);
        if (isLast)
            writeTex = targetTexture;

        if (!effect.vertShader.gpuShader || !effect.fragShader.gpuShader) {
            LOG_ERROR("Effect shaders are NULL: vert={}, frag={}",
                (void *)effect.vertShader.gpuShader, (void *)effect.fragShader.gpuShader);
            continue;
        }

        GpuColorTargetBlendState blend {};
        if (isLast && _noMSAA) {
            blend.blendEnabled   = false;
            blend.srcColorFactor = GpuBlendFactor::One;
            blend.dstColorFactor = GpuBlendFactor::Zero;
            blend.colorOp        = GpuBlendOp::Add;
            blend.srcAlphaFactor = GpuBlendFactor::One;
            blend.dstAlphaFactor = GpuBlendFactor::Zero;
            blend.alphaOp        = GpuBlendOp::Add;
        } else if (isLast) {
            blend.blendEnabled   = true;
            blend.srcColorFactor = GpuBlendFactor::SrcAlpha;
            blend.dstColorFactor = GpuBlendFactor::OneMinusSrcAlpha;
            blend.colorOp        = GpuBlendOp::Add;
            blend.srcAlphaFactor = GpuBlendFactor::One;
            blend.dstAlphaFactor = GpuBlendFactor::OneMinusSrcAlpha;
            blend.alphaOp        = GpuBlendOp::Add;
        } else {
            blend.blendEnabled   = true;
            blend.srcColorFactor = GpuBlendFactor::One;
            blend.dstColorFactor = GpuBlendFactor::Zero;
            blend.colorOp        = GpuBlendOp::Add;
            blend.srcAlphaFactor = GpuBlendFactor::One;
            blend.dstAlphaFactor = GpuBlendFactor::Zero;
            blend.alphaOp        = GpuBlendOp::Add;
        }

        GpuSampleCount pipelineSampleCount = (isLast && !_noMSAA) ? Renderer::GetSampleCount() : GpuSampleCount::X1;

        GpuGraphicsPipelineCreateInfo pi {};
        pi.vertexShader      = effect.vertShader.gpuShader;
        pi.fragmentShader    = effect.fragShader.gpuShader;
        pi.attributes        = vertexAttribs;
        pi.attributeCount    = 2;
        pi.bindings          = &vertBinding;
        pi.bindingCount      = 1;
        pi.fillMode          = GpuFillMode::Fill;
        pi.cullMode          = GpuCullMode::None;
        pi.frontFace         = GpuFrontFace::CounterClockwise;
        pi.colorTargetFormat = isLast ? targetFormat : GpuTextureFormat::R8G8B8A8_Unorm;
        pi.blend             = blend;
        pi.hasDepthTarget    = false;
        pi.sampleCount       = pipelineSampleCount;

        EffectPipelineKey         key { effect.vertShader.gpuShader, effect.fragShader.gpuShader,
            isLast, pi.colorTargetFormat, pipelineSampleCount };
        GpuGraphicsPipelineHandle pipeline = 0;
        if (auto it = _effectPipelineCache.find(key); it != _effectPipelineCache.end()) {
            pipeline = it->second;
        } else {
            pipeline = gpu.CreateGraphicsPipeline(pi);
            if (!pipeline) {
                LOG_ERROR("Failed to create effect pipeline: {}", SDL_GetError());
                continue;
            }
            _effectPipelineCache.emplace(key, pipeline);
        }

        GpuColorTargetInfo ct {};
        ct.texture = writeTex;
        ct.loadOp  = isLast ? (isFirstBatch ? GpuLoadOp::Clear : GpuLoadOp::Load) : GpuLoadOp::Clear;
        ct.storeOp = GpuStoreOp::Store;

        GpuRenderPassHandle effectPass = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
        {
            float vpW = std::min((float)Window::GetPhysicalWidth(), (float)_surfaceWidth);
            float vpH = std::min((float)Window::GetPhysicalHeight(), (float)_surfaceHeight);
            gpu.SetViewport(effectPass, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
        }
        gpu.BindGraphicsPipeline(effectPass, pipeline);

        std::vector<GpuTextureSamplerBinding> textureBindings;
        textureBindings.push_back({ readTex, Renderer::GetSampler(ScaleMode::Nearest) });
        for (const auto &[binding, texture] : effectTextures) {
            while (textureBindings.size() <= binding) {
                textureBindings.push_back(textureBindings[0]);
            }
            textureBindings[binding] = { texture.first, Renderer::GetSampler(texture.second) };
        }
        gpu.BindFragmentSamplers(effectPass, 0, textureBindings.data(),
            static_cast<uint32_t>(textureBindings.size()));

        if (effect.uniforms && effect.uniforms->GetBufferSize() > 0) {
            gpu.PushFragmentUniformData(cmdBuffer, 0,
                effect.uniforms->GetBufferPointer(),
                effect.uniforms->GetBufferSize());
        } else {
            float dummy[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            gpu.PushFragmentUniformData(cmdBuffer, 0, &dummy, sizeof(dummy));
        }

        GpuBufferBinding vb { _effectQuadVbuf, 0 };
        gpu.BindVertexBuffers(effectPass, 0, &vb, 1);
        GpuBufferBinding ib { _effectQuadIbuf, 0 };
        gpu.BindIndexBuffer(effectPass, ib, true);

        gpu.DrawIndexedPrimitives(effectPass, 6, 1, 0, 0, 0);
        gpu.EndRenderPass(effectPass);

        // Pipeline owned by _effectPipelineCache; released in _releaseEffectResources().

        if (!isLast) {
            readTex  = writeTex;
            writeTex = (readTex == effectTempA.gpuTexture) ? effectTempB.gpuTexture : effectTempA.gpuTexture;
        }
    }
}
