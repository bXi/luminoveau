#include "shaderrenderpass.h"

#include <utility>

#include "SDL3/SDL_gpu.h"
#include "assethandler/shaders_generated.h"
#include "spriterenderpass.h"
#include "window/windowhandler.h"
#include "shaderhandler.h"
#include "../log/loghandler.h"

void ShaderRenderPass::release(bool logRelease) {
    m_depth_texture.release(Renderer::GetDevice());

    if (m_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), m_pipeline);
        m_pipeline = nullptr;
    }

    // Release result texture
    if (resultTexture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), resultTexture);
        resultTexture = nullptr;
    }
    
    // Release input texture
    if (inputTexture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), inputTexture);
        inputTexture = nullptr;
    }

    // Release the temp 1x1 texture
    if (fs.texture.gpuTexture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), fs.texture.gpuTexture);
        fs.texture.gpuTexture = nullptr;
    }

    // Release final render pipeline
    if (finalrender_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), finalrender_pipeline);
        finalrender_pipeline = nullptr;
    }

    // Release shared shaders (only if ShaderRenderPass owns them)
    if (finalrender_vertex_shader) {
        SDL_ReleaseGPUShader(Renderer::GetDevice(), finalrender_vertex_shader);
        finalrender_vertex_shader = nullptr;
    }

    if (finalrender_fragment_shader) {
        SDL_ReleaseGPUShader(Renderer::GetDevice(), finalrender_fragment_shader);
        finalrender_fragment_shader = nullptr;
    }

    if (logRelease) {
        LOG_INFO("Released graphics pipeline: {}", passname.c_str());
    }
}

void ShaderRenderPass::_loadUniformsFromShader(const std::vector<uint8_t> &spirvBinary) {
    // Load metadata from cache instead of doing runtime reflection
    // The metadata was extracted during shader compilation and cached
    ShaderMetadata metadata = Shaders::GetShaderMetadata(vertShader.shaderFilename);
    
    for (const auto& [name, offset] : metadata.uniform_offsets) {
        size_t size = metadata.uniform_sizes.at(name);
        uniformBuffer.addVariable(name, size, offset);
    }
}

void ShaderRenderPass::_loadSamplerNamesFromShader(const std::vector<uint8_t> &spirvBinary) {
    // Load sampler names from cached metadata
    ShaderMetadata metadata = Shaders::GetShaderMetadata(fragShader.shaderFilename);
    foundSamplers = metadata.sampler_names;
}

bool ShaderRenderPass::init(
    SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name, bool logInit) {

    passname = std::move(name);

    // Store desktop dimensions for UV scaling when sampling FROM desktop-sized framebuffers
    m_desktop_width = surface_width;
    m_desktop_height = surface_height;

    vertex_shader   = vertShader.shader;
    fragment_shader = fragShader.shader;

    _loadUniformsFromShader(vertShader.fileData);
    _loadSamplerNamesFromShader(fragShader.fileData);

    // resultTexture is WINDOW-SIZED for shader output
    // User shaders expect to sample and output at window resolution
    resultTexture = AssetHandler::CreateEmptyTexture(Window::GetPhysicalSize()).gpuTexture;
    inputTexture = AssetHandler::CreateEmptyTexture(Window::GetPhysicalSize()).gpuTexture; 

    SDL_SetGPUTextureName(Renderer::GetDevice(), resultTexture, Helpers::TextFormat("ShaderRenderPass: %s resultTexture", passname.c_str()));

    fs.texture = AssetHandler::CreateEmptyTexture({1, 1});
    fs.x       = 0.f;
    fs.y       = 0.f;

    fs.r = 255;
    fs.g = 255;
    fs.b = 255;
    fs.a = 255;

    std::vector<SDL_GPUColorTargetDescription> color_target_descriptions(fragShader.samplerCount, SDL_GPUColorTargetDescription{
        .format = swapchain_texture_format,
        .blend_state = GPUstructs::srcAlphaBlendState,
    });

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
        .multisample_state = {},
        .depth_stencil_state =
            {
                .compare_op = SDL_GPU_COMPAREOP_LESS,
                .back_stencil_state = {},
                .front_stencil_state = {},
                .compare_mask = 0,
                .write_mask = 0,
                .enable_depth_test = false,
                .enable_depth_write = false,
                .enable_stencil_test = false,
            },
        .target_info =
            {
                .color_target_descriptions = color_target_descriptions.data(),
                .num_color_targets = fragShader.samplerCount,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                .has_depth_stencil_target = false,
            },
        .props = 0,
    };
    m_pipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &pipeline_create_info);

    if (!m_pipeline) {
        LOG_CRITICAL("failed to create graphics pipeline: {}", SDL_GetError());
    }

    if (!finalrender_vertex_shader) {
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
            const char* builtinEntryPoint = "main0";
        #else
            const char* builtinEntryPoint = "main";
        #endif
        
        SDL_GPUShaderCreateInfo vertexShaderInfo = {
            .code_size            = Luminoveau::Shaders::FullscreenQuad_Vert_Size,
            .code                 = Luminoveau::Shaders::FullscreenQuad_Vert,
            .entrypoint           = builtinEntryPoint,
            .format               = shaderFormat,  // Use selected format
            .stage                = SDL_GPU_SHADERSTAGE_VERTEX,
            .num_samplers         = 0,
            .num_storage_textures = 0,
            .num_storage_buffers  = 0,
            .num_uniform_buffers  = 1,  // CameraUniforms at space1
        };

        finalrender_vertex_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &vertexShaderInfo);
    }

    if (!finalrender_fragment_shader) {
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
            const char* builtinEntryPoint = "main0";
        #else
            const char* builtinEntryPoint = "main";
        #endif
        
        SDL_GPUShaderCreateInfo fragmentShaderInfo = {
            .code_size            = Luminoveau::Shaders::FullscreenQuad_Frag_Size,
            .code                 = Luminoveau::Shaders::FullscreenQuad_Frag,
            .entrypoint           = builtinEntryPoint,
            .format               = shaderFormat,  // Use selected format
            .stage                = SDL_GPU_SHADERSTAGE_FRAGMENT,
            .num_samplers         = 1,
            .num_storage_textures = 0,
            .num_storage_buffers  = 0,
            .num_uniform_buffers  = 0,  // No fragment uniforms
        };

        finalrender_fragment_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &fragmentShaderInfo);
    }

    // Create the final render pipeline once during init
    if (!finalrender_pipeline) {
        std::vector<SDL_GPUColorTargetDescription> finalrender_color_target_descriptions(1, SDL_GPUColorTargetDescription{
            .format = SDL_GetGPUSwapchainTextureFormat(Renderer::GetDevice(), Window::GetWindow()),
            .blend_state = GPUstructs::defaultBlendState
        });

        SDL_GPUGraphicsPipelineCreateInfo finalrender_pipeline_create_info{
            .vertex_shader = finalrender_vertex_shader,
            .fragment_shader = finalrender_fragment_shader,
            .vertex_input_state =
                {
                    .vertex_buffer_descriptions = nullptr,
                    .num_vertex_buffers = 0,
                    .vertex_attributes = nullptr,
                    .num_vertex_attributes = 0,
                },
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = GPUstructs::defaultRasterizerState,
            .multisample_state = {},
            .depth_stencil_state =
                {
                    .compare_op = SDL_GPU_COMPAREOP_LESS,
                    .back_stencil_state = {},
                    .front_stencil_state = {},
                    .compare_mask = 0,
                    .write_mask = 0,
                    .enable_depth_test = false,
                    .enable_depth_write = false,
                    .enable_stencil_test = false,
                },
            .target_info =
                {
                    .color_target_descriptions = finalrender_color_target_descriptions.data(),
                    .num_color_targets = 1,
                    .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                    .has_depth_stencil_target = false,
                },
            .props = 0,
        };

        finalrender_pipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &finalrender_pipeline_create_info);

        if (!finalrender_pipeline) {
            LOG_CRITICAL("failed to create final render graphics pipeline: {}", SDL_GetError());
        }
    }

    if (logInit) {
        LOG_INFO("created graphics pipeline: {}", passname.c_str());
    }

    return true;
}

void ShaderRenderPass::render(
    SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
) {
    LUMI_UNUSED(target_texture);
    #ifdef LUMIDEBUG
    SDL_PushGPUDebugGroup(cmd_buffer, CURRENT_METHOD());
    #endif

    auto framebuffer = Renderer::GetFramebuffer("primaryFramebuffer");
    
    // STEP 1: Copy window region from desktop-sized framebuffer to window-sized inputTexture
    {
        std::vector<SDL_GPUColorTargetInfo> copy_target_info(1, SDL_GPUColorTargetInfo{
            .texture = inputTexture,
            .mip_level = 0,
            .layer_or_depth_plane = 0,
            .clear_color = {0.0, 0.0, 0.0, 1.0},
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
        });
        
        SDL_GPURenderPass *copy_pass = SDL_BeginGPURenderPass(cmd_buffer, copy_target_info.data(), 1, nullptr);
        SDL_GPUViewport viewport = {
            .x = 0, .y = 0,
            .w = (float)Window::GetPhysicalWidth(),
            .h = (float)Window::GetPhysicalHeight(),
            .min_depth = 0.0f,
            .max_depth = 1.0f
        };
        SDL_SetGPUViewport(copy_pass, &viewport);
        SDL_BindGPUGraphicsPipeline(copy_pass, finalrender_pipeline);
        
        // Push full uniforms struct to match shader expectations
        glm::mat4 model = glm::mat4(
            (float)Window::GetWidth(), 0.0f, 0.0f, 0.0f,
            0.0f, (float)Window::GetHeight(), 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        );
        
        // Calculate UV coordinates to sample window region from desktop framebuffer
        float uMax = (float)Window::GetPhysicalWidth() / (float)m_desktop_width;
        float vMax = (float)Window::GetPhysicalHeight() / (float)m_desktop_height;
        
        struct Uniforms {
            glm::mat4 camera;
            glm::mat4 model;
            glm::vec2 flipped;
            glm::vec2 uv0;
            glm::vec2 uv1;
            glm::vec2 uv2;
            glm::vec2 uv3;
            glm::vec2 uv4;
            glm::vec2 uv5;
            float tintColorR;
            float tintColorG;
            float tintColorB;
            float tintColorA;
        };
        
        Uniforms copy_uniforms{
            .camera = camera,
            .model = model,
            .flipped = glm::vec2(1.0, 1.0),
            .uv0 = glm::vec2(uMax, vMax),
            .uv1 = glm::vec2(0.0, vMax),
            .uv2 = glm::vec2(uMax, 0.0),
            .uv3 = glm::vec2(0.0, vMax),
            .uv4 = glm::vec2(0.0, 0.0),
            .uv5 = glm::vec2(uMax, 0.0),
            .tintColorR = 1.0f,
            .tintColorG = 1.0f,
            .tintColorB = 1.0f,
            .tintColorA = 1.0f
        };
        SDL_PushGPUVertexUniformData(cmd_buffer, 0, &copy_uniforms, sizeof(copy_uniforms));
        
        SDL_GPUTextureSamplerBinding binding{
            .texture = framebuffer->fbContent,
            .sampler = Renderer::GetSampler(ScaleMode::LINEAR),
        };
        SDL_BindGPUFragmentSamplers(copy_pass, 0, &binding, 1);
        SDL_DrawGPUPrimitives(copy_pass, 6, 1, 0, 0);  // 6 vertices for fullscreen quad
        SDL_EndGPURenderPass(copy_pass);
    }
    
    // STEP 2: Run user shader with inputTexture (window-sized) → resultTexture (window-sized)
    std::vector<SDL_GPUColorTargetInfo> color_target_info(fragShader.samplerCount, SDL_GPUColorTargetInfo{
        .texture = resultTexture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = color_target_info_clear_color,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
    });

    render_pass = SDL_BeginGPURenderPass(cmd_buffer, color_target_info.data(), color_target_info.size(), nullptr);
    assert(render_pass);
    {
        // Set viewport to window size (render to top-left portion of desktop-sized buffer)
        SDL_GPUViewport viewport = {
            .x = 0,
            .y = 0,
            .w = (float)Window::GetPhysicalWidth(),
            .h = (float)Window::GetPhysicalHeight(),
            .min_depth = 0.0f,
            .max_depth = 1.0f
        };
        SDL_SetGPUViewport(render_pass, &viewport);

        if (_scissorEnabled) {
            SDL_SetGPUScissor(render_pass, _scissorRect);
            _scissorEnabled = false;
        }

        SDL_BindGPUGraphicsPipeline(render_pass, m_pipeline);
        if (Input::MouseButtonDown(SDL_BUTTON_LEFT)) {
            lastMousePos = Input::GetMousePosition();
        }

        glm::mat4 model = glm::mat4(
            (float)Window::GetWidth(), 0.0f, 0.0f, 0.0f,  // X scale to window width
            0.0f, (float)Window::GetHeight(), 0.0f, 0.0f, // Y scale to window height
            0.0f, 0.0f, 1.0f, 0.0f,                      // Z unchanged (depth)
            0.0f, 0.0f, 0.1f, 1.0f                       // Slight Z offset, W = 1
        );


        uniformBuffer["model"] = model;


        uniformBuffer["camera"] = camera;
        uniformBuffer["flipped"] = glm::vec2(1.0, 1.0);
        
        // UVs at 1.0 since we're now sampling from window-sized inputTexture
        float uv_max_x = 1.0;
        float uv_max_y = 1.0;
        
        uniformBuffer["uv"]      = std::array<glm::vec2, 6>{
            glm::vec2(uv_max_x, uv_max_y),
            glm::vec2(0.0, uv_max_y),
            glm::vec2(uv_max_x, 0.0),
            glm::vec2(0.0, uv_max_y),
            glm::vec2(0.0, 0.0),
            glm::vec2(uv_max_x, 0.0),
        };

        uniformBuffer["tintColor"] = Color(WHITE).asVec4();

        uniformBuffer["iResolution"] = glm::vec3{(float) Window::GetPhysicalWidth(), (float) Window::GetPhysicalHeight(), 0.0f};
        uniformBuffer["iTime"]       = (float) Window::GetRunTime();
        uniformBuffer["iTimeDelta"]  = (float) (Window::GetFrameTime() * 1.0f);
        uniformBuffer["iFrame"]      = (float) EngineState::_frameCount;

        uniformBuffer["iMouse"] = glm::vec4{Input::GetMousePosition().x, Input::GetMousePosition().y, lastMousePos.x, lastMousePos.y};

        SDL_PushGPUVertexUniformData(cmd_buffer, 0, uniformBuffer.getBufferPointer(), uniformBuffer.getBufferSize());

        // Bind window-sized inputTexture (not desktop framebuffer)
        std::vector<SDL_GPUTextureSamplerBinding> texture_sampler_bindings(fragShader.samplerCount, SDL_GPUTextureSamplerBinding{
            .texture = inputTexture,
            .sampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode()),
        });

        int i = 0;
        for (const auto &sampler: foundSamplers) {
            if (fragShader.frameBufferToSamplerMapping.contains(sampler)) {
                auto fb = Renderer::GetFramebuffer(fragShader.frameBufferToSamplerMapping[sampler]);
                texture_sampler_bindings[i].texture = fb->fbContent;
            }
            i++;
        }

        SDL_BindGPUFragmentSamplers(render_pass, 0, texture_sampler_bindings.data(), fragShader.samplerCount);
        SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
    }

    SDL_EndGPURenderPass(render_pass);

    _renderShaderOutputToFramebuffer(cmd_buffer, framebuffer->fbContent, resultTexture, camera);

    #ifdef LUMIDEBUG
    SDL_PopGPUDebugGroup(cmd_buffer);
    #endif
}

UniformBuffer &ShaderRenderPass::getUniformBuffer() {
    return uniformBuffer;
}

void
ShaderRenderPass::_renderShaderOutputToFramebuffer(SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, SDL_GPUTexture *result_texture,
                                                   const glm::mat4 &camera) {

    if (!fragment_shader) {
        LOG_CRITICAL("failed to create fragment shader for: {} ({})", passname.c_str(), SDL_GetError());
    }

    std::vector<SDL_GPUColorTargetInfo> color_target_info(1, SDL_GPUColorTargetInfo{
        .texture = target_texture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0.0, 0.0, 0.0, 1.0},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    });

    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(cmd_buffer, color_target_info.data(), 1, nullptr);
    assert(render_pass);
    {
        // Set viewport to window size (render to top-left portion of desktop-sized buffer)
        SDL_GPUViewport viewport = {
            .x = 0,
            .y = 0,
            .w = (float)Window::GetPhysicalWidth(),
            .h = (float)Window::GetPhysicalHeight(),
            .min_depth = 0.0f,
            .max_depth = 1.0f
        };
        SDL_SetGPUViewport(render_pass, &viewport);

        // Use the pipeline created during init instead of creating a new one each frame
        SDL_BindGPUGraphicsPipeline(render_pass, finalrender_pipeline);

        // Push full uniforms struct to match shader expectations
        glm::mat4 model = glm::mat4(
            (float)Window::GetWidth(), 0.0f, 0.0f, 0.0f,
            0.0f, (float)Window::GetHeight(), 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        );
        
        struct Uniforms {
            glm::mat4 camera;
            glm::mat4 model;
            glm::vec2 flipped;
            glm::vec2 uv0;
            glm::vec2 uv1;
            glm::vec2 uv2;
            glm::vec2 uv3;
            glm::vec2 uv4;
            glm::vec2 uv5;
            float tintColorR;
            float tintColorG;
            float tintColorB;
            float tintColorA;
        };
        
        // Sample entire window-sized resultTexture (UVs 0-1)
        Uniforms finalrender_uniforms{
            .camera = camera,
            .model = model,
            .flipped = glm::vec2(1.0, 1.0),
            .uv0 = glm::vec2(1.0, 1.0),
            .uv1 = glm::vec2(0.0, 1.0),
            .uv2 = glm::vec2(1.0, 0.0),
            .uv3 = glm::vec2(0.0, 1.0),
            .uv4 = glm::vec2(0.0, 0.0),
            .uv5 = glm::vec2(1.0, 0.0),
            .tintColorR = 1.0f,
            .tintColorG = 1.0f,
            .tintColorB = 1.0f,
            .tintColorA = 1.0f
        };
        SDL_PushGPUVertexUniformData(cmd_buffer, 0, &finalrender_uniforms, sizeof(finalrender_uniforms));

        std::vector<SDL_GPUTextureSamplerBinding> texture_sampler_bindings(1, SDL_GPUTextureSamplerBinding{
            .texture = result_texture,
            .sampler = Renderer::GetSampler(ScaleMode::LINEAR),
        });

        SDL_BindGPUFragmentSamplers(render_pass, 0, texture_sampler_bindings.data(), texture_sampler_bindings.size());
        SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);  // 6 vertices for fullscreen quad
    }

    SDL_EndGPURenderPass(render_pass);
    // No longer creating and releasing pipeline here - it's now managed in init() and release()
}