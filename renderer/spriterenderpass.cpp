#include "spriterenderpass.h"

#include <utility>

#include "SDL3/SDL_gpu.h"
#include "window/windowhandler.h"
#include "assethandler/shaders_generated.h"
#include "log/loghandler.h"

#include "utils/constants.h"

void SpriteRenderPass::release(bool logRelease) {
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
                
                // Pack each pair of floats into a single uint32
                dataPtr[i].pos_xy = pack_half2(x, y);
                dataPtr[i].pos_z_rot = pack_half2(z, rotation);
                dataPtr[i].tex_uv = pack_half2(tex_u, tex_v);
                dataPtr[i].tex_wh = pack_half2(tex_w, tex_h);
                dataPtr[i].color_rg = pack_half2(r, g);
                dataPtr[i].color_ba = pack_half2(b, a);
                dataPtr[i].size_wh = pack_half2(w, h);
                dataPtr[i].pivot_xy = pack_half2(pivot_x, pivot_y);
            }
        });
    }
    thread_pool.wait_all();

    // Build batches respecting z-order, geometry, and texture changes
    std::vector<Batch> batches;
    batches.reserve(64);

    size_t      currentOffset = 0;
    for (size_t i             = 0; i < renderQueueCount; ++i) {
        bool geometryChanged = (i > 0 && renderQueue[i].geometry != renderQueue[i - 1].geometry);
        bool textureChanged = (i > 0 && renderQueue[i].texture.gpuTexture != renderQueue[i - 1].texture.gpuTexture);
        
        if (i == 0 || geometryChanged || textureChanged) {
            // Start a new batch when geometry or texture changes
            Batch batch;
            batch.offset  = currentOffset;
            batch.count   = 1;
            batch.geometry = renderQueue[i].geometry;
            batch.texture = renderQueue[i].texture.gpuTexture;
            batch.sampler = renderQueue[i].texture.gpuSampler;
            batches.push_back(batch);
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

    // Render directly to target texture
    SDL_GPUColorTargetInfo color_target_info{
        .texture              = target_texture,
        .mip_level            = 0,
        .layer_or_depth_plane = 0,
        .clear_color          = color_target_info_clear_color,
        .load_op              = color_target_info_loadop,
        .store_op             = shouldResolve ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE,
        .resolve_texture      = renderTargetResolve,
        .resolve_mip_level    = 0,
        .resolve_layer        = 0,
        .cycle                = false};

    SDL_GPUDepthStencilTargetInfo depth_stencil_info{
        .texture          = renderTargetDepth ? renderTargetDepth : m_depth_texture.gpuTexture,
        .clear_depth      = 1.0f,
        .load_op          = SDL_GPU_LOADOP_CLEAR,  // CLEAR since we're the first pass!
        .store_op         = SDL_GPU_STOREOP_DONT_CARE,
        .stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
    };

    render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target_info, 1, nullptr);  // No depth!


    assert(render_pass);
    {
        // Set viewport to window size (render to top-left portion of desktop-sized buffer)
        SDL_GPUViewport viewport = {
            .x = 0,
            .y = 0,
            .w = (float)Window::GetWidth(),
            .h = (float)Window::GetHeight(),
            .min_depth = 0.0f,
            .max_depth = 1.0f
        };
        SDL_SetGPUViewport(render_pass, &viewport);

        if (_scissorEnabled) {
            SDL_SetGPUScissor(render_pass, _scissorRect);
            _scissorEnabled = false;
        }


        SDL_BindGPUGraphicsPipeline(render_pass, m_pipeline);
        
        // Bind sprite instance data as storage buffer
        SDL_BindGPUVertexStorageBuffers(
            render_pass,
            0,
            &SpriteDataBuffer,
            1
        );

        for (const auto &batch: batches) {
            if (batch.texture == nullptr || batch.sampler == nullptr || batch.geometry == nullptr) continue;
            
            // Bind vertex buffer for this geometry
            SDL_GPUBufferBinding vertexBinding = {
                .buffer = batch.geometry->vertexBuffer,
                .offset = 0
            };
            SDL_BindGPUVertexBuffers(render_pass, 0, &vertexBinding, 1);
            
            // Bind index buffer for this geometry
            SDL_GPUBufferBinding indexBinding = {
                .buffer = batch.geometry->indexBuffer,
                .offset = 0
            };
            SDL_BindGPUIndexBuffer(render_pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
            
            SDL_GPUTextureSamplerBinding samplerBinding = {
                .texture = batch.texture,
                .sampler = batch.sampler
            };
            SDL_BindGPUFragmentSamplers(render_pass, 0, &samplerBinding, 1);
            SDL_PushGPUVertexUniformData(cmd_buffer, 0, &camera, sizeof(glm::mat4));
            
            // Push batch offset for DirectX 12 compatibility (SV_InstanceID doesn't include first_instance)
            uint32_t batchOffset = static_cast<uint32_t>(batch.offset);
            SDL_PushGPUVertexUniformData(cmd_buffer, 1, &batchOffset, sizeof(uint32_t));
            
            // Draw using instancing with geometry-specific index count
            SDL_DrawGPUIndexedPrimitives(
                render_pass,
                static_cast<uint32_t>(batch.geometry->GetIndexCount()),  // Use geometry's index count
                batch.count,    // num_instances - one instance per sprite
                0,              // first_index
                0,              // vertex_offset
                0               // first_instance - always 0, we use baseInstance uniform instead
            );
        }
    }

    SDL_EndGPURenderPass(render_pass);
#ifdef LUMIDEBUG
    SDL_PopGPUDebugGroup(cmd_buffer);
#endif
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

    SDL_GPUShaderCreateInfo vertexShaderInfo = {
        .code_size = Luminoveau::Shaders::Sprite_Vert_Size,
        .code = Luminoveau::Shaders::Sprite_Vert,
        .entrypoint = "main",
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
        .entrypoint = "main",
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
