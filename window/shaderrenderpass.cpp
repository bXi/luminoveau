#include "shaderrenderpass.h"

#include <utility>

#include "SDL3/SDL_gpu.h"

void ShaderRenderPass::release() {
    m_depth_texture.release(Window::GetDevice());
    SDL_ReleaseGPUGraphicsPipeline(Window::GetDevice(), m_pipeline);
    SDL_Log("%s: released graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());
}

ShaderType ShaderRenderPass::mapSPIRTypeToShaderType(const spirv_cross::SPIRType& type) {
    if (type.columns > 1) {
        if (type.columns == 2) return ShaderType::Mat2;
        if (type.columns == 3) return ShaderType::Mat3;
        if (type.columns == 4) return ShaderType::Mat4;
    } else if (type.vecsize > 1) {
        if (type.basetype == spirv_cross::SPIRType::Float) {
            if (type.vecsize == 2) return ShaderType::Vec2;
            if (type.vecsize == 3) return ShaderType::Vec3;
            if (type.vecsize == 4) return ShaderType::Vec4;
        } else if (type.basetype == spirv_cross::SPIRType::Int) {
            if (type.vecsize == 2) return ShaderType::IVec2;
            if (type.vecsize == 3) return ShaderType::IVec3;
            if (type.vecsize == 4) return ShaderType::IVec4;
        } else if (type.basetype == spirv_cross::SPIRType::UInt) {
            if (type.vecsize == 2) return ShaderType::UVec2;
            if (type.vecsize == 3) return ShaderType::UVec3;
            if (type.vecsize == 4) return ShaderType::UVec4;
        } else if (type.basetype == spirv_cross::SPIRType::Boolean) {
            if (type.vecsize == 2) return ShaderType::BVec2;
            if (type.vecsize == 3) return ShaderType::BVec3;
            if (type.vecsize == 4) return ShaderType::BVec4;
        }
    } else {
        switch (type.basetype) {
            case spirv_cross::SPIRType::Float: return ShaderType::Float;
            case spirv_cross::SPIRType::Int: return ShaderType::Int;
            case spirv_cross::SPIRType::UInt: return ShaderType::UInt;
            case spirv_cross::SPIRType::Boolean: return ShaderType::Bool;
            default: break;
        }
    }

    return ShaderType::Float;  // Default to float if type is unrecognized.
}



void ShaderRenderPass::loadUniformsFromShader(const std::vector<uint8_t>& spirvBinary) {
    try {
        spirv_cross::Compiler compiler(reinterpret_cast<const uint32_t*>(spirvBinary.data()),
                                       spirvBinary.size() / sizeof(uint32_t));

        auto resources = compiler.get_shader_resources();

        for (const auto& uniform : resources.uniform_buffers) {
            auto& bufferType = compiler.get_type(uniform.base_type_id);

            for (size_t i = 0; i < bufferType.member_types.size(); ++i) {
                const auto& memberType = compiler.get_type(bufferType.member_types[i]);
                const std::string& memberName = compiler.get_member_name(uniform.base_type_id, i);

                uniformBuffer.addVariable(memberName, mapSPIRTypeToShaderType(memberType));
            }
        }

        // List push constants
        // for (const auto& pushConstant : resources.push_constant_buffers) {
        //     auto& bufferType = compiler.get_type(pushConstant.base_type_id);
        //     std::cout << "Push Constant: " << pushConstant.name << "\n";
        //
        //     for (size_t i = 0; i < bufferType.member_types.size(); ++i) {
        //         std::string memberName = compiler.get_member_name(pushConstant.base_type_id, i);
        //         std::cout << "  Member: " << memberName
        //                   << ", Offset: " << compiler.type_struct_member_offset(bufferType, i)
        //                   << "\n";
        //     }
        // }
        //
        //  List sampled images
        // for (const auto& sampledImage : resources.sampled_images) {
        //     std::cout << "Sampled Image: " << sampledImage.name << "\n";
        // }

        // Other resources can be queried similarly
    } catch (const std::exception& e) {
        std::cerr << "Reflection failed: " << e.what() << "\n";
    }
}



bool ShaderRenderPass::init(
    SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name
) {
    passname = std::move(name);

    if (!vertex_shader) {
        auto vertShader = AssetHandler::GetShader("assets/shaders/crtshader.vert");//, 0, 2, 0, 0);
        vertex_shader = vertShader.shader;
    }

    if (!fragment_shader) {
        Shader fragShader = AssetHandler::GetShader("assets/shaders/crtshader.frag");//, 1, 1, 0, 0);
        fragment_shader = fragShader.shader;
    }


    size_t fileSize = 0;
    void* fileData = SDL_LoadFile("assets/shaders/crtshader.vert.bin", &fileSize);

    std::vector<uint8_t> shaderCode(static_cast<uint8_t*>(fileData),
                                    static_cast<uint8_t*>(fileData) + fileSize);

    // Free the memory allocated by SDL_LoadFile
    SDL_free(fileData);

    loadUniformsFromShader(shaderCode);

    fs.texture = AssetHandler::GetTexture("assets/transparent_pixel.png");


    SDL_GPUColorTargetDescription color_target_description{
        .format = swapchain_texture_format,
        .blend_state =
            {
                .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .color_blend_op = SDL_GPU_BLENDOP_ADD,
                .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                .enable_blend = true,
            },
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
        .rasterizer_state =
            {
                .fill_mode = SDL_GPU_FILLMODE_FILL,
                .cull_mode = SDL_GPU_CULLMODE_NONE,
                .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
                .depth_bias_constant_factor = 0.0,
                .depth_bias_clamp = 0.0,
                .depth_bias_slope_factor = 0.0,
                .enable_depth_bias = false,
                .enable_depth_clip = false,
            },
        .multisample_state = {},
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
                .has_depth_stencil_target = false,
            },
        .props = 0,
    };
    m_pipeline = SDL_CreateGPUGraphicsPipeline(Window::GetDevice(), &pipeline_create_info);

    if (!m_pipeline) {
        throw std::runtime_error(Helpers::TextFormat("%s: failed to create graphics pipeline: %s", CURRENT_METHOD(), SDL_GetError()));
    }

    SDL_Log("%s: created graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());

    return true;
}

void ShaderRenderPass::render(
    SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
) {

    SDL_PushGPUDebugGroup(cmd_buffer, CURRENT_METHOD());

    SDL_GPUColorTargetInfo color_target_info{
        .texture = target_texture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = color_target_info_clear_color,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
    };

    fs.size      = {Window::GetWidth(), Window::GetHeight()};
    fs.transform = {
        .position = {0.f, 0.f}
    };

    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target_info, 1, nullptr);
    assert(render_pass);
    {
        SDL_BindGPUGraphicsPipeline(render_pass, m_pipeline);

        glm::mat4 z_index_matrix = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(0.0f, 0.0f, (float) Window::GetZIndex() / (float) INT32_MAX)// + (renderable.z_index * 10000)))
        );
        glm::mat4 size_matrix    = glm::scale(glm::mat4(1.0f), glm::vec3(Window::GetWidth(), Window::GetHeight(), 1.0f));

        if (Input::MouseButtonDown(SDL_BUTTON_LEFT)) {
            lastMousePos = Input::GetMousePosition();
        }

        uniformBuffer.setVariable("camera", camera);
        uniformBuffer.setVariable("model", fs.transform.to_matrix() * z_index_matrix * size_matrix);
        uniformBuffer.setVariable("flipped", glm::vec2(1.0, 1.0));
        uniformBuffer.setVariable("uv0", glm::vec2(1.0, 1.0));
        uniformBuffer.setVariable("uv1", glm::vec2(0.0, 1.0));
        uniformBuffer.setVariable("uv2", glm::vec2(1.0, 0.0));
        uniformBuffer.setVariable("uv3", glm::vec2(0.0, 1.0));
        uniformBuffer.setVariable("uv4", glm::vec2(0.0, 0.0));
        uniformBuffer.setVariable("uv5", glm::vec2(1.0, 0.0));


        uniformBuffer.setVariable("iResolution", glm::vec3{(float)Window::GetWidth(), (float)Window::GetHeight(), 0.0f});
        uniformBuffer.setVariable("iTime", (float) Window::GetRunTime());
        uniformBuffer.setVariable("iTimeDelta", (float) (Window::GetFrameTime() * 1.0f));
        uniformBuffer.setVariable("iFrame", (float) _frameCounter);

        uniformBuffer.setVariable("iMouse", glm::vec4{Input::GetMousePosition().x, Input::GetMousePosition().y, lastMousePos.x, lastMousePos.y});

        SDL_PushGPUVertexUniformData(cmd_buffer, 0, uniformBuffer.getBufferPointer(), uniformBuffer.getBufferSize());
        auto texture_sampler_binding = SDL_GPUTextureSamplerBinding{
            .texture = fs.texture.gpuTexture,
            .sampler = fs.texture.gpuSampler,
        };

        SDL_BindGPUFragmentSamplers(render_pass, 0, &texture_sampler_binding, 1);
        SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
    }


    _frameCounter++;

    SDL_EndGPURenderPass(render_pass);

    SDL_PopGPUDebugGroup(cmd_buffer);
}
