#include "model3drenderpass.h"
#include "renderer/sdl_gpu_structs.h"
#include "window/windowhandler.h"
#include "assethandler/shaders_generated.h"
#include <algorithm>

bool Model3DRenderPass::init(
    SDL_GPUTextureFormat swapchain_texture_format,
    uint32_t width,
    uint32_t height,
    std::string name,
    bool logInit
) {
    passname = name;
    surface_width = width;
    surface_height = height;
    
    SDL_GPUSampleCount sampleCount = Renderer::GetSampleCount();
    current_sample_count = sampleCount;  // Store current MSAA setting
    
    // Don't create MSAA textures - use shared framebuffer MSAA texture
    
    // Create regular depth texture
    SDL_GPUTextureCreateInfo depth_create_info{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };
    
    m_depth_texture.gpuTexture = SDL_CreateGPUTexture(m_gpu_device, &depth_create_info);
    if (!m_depth_texture.gpuTexture) {
        SDL_Log("%s: Failed to create depth texture: %s", passname.c_str(), SDL_GetError());
        return false;
    }
    
    // Create uniform buffer
    SDL_GPUBufferCreateInfo uniform_buffer_info{
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = sizeof(SceneUniforms),
    };
    
    uniformBuffer = SDL_CreateGPUBuffer(m_gpu_device, &uniform_buffer_info);
    if (!uniformBuffer) {
        SDL_Log("%s: Failed to create uniform buffer: %s", passname.c_str(), SDL_GetError());
        return false;
    }
    
    SDL_GPUTransferBufferCreateInfo transfer_buffer_info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(SceneUniforms),
    };
    
    uniformTransferBuffer = SDL_CreateGPUTransferBuffer(m_gpu_device, &transfer_buffer_info);
    if (!uniformTransferBuffer) {
        SDL_Log("%s: Failed to create uniform transfer buffer: %s", passname.c_str(), SDL_GetError());
        return false;
    }
    
    createShaders();
    createPipeline(swapchain_texture_format);
    
    if (logInit) {
        SDL_Log("%s: Initialized 3D model render pass with MSAA=%d", CURRENT_METHOD(), sampleCount);
    }
    
    return true;
}

void Model3DRenderPass::release(bool logRelease) {
    if (msaa_color_texture) {
        SDL_ReleaseGPUTexture(m_gpu_device, msaa_color_texture);
        msaa_color_texture = nullptr;
    }
    
    if (msaa_depth_texture) {
        SDL_ReleaseGPUTexture(m_gpu_device, msaa_depth_texture);
        msaa_depth_texture = nullptr;
    }
    
    if (m_depth_texture.gpuTexture) {
        SDL_ReleaseGPUTexture(m_gpu_device, m_depth_texture.gpuTexture);
        m_depth_texture.gpuTexture = nullptr;
    }
    
    if (uniformBuffer) {
        SDL_ReleaseGPUBuffer(m_gpu_device, uniformBuffer);
        uniformBuffer = nullptr;
    }
    
    if (uniformTransferBuffer) {
        SDL_ReleaseGPUTransferBuffer(m_gpu_device, uniformTransferBuffer);
        uniformTransferBuffer = nullptr;
    }
    
    if (m_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_gpu_device, m_pipeline);
        m_pipeline = nullptr;
    }
    
    if (vertex_shader) {
        SDL_ReleaseGPUShader(m_gpu_device, vertex_shader);
        vertex_shader = nullptr;
    }
    
    if (fragment_shader) {
        SDL_ReleaseGPUShader(m_gpu_device, fragment_shader);
        fragment_shader = nullptr;
    }
    
    if (logRelease) {
        SDL_Log("%s: Released 3D model render pass", passname.c_str());
    }
}

void Model3DRenderPass::createShaders() {

    SDL_Log("%s: Creating shaders - vert size: %zu, frag size: %zu",
            CURRENT_METHOD(), Luminoveau::Shaders::Model3d_Vert_Size, Luminoveau::Shaders::Model3d_Frag_Size);

    // Select shader format based on build configuration
    SDL_GPUShaderFormat shaderFormat;
    #if defined(LUMINOVEAU_SHADER_BACKEND_DXIL)
        shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
    #elif defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        shaderFormat = SDL_GPU_SHADERFORMAT_METALLIB;
    #else
        shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;  // Default: Vulkan
    #endif

    // Create vertex shader
    SDL_GPUShaderCreateInfo vertexShaderInfo = {
        .code_size = Luminoveau::Shaders::Model3d_Vert_Size,
        .code = Luminoveau::Shaders::Model3d_Vert,
        .entrypoint = "main",
        .format = shaderFormat,  // Use selected format
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = 1,  // SceneUniforms at set 0
        .num_uniform_buffers = 0,  // Not using push constants
    };
    
    SDL_Log("%s: Vertex shader info - storage_buffers: %u, uniform_buffers: %u",
            CURRENT_METHOD(), vertexShaderInfo.num_storage_buffers, vertexShaderInfo.num_uniform_buffers);
    
    vertex_shader = SDL_CreateGPUShader(m_gpu_device, &vertexShaderInfo);
    if (!vertex_shader) {
        SDL_Log("%s: Failed to create vertex shader: %s", passname.c_str(), SDL_GetError());
        return;
    }
    
    // Create fragment shader (now with texture sampling)
    SDL_GPUShaderCreateInfo fragmentShaderInfo = {
        .code_size = Luminoveau::Shaders::Model3d_Frag_Size,
        .code = Luminoveau::Shaders::Model3d_Frag,
        .entrypoint = "main",
        .format = shaderFormat,  // Use selected format
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,  // Sampler at binding 0
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 0,
    };
    
    fragment_shader = SDL_CreateGPUShader(m_gpu_device, &fragmentShaderInfo);
    if (!fragment_shader) {
        SDL_Log("%s: Failed to create fragment shader: %s", CURRENT_METHOD(), SDL_GetError());
        SDL_ReleaseGPUShader(m_gpu_device, vertex_shader);
        vertex_shader = nullptr;
        return;
    }
    
    SDL_Log("%s: Shaders created successfully - vertex=%p, fragment=%p", 
            CURRENT_METHOD(), (void*)vertex_shader, (void*)fragment_shader);
    
}

void Model3DRenderPass::createPipeline(SDL_GPUTextureFormat swapchain_format) {

    SDL_GPUSampleCount currentSampleCount = Renderer::GetSampleCount();
    SDL_Log("%s: Called with sampleCount=%d", CURRENT_METHOD(), currentSampleCount);

    if (!vertex_shader || !fragment_shader) {
        SDL_Log("%s: Cannot create pipeline - shaders not loaded", CURRENT_METHOD());
        return;
    }
    
    // Define vertex input layout matching Vertex3D
    static SDL_GPUVertexAttribute vertexAttributes[] = {
        {
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,  // position
            .offset = 0
        },
        {
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,  // normal
            .offset = sizeof(float) * 3
        },
        {
            .location = 2,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,  // texcoord
            .offset = sizeof(float) * 6
        },
        {
            .location = 3,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,  // color
            .offset = sizeof(float) * 8
        }
    };
    
    static SDL_GPUVertexBufferDescription vertexBufferDesc = {
        .slot = 0,
        .pitch = sizeof(Vertex3D),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0
    };
    
    SDL_GPUVertexInputState vertexInputState = {
        .vertex_buffer_descriptions = &vertexBufferDesc,
        .num_vertex_buffers = 1,
        .vertex_attributes = vertexAttributes,
        .num_vertex_attributes = 4
    };
    
    SDL_GPUColorTargetDescription colorTarget = {
        .format = swapchain_format,
        .blend_state = GPUstructs::defaultBlendState
    };
    
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state = vertexInputState,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_NONE,  // Temporarily disable to debug
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .depth_bias_constant_factor = 0.0f,
            .depth_bias_clamp = 0.0f,
            .depth_bias_slope_factor = 0.0f,
            .enable_depth_bias = false,
            .enable_depth_clip = true,
        },
        .multisample_state = {
            .sample_count = Renderer::GetSampleCount(),
            .sample_mask = 0,
            .enable_mask = false,
        },
        .depth_stencil_state = {
            .compare_op = SDL_GPU_COMPAREOP_LESS,
            .back_stencil_state = {},
            .front_stencil_state = {},
            .compare_mask = 0,
            .write_mask = 0,
            .enable_depth_test = true,
            .enable_depth_write = true,
            .enable_stencil_test = false,
        },
        .target_info = {
            .color_target_descriptions = &colorTarget,
            .num_color_targets = 1,
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            .has_depth_stencil_target = true,
        },
        .props = 0,
    };
    
    SDL_Log("%s: Creating pipeline with sample_count=%d", CURRENT_METHOD(), pipelineInfo.multisample_state.sample_count);
    
    m_pipeline = SDL_CreateGPUGraphicsPipeline(m_gpu_device, &pipelineInfo);
    if (!m_pipeline) {
        SDL_Log("%s: Failed to create graphics pipeline: %s", CURRENT_METHOD(), SDL_GetError());
        return;
    }
    
    SDL_Log("%s: Graphics pipeline created successfully", CURRENT_METHOD());
}

void Model3DRenderPass::uploadModelToGPU(ModelAsset* model) {
    if (!model || model->vertices.empty() || model->indices.empty()) {
        return;
    }
    
    // Skip if already uploaded
    if (model->vertexBuffer && model->indexBuffer) {
        return;
    }
    
    // Create vertex buffer
    SDL_GPUBufferCreateInfo vertex_buffer_info{
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = static_cast<uint32_t>(model->vertices.size() * sizeof(Vertex3D)),
    };
    
    model->vertexBuffer = SDL_CreateGPUBuffer(m_gpu_device, &vertex_buffer_info);
    if (!model->vertexBuffer) {
        SDL_Log("%s: Failed to create vertex buffer: %s", CURRENT_METHOD(), SDL_GetError());
        return;
    }
    
    // Create index buffer
    SDL_GPUBufferCreateInfo index_buffer_info{
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = static_cast<uint32_t>(model->indices.size() * sizeof(uint32_t)),
    };
    
    model->indexBuffer = SDL_CreateGPUBuffer(m_gpu_device, &index_buffer_info);
    if (!model->indexBuffer) {
        SDL_Log("%s: Failed to create index buffer: %s", CURRENT_METHOD(), SDL_GetError());
        return;
    }
    
    // Create transfer buffers
    SDL_GPUTransferBufferCreateInfo vertex_transfer_info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<uint32_t>(model->vertices.size() * sizeof(Vertex3D)),
    };
    
    model->vertexTransferBuffer = SDL_CreateGPUTransferBuffer(m_gpu_device, &vertex_transfer_info);
    if (!model->vertexTransferBuffer) {
        SDL_Log("%s: Failed to create vertex transfer buffer: %s", CURRENT_METHOD(), SDL_GetError());
        return;
    }
    
    SDL_GPUTransferBufferCreateInfo index_transfer_info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<uint32_t>(model->indices.size() * sizeof(uint32_t)),
    };
    
    model->indexTransferBuffer = SDL_CreateGPUTransferBuffer(m_gpu_device, &index_transfer_info);
    if (!model->indexTransferBuffer) {
        SDL_Log("%s: Failed to create index transfer buffer: %s", CURRENT_METHOD(), SDL_GetError());
        return;
    }
    
    // Upload vertex data
    void* vertex_data = SDL_MapGPUTransferBuffer(m_gpu_device, model->vertexTransferBuffer, false);
    SDL_memcpy(vertex_data, model->vertices.data(), model->vertices.size() * sizeof(Vertex3D));
    SDL_UnmapGPUTransferBuffer(m_gpu_device, model->vertexTransferBuffer);
    
    // Upload index data
    void* index_data = SDL_MapGPUTransferBuffer(m_gpu_device, model->indexTransferBuffer, false);
    SDL_memcpy(index_data, model->indices.data(), model->indices.size() * sizeof(uint32_t));
    SDL_UnmapGPUTransferBuffer(m_gpu_device, model->indexTransferBuffer);
    
    // Copy to GPU buffers
    SDL_GPUCommandBuffer* upload_cmd = SDL_AcquireGPUCommandBuffer(m_gpu_device);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(upload_cmd);
    
    SDL_GPUTransferBufferLocation vertex_src{
        .transfer_buffer = model->vertexTransferBuffer,
        .offset = 0,
    };
    
    SDL_GPUBufferRegion vertex_dst{
        .buffer = model->vertexBuffer,
        .offset = 0,
        .size = static_cast<uint32_t>(model->vertices.size() * sizeof(Vertex3D)),
    };
    
    SDL_UploadToGPUBuffer(copy_pass, &vertex_src, &vertex_dst, false);
    
    SDL_GPUTransferBufferLocation index_src{
        .transfer_buffer = model->indexTransferBuffer,
        .offset = 0,
    };
    
    SDL_GPUBufferRegion index_dst{
        .buffer = model->indexBuffer,
        .offset = 0,
        .size = static_cast<uint32_t>(model->indices.size() * sizeof(uint32_t)),
    };
    
    SDL_UploadToGPUBuffer(copy_pass, &index_src, &index_dst, false);
    
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(upload_cmd);
    
    // CRITICAL: Wait for upload to complete before using the buffers
    SDL_WaitForGPUIdle(m_gpu_device);
}

void Model3DRenderPass::render(
    SDL_GPUCommandBuffer* cmd_buffer,
    SDL_GPUTexture* target_texture,
    const glm::mat4& camera_unused
) {
    //SDL_PushGPUDebugGroup(cmd_buffer, CURRENT_METHOD());
    
    // Get current scene data
    Camera3D& camera = Scene::GetCamera();
    std::vector<ModelInstance>& models = Scene::GetModels();
    std::vector<Light>& lights = Scene::GetLights();
    Color ambientColor = Scene::GetAmbientLight();
    
    // Prepare scene uniforms structure (model matrix updated per draw)
    SceneUniforms sceneUniforms{};

    // Upload scene uniforms and models BEFORE render pass
    if (!models.empty() && m_pipeline) {
        // Calculate aspect ratio using WINDOW size, not desktop size
        float aspectRatio = static_cast<float>(Window::GetWidth()) / static_cast<float>(Window::GetHeight());
        
        // Fill in scene uniforms with ALL model matrices
        sceneUniforms.viewProj = camera.GetViewProjectionMatrix(aspectRatio);
        sceneUniforms.modelCount = std::min(static_cast<int>(models.size()), 16);
        
        // Upload all model matrices
        for (int i = 0; i < sceneUniforms.modelCount; i++) {
            sceneUniforms.models[i] = models[i].GetModelMatrix();
        }
        sceneUniforms.cameraPos = glm::vec4(camera.position.x, camera.position.y, camera.position.z, 1.0f);
        sceneUniforms.ambientLight = glm::vec4(
            ambientColor.r / 255.0f,
            ambientColor.g / 255.0f,
            ambientColor.b / 255.0f,
            ambientColor.a / 255.0f
        );
        
        // Pack light data (max 4 lights)
        sceneUniforms.lightCount = std::min(static_cast<int>(lights.size()), 4);

        for (int i = 0; i < sceneUniforms.lightCount; i++) {
            const Light& light = lights[i];
            
            // For directional lights, store direction; for others, store position
            if (light.type == LightType::Directional) {
                sceneUniforms.lightPositions[i] = glm::vec4(
                    light.direction.x, light.direction.y, light.direction.z, static_cast<float>(light.type)
                );
            } else {
                sceneUniforms.lightPositions[i] = glm::vec4(
                    light.position.x, light.position.y, light.position.z, static_cast<float>(light.type)
                );
            }
            
            sceneUniforms.lightColors[i] = glm::vec4(
                light.color.r / 255.0f, light.color.g / 255.0f, light.color.b / 255.0f, light.intensity
            );
            sceneUniforms.lightParams[i] = glm::vec4(
                light.constant, light.linear, light.quadratic, 0.0f
            );
        }
        
        // Upload scene uniforms to storage buffer (ONCE before render pass)
        void* uniformData = SDL_MapGPUTransferBuffer(m_gpu_device, uniformTransferBuffer, false);
        SDL_memcpy(uniformData, &sceneUniforms, sizeof(SceneUniforms));
        SDL_UnmapGPUTransferBuffer(m_gpu_device, uniformTransferBuffer);
        
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd_buffer);
        SDL_GPUTransferBufferLocation src = {
            .transfer_buffer = uniformTransferBuffer,
            .offset = 0,
        };
        SDL_GPUBufferRegion dst = {
            .buffer = uniformBuffer,
            .offset = 0,
            .size = sizeof(SceneUniforms),
        };
        SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
        SDL_EndGPUCopyPass(copyPass);
        
        // Upload models to GPU if needed
        for (auto& instance : models) {
            if (instance.model) {
                uploadModelToGPU(instance.model);
            }
        }
    }
    
    // Check if we should resolve (if renderTargetResolve is set)
    bool shouldResolve = (renderTargetResolve != nullptr);
    
    // Render directly to target texture (no separate MSAA texture)
    SDL_GPUColorTargetInfo colorTarget = {
        .texture = target_texture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = color_target_info_clear_color,
        .load_op = color_target_info_loadop,
        .store_op = shouldResolve ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE,
        .resolve_texture = renderTargetResolve,
        .resolve_mip_level = 0,
        .resolve_layer = 0,
        .cycle = false,
        .cycle_resolve_texture = false,
    };
    
    SDL_GPUDepthStencilTargetInfo depthTarget = {
        .texture = renderTargetDepth ? renderTargetDepth : m_depth_texture.gpuTexture,  // Use shared MSAA depth if provided
        .clear_depth = 1.0f,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,  // MUST STORE for multi-model rendering!
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
        .cycle = false,
        .clear_stencil = 0,
    };
    
    render_pass = SDL_BeginGPURenderPass(cmd_buffer, &colorTarget, 1, &depthTarget);
    
    // Set viewport to window size (not desktop size)
    SDL_GPUViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .w = static_cast<float>(Window::GetWidth()),
        .h = static_cast<float>(Window::GetHeight()),
        .min_depth = 0.0f,
        .max_depth = 1.0f
    };
    SDL_SetGPUViewport(render_pass, &viewport);
    
    // If no models or no pipeline, just end (clear still happens via LOADOP_CLEAR)
    if (models.empty() || !m_pipeline) {
        SDL_EndGPURenderPass(render_pass);
        SDL_PopGPUDebugGroup(cmd_buffer);
        return;
    }
    
    // Bind pipeline and storage buffers once
    SDL_BindGPUGraphicsPipeline(render_pass, m_pipeline);
    SDL_BindGPUVertexStorageBuffers(render_pass, 0, &uniformBuffer, 1);
    
    // For now, assume all models use the same mesh (cube)
    // TODO: Group models by mesh and do separate instanced draws per mesh
    if (!models.empty() && models[0].model && models[0].model->vertexBuffer && models[0].model->indexBuffer) {
        // Bind vertex buffer
        SDL_GPUBufferBinding vertexBinding = {
            .buffer = models[0].model->vertexBuffer,
            .offset = 0,
        };
        SDL_BindGPUVertexBuffers(render_pass, 0, &vertexBinding, 1);
        
        // Bind index buffer
        SDL_GPUBufferBinding indexBinding = {
            .buffer = models[0].model->indexBuffer,
            .offset = 0,
        };
        SDL_BindGPUIndexBuffer(render_pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        
        // Determine texture to use: instance override -> model texture -> white pixel
        SDL_GPUTexture* textureToUse = nullptr;
        SDL_GPUSampler* samplerToUse = nullptr;
        
        // Check if instance has texture override
        if (models[0].textureOverride.gpuTexture) {
            textureToUse = models[0].textureOverride.gpuTexture;
        }
        // Otherwise use model's default texture
        else if (models[0].model->texture.gpuTexture) {
            textureToUse = models[0].model->texture.gpuTexture;
        }
        // Fallback to white pixel if no texture set
        else {
            Texture whitePixel = Renderer::WhitePixel();
            textureToUse = whitePixel.gpuTexture;
        }
        
        // Always use LINEAR sampler for 3D models (smooth filtering, not pixel art)
        samplerToUse = Renderer::GetSampler(ScaleMode::LINEAR);
        
        // Bind the texture at slot 0 (matches shader binding 0)
        SDL_GPUTextureSamplerBinding textureBinding = {
            .texture = textureToUse,
            .sampler = samplerToUse,
        };
        SDL_BindGPUFragmentSamplers(render_pass, 0, &textureBinding, 1);
        
        // Draw ALL models in one instanced draw call!
        SDL_DrawGPUIndexedPrimitives(
            render_pass,
            static_cast<uint32_t>(models[0].model->indices.size()),
            static_cast<uint32_t>(models.size()),  // Instance count
            0,
            0,
            0
        );

    }
    
    SDL_EndGPURenderPass(render_pass);
    SDL_PopGPUDebugGroup(cmd_buffer);
}
