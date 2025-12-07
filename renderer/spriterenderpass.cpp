#include "spriterenderpass.h"

#include <utility>

#include "SDL3/SDL_gpu.h"
#include "window/windowhandler.h"

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
        SDL_Log("%s: released graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());
    }
}

bool SpriteRenderPass::init(
    SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name, bool logInit
) {
    passname = std::move(name);

    // Create MSAA color target
    SDL_GPUTextureCreateInfo msaaColorTargetInfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = swapchain_texture_format,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = surface_width,
        .height = surface_height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = Renderer::GetSampleCount(),
    };

    m_msaa_color_texture = SDL_CreateGPUTexture(Renderer::GetDevice(), &msaaColorTargetInfo);

    // Create MSAA depth target
    SDL_GPUTextureCreateInfo msaaDepthTargetInfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = surface_width,
        .height = surface_height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = Renderer::GetSampleCount(), // MSAA 4x
    };

    m_msaa_depth_texture = SDL_CreateGPUTexture(Renderer::GetDevice(), &msaaDepthTargetInfo);



    m_depth_texture = AssetHandler::CreateDepthTarget(Renderer::GetDevice(), surface_width, surface_height);

    renderQueue.resize(MAX_SPRITES);

    createShaders();

    SDL_GPUColorTargetDescription color_target_description{
        .format = swapchain_texture_format,
        .blend_state = renderPassBlendState,
    };

    SDL_GPUGraphicsPipelineCreateInfo pipeline_create_info{
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state =
            {
                .vertex_buffer_descriptions = nullptr,
                .num_vertex_buffers = 0,
                .vertex_attributes = nullptr,
                .num_vertex_attributes = 0,
            },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = GPUstructs::defaultRasterizerState,
        .multisample_state = {
            .sample_count = Renderer::GetSampleCount(), // or SDL_GPU_SAMPLECOUNT_8
            .sample_mask = 0xFFFFFFFF,
            .enable_mask = true,
        },
        .depth_stencil_state =
            {
                .compare_op = SDL_GPU_COMPAREOP_LESS,
                .back_stencil_state = {},
                .front_stencil_state = {},
                .compare_mask = 0,
                .write_mask = 0,
                .enable_depth_test = true,
                .enable_depth_write = false,
                .enable_stencil_test = false,
            },
        .target_info =
            {
                .color_target_descriptions = &color_target_description,
                .num_color_targets = 1,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                .has_depth_stencil_target = true,
            },
        .props = 0,
    };
    m_pipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &pipeline_create_info);

    SDL_GPUTransferBufferCreateInfo transferBufferCreateInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = MAX_SPRITES * sizeof(SpriteInstance)
    };

    SpriteDataTransferBuffer = SDL_CreateGPUTransferBuffer(
        m_gpu_device,
        &transferBufferCreateInfo
    );

    SDL_GPUBufferCreateInfo bufferCreateInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = MAX_SPRITES * sizeof(SpriteInstance)
    };

    SpriteDataBuffer = SDL_CreateGPUBuffer(
        m_gpu_device,
        &bufferCreateInfo
    );

    if (!m_pipeline) {
        throw std::runtime_error(Helpers::TextFormat("%s: failed to create graphics pipeline: %s", CURRENT_METHOD(), SDL_GetError()));
    }

    if (logInit) {
        SDL_Log("%s: created graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());
    }

    return true;
}

void SpriteRenderPass::render(
    SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
) {
    #ifdef LUMIDEBUG
    SDL_PushGPUDebugGroup(cmd_buffer, CURRENT_METHOD());
    #endif

    //sets up transfer
    auto *dataPtr = static_cast<SpriteInstance *>(SDL_MapGPUTransferBuffer(
        m_gpu_device,
        SpriteDataTransferBuffer,
        false
    ));

    if (sprite_data.capacity() < renderQueueCount) sprite_data.reserve(renderQueueCount);
    if (sprite_data.size() < renderQueueCount) sprite_data.resize(renderQueueCount); // Still needed for indexing

    size_t      thread_count = thread_pool.get_thread_count();
    size_t      chunk_size   = renderQueueCount / thread_count + 1;
    for (size_t start        = 0; start < renderQueueCount; start += chunk_size) {
        size_t end = std::min(start + chunk_size, renderQueueCount);
        thread_pool.enqueue([this, start, end]() {
            constexpr size_t sprite_size = sizeof(SpriteInstance);
            for (size_t      i           = start; i < end; ++i) {
                std::memcpy(&sprite_data[i], &renderQueue[i].x, sprite_size);
            }
        });
    }
    thread_pool.wait_all();

    // Build batches respecting z-order and texture changes
    std::vector<Batch> batches;
    batches.reserve(64);

    size_t      currentOffset = 0;
    for (size_t i             = 0; i < renderQueueCount; ++i) {
        if (i == 0 || renderQueue[i].texture.gpuTexture != renderQueue[i - 1].texture.gpuTexture) {
            // Start a new batch when texture changes
            Batch batch;
            batch.offset  = currentOffset;
            batch.count   = 1;
            batch.texture = renderQueue[i].texture.gpuTexture;
            batch.sampler = renderQueue[i].texture.gpuSampler;
            batches.push_back(batch);
        } else {
            // Continue the current batch
            batches.back().count++;
        }
        currentOffset++;
    }
    // Transfer to GPU
    std::memcpy(dataPtr, sprite_data.data(), renderQueueCount * sizeof(SpriteInstance));
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
            .size = static_cast<Uint32>(renderQueueCount * sizeof(SpriteInstance))
        };

        SDL_UploadToGPUBuffer(
            copyPass,
            &transferBufferLocation,
            &bufferRegion,
            false
        );
        SDL_EndGPUCopyPass(copyPass);
    }

    // Render pass
    SDL_GPUColorTargetInfo color_target_info{
        .texture = m_msaa_color_texture, // Render to MSAA texture
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = color_target_info_clear_color,
        .load_op = color_target_info_loadop,
        .store_op = SDL_GPU_STOREOP_RESOLVE, // Resolve MSAA
        .resolve_texture = target_texture,    // Resolve to this texture
        .resolve_mip_level = 0,
        .resolve_layer = 0,
        .cycle = false
    };

    SDL_GPUDepthStencilTargetInfo depth_stencil_info{
        .texture = m_msaa_depth_texture, // Use MSAA depth
        .clear_depth = 1.0f,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_DONT_CARE, // Don't need to keep MSAA depth
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
    };

    render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target_info, 1, &depth_stencil_info);
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
        SDL_BindGPUVertexStorageBuffers(
            render_pass,
            0,
            &SpriteDataBuffer,
            1
        );

        for (const auto &batch: batches) {
            SDL_GPUTextureSamplerBinding samplerBinding = {
                .texture = batch.texture,
                .sampler = batch.sampler
            };

            if (batch.texture == nullptr || batch.sampler == nullptr) continue;

            SDL_BindGPUFragmentSamplers(render_pass, 0, &samplerBinding, 1);
            SDL_PushGPUVertexUniformData(cmd_buffer, 0, &camera, sizeof(glm::mat4));
            SDL_DrawGPUPrimitives(render_pass, batch.count * 6, 1, batch.offset * 6, 0);
        }
    }

    SDL_EndGPURenderPass(render_pass);
#ifdef LUMIDEBUG
    SDL_PopGPUDebugGroup(cmd_buffer);
#endif
}

void SpriteRenderPass::createShaders() {
    SDL_GPUShaderCreateInfo vertexShaderInfo = {
        .code_size = sprite_batch_vert_bin_len,
        .code = sprite_batch_vert_bin,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = 1,
        .num_uniform_buffers = 1,
    };

    vertex_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &vertexShaderInfo);

    if (!vertex_shader) {
        throw std::runtime_error(
            Helpers::TextFormat("%s: failed to create vertex shader for: %s (%s)", CURRENT_METHOD(), passname.c_str(), SDL_GetError()));
    }

    SDL_GPUShaderCreateInfo fragmentShaderInfo = {
        .code_size = sprite_batch_frag_bin_len,
        .code = sprite_batch_frag_bin,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 0,
    };

    fragment_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &fragmentShaderInfo);

    if (!fragment_shader) {
        throw std::runtime_error(
            Helpers::TextFormat("%s: failed to create fragment shader for: %s (%s)", CURRENT_METHOD(), passname.c_str(), SDL_GetError()));
    }
}
