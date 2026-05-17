#include "renderer/passes/model3drenderpass.h"
#include "gpu/backends/sdl/sdlgpu.h"
#include "gpu/presets.h"
#include "platform/window/window.h"
#include "assets/shaders_generated.h"
#include "core/log/loghandler.h"
#include <algorithm>

bool Model3DRenderPass::init(
    GpuTextureFormat swapchain_texture_format,
    uint32_t width,
    uint32_t height,
    std::string name,
    bool logInit,
    size_t /*capacity*/,
    bool /*forceNoMSAA*/
) {
    passname = name;
    surface_width = width;
    surface_height = height;
    
    SDL_GPUSampleCount sampleCount = toSDL(Renderer::GetSampleCount());
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
    
    m_depth_texture.gpuTexture = reinterpret_cast<GpuTextureHandle>(SDL_CreateGPUTexture(m_gpu_device, &depth_create_info));
    if (!m_depth_texture.gpuTexture) {
        LOG_ERROR("Failed to create depth texture: {}", SDL_GetError());
        return false;
    }
    
    // Create uniform buffer
    SDL_GPUBufferCreateInfo uniform_buffer_info{
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = sizeof(SceneUniforms),
    };
    
    uniformBuffer = SDL_CreateGPUBuffer(m_gpu_device, &uniform_buffer_info);
    if (!uniformBuffer) {
        LOG_ERROR("Failed to create uniform buffer: {}", SDL_GetError());
        return false;
    }
    
    SDL_GPUTransferBufferCreateInfo transfer_buffer_info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(SceneUniforms),
    };
    
    uniformTransferBuffer = SDL_CreateGPUTransferBuffer(m_gpu_device, &transfer_buffer_info);
    if (!uniformTransferBuffer) {
        LOG_ERROR("Failed to create uniform transfer buffer: {}", SDL_GetError());
        return false;
    }
    
    createShaders();
    createPipeline(toSDL(swapchain_texture_format));
    
    if (logInit) {
        LOG_INFO("Render pass initialized: 3d (MSAA={})", static_cast<int>(sampleCount));
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
        SDL_ReleaseGPUTexture(m_gpu_device, reinterpret_cast<SDL_GPUTexture*>(m_depth_texture.gpuTexture));
        m_depth_texture.gpuTexture = 0;
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
        SDL_ReleaseGPUGraphicsPipeline(m_gpu_device, reinterpret_cast<SDL_GPUGraphicsPipeline*>(m_pipeline));
        m_pipeline = 0;
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
        LOG_INFO("Released 3D model render pass");
    }
}

void Model3DRenderPass::createShaders() {
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

    // Create vertex shader
    SDL_GPUShaderCreateInfo vertexShaderInfo = {
        .code_size = Luminoveau::Shaders::Model3d_Vert_Size,
        .code = Luminoveau::Shaders::Model3d_Vert,
        .entrypoint = entryPoint,
        .format = shaderFormat,  // Use selected format
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = 1,  // SceneUniforms at set 0
        .num_uniform_buffers = 0,  // Not using push constants
    };
    
    vertex_shader = SDL_CreateGPUShader(m_gpu_device, &vertexShaderInfo);
    if (!vertex_shader) {
        LOG_ERROR("Failed to create vertex shader: {}", SDL_GetError());
        return;
    }
    
    // Create fragment shader (now with texture sampling)
    SDL_GPUShaderCreateInfo fragmentShaderInfo = {
        .code_size = Luminoveau::Shaders::Model3d_Frag_Size,
        .code = Luminoveau::Shaders::Model3d_Frag,
        .entrypoint = entryPoint,
        .format = shaderFormat,  // Use selected format
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,  // Sampler at binding 0
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 0,
    };
    
    fragment_shader = SDL_CreateGPUShader(m_gpu_device, &fragmentShaderInfo);
    if (!fragment_shader) {
        LOG_ERROR("Failed to create fragment shader: {}", SDL_GetError());
        SDL_ReleaseGPUShader(m_gpu_device, vertex_shader);
        vertex_shader = nullptr;
        return;
    }

}

void Model3DRenderPass::createPipeline(SDL_GPUTextureFormat swapchain_format) {

    SDL_GPUSampleCount currentSampleCount = toSDL(Renderer::GetSampleCount());

    if (!vertex_shader || !fragment_shader) {
        LOG_ERROR("Cannot create pipeline - shaders not loaded");
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
        .blend_state = toSDL(GpuPresets::AlphaBlendKeepDstAlpha)
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
            .sample_count = toSDL(Renderer::GetSampleCount()),
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
    
    
    m_pipeline = reinterpret_cast<GpuGraphicsPipelineHandle>(SDL_CreateGPUGraphicsPipeline(m_gpu_device, &pipelineInfo));
    if (!m_pipeline) {
        LOG_ERROR("Failed to create graphics pipeline: {}", SDL_GetError());
        return;
    }
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
    
    model->vertexBuffer = reinterpret_cast<GpuBufferHandle>(SDL_CreateGPUBuffer(m_gpu_device, &vertex_buffer_info));
    if (!model->vertexBuffer) {
        LOG_ERROR("Failed to create vertex buffer: {}", SDL_GetError());
        return;
    }
    
    // Create index buffer
    SDL_GPUBufferCreateInfo index_buffer_info{
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = static_cast<uint32_t>(model->indices.size() * sizeof(uint32_t)),
    };
    
    model->indexBuffer = reinterpret_cast<GpuBufferHandle>(SDL_CreateGPUBuffer(m_gpu_device, &index_buffer_info));
    if (!model->indexBuffer) {
        LOG_ERROR("Failed to create index buffer: {}", SDL_GetError());
        return;
    }
    
    // Create transfer buffers
    SDL_GPUTransferBufferCreateInfo vertex_transfer_info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<uint32_t>(model->vertices.size() * sizeof(Vertex3D)),
    };
    
    model->vertexTransferBuffer = reinterpret_cast<GpuTransferBufferHandle>(SDL_CreateGPUTransferBuffer(m_gpu_device, &vertex_transfer_info));
    if (!model->vertexTransferBuffer) {
        LOG_ERROR("Failed to create vertex transfer buffer: {}", SDL_GetError());
        return;
    }
    
    SDL_GPUTransferBufferCreateInfo index_transfer_info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<uint32_t>(model->indices.size() * sizeof(uint32_t)),
    };
    
    model->indexTransferBuffer = reinterpret_cast<GpuTransferBufferHandle>(SDL_CreateGPUTransferBuffer(m_gpu_device, &index_transfer_info));
    if (!model->indexTransferBuffer) {
        LOG_ERROR("Failed to create index transfer buffer: {}", SDL_GetError());
        return;
    }
    
    // Upload vertex data
    void* vertex_data = SDL_MapGPUTransferBuffer(m_gpu_device, reinterpret_cast<SDL_GPUTransferBuffer*>(model->vertexTransferBuffer), false);
    SDL_memcpy(vertex_data, model->vertices.data(), model->vertices.size() * sizeof(Vertex3D));
    SDL_UnmapGPUTransferBuffer(m_gpu_device, reinterpret_cast<SDL_GPUTransferBuffer*>(model->vertexTransferBuffer));

    // Upload index data
    void* index_data = SDL_MapGPUTransferBuffer(m_gpu_device, reinterpret_cast<SDL_GPUTransferBuffer*>(model->indexTransferBuffer), false);
    SDL_memcpy(index_data, model->indices.data(), model->indices.size() * sizeof(uint32_t));
    SDL_UnmapGPUTransferBuffer(m_gpu_device, reinterpret_cast<SDL_GPUTransferBuffer*>(model->indexTransferBuffer));
    
    // Copy to GPU buffers
    SDL_GPUCommandBuffer* upload_cmd = SDL_AcquireGPUCommandBuffer(m_gpu_device);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(upload_cmd);
    
    SDL_GPUTransferBufferLocation vertex_src{
        .transfer_buffer = reinterpret_cast<SDL_GPUTransferBuffer*>(model->vertexTransferBuffer),
        .offset = 0,
    };

    SDL_GPUBufferRegion vertex_dst{
        .buffer = reinterpret_cast<SDL_GPUBuffer*>(model->vertexBuffer),
        .offset = 0,
        .size = static_cast<uint32_t>(model->vertices.size() * sizeof(Vertex3D)),
    };

    SDL_UploadToGPUBuffer(copy_pass, &vertex_src, &vertex_dst, false);

    SDL_GPUTransferBufferLocation index_src{
        .transfer_buffer = reinterpret_cast<SDL_GPUTransferBuffer*>(model->indexTransferBuffer),
        .offset = 0,
    };

    SDL_GPUBufferRegion index_dst{
        .buffer = reinterpret_cast<SDL_GPUBuffer*>(model->indexBuffer),
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
    GpuCmdBufferHandle cmdBuffer,
    GpuTextureHandle targetTexture,
    const glm::mat4& camera_unused
) {
    auto* cmd_buffer = reinterpret_cast<SDL_GPUCommandBuffer*>(cmdBuffer);
    auto* target_texture = reinterpret_cast<SDL_GPUTexture*>(targetTexture);
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
    bool shouldResolve = (renderTargetResolve != 0);

    // Render directly to target texture (no separate MSAA texture)
    SDL_GPUColorTargetInfo colorTarget = {
        .texture = target_texture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = SDL_FColor{color_target_clear_r, color_target_clear_g, color_target_clear_b, color_target_clear_a},
        .load_op = toSDL(color_target_info_loadop),
        .store_op = shouldResolve ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE,
        .resolve_texture = reinterpret_cast<SDL_GPUTexture*>(renderTargetResolve),
        .resolve_mip_level = 0,
        .resolve_layer = 0,
        .cycle = false,
        .cycle_resolve_texture = false,
    };

    SDL_GPUDepthStencilTargetInfo depthTarget = {
        .texture = renderTargetDepth ? reinterpret_cast<SDL_GPUTexture*>(renderTargetDepth) : reinterpret_cast<SDL_GPUTexture*>(m_depth_texture.gpuTexture),  // Use shared MSAA depth if provided
        .clear_depth = 1.0f,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,  // MUST STORE for multi-model rendering!
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
        .cycle = false,
        .clear_stencil = 0,
    };
    
    SDL_GPURenderPass* sdl_rp = SDL_BeginGPURenderPass(cmd_buffer, &colorTarget, 1, &depthTarget);
    render_pass = reinterpret_cast<GpuRenderPassHandle>(sdl_rp);

    // Set viewport to physical pixel size (GPU renders at device resolution)
    SDL_GPUViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .w = static_cast<float>(Window::GetPhysicalWidth()),
        .h = static_cast<float>(Window::GetPhysicalHeight()),
        .min_depth = 0.0f,
        .max_depth = 1.0f
    };
    SDL_SetGPUViewport(sdl_rp, &viewport);

    // If no models or no pipeline, just end (clear still happens via LOADOP_CLEAR)
    if (models.empty() || !m_pipeline) {
        SDL_EndGPURenderPass(sdl_rp);
        SDL_PopGPUDebugGroup(cmd_buffer);
        return;
    }

    // Bind pipeline and storage buffers once
    SDL_BindGPUGraphicsPipeline(sdl_rp, reinterpret_cast<SDL_GPUGraphicsPipeline*>(m_pipeline));
    SDL_BindGPUVertexStorageBuffers(sdl_rp, 0, &uniformBuffer, 1);

    // For now, assume all models use the same mesh (cube)
    // TODO: Group models by mesh and do separate instanced draws per mesh
    if (!models.empty() && models[0].model && models[0].model->vertexBuffer && models[0].model->indexBuffer) {
        // Bind vertex buffer
        SDL_GPUBufferBinding vertexBinding = {
            .buffer = reinterpret_cast<SDL_GPUBuffer*>(models[0].model->vertexBuffer),
            .offset = 0,
        };
        SDL_BindGPUVertexBuffers(sdl_rp, 0, &vertexBinding, 1);

        // Bind index buffer
        SDL_GPUBufferBinding indexBinding = {
            .buffer = reinterpret_cast<SDL_GPUBuffer*>(models[0].model->indexBuffer),
            .offset = 0,
        };
        SDL_BindGPUIndexBuffer(sdl_rp, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        // Determine texture to use: instance override -> model texture -> white pixel
        SDL_GPUTexture* textureToUse = nullptr;
        SDL_GPUSampler* samplerToUse = nullptr;

        // Check if instance has texture override
        if (models[0].textureOverride.gpuTexture) {
            textureToUse = reinterpret_cast<SDL_GPUTexture*>(models[0].textureOverride.gpuTexture);
        }
        // Otherwise use model's default texture
        else if (models[0].model->texture.gpuTexture) {
            textureToUse = reinterpret_cast<SDL_GPUTexture*>(models[0].model->texture.gpuTexture);
        }
        // Fallback to white pixel if no texture set
        else {
            Texture whitePixel = Renderer::WhitePixel();
            textureToUse = reinterpret_cast<SDL_GPUTexture*>(whitePixel.gpuTexture);
        }

        // Always use LINEAR sampler for 3D models (smooth filtering, not pixel art)
        samplerToUse = reinterpret_cast<SDL_GPUSampler*>(Renderer::GetSampler(ScaleMode::Linear));

        // Bind the texture at slot 0 (matches shader binding 0)
        SDL_GPUTextureSamplerBinding textureBinding = {
            .texture = textureToUse,
            .sampler = samplerToUse,
        };
        SDL_BindGPUFragmentSamplers(sdl_rp, 0, &textureBinding, 1);

        // Draw ALL models in one instanced draw call!
        SDL_DrawGPUIndexedPrimitives(
            sdl_rp,
            static_cast<uint32_t>(models[0].model->indices.size()),
            static_cast<uint32_t>(models.size()),  // Instance count
            0,
            0,
            0
        );
    }

    SDL_EndGPURenderPass(sdl_rp);
    SDL_PopGPUDebugGroup(cmd_buffer);
}
