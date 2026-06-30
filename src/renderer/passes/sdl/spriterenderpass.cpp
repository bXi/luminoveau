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

// ── release / init (SDL) ─────────────────────────────────────────────────────

void SpriteRenderPass::release(bool logRelease) {
    IGpu& gpu = Renderer::GetGpu();

    releaseEffectResources();

    if (m_msaa_color_texture)       { gpu.releaseTexture(m_msaa_color_texture);              m_msaa_color_texture = 0; }
    if (m_msaa_depth_texture)       { gpu.releaseTexture(m_msaa_depth_texture);              m_msaa_depth_texture = 0; }
    if (m_depth_texture.gpuTexture) { gpu.releaseTexture(m_depth_texture.gpuTexture);        m_depth_texture.gpuTexture = 0; }

    if (SpriteDataTransferBuffer) { gpu.releaseTransferBuffer(SpriteDataTransferBuffer); SpriteDataTransferBuffer = 0; }
    if (SpriteDataBuffer)         { gpu.releaseBuffer(SpriteDataBuffer);                 SpriteDataBuffer         = 0; }
    if (vertex_shader)            { gpu.releaseShader(vertex_shader);                    vertex_shader            = 0; }
    if (fragment_shader)          { gpu.releaseShader(fragment_shader);                  fragment_shader          = 0; }
    if (m_pipeline)               { gpu.releaseGraphicsPipeline(m_pipeline);             m_pipeline               = 0; }

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

    // Local depth texture (D32_FLOAT) to match pipeline; MSAA color/depth come from
    // the shared framebuffer when MSAA is enabled.
    {
        GpuTextureCreateInfo depthInfo{};
        depthInfo.width       = surface_width;
        depthInfo.height      = surface_height;
        depthInfo.format      = GpuTextureFormat::D32_Float;
        depthInfo.usage       = GpuTextureUsage::DepthStencilTarget;
        depthInfo.sampleCount = GpuSampleCount::x1;
        m_depth_texture.gpuTexture = gpu.createTexture(depthInfo);
    }
    createEffectResources();
    GpuSampleCount sampleCount = m_noMSAA ? GpuSampleCount::x1 : Renderer::GetSampleCount();

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
    pipelineInfo.sampleCount              = sampleCount;
    pipelineInfo.vertexStorageBufferCount = 1;
    m_pipeline = gpu.createGraphicsPipeline(pipelineInfo);

    if (!m_pipeline) {
        LOG_CRITICAL("SpriteRenderPass: failed to create pipeline for {}", passname);
        return false;
    }

    SpriteDataTransferBuffer = gpu.createTransferBuffer({
        static_cast<uint32_t>(MAX_SPRITES * sizeof(CompactSpriteInstance)),
        GpuTransferUsage::Upload,
    });
    SpriteDataBuffer = gpu.createBuffer({
        static_cast<uint32_t>(MAX_SPRITES * sizeof(CompactSpriteInstance)),
        GpuBufferUsage::StorageRead,
    });

    if (logInit) {
        LOG_INFO("Render pass initialized: {}", passname.c_str());
    }
    return true;
}

// ── createShaders (SDL) ──────────────────────────────────────────────────────

void SpriteRenderPass::createShaders() {
    IGpu& gpu = Renderer::GetGpu();

    // spirv-cross renames "main" to "main0" in MSL (reserved keyword)
    #if defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        const char* entryPoint = "main0";
    #else
        const char* entryPoint = "main";
    #endif

    GpuShaderCreateInfo vsi{};
    vsi.code                = Luminoveau::Shaders::Sprite_Vert;
    vsi.codeSize            = Luminoveau::Shaders::Sprite_Vert_Size;
    vsi.entrypoint          = entryPoint;
    vsi.stage               = GpuShaderStage::Vertex;
    vsi.samplerCount        = 0;
    vsi.uniformBufferCount  = 2;  // ViewProjection + InstanceOffset
    vsi.storageBufferCount  = 1;
    vsi.storageTextureCount = 0;
    vertex_shader = gpu.createShader(vsi);
    if (!vertex_shader) {
        LOG_CRITICAL("failed to create vertex shader for: {} ({})", passname.c_str(), SDL_GetError());
    }

    GpuShaderCreateInfo fsi{};
    fsi.code                = Luminoveau::Shaders::Sprite_Frag;
    fsi.codeSize            = Luminoveau::Shaders::Sprite_Frag_Size;
    fsi.entrypoint          = entryPoint;
    fsi.stage               = GpuShaderStage::Fragment;
    fsi.samplerCount        = 1;
    fsi.uniformBufferCount  = 0;
    fsi.storageBufferCount  = 0;
    fsi.storageTextureCount = 0;
    fragment_shader = gpu.createShader(fsi);
    if (!fragment_shader) {
        LOG_CRITICAL("failed to create fragment shader for: {} ({})", passname.c_str(), SDL_GetError());
    }
}

// ── render (SDL) ─────────────────────────────────────────────────────────────

void SpriteRenderPass::render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4 &camera
) {
    #ifdef LUMIDEBUG
    SDL_PushGPUDebugGroup(reinterpret_cast<SDL_GPUCommandBuffer*>(cmdBuffer), CURRENT_METHOD());
    #endif

    // Check if ANY sprite in the queue has effects
    bool hasAnyEffects = false;
    for (uint32_t i = 0; i < renderQueue->Count(); i++) {
        if ((*renderQueue)[i].effectIndex >= 0) {
            hasAnyEffects = true;
            break;
        }
    }

    //sets up transfer - map the transfer buffer directly
    auto *dataPtr = static_cast<CompactSpriteInstance *>(
        Renderer::GetGpu().mapTransferBuffer(SpriteDataTransferBuffer, false));

    // Copy and compress from renderQueue to transfer buffer
    // Convert float32 to float16 and pack pairs into uint32

    size_t spriteCount = renderQueue->Count();
    size_t thread_count = thread_pool.get_thread_count();
    size_t chunk_size = spriteCount / thread_count + 1;

    for (size_t start = 0; start < spriteCount; start += chunk_size) {
        size_t end = std::min(start + chunk_size, spriteCount);
        thread_pool.enqueue([this, dataPtr, start, end]() {
            for (size_t i = start; i < end; ++i) {
                const auto& sprite = (*renderQueue)[i];
                float x = sprite.x;
                float y = sprite.y;
                float z = sprite.z;
                float rotation = sprite.rotation;
                float tex_u = fast_clamp(sprite.tex_u, 0.0f, 1.0f);
                float tex_v = fast_clamp(sprite.tex_v, 0.0f, 1.0f);
                float tex_w = fast_clamp(sprite.tex_w, -1.0f, 1.0f);
                float tex_h = fast_clamp(sprite.tex_h, -1.0f, 1.0f);
                float r = fast_clamp(sprite.r, 0.0f, 1.0f);
                float g = fast_clamp(sprite.g, 0.0f, 1.0f);
                float b = fast_clamp(sprite.b, 0.0f, 1.0f);
                float a = fast_clamp(sprite.a, 0.0f, 1.0f);
                float w = fast_max(sprite.w, 0.001f);
                float h = fast_max(sprite.h, 0.001f);
                float pivot_x = sprite.pivot_x;
                float pivot_y = sprite.pivot_y;
                bool isSDF = sprite.isSDF;

                dataPtr[i].pos_xy = pack_half2(x, y);
                dataPtr[i].pos_z_rot = pack_half2(z, rotation);
                dataPtr[i].tex_uv = pack_half2(tex_u, tex_v);
                dataPtr[i].tex_wh = pack_half2(tex_w, tex_h);
                dataPtr[i].color_rg = pack_half2(r, g);
                dataPtr[i].color_ba = pack_half2(b, a);
                dataPtr[i].size_wh = pack_half2(w, h);

                uint32_t pivot_packed = pack_half2(pivot_x, pivot_y);
                if (isSDF) {
                    pivot_packed |= 0x80000000u;
                }
                dataPtr[i].pivot_xy = pivot_packed;
            }
        });
    }
    thread_pool.wait_all();

    // Build batches respecting z-order, geometry, and texture changes
    std::vector<Batch> batches;
    batches.reserve(64);
    std::vector<bool> batchHasEffects;
    batchHasEffects.reserve(64);

    size_t      currentOffset = 0;
    for (size_t i             = 0; i < spriteCount; ++i) {
        const auto& cur = (*renderQueue)[i];
        bool geometryChanged = (i > 0 && cur.geometry != (*renderQueue)[i - 1].geometry);
        bool textureChanged = (i > 0 && cur.texture.gpuTexture != (*renderQueue)[i - 1].texture.gpuTexture);
        bool effectsChanged = (i > 0 && cur.effectIndex != (*renderQueue)[i - 1].effectIndex);

        if (i == 0 || geometryChanged || textureChanged || effectsChanged) {
            Batch batch;
            batch.offset  = currentOffset;
            batch.count   = 1;
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
    Renderer::GetGpu().unmapTransferBuffer(SpriteDataTransferBuffer);

    if (spriteCount > 0) {
        Renderer::GetGpu().uploadToBuffer(
            cmdBuffer,
            SpriteDataTransferBuffer, 0,
            SpriteDataBuffer,         0,
            static_cast<uint32_t>(spriteCount * sizeof(CompactSpriteInstance)),
            false);
    }

    bool shouldResolve = (renderTargetResolve != 0);

    if (!hasAnyEffects) {
        IGpu& gpu = Renderer::GetGpu();

        GpuColorTargetInfo ct{};
        ct.texture        = targetTexture;
        ct.resolveTexture = renderTargetResolve;
        ct.loadOp         = color_target_info_loadop;
        ct.storeOp        = shouldResolve ? GpuStoreOp::Resolve : GpuStoreOp::Store;
        ct.clearR         = color_target_clear_r;
        ct.clearG         = color_target_clear_g;
        ct.clearB         = color_target_clear_b;
        ct.clearA         = color_target_clear_a;

        GpuRenderPassHandle rp = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
        render_pass = rp;

        {
            // Cap viewport to the surface dims so fixedSize FBs (e.g. LightToy's hrc_scene)
            // get the FB-sized viewport their FB-sized camera projects against. Without the
            // cap, a 1348-wide FB would receive a 1598-wide viewport and sprites projected
            // through ortho(0,1348,…) would land outside the FB's pixel range.
            float vpW = std::min((float)Window::GetPhysicalWidth(),  (float)m_surface_width);
            float vpH = std::min((float)Window::GetPhysicalHeight(), (float)m_surface_height);
            gpu.setViewport(rp, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
        }

        if (_scissorEnabled) {
            gpu.setScissor(rp, _scissorX, _scissorY, _scissorW, _scissorH);
            _scissorEnabled = false;
        }

        gpu.bindGraphicsPipeline(rp, m_pipeline);
        gpu.bindVertexStorageBuffers(rp, 0, &SpriteDataBuffer, 1);

        // Camera is identical for every batch; push once and let it persist across draws.
        gpu.pushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));

        for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
            const auto &batch = batches[batchIdx];
            if (!batch.texture || !batch.sampler || !batch.vertexBuffer || !batch.indexBuffer) continue;

            GpuBufferBinding vb{ batch.vertexBuffer, 0 };
            gpu.bindVertexBuffers(rp, 0, &vb, 1);

            GpuBufferBinding ib{ batch.indexBuffer, 0 };
            gpu.bindIndexBuffer(rp, ib, true);

            GpuTextureSamplerBinding tsb{ batch.texture, batch.sampler };
            gpu.bindFragmentSamplers(rp, 0, &tsb, 1);

            uint32_t batchOffset = static_cast<uint32_t>(batch.offset);
            gpu.pushVertexUniformData(cmdBuffer, 1, &batchOffset, sizeof(uint32_t));

            gpu.drawIndexedPrimitives(rp,
                batch.indexCount,
                static_cast<uint32_t>(batch.count), 0, 0, 0);
        }

        gpu.endRenderPass(rp);
    } else {
        IGpu& gpu = Renderer::GetGpu();
        GpuRenderPassHandle currentPass = 0;

        for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
            const auto& batch = batches[batchIdx];
            if (!batch.texture || !batch.sampler || !batch.vertexBuffer || !batch.indexBuffer) continue;

            if (!batchHasEffects[batchIdx]) {
                if (!currentPass) {
                    GpuColorTargetInfo ct{};
                    ct.texture = targetTexture;
                    ct.loadOp  = (batchIdx == 0) ? color_target_info_loadop : GpuLoadOp::Load;
                    ct.storeOp = GpuStoreOp::Store;
                    ct.clearR  = color_target_clear_r;
                    ct.clearG  = color_target_clear_g;
                    ct.clearB  = color_target_clear_b;
                    ct.clearA  = color_target_clear_a;
                    currentPass = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);

                    {
                        float vpW = std::min((float)Window::GetPhysicalWidth(),  (float)m_surface_width);
                        float vpH = std::min((float)Window::GetPhysicalHeight(), (float)m_surface_height);
                        gpu.setViewport(currentPass, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
                    }
                    if (_scissorEnabled) {
                        gpu.setScissor(currentPass, _scissorX, _scissorY, _scissorW, _scissorH);
                        _scissorEnabled = false;
                    }
                    gpu.bindGraphicsPipeline(currentPass, m_pipeline);
                    gpu.bindVertexStorageBuffers(currentPass, 0, &SpriteDataBuffer, 1);
                    // Push once per pass; camera is constant across the batches drawn into it.
                    gpu.pushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));
                }

                GpuBufferBinding vb{ batch.vertexBuffer, 0 };
                gpu.bindVertexBuffers(currentPass, 0, &vb, 1);
                GpuBufferBinding ib{ batch.indexBuffer, 0 };
                gpu.bindIndexBuffer(currentPass, ib, true);

                GpuTextureSamplerBinding tsb{ batch.texture, batch.sampler };
                gpu.bindFragmentSamplers(currentPass, 0, &tsb, 1);

                uint32_t batchOffset = static_cast<uint32_t>(batch.offset);
                gpu.pushVertexUniformData(cmdBuffer, 1, &batchOffset, sizeof(uint32_t));

                gpu.drawIndexedPrimitives(currentPass,
                    batch.indexCount,
                    static_cast<uint32_t>(batch.count), 0, 0, 0);
            } else {
                if (currentPass) {
                    gpu.endRenderPass(currentPass);
                    currentPass = 0;
                }

                size_t spriteIdx = batch.offset;
                int32_t effectIdx = (*renderQueue)[spriteIdx].effectIndex;
                if (spriteIdx >= spriteCount || effectIdx < 0) continue;
                const auto& effectStore = Draw::GetEffectStore();
                if (effectIdx >= (int32_t)effectStore.size()) continue;
                const auto& effects = effectStore[effectIdx];

                // Step 1: Render this batch to temp texture
                GpuColorTargetInfo tempCT{};
                tempCT.texture = effectTempA.gpuTexture;
                tempCT.loadOp  = GpuLoadOp::Clear;
                tempCT.storeOp = GpuStoreOp::Store;
                GpuRenderPassHandle tempPass = gpu.beginRenderPass(cmdBuffer, &tempCT, 1, nullptr);

                {
                    float vpW = std::min((float)Window::GetPhysicalWidth(),  (float)m_surface_width);
                    float vpH = std::min((float)Window::GetPhysicalHeight(), (float)m_surface_height);
                    gpu.setViewport(tempPass, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
                }
                gpu.bindGraphicsPipeline(tempPass, effectSpritePipeline);
                gpu.bindVertexStorageBuffers(tempPass, 0, &SpriteDataBuffer, 1);

                GpuBufferBinding vb{ batch.vertexBuffer, 0 };
                gpu.bindVertexBuffers(tempPass, 0, &vb, 1);
                GpuBufferBinding ib{ batch.indexBuffer, 0 };
                gpu.bindIndexBuffer(tempPass, ib, true);

                GpuTextureSamplerBinding tsb{ batch.texture, batch.sampler };
                gpu.bindFragmentSamplers(tempPass, 0, &tsb, 1);
                gpu.pushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));

                uint32_t batchOffset = static_cast<uint32_t>(batch.offset);
                gpu.pushVertexUniformData(cmdBuffer, 1, &batchOffset, sizeof(uint32_t));

                gpu.drawIndexedPrimitives(tempPass,
                    batch.indexCount,
                    static_cast<uint32_t>(batch.count), 0, 0, 0);
                gpu.endRenderPass(tempPass);

                const auto& effectTextureStore = Draw::GetEffectTextureStore();
                const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>> emptyTextures;
                const auto& storedTextures = (effectIdx < (int32_t)effectTextureStore.size()) ? effectTextureStore[effectIdx] : emptyTextures;
                applyEffects(cmdBuffer, effects, effectTempA.gpuTexture, targetTexture, camera, m_swapchain_format, batchIdx == 0, storedTextures);
            }
        }

        if (currentPass) {
            gpu.endRenderPass(currentPass);
        }

        if (shouldResolve) {
            GpuColorTargetInfo resolveCT{};
            resolveCT.texture        = targetTexture;
            resolveCT.resolveTexture = renderTargetResolve;
            resolveCT.loadOp         = GpuLoadOp::Load;
            resolveCT.storeOp        = GpuStoreOp::Resolve;
            GpuRenderPassHandle resolvePass = gpu.beginRenderPass(cmdBuffer, &resolveCT, 1, nullptr);
            gpu.endRenderPass(resolvePass);
        }
    }

#ifdef LUMIDEBUG
    SDL_PopGPUDebugGroup(reinterpret_cast<SDL_GPUCommandBuffer*>(cmdBuffer));
#endif
}

// ── Effect resources (SDL) ───────────────────────────────────────────────────

void SpriteRenderPass::onResize(uint32_t surfaceWidth, uint32_t surfaceHeight) {
    if (surfaceWidth == 0 || surfaceHeight == 0) return;
    if (surfaceWidth == m_surface_width && surfaceHeight == m_surface_height) return;

    IGpu& gpu = Renderer::GetGpu();
    gpu.waitIdle();

    // Recreate only the surface-sized targets (depth + effect temps). Pipelines, shaders and
    // the render queue are untouched — no recompile.
    if (m_depth_texture.gpuTexture) { gpu.releaseTexture(m_depth_texture.gpuTexture); m_depth_texture.gpuTexture = 0; }
    if (effectTempA.gpuTexture)     { gpu.releaseTexture(effectTempA.gpuTexture);     effectTempA.gpuTexture = 0; }
    if (effectTempB.gpuTexture)     { gpu.releaseTexture(effectTempB.gpuTexture);     effectTempB.gpuTexture = 0; }

    m_surface_width  = surfaceWidth;
    m_surface_height = surfaceHeight;

    GpuTextureCreateInfo depthInfo{};
    depthInfo.width       = surfaceWidth;
    depthInfo.height      = surfaceHeight;
    depthInfo.format      = GpuTextureFormat::D32_Float;
    depthInfo.usage       = GpuTextureUsage::DepthStencilTarget;
    depthInfo.sampleCount = GpuSampleCount::x1;
    m_depth_texture.gpuTexture = gpu.createTexture(depthInfo);

    createEffectResources();
}

void SpriteRenderPass::createEffectResources() {
    IGpu& gpu = Renderer::GetGpu();

    GpuTextureCreateInfo tempTexInfo{};
    tempTexInfo.width       = m_surface_width;
    tempTexInfo.height      = m_surface_height;
    tempTexInfo.format      = GpuTextureFormat::R8G8B8A8_Unorm;
    tempTexInfo.usage       = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
    tempTexInfo.sampleCount = GpuSampleCount::x1;

    effectTempA.gpuTexture = gpu.createTexture(tempTexInfo);
    effectTempA.gpuSampler = Renderer::GetSampler(ScaleMode::Nearest);
    effectTempA.width      = m_surface_width;
    effectTempA.height     = m_surface_height;
    effectTempA.filename   = "[Lumi]EffectTempA";

    effectTempB.gpuTexture = gpu.createTexture(tempTexInfo);
    effectTempB.gpuSampler = Renderer::GetSampler(ScaleMode::Nearest);
    effectTempB.width      = m_surface_width;
    effectTempB.height     = m_surface_height;
    effectTempB.filename   = "[Lumi]EffectTempB";

    if (!effectTempA.gpuTexture || !effectTempB.gpuTexture) {
        LOG_ERROR("Failed to create effect temp textures: {}", SDL_GetError());
        return;
    }

    GpuColorTargetBlendState noBlend{};
    noBlend.blendEnabled    = true;
    noBlend.srcColorFactor  = GpuBlendFactor::One;
    noBlend.dstColorFactor  = GpuBlendFactor::Zero;
    noBlend.colorOp         = GpuBlendOp::Add;
    noBlend.srcAlphaFactor  = GpuBlendFactor::One;
    noBlend.dstAlphaFactor  = GpuBlendFactor::Zero;
    noBlend.alphaOp         = GpuBlendOp::Add;

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
    pipelineInfo.colorTargetFormat        = GpuTextureFormat::R8G8B8A8_Unorm;
    pipelineInfo.blend                    = noBlend;
    pipelineInfo.hasDepthTarget           = false;
    pipelineInfo.sampleCount              = GpuSampleCount::x1;
    pipelineInfo.vertexStorageBufferCount = 1;

    effectSpritePipeline = gpu.createGraphicsPipeline(pipelineInfo);
    if (!effectSpritePipeline) {
        LOG_ERROR("Failed to create effect sprite pipeline: {}", SDL_GetError());
    }
}

void SpriteRenderPass::releaseEffectResources() {
    IGpu& gpu = Renderer::GetGpu();
    if (effectTempA.gpuTexture)   { gpu.releaseTexture(effectTempA.gpuTexture);     effectTempA.gpuTexture = 0; }
    if (effectTempB.gpuTexture)   { gpu.releaseTexture(effectTempB.gpuTexture);     effectTempB.gpuTexture = 0; }
    if (effectPipeline)           { gpu.releaseGraphicsPipeline(effectPipeline);    effectPipeline       = 0; }
    if (effectSpritePipeline)     { gpu.releaseGraphicsPipeline(effectSpritePipeline); effectSpritePipeline = 0; }
    if (effectVertShader)         { gpu.releaseShader(effectVertShader);            effectVertShader     = 0; }
    for (auto& [key, pipeline] : m_effectPipelineCache) {
        if (pipeline) gpu.releaseGraphicsPipeline(pipeline);
    }
    m_effectPipelineCache.clear();
    if (m_effectQuadVbuf) { gpu.releaseBuffer(m_effectQuadVbuf); m_effectQuadVbuf = 0; }
    if (m_effectQuadIbuf) { gpu.releaseBuffer(m_effectQuadIbuf); m_effectQuadIbuf = 0; }
    m_effectQuadUvScaleX = -1.0f;
    m_effectQuadUvScaleY = -1.0f;
}

void SpriteRenderPass::applyEffects(GpuCmdBufferHandle cmdBuffer, const std::vector<EffectAsset>& effects,
                                   GpuTextureHandle sourceTexture, GpuTextureHandle targetTexture, const glm::mat4& camera,
                                   GpuTextureFormat targetFormat, bool isFirstBatch,
                                   const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>>& effectTextures) {
    (void)camera;
    if (effects.empty()) return;

    IGpu& gpu = Renderer::GetGpu();

    // Fullscreen quad geometry (position + texcoord). Temp textures are surface-sized
    // but only the physical-pixel area was drawn, so scale UVs to compensate.
    struct Vertex { float x, y, u, v; };
    float uvScaleX = (float)Window::GetPhysicalWidth()  / (float)m_surface_width;
    float uvScaleY = (float)Window::GetPhysicalHeight() / (float)m_surface_height;

    // Lazily create the static quad buffers once; index data never changes.
    if (!m_effectQuadVbuf) {
        m_effectQuadVbuf = gpu.createBuffer({ sizeof(Vertex) * 4, GpuBufferUsage::Vertex });
        m_effectQuadIbuf = gpu.createBuffer({ sizeof(uint16_t) * 6, GpuBufferUsage::Index });

        uint16_t quadIndices[] = {0, 1, 2, 2, 1, 3};
        GpuTransferBufferHandle idxXfer = gpu.createTransferBuffer({ sizeof(quadIndices), GpuTransferUsage::Upload });
        memcpy(gpu.mapTransferBuffer(idxXfer, false), quadIndices, sizeof(quadIndices));
        gpu.unmapTransferBuffer(idxXfer);
        gpu.uploadToBuffer(cmdBuffer, idxXfer, 0, m_effectQuadIbuf, 0, sizeof(quadIndices));
        gpu.releaseTransferBuffer(idxXfer);
        m_effectQuadUvScaleX = -1.0f;  // force the vertex upload below
    }

    // Re-upload vertices only when the UV scale changed (resize); otherwise the persistent
    // buffer already holds the right geometry from a previous frame.
    if (uvScaleX != m_effectQuadUvScaleX || uvScaleY != m_effectQuadUvScaleY) {
        Vertex quadVertices[] = {
            {0.0f, 0.0f, 0.0f,     uvScaleY},
            {1.0f, 0.0f, uvScaleX, uvScaleY},
            {0.0f, 1.0f, 0.0f,     0.0f},
            {1.0f, 1.0f, uvScaleX, 0.0f},
        };
        GpuTransferBufferHandle vtxXfer = gpu.createTransferBuffer({ sizeof(quadVertices), GpuTransferUsage::Upload });
        memcpy(gpu.mapTransferBuffer(vtxXfer, false), quadVertices, sizeof(quadVertices));
        gpu.unmapTransferBuffer(vtxXfer);
        gpu.uploadToBuffer(cmdBuffer, vtxXfer, 0, m_effectQuadVbuf, 0, sizeof(quadVertices));
        gpu.releaseTransferBuffer(vtxXfer);
        m_effectQuadUvScaleX = uvScaleX;
        m_effectQuadUvScaleY = uvScaleY;
    }

    GpuTextureHandle readTex  = sourceTexture;
    GpuTextureHandle writeTex = (effects.size() == 1) ? targetTexture : effectTempB.gpuTexture;

    GpuVertexAttribute vertexAttribs[] = {
        { .location = 0, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 0 },
        { .location = 1, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 8 },
    };
    GpuVertexBinding vertBinding = { .binding = 0, .stride = 16, .instanceStepping = false };

    for (size_t i = 0; i < effects.size(); ++i) {
        const auto& effect    = effects[i];
        bool        isLast    = (i == effects.size() - 1);
        if (isLast) writeTex = targetTexture;

        if (!effect.vertShader.gpuShader || !effect.fragShader.gpuShader) {
            LOG_ERROR("Effect shaders are NULL: vert={}, frag={}",
                (void*)effect.vertShader.gpuShader, (void*)effect.fragShader.gpuShader);
            continue;
        }

        GpuColorTargetBlendState blend{};
        if (isLast && m_noMSAA) {
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

        GpuSampleCount pipelineSampleCount =
            (isLast && !m_noMSAA) ? Renderer::GetSampleCount() : GpuSampleCount::x1;

        GpuGraphicsPipelineCreateInfo pi{};
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

        EffectPipelineKey key{ effect.vertShader.gpuShader, effect.fragShader.gpuShader,
                               isLast, pi.colorTargetFormat, pipelineSampleCount };
        GpuGraphicsPipelineHandle pipeline = 0;
        if (auto it = m_effectPipelineCache.find(key); it != m_effectPipelineCache.end()) {
            pipeline = it->second;
        } else {
            pipeline = gpu.createGraphicsPipeline(pi);
            if (!pipeline) {
                LOG_ERROR("Failed to create effect pipeline: {}", SDL_GetError());
                continue;
            }
            m_effectPipelineCache.emplace(key, pipeline);
        }

        GpuColorTargetInfo ct{};
        ct.texture = writeTex;
        ct.loadOp  = isLast ? (isFirstBatch ? GpuLoadOp::Clear : GpuLoadOp::Load) : GpuLoadOp::Clear;
        ct.storeOp = GpuStoreOp::Store;

        GpuRenderPassHandle effectPass = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
        {
            float vpW = std::min((float)Window::GetPhysicalWidth(),  (float)m_surface_width);
            float vpH = std::min((float)Window::GetPhysicalHeight(), (float)m_surface_height);
            gpu.setViewport(effectPass, 0.0f, 0.0f, vpW, vpH, 0.0f, 1.0f);
        }
        gpu.bindGraphicsPipeline(effectPass, pipeline);

        std::vector<GpuTextureSamplerBinding> textureBindings;
        textureBindings.push_back({ readTex, Renderer::GetSampler(ScaleMode::Nearest) });
        for (const auto& [binding, texture] : effectTextures) {
            while (textureBindings.size() <= binding) {
                textureBindings.push_back(textureBindings[0]);
            }
            textureBindings[binding] = { texture.first, Renderer::GetSampler(texture.second) };
        }
        gpu.bindFragmentSamplers(effectPass, 0, textureBindings.data(),
                                 static_cast<uint32_t>(textureBindings.size()));

        if (effect.uniforms && effect.uniforms->getBufferSize() > 0) {
            gpu.pushFragmentUniformData(cmdBuffer, 0,
                effect.uniforms->getBufferPointer(),
                effect.uniforms->getBufferSize());
        } else {
            float dummy[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            gpu.pushFragmentUniformData(cmdBuffer, 0, &dummy, sizeof(dummy));
        }

        GpuBufferBinding vb{ m_effectQuadVbuf, 0 };
        gpu.bindVertexBuffers(effectPass, 0, &vb, 1);
        GpuBufferBinding ib{ m_effectQuadIbuf, 0 };
        gpu.bindIndexBuffer(effectPass, ib, true);

        gpu.drawIndexedPrimitives(effectPass, 6, 1, 0, 0, 0);
        gpu.endRenderPass(effectPass);

        // Pipeline owned by m_effectPipelineCache; released in releaseEffectResources().

        if (!isLast) {
            readTex  = writeTex;
            writeTex = (readTex == effectTempA.gpuTexture) ? effectTempB.gpuTexture : effectTempA.gpuTexture;
        }
    }
}
