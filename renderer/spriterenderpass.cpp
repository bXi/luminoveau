#include "spriterenderpass.h"

#include <utility>

#include "SDL3/SDL_gpu.h"
#include "window/windowhandler.h"
#include "assethandler/shaders_generated.h"
#include "log/loghandler.h"
#include "draw/drawhandler.h"

#include "utils/constants.h"

void SpriteRenderPass::release(bool logRelease) {
    // Release effect resources
    releaseEffectResources();
    
    if (m_msaa_color_texture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), m_msaa_color_texture);
        m_msaa_color_texture = nullptr;
    }
    if (m_msaa_depth_texture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), m_msaa_depth_texture);
        m_msaa_depth_texture = nullptr;
    }

    m_depth_texture.release(Renderer::GetDevice());

    // Release buffers
    if (SpriteDataTransferBuffer) {
        SDL_ReleaseGPUTransferBuffer(Renderer::GetDevice(), SpriteDataTransferBuffer);
        SpriteDataTransferBuffer = nullptr;
    }

    if (SpriteDataBuffer) {
        SDL_ReleaseGPUBuffer(Renderer::GetDevice(), SpriteDataBuffer);
        SpriteDataBuffer = nullptr;
    }

    // Release shaders
    if (vertex_shader) {
        SDL_ReleaseGPUShader(Renderer::GetDevice(), vertex_shader);
        vertex_shader = nullptr;
    }

    if (fragment_shader) {
        SDL_ReleaseGPUShader(Renderer::GetDevice(), fragment_shader);
        fragment_shader = nullptr;
    }

    // Release pipeline
    if (m_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), m_pipeline);
        m_pipeline = nullptr;
    }

    if (logRelease) {
        LOG_INFO("Released graphics pipeline: {}", passname.c_str());
    }
}

bool SpriteRenderPass::init(
    SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name, bool logInit) {
    passname = std::move(name);
    
    // Store surface dimensions and format for effect textures
    m_surface_width = surface_width;
    m_surface_height = surface_height;
    m_swapchain_format = swapchain_texture_format;

    SDL_GPUSampleCount sampleCount = Renderer::GetSampleCount();

    // Don't create MSAA textures - use shared framebuffer MSAA texture
    // Create local depth texture with D32_FLOAT to match pipeline
    SDL_GPUTextureCreateInfo depth_create_info{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,  // Must match pipeline format
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = surface_width,
        .height = surface_height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };
    m_depth_texture.gpuTexture = SDL_CreateGPUTexture(Renderer::GetDevice(), &depth_create_info);

    renderQueue.resize(MAX_SPRITES);

    createShaders();
    createEffectResources();

    SDL_GPUColorTargetDescription color_target_description{
        .format = swapchain_texture_format,
        .blend_state = renderPassBlendState,
    };

    // Define vertex buffer layout for Vertex2D (CompactVertex2D)
    SDL_GPUVertexAttribute vertexAttributes[] = {
        {
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT,  // pos_xy (uint32 with 2 packed half-floats)
            .offset = 0
        },
        {
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT,  // uv (uint32 with 2 packed half-floats)
            .offset = 4
        }
    };
    
    SDL_GPUVertexBufferDescription vertexBufferDesc = {
        .slot = 0,
        .pitch = 8,  // sizeof(CompactVertex2D)
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0
    };
    
    SDL_GPUGraphicsPipelineCreateInfo pipeline_create_info{
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state =
            {
                .vertex_buffer_descriptions = &vertexBufferDesc,
                .num_vertex_buffers = 1,
                .vertex_attributes = vertexAttributes,
                .num_vertex_attributes = 2,
            },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = GPUstructs::defaultRasterizerState,
        .multisample_state = {
            .sample_count = sampleCount,
            .sample_mask  = 0,
            .enable_mask  = false,// Must be false when using multisampling
        },
        .depth_stencil_state = {
            .compare_op          = SDL_GPU_COMPAREOP_LESS,
            .back_stencil_state  = {},
            .front_stencil_state = {},
            .compare_mask        = 0,
            .write_mask          = 0,
            .enable_depth_test   = false,  // Disabled for 2D sprites!
            .enable_depth_write  = false,
            .enable_stencil_test = false,
        },
        .target_info = {
            .color_target_descriptions = &color_target_description,
            .num_color_targets         = 1,
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_INVALID,  // No depth needed
            .has_depth_stencil_target  = false,
        },
        .props = 0,
    };
    m_pipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &pipeline_create_info);

    SDL_GPUTransferBufferCreateInfo transferBufferCreateInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = MAX_SPRITES * sizeof(CompactSpriteInstance)  // Use compact size
    };

    SpriteDataTransferBuffer = SDL_CreateGPUTransferBuffer(
        m_gpu_device,
        &transferBufferCreateInfo
    );

    SDL_GPUBufferCreateInfo bufferCreateInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = MAX_SPRITES * sizeof(CompactSpriteInstance)  // Use compact size
    };

    SpriteDataBuffer = SDL_CreateGPUBuffer(
        m_gpu_device,
        &bufferCreateInfo
    );

    if (!m_pipeline) {
        LOG_CRITICAL("failed to create graphics pipeline: {}", SDL_GetError());
    }

    if (logInit) {
        LOG_INFO("Created graphics pipeline: {}", passname.c_str());
    }

    return true;
}

void SpriteRenderPass::render(
    SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
) {
    #ifdef LUMIDEBUG
    SDL_PushGPUDebugGroup(cmd_buffer, CURRENT_METHOD());
    #endif
    
    // Check if ANY sprite in the queue has effects
    bool hasAnyEffects = false;
    for (uint32_t i = 0; i < renderQueueCount; i++) {
        if (!renderQueue[i].effects.empty()) {
            hasAnyEffects = true;
            break;
        }
    }

    //sets up transfer - map the transfer buffer directly
    auto *dataPtr = static_cast<CompactSpriteInstance *>(SDL_MapGPUTransferBuffer(
        m_gpu_device,
        SpriteDataTransferBuffer,
        false
    ));

    // Copy and compress from renderQueue to transfer buffer
    // Convert float32 to float16 and pack pairs into uint32
    
    size_t thread_count = thread_pool.get_thread_count();
    size_t chunk_size = renderQueueCount / thread_count + 1;
    
    for (size_t start = 0; start < renderQueueCount; start += chunk_size) {
        size_t end = std::min(start + chunk_size, renderQueueCount);
        thread_pool.enqueue([this, dataPtr, start, end]() {
            for (size_t i = start; i < end; ++i) {
                // Validate and sanitize input values
                float x = renderQueue[i].x;
                float y = renderQueue[i].y;
                float z = renderQueue[i].z;
                float rotation = renderQueue[i].rotation;
                float tex_u = fast_clamp(renderQueue[i].tex_u, 0.0f, 1.0f);
                float tex_v = fast_clamp(renderQueue[i].tex_v, 0.0f, 1.0f);
                float tex_w = fast_clamp(renderQueue[i].tex_w, 0.0f, 1.0f);
                float tex_h = fast_clamp(renderQueue[i].tex_h, 0.0f, 1.0f);
                float r = fast_clamp(renderQueue[i].r, 0.0f, 1.0f);
                float g = fast_clamp(renderQueue[i].g, 0.0f, 1.0f);
                float b = fast_clamp(renderQueue[i].b, 0.0f, 1.0f);
                float a = fast_clamp(renderQueue[i].a, 0.0f, 1.0f);
                float w = fast_max(renderQueue[i].w, 0.001f);  // Prevent zero size
                float h = fast_max(renderQueue[i].h, 0.001f);
                float pivot_x = renderQueue[i].pivot_x;
                float pivot_y = renderQueue[i].pivot_y;
                bool isSDF = renderQueue[i].isSDF;
                
                // Pack each pair of floats into a single uint32
                dataPtr[i].pos_xy = pack_half2(x, y);
                dataPtr[i].pos_z_rot = pack_half2(z, rotation);
                dataPtr[i].tex_uv = pack_half2(tex_u, tex_v);
                dataPtr[i].tex_wh = pack_half2(tex_w, tex_h);
                dataPtr[i].color_rg = pack_half2(r, g);
                dataPtr[i].color_ba = pack_half2(b, a);
                dataPtr[i].size_wh = pack_half2(w, h);
                
                // Pack pivot_xy with SDF flag in highest bit
                uint32_t pivot_packed = pack_half2(pivot_x, pivot_y);
                if (isSDF) {
                    pivot_packed |= 0x80000000u;  // Set highest bit
                }
                dataPtr[i].pivot_xy = pivot_packed;
            }
        });
    }
    thread_pool.wait_all();

    // Build batches respecting z-order, geometry, and texture changes
    // Also track which batches have effects
    std::vector<Batch> batches;
    batches.reserve(64);
    std::vector<bool> batchHasEffects;  // Track if each batch has effects
    batchHasEffects.reserve(64);

    size_t      currentOffset = 0;
    for (size_t i             = 0; i < renderQueueCount; ++i) {
        bool geometryChanged = (i > 0 && renderQueue[i].geometry != renderQueue[i - 1].geometry);
        bool textureChanged = (i > 0 && renderQueue[i].texture.gpuTexture != renderQueue[i - 1].texture.gpuTexture);
        bool effectsChanged = (i > 0 && renderQueue[i].effects.size() != renderQueue[i - 1].effects.size());
        
        if (i == 0 || geometryChanged || textureChanged || effectsChanged) {
            // Start a new batch when geometry, texture, or effects change
            Batch batch;
            batch.offset  = currentOffset;
            batch.count   = 1;
            batch.geometry = renderQueue[i].geometry;
            batch.texture = renderQueue[i].texture.gpuTexture;
            batch.sampler = renderQueue[i].texture.gpuSampler;
            batches.push_back(batch);
            batchHasEffects.push_back(!renderQueue[i].effects.empty());
        } else {
            // Continue the current batch
            batches.back().count++;
        }
        currentOffset++;
    }
    // Data already copied directly to transfer buffer by thread pool
    SDL_UnmapGPUTransferBuffer(m_gpu_device, SpriteDataTransferBuffer);

    if (renderQueueCount > 0) {
        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmd_buffer);

        SDL_GPUTransferBufferLocation transferBufferLocation = {
            .transfer_buffer = SpriteDataTransferBuffer,
            .offset = 0
        };

        SDL_GPUBufferRegion bufferRegion = {
            .buffer = SpriteDataBuffer,
            .offset = 0,
            .size = static_cast<Uint32>(renderQueueCount * sizeof(CompactSpriteInstance))
        };

        SDL_UploadToGPUBuffer(
            copyPass,
            &transferBufferLocation,
            &bufferRegion,
            false
        );
        SDL_EndGPUCopyPass(copyPass);
    }

    // Check if we should resolve (if renderTargetResolve is set)
    bool shouldResolve = (renderTargetResolve != nullptr);

    // Render batches in Z-order, handling effects appropriately
    if (!hasAnyEffects) {
        // No effects - simple single render pass
        SDL_GPUTexture* renderTarget = target_texture;
        
        SDL_GPUColorTargetInfo color_target_info{
            .texture              = renderTarget,
            .mip_level            = 0,
            .layer_or_depth_plane = 0,
            .clear_color          = color_target_info_clear_color,
            .load_op              = color_target_info_loadop,
            .store_op             = shouldResolve ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE,
            .resolve_texture      = renderTargetResolve,
            .resolve_mip_level    = 0,
            .resolve_layer        = 0,
            .cycle                = false};

        render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target_info, 1, nullptr);
        assert(render_pass);
        
        SDL_GPUViewport viewport = {
            .x = 0, .y = 0,
            .w = (float)Window::GetWidth(),
            .h = (float)Window::GetHeight(),
            .min_depth = 0.0f, .max_depth = 1.0f
        };
        SDL_SetGPUViewport(render_pass, &viewport);

        if (_scissorEnabled) {
            SDL_SetGPUScissor(render_pass, _scissorRect);
            _scissorEnabled = false;
        }

        SDL_BindGPUGraphicsPipeline(render_pass, m_pipeline);
        SDL_BindGPUVertexStorageBuffers(render_pass, 0, &SpriteDataBuffer, 1);

        for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
            const auto &batch = batches[batchIdx];
            if (batch.texture == nullptr || batch.sampler == nullptr || batch.geometry == nullptr) continue;
            
            SDL_GPUBufferBinding vertexBinding = {.buffer = batch.geometry->vertexBuffer, .offset = 0};
            SDL_BindGPUVertexBuffers(render_pass, 0, &vertexBinding, 1);
            
            SDL_GPUBufferBinding indexBinding = {.buffer = batch.geometry->indexBuffer, .offset = 0};
            SDL_BindGPUIndexBuffer(render_pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
            
            SDL_GPUTextureSamplerBinding samplerBinding = {.texture = batch.texture, .sampler = batch.sampler};
            SDL_BindGPUFragmentSamplers(render_pass, 0, &samplerBinding, 1);
            SDL_PushGPUVertexUniformData(cmd_buffer, 0, &camera, sizeof(glm::mat4));
            
            uint32_t batchOffset = static_cast<uint32_t>(batch.offset);
            SDL_PushGPUVertexUniformData(cmd_buffer, 1, &batchOffset, sizeof(uint32_t));
            
            SDL_DrawGPUIndexedPrimitives(render_pass,
                static_cast<uint32_t>(batch.geometry->GetIndexCount()),
                batch.count, 0, 0, 0);
        }
        
        SDL_EndGPURenderPass(render_pass);
    } else {
        // Has effects - need to process batches in order to maintain z-ordering
        // Strategy: iterate batches in order, accumulating non-effect batches in a render pass,
        // then when we hit an effect batch, end pass, render effect batch, restart pass
        
        SDL_GPURenderPass* currentPass = nullptr;
        
        for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
            const auto& batch = batches[batchIdx];
            if (batch.texture == nullptr || batch.sampler == nullptr || batch.geometry == nullptr) continue;
            
            if (!batchHasEffects[batchIdx]) {
                // Non-effect batch - render normally
                // Start a render pass if needed
                if (!currentPass) {
                    SDL_GPUColorTargetInfo colorTarget = {
                        .texture = target_texture,
                        .mip_level = 0,
                        .layer_or_depth_plane = 0,
                        .clear_color = color_target_info_clear_color,
                        .load_op = (batchIdx == 0) ? color_target_info_loadop : SDL_GPU_LOADOP_LOAD,
                        .store_op = SDL_GPU_STOREOP_STORE,
                        .resolve_texture = nullptr,
                        .resolve_mip_level = 0,
                        .resolve_layer = 0,
                        .cycle = false
                    };
                    currentPass = SDL_BeginGPURenderPass(cmd_buffer, &colorTarget, 1, nullptr);
                    
                    SDL_GPUViewport viewport = {.x = 0, .y = 0,
                        .w = (float)Window::GetWidth(), .h = (float)Window::GetHeight(),
                        .min_depth = 0.0f, .max_depth = 1.0f};
                    SDL_SetGPUViewport(currentPass, &viewport);
                    if (_scissorEnabled) {
                        SDL_SetGPUScissor(currentPass, _scissorRect);
                        _scissorEnabled = false;
                    }
                    SDL_BindGPUGraphicsPipeline(currentPass, m_pipeline);
                    SDL_BindGPUVertexStorageBuffers(currentPass, 0, &SpriteDataBuffer, 1);
                }
                
                // Render this batch
                SDL_GPUBufferBinding vertexBinding = {.buffer = batch.geometry->vertexBuffer, .offset = 0};
                SDL_BindGPUVertexBuffers(currentPass, 0, &vertexBinding, 1);
                
                SDL_GPUBufferBinding indexBinding = {.buffer = batch.geometry->indexBuffer, .offset = 0};
                SDL_BindGPUIndexBuffer(currentPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                
                SDL_GPUTextureSamplerBinding samplerBinding = {.texture = batch.texture, .sampler = batch.sampler};
                SDL_BindGPUFragmentSamplers(currentPass, 0, &samplerBinding, 1);
                SDL_PushGPUVertexUniformData(cmd_buffer, 0, &camera, sizeof(glm::mat4));
                
                uint32_t batchOffset = static_cast<uint32_t>(batch.offset);
                SDL_PushGPUVertexUniformData(cmd_buffer, 1, &batchOffset, sizeof(uint32_t));
                
                SDL_DrawGPUIndexedPrimitives(currentPass,
                    static_cast<uint32_t>(batch.geometry->GetIndexCount()),
                    batch.count, 0, 0, 0);
            } else {
                // Effect batch - need to end current pass, render through effect pipeline
                if (currentPass) {
                    SDL_EndGPURenderPass(currentPass);
                    currentPass = nullptr;
                }
                
                // Get effects from first sprite in batch
                size_t spriteIdx = batch.offset;
                if (spriteIdx >= renderQueueCount || renderQueue[spriteIdx].effects.empty()) continue;
                const auto& effects = renderQueue[spriteIdx].effects;
                
                // Step 1: Render this batch to temp texture
                SDL_GPUColorTargetInfo tempTarget = {
                    .texture = effectTempA.gpuTexture,
                    .mip_level = 0,
                    .layer_or_depth_plane = 0,
                    .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
                    .load_op = SDL_GPU_LOADOP_CLEAR,
                    .store_op = SDL_GPU_STOREOP_STORE,
                    .resolve_texture = nullptr,
                    .resolve_mip_level = 0,
                    .resolve_layer = 0,
                    .cycle = false  // Don't cycle - we're using it in the same command buffer
                };
                
                SDL_GPURenderPass* tempPass = SDL_BeginGPURenderPass(cmd_buffer, &tempTarget, 1, nullptr);
                
                // Viewport matches window size within desktop-sized texture
                SDL_GPUViewport viewport = {.x = 0, .y = 0,
                    .w = (float)Window::GetWidth(), .h = (float)Window::GetHeight(),
                    .min_depth = 0.0f, .max_depth = 1.0f};
                SDL_SetGPUViewport(tempPass, &viewport);
                SDL_BindGPUGraphicsPipeline(tempPass, effectSpritePipeline);  // Use no-blend pipeline!
                SDL_BindGPUVertexStorageBuffers(tempPass, 0, &SpriteDataBuffer, 1);
                
                SDL_GPUBufferBinding vertexBinding = {.buffer = batch.geometry->vertexBuffer, .offset = 0};
                SDL_BindGPUVertexBuffers(tempPass, 0, &vertexBinding, 1);
                
                SDL_GPUBufferBinding indexBinding = {.buffer = batch.geometry->indexBuffer, .offset = 0};
                SDL_BindGPUIndexBuffer(tempPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                
                SDL_GPUTextureSamplerBinding samplerBinding = {.texture = batch.texture, .sampler = batch.sampler};
                SDL_BindGPUFragmentSamplers(tempPass, 0, &samplerBinding, 1);
                SDL_PushGPUVertexUniformData(cmd_buffer, 0, &camera, sizeof(glm::mat4));
                
                uint32_t batchOffset = static_cast<uint32_t>(batch.offset);
                SDL_PushGPUVertexUniformData(cmd_buffer, 1, &batchOffset, sizeof(uint32_t));
                
                SDL_DrawGPUIndexedPrimitives(tempPass,
                    static_cast<uint32_t>(batch.geometry->GetIndexCount()),
                    batch.count, 0, 0, 0);
                
                SDL_EndGPURenderPass(tempPass);
                
                applyEffects(cmd_buffer, effects, effectTempA.gpuTexture, target_texture, camera, m_swapchain_format, batchIdx == 0);
            }
        }
        
        // End any remaining render pass
        if (currentPass) {
            SDL_EndGPURenderPass(currentPass);
        }
    }
    
#ifdef LUMIDEBUG
    SDL_PopGPUDebugGroup(cmd_buffer);
#endif
}

void SpriteRenderPass::createEffectResources() {
    // Create temporary textures for effect ping-pong rendering
    // Use surface (desktop) size to match framebuffer
    uint32_t width = m_surface_width;
    uint32_t height = m_surface_height;
    
    SDL_GPUTextureCreateInfo tempTexInfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    
    effectTempA.gpuTexture = SDL_CreateGPUTexture(Renderer::GetDevice(), &tempTexInfo);
    effectTempA.gpuSampler = Renderer::GetSampler(ScaleMode::NEAREST);
    effectTempA.width = width;
    effectTempA.height = height;
    effectTempA.filename = "[Lumi]EffectTempA";
    
    effectTempB.gpuTexture = SDL_CreateGPUTexture(Renderer::GetDevice(), &tempTexInfo);
    effectTempB.gpuSampler = Renderer::GetSampler(ScaleMode::NEAREST);
    effectTempB.width = width;
    effectTempB.height = height;
    effectTempB.filename = "[Lumi]EffectTempB";
    
    if (!effectTempA.gpuTexture || !effectTempB.gpuTexture) {
        LOG_ERROR("Failed to create effect temp textures: {}", SDL_GetError());
        return;
    }
    
    // Create pipeline for rendering sprites to temp texture (no blending - direct copy)
    SDL_GPUColorTargetBlendState noBlendState = {
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
        .color_blend_op = SDL_GPU_BLENDOP_ADD,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
        .color_write_mask = 0xF,
        .enable_blend = true,  // Enable but with ONE/ZERO = direct copy
        .enable_color_write_mask = false,
    };
    
    SDL_GPUColorTargetDescription colorTargetDesc = {
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_state = noBlendState,
    };
    
    SDL_GPUVertexAttribute vertexAttributes[] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT, .offset = 0},
        {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT, .offset = 4}
    };
    
    SDL_GPUVertexBufferDescription vertexBufferDesc = {
        .slot = 0,
        .pitch = 8,  // sizeof(CompactVertex2D)
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0
    };
    
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state = {
            .vertex_buffer_descriptions = &vertexBufferDesc,
            .num_vertex_buffers = 1,
            .vertex_attributes = vertexAttributes,
            .num_vertex_attributes = 2,
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = GPUstructs::defaultRasterizerState,
        .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1, .sample_mask = 0, .enable_mask = false},
        .depth_stencil_state = {.enable_depth_test = false, .enable_depth_write = false, .enable_stencil_test = false},
        .target_info = {
            .color_target_descriptions = &colorTargetDesc,
            .num_color_targets = 1,
            .has_depth_stencil_target = false,
        },
        .props = 0,
    };
    
    effectSpritePipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &pipelineInfo);
    if (!effectSpritePipeline) {
        LOG_ERROR("Failed to create effect sprite pipeline: {}", SDL_GetError());
    }
}

void SpriteRenderPass::releaseEffectResources() {
    if (effectTempA.gpuTexture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), effectTempA.gpuTexture);
        effectTempA.gpuTexture = nullptr;
    }
    if (effectTempB.gpuTexture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), effectTempB.gpuTexture);
        effectTempB.gpuTexture = nullptr;
    }
    if (effectPipeline) {
        SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), effectPipeline);
        effectPipeline = nullptr;
    }
    if (effectSpritePipeline) {
        SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), effectSpritePipeline);
        effectSpritePipeline = nullptr;
    }
    if (effectVertShader) {
        SDL_ReleaseGPUShader(Renderer::GetDevice(), effectVertShader);
        effectVertShader = nullptr;
    }
}

void SpriteRenderPass::applyEffects(SDL_GPUCommandBuffer* cmd_buffer, const std::vector<EffectAsset>& effects,
                                   SDL_GPUTexture* sourceTexture, SDL_GPUTexture* targetTexture, const glm::mat4& camera,
                                   SDL_GPUTextureFormat targetFormat, bool isFirstBatch) {


    if (effects.empty()) {
        return;
    }
    
    // Create fullscreen quad vertex data (position + texcoord)
    struct Vertex {
        float x, y;    // Position (0-1 range, shader converts to NDC)
        float u, v;    // Texcoord
    };
    
    // Calculate UV scale - temp textures are desktop-sized but only window portion is rendered
    float uvScaleX = (float)Window::GetWidth() / (float)m_surface_width;
    float uvScaleY = (float)Window::GetHeight() / (float)m_surface_height;
    
    // Note: V coordinate flipped (0 at bottom, uvScaleY at top) to account for texture orientation
    Vertex quadVertices[] = {
        {0.0f, 0.0f, 0.0f, uvScaleY},          // Top-left (V flipped)
        {1.0f, 0.0f, uvScaleX, uvScaleY},      // Top-right (V flipped)
        {0.0f, 1.0f, 0.0f, 0.0f},              // Bottom-left (V flipped)
        {1.0f, 1.0f, uvScaleX, 0.0f},          // Bottom-right (V flipped)
    };
    
    uint16_t quadIndices[] = {0, 1, 2, 2, 1, 3};
    
    // Create temporary buffers for the quad
    SDL_GPUTransferBufferCreateInfo transferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(quadVertices),
        .props = 0
    };
    SDL_GPUTransferBuffer* vertexTransfer = SDL_CreateGPUTransferBuffer(Renderer::GetDevice(), &transferInfo);
    
    transferInfo.size = sizeof(quadIndices);
    SDL_GPUTransferBuffer* indexTransfer = SDL_CreateGPUTransferBuffer(Renderer::GetDevice(), &transferInfo);
    
    SDL_GPUBufferCreateInfo bufferInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(quadVertices),
        .props = 0
    };
    SDL_GPUBuffer* vertexBuffer = SDL_CreateGPUBuffer(Renderer::GetDevice(), &bufferInfo);
    
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    bufferInfo.size = sizeof(quadIndices);
    SDL_GPUBuffer* indexBuffer = SDL_CreateGPUBuffer(Renderer::GetDevice(), &bufferInfo);
    
    // Upload vertex data
    void* vertexData = SDL_MapGPUTransferBuffer(Renderer::GetDevice(), vertexTransfer, false);
    memcpy(vertexData, quadVertices, sizeof(quadVertices));
    SDL_UnmapGPUTransferBuffer(Renderer::GetDevice(), vertexTransfer);
    
    void* indexData = SDL_MapGPUTransferBuffer(Renderer::GetDevice(), indexTransfer, false);
    memcpy(indexData, quadIndices, sizeof(quadIndices));
    SDL_UnmapGPUTransferBuffer(Renderer::GetDevice(), indexTransfer);
    
    // Copy to GPU buffers
    SDL_GPUTransferBufferLocation vertexLocation = {
        .transfer_buffer = vertexTransfer,
        .offset = 0
    };
    SDL_GPUBufferRegion vertexRegion = {
        .buffer = vertexBuffer,
        .offset = 0,
        .size = sizeof(quadVertices)
    };
    
    SDL_GPUTransferBufferLocation indexLocation = {
        .transfer_buffer = indexTransfer,
        .offset = 0
    };
    SDL_GPUBufferRegion indexRegion = {
        .buffer = indexBuffer,
        .offset = 0,
        .size = sizeof(quadIndices)
    };
    
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd_buffer);
    SDL_UploadToGPUBuffer(copyPass, &vertexLocation, &vertexRegion, false);
    SDL_UploadToGPUBuffer(copyPass, &indexLocation, &indexRegion, false);
    SDL_EndGPUCopyPass(copyPass);
    
    // Ping-pong between temp textures for multi-effect chains
    SDL_GPUTexture* readTex = sourceTexture;
    SDL_GPUTexture* writeTex = (effects.size() == 1) ? targetTexture : effectTempB.gpuTexture;
    
    for (size_t i = 0; i < effects.size(); ++i) {
        const auto& effect = effects[i];
        bool isLastEffect = (i == effects.size() - 1);
        
        // On last effect, write to final target instead of temp
        if (isLastEffect) {
            writeTex = targetTexture;
        }
        
        // Use the effect's shaders
        SDL_GPUShader* vertShader = effect.vertShader.shader;
        SDL_GPUShader* fragShader = effect.fragShader.shader;
        
        if (!vertShader || !fragShader) {
            LOG_ERROR("Effect shaders are NULL: vert={}, frag={}", (void*)vertShader, (void*)fragShader);
            continue;
        }
        
        // Begin render pass to write texture
        SDL_GPUColorTargetInfo colorTarget = {
            .texture = writeTex,
            .mip_level = 0,
            .layer_or_depth_plane = 0,
            .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
            .load_op = isLastEffect ? (isFirstBatch ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD) : SDL_GPU_LOADOP_CLEAR,  // Clear intermediate, preserve/clear final
            .store_op = SDL_GPU_STOREOP_STORE,
            .resolve_texture = nullptr,
            .resolve_mip_level = 0,
            .resolve_layer = 0,
            .cycle = false  // Don't cycle - we're explicitly ping-ponging between A/B
        };
        
        SDL_GPURenderPass* effectPass = SDL_BeginGPURenderPass(cmd_buffer, &colorTarget, 1, nullptr);
        
        // CRITICAL: Set viewport for the effect pass!
        SDL_GPUViewport viewport = {
            .x = 0,
            .y = 0,
            .w = (float)Window::GetWidth(),
            .h = (float)Window::GetHeight(),
            .min_depth = 0.0f,
            .max_depth = 1.0f
        };
        SDL_SetGPUViewport(effectPass, &viewport);
        
        // Create pipeline for this effect's shaders
        // TODO: Cache pipelines per effect to avoid recreation every frame
        
        // Use alpha blending when compositing to final target
        SDL_GPUColorTargetBlendState blendState;
        if (isLastEffect) {
            // Final pass - blend with what's already on target
            blendState = {
                .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .color_blend_op = SDL_GPU_BLENDOP_ADD,
                .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                .color_write_mask = 0xF,
                .enable_blend = true,
                .enable_color_write_mask = false,
            };
        } else {
            // Intermediate pass - no blending, direct write (ONE/ZERO)
            blendState = {
                .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                .color_blend_op = SDL_GPU_BLENDOP_ADD,
                .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                .color_write_mask = 0xF,
                .enable_blend = true,
                .enable_color_write_mask = false,
            };
        }
        
        SDL_GPUColorTargetDescription colorTargetDesc = {
            .format = isLastEffect ? targetFormat : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,  // Use target format for final composite
            .blend_state = blendState,
        };
        
        SDL_GPUVertexAttribute vertexAttribs[] = {
            {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0},
            {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 8}
        };
        
        SDL_GPUVertexBufferDescription vertexBufferDesc = {
            .slot = 0,
            .pitch = 16,  // sizeof(Vertex2D)
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0
        };
        
        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
            .vertex_shader = vertShader,
            .fragment_shader = fragShader,
            .vertex_input_state = {
                .vertex_buffer_descriptions = &vertexBufferDesc,
                .num_vertex_buffers = 1,
                .vertex_attributes = vertexAttribs,
                .num_vertex_attributes = 2,
            },
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = GPUstructs::defaultRasterizerState,
            .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1, .sample_mask = 0, .enable_mask = false},
            .depth_stencil_state = {.enable_depth_test = false, .enable_depth_write = false, .enable_stencil_test = false},
            .target_info = {
                .color_target_descriptions = &colorTargetDesc,
                .num_color_targets = 1,
                .has_depth_stencil_target = false,
            },
            .props = 0,
        };
        
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &pipelineInfo);
        if (!pipeline) {
            LOG_ERROR("Failed to create effect pipeline: {}", SDL_GetError());
            SDL_EndGPURenderPass(effectPass);
            continue;
        }

        SDL_BindGPUGraphicsPipeline(effectPass, pipeline);
        
        // Bind textures - source texture plus any additional effect textures
        const auto& additionalTextures = Draw::GetEffectTextures();
        std::vector<SDL_GPUTextureSamplerBinding> textureBindings;
        
        // Always bind source texture at binding 0
        textureBindings.push_back({
            .texture = readTex,
            .sampler = Renderer::GetSampler(ScaleMode::NEAREST)
        });
        
        // Bind additional textures at their specified bindings
        // Note: bindings must be sequential starting from 0
        for (const auto& [binding, texture] : additionalTextures) {
            // Resize vector if needed to accommodate the binding index
            while (textureBindings.size() <= binding) {
                // Fill gaps with the first texture as a placeholder
                textureBindings.push_back(textureBindings[0]);
            }
            
            // Set the texture at the specified binding
            textureBindings[binding] = {
                .texture = texture,
                .sampler = Renderer::GetSampler(ScaleMode::NEAREST)
            };
        }
        
        SDL_BindGPUFragmentSamplers(effectPass, 0, textureBindings.data(), (uint32_t)textureBindings.size());
        
        // Always bind effect's uniform buffer - shader expects it even if empty
        // Push dummy data if no uniforms exist
        if (effect.uniforms && effect.uniforms->getBufferSize() > 0) {
            SDL_PushGPUFragmentUniformData(cmd_buffer, 0,
                effect.uniforms->getBufferPointer(), 
                effect.uniforms->getBufferSize());
        } else {
            // Push empty/dummy uniform data to satisfy shader requirements
            float dummyData[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            SDL_PushGPUFragmentUniformData(cmd_buffer, 0, &dummyData, sizeof(dummyData));
        }
        
        // Bind quad geometry
        SDL_GPUBufferBinding vertexBinding = {.buffer = vertexBuffer, .offset = 0};
        SDL_BindGPUVertexBuffers(effectPass, 0, &vertexBinding, 1);
        
        SDL_GPUBufferBinding indexBinding = {.buffer = indexBuffer, .offset = 0};
        SDL_BindGPUIndexBuffer(effectPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

        // Draw fullscreen quad
        SDL_DrawGPUIndexedPrimitives(effectPass, 6, 1, 0, 0, 0);
        
        SDL_EndGPURenderPass(effectPass);
        
        // Clean up pipeline (TODO: cache these)
        SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), pipeline);

        // Ping-pong: read from where we just wrote, write to the other temp
        if (!isLastEffect) {
            readTex = writeTex;
            writeTex = (readTex == effectTempA.gpuTexture) ? effectTempB.gpuTexture : effectTempA.gpuTexture;
        }
    }
    
    // Clean up temporary buffers
    SDL_ReleaseGPUBuffer(Renderer::GetDevice(), vertexBuffer);
    SDL_ReleaseGPUBuffer(Renderer::GetDevice(), indexBuffer);
    SDL_ReleaseGPUTransferBuffer(Renderer::GetDevice(), vertexTransfer);
    SDL_ReleaseGPUTransferBuffer(Renderer::GetDevice(), indexTransfer);

}

void SpriteRenderPass::createShaders() {
    // Select shader format based on build configuration
    SDL_GPUShaderFormat shaderFormat;
    #if defined(LUMINOVEAU_SHADER_BACKEND_DXIL)
        shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
    #elif defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        shaderFormat = SDL_GPU_SHADERFORMAT_METALLIB;
    #else
        shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;  // Default: Vulkan
    #endif

    // spirv-cross renames "main" to "main0" in MSL (reserved keyword)
    #if defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        const char* entryPoint = "main0";
    #else
        const char* entryPoint = "main";
    #endif

    SDL_GPUShaderCreateInfo vertexShaderInfo = {
        .code_size = Luminoveau::Shaders::Sprite_Vert_Size,
        .code = Luminoveau::Shaders::Sprite_Vert,
        .entrypoint = entryPoint,
        .format = shaderFormat,  // Use selected format
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = 1,
        .num_uniform_buffers = 2,  // Now we have 2: ViewProjection and InstanceOffset
    };

    vertex_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &vertexShaderInfo);

    if (!vertex_shader) {
        LOG_CRITICAL("failed to create vertex shader for: {} ({})", passname.c_str(), SDL_GetError());
    }

    SDL_GPUShaderCreateInfo fragmentShaderInfo = {
        .code_size = Luminoveau::Shaders::Sprite_Frag_Size,
        .code = Luminoveau::Shaders::Sprite_Frag,
        .entrypoint = entryPoint,
        .format = shaderFormat,  // Use selected format
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 0,
    };

    fragment_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &fragmentShaderInfo);

    if (!fragment_shader) {
        LOG_CRITICAL("failed to create fragment shader for: {} ({})", passname.c_str(), SDL_GetError());
    }
}
