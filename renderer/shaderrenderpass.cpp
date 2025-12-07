#include "shaderrenderpass.h"

#include <utility>

#include "SDL3/SDL_gpu.h"
#include "spriterenderpass.h"
#include "window/windowhandler.h"

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
        SDL_Log("%s: released graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());
    }
}

void ShaderRenderPass::_loadUniformsFromShader(const std::vector<uint8_t> &spirvBinary) {

    try {
        spirv_cross::Compiler compiler(reinterpret_cast<const uint32_t *>(spirvBinary.data()),
                                       spirvBinary.size() / sizeof(uint32_t));

        auto resources = compiler.get_shader_resources();

        for (const auto &uniform: resources.uniform_buffers) {
            auto &bufferType = compiler.get_type(uniform.base_type_id);

            for (size_t i = 0; i < bufferType.member_types.size(); ++i) {
                const std::string &memberName = compiler.get_member_name(uniform.base_type_id, i);
                size_t            memberSize  = compiler.get_declared_struct_member_size(bufferType, i);

                uniformBuffer.addVariable(memberName, memberSize, compiler.type_struct_member_offset(bufferType, i));
            }
        }
    } catch (const std::exception &e) {
        throw std::runtime_error(Helpers::TextFormat("%s: Reflection failed: %s", CURRENT_METHOD(), e.what()));
    }
}

void ShaderRenderPass::_loadSamplerNamesFromShader(const std::vector<uint8_t> &spirvBinary) {

    try {
        spirv_cross::Compiler compiler(reinterpret_cast<const uint32_t *>(spirvBinary.data()),
                                       spirvBinary.size() / sizeof(uint32_t));

        auto resources = compiler.get_shader_resources();

        for (const auto &sampler: resources.sampled_images) {
            const std::string &samplerName = compiler.get_name(sampler.id);
            //uint32_t binding = compiler.get_decoration(sampler.id, spv::DecorationBinding);

            foundSamplers.push_back(samplerName);
        }
    } catch (const std::exception &e) {
        throw std::runtime_error(Helpers::TextFormat("%s: Reflection failed: %s", CURRENT_METHOD(), e.what()));
    }
}

bool ShaderRenderPass::init(
    SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name, bool logInit) {

    LUMI_UNUSED(surface_height, surface_width);

    passname = std::move(name);

    vertex_shader   = vertShader.shader;
    fragment_shader = fragShader.shader;

    _loadUniformsFromShader(vertShader.fileData);
    _loadSamplerNamesFromShader(fragShader.fileData);

    resultTexture = AssetHandler::CreateEmptyTexture(Window::GetSize()).gpuTexture;

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
        throw std::runtime_error(Helpers::TextFormat("%s: failed to create graphics pipeline: %s", CURRENT_METHOD(), SDL_GetError()));
    }

    if (!finalrender_vertex_shader) {
        SDL_GPUShaderCreateInfo vertexShaderInfo = {
            .code_size            = SpriteRenderPass::sprite_vert_bin_len,
            .code                 = SpriteRenderPass::sprite_vert_bin,
            .entrypoint           = "main",
            .format               = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage                = SDL_GPU_SHADERSTAGE_VERTEX,
            .num_samplers         = 0,
            .num_storage_textures = 0,
            .num_storage_buffers  = 0,
            .num_uniform_buffers  = 2,
        };

        finalrender_vertex_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &vertexShaderInfo);
    }

    if (!finalrender_fragment_shader) {
        SDL_GPUShaderCreateInfo fragmentShaderInfo = {
            .code_size            = SpriteRenderPass::sprite_frag_bin_len,
            .code                 = SpriteRenderPass::sprite_frag_bin,
            .entrypoint           = "main",
            .format               = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage                = SDL_GPU_SHADERSTAGE_FRAGMENT,
            .num_samplers         = 1,
            .num_storage_textures = 0,
            .num_storage_buffers  = 0,
            .num_uniform_buffers  = 1,
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
            throw std::runtime_error(Helpers::TextFormat("%s: failed to create final render graphics pipeline: %s", CURRENT_METHOD(), SDL_GetError()));
        }
    }

    if (logInit) {
        SDL_Log("%s: created graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());
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

    std::vector<SDL_GPUColorTargetInfo> color_target_info(fragShader.samplerCount, SDL_GPUColorTargetInfo{
        .texture = resultTexture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = color_target_info_clear_color,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
    });

    auto framebuffer = Renderer::GetFramebuffer("primaryFramebuffer");

    render_pass = SDL_BeginGPURenderPass(cmd_buffer, color_target_info.data(), color_target_info.size(), nullptr);
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
        uniformBuffer["uv"]      = std::array<glm::vec2, 6>{
            glm::vec2(1.0, 1.0),
            glm::vec2(0.0, 1.0),
            glm::vec2(1.0, 0.0),
            glm::vec2(0.0, 1.0),
            glm::vec2(0.0, 0.0),
            glm::vec2(1.0, 0.0),
        };

        uniformBuffer["tintColor"] = Color(WHITE).asVec4();

        uniformBuffer["iResolution"] = glm::vec3{(float) Window::GetWidth(), (float) Window::GetHeight(), 0.0f};
        uniformBuffer["iTime"]       = (float) Window::GetRunTime();
        uniformBuffer["iTimeDelta"]  = (float) (Window::GetFrameTime() * 1.0f);
        uniformBuffer["iFrame"]      = (float) EngineState::_frameCount;

        uniformBuffer["iMouse"] = glm::vec4{Input::GetMousePosition().x, Input::GetMousePosition().y, lastMousePos.x, lastMousePos.y};

        SDL_PushGPUVertexUniformData(cmd_buffer, 0, uniformBuffer.getBufferPointer(), uniformBuffer.getBufferSize());

        std::vector<SDL_GPUTextureSamplerBinding> texture_sampler_bindings(fragShader.samplerCount, SDL_GPUTextureSamplerBinding{
            .texture = framebuffer->fbContent,
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
        throw std::runtime_error(
            Helpers::TextFormat("%s: failed to create fragment shader for: %s (%s)", CURRENT_METHOD(), passname.c_str(), SDL_GetError()));
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
            .w = (float)Window::GetWidth(),
            .h = (float)Window::GetHeight(),
            .min_depth = 0.0f,
            .max_depth = 1.0f
        };
        SDL_SetGPUViewport(render_pass, &viewport);

        // Use the pipeline created during init instead of creating a new one each frame
        SDL_BindGPUGraphicsPipeline(render_pass, finalrender_pipeline);

        glm::mat4 model = glm::mat4(
            (float)Window::GetWidth(), 0.0f, 0.0f, 0.0f,  // X scale to window width
            0.0f, (float)Window::GetHeight(), 0.0f, 0.0f, // Y scale to window height
            0.0f, 0.0f, 1.0f, 0.0f,                      // Z unchanged (depth)
            0.0f, 0.0f, 0.1f, 1.0f                       // Slight Z offset, W = 1
        );

        Uniforms uniforms{
            .camera = camera,
            .model = model,
            .flipped = glm::vec2(
                1.0, 1.0
            ),
            .uv0 = glm::vec2(1.0, 1.0),
            .uv1 = glm::vec2(0.0, 1.0),
            .uv2 = glm::vec2(1.0, 0.0),
            .uv3 = glm::vec2(0.0, 1.0),
            .uv4 = glm::vec2(0.0, 0.0),
            .uv5 = glm::vec2(1.0, 0.0),
            .tintColorR = fs.r / 255.f,
            .tintColorG = fs.g / 255.f,
            .tintColorB = fs.b / 255.f,
            .tintColorA = fs.a / 255.f,
        };

        SDL_PushGPUVertexUniformData(cmd_buffer, 0, &uniforms, sizeof(uniforms));

        std::vector<SDL_GPUTextureSamplerBinding> texture_sampler_bindings(1, SDL_GPUTextureSamplerBinding{
            .texture = result_texture,
            .sampler = Renderer::GetSampler(ScaleMode::LINEAR),
        });

        SDL_BindGPUFragmentSamplers(render_pass, 0, texture_sampler_bindings.data(), texture_sampler_bindings.size());
        SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
    }

    SDL_EndGPURenderPass(render_pass);
    // No longer creating and releasing pipeline here - it's now managed in init() and release()
}