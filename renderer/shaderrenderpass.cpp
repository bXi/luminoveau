#include "shaderrenderpass.h"

#include <utility>

#include "SDL3/SDL_gpu.h"
#include "spriterenderpass.h"

void ShaderRenderPass::release() {
    m_depth_texture.release(Renderer::GetDevice());
    SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), m_pipeline);
    SDL_Log("%s: released graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());
}

void ShaderRenderPass::loadUniformsFromShader(const std::vector<uint8_t> &spirvBinary) {

    try {
        spirv_cross::Compiler compiler(reinterpret_cast<const uint32_t *>(spirvBinary.data()),
                                       spirvBinary.size() / sizeof(uint32_t));

        auto resources = compiler.get_shader_resources();

        for (const auto &uniform: resources.uniform_buffers) {
            auto &bufferType = compiler.get_type(uniform.base_type_id);

            for (size_t i = 0; i < bufferType.member_types.size(); ++i) {
                const auto        &memberType = compiler.get_type(bufferType.member_types[i]);
                const std::string &memberName = compiler.get_member_name(uniform.base_type_id, i);

                size_t      memberSize = compiler.get_declared_struct_member_size(bufferType, i);

                uniformBuffer.addVariable(memberName, memberSize, compiler.type_struct_member_offset(bufferType, i));
            }
        }
    } catch (const std::exception &e) {
        throw std::runtime_error(Helpers::TextFormat("%s: Reflection failed: %s", CURRENT_METHOD(), e.what()));
    }
}

bool ShaderRenderPass::init(
    SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name
) {
    passname        = std::move(name);

    if (!vertShader.shader) {
        vertShader = AssetHandler::GetShader("assets/shaders/crtshader.vert");
    }

    if (!fragShader.shader) {
        fragShader = AssetHandler::GetShader("assets/shaders/crtshader.frag");
    }
    vertex_shader   = vertShader.shader;
    fragment_shader = fragShader.shader;

    loadUniformsFromShader(vertShader.fileData);

    fs.texture   = AssetHandler::GetTexture("assets/transparent_pixel.png");
    fs.transform = {
        .position = {0.f, 0.f}
    };
    fs.tintColor = WHITE;

    Uint32 color_target_description_amount = fragShader.samplerCount;

    std::vector<SDL_GPUColorTargetDescription> color_target_descriptions(color_target_description_amount, SDL_GPUColorTargetDescription{
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
                .color_target_descriptions = color_target_descriptions.data(),
                .num_color_targets = color_target_description_amount,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                .has_depth_stencil_target = false,
            },
        .props = 0,
    };
    m_pipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &pipeline_create_info);

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

    std::vector<SDL_GPUColorTargetInfo> color_target_info(fragShader.samplerCount, SDL_GPUColorTargetInfo{
        .texture = target_texture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = color_target_info_clear_color,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
    });

    fs.size = {Window::GetWidth(), Window::GetHeight()};

    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(cmd_buffer, color_target_info.data(), fragShader.samplerCount, nullptr);
    assert(render_pass);
    {
        SDL_BindGPUGraphicsPipeline(render_pass, m_pipeline);

        glm::mat4 z_index_matrix = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(0.0f, 0.0f, (float) Renderer::GetZIndex() / (float) INT32_MAX)// + (renderable.z_index * 10000)))
        );
        glm::mat4 size_matrix    = glm::scale(glm::mat4(1.0f), glm::vec3(Window::GetWidth(), Window::GetHeight(), 1.0f));

        if (Input::MouseButtonDown(SDL_BUTTON_LEFT)) {
            lastMousePos = Input::GetMousePosition();
        }

        uniformBuffer["camera"] = camera;

        uniformBuffer["model"]   = fs.transform.to_matrix() * z_index_matrix * size_matrix;
        uniformBuffer["flipped"] = glm::vec2(1.0, 1.0);
        uniformBuffer["uv"]      = std::array<glm::vec2, 6>{
            glm::vec2(1.0, 1.0),
            glm::vec2(0.0, 1.0),
            glm::vec2(1.0, 0.0),
            glm::vec2(0.0, 1.0),
            glm::vec2(0.0, 0.0),
            glm::vec2(1.0, 0.0),
        };

        uniformBuffer["tintColor"] = fs.tintColor.asVec4();

        uniformBuffer["iResolution"] = glm::vec3{(float) Window::GetWidth(), (float) Window::GetHeight(), 0.0f};
        uniformBuffer["iTime"]       = (float) Window::GetRunTime();
        uniformBuffer["iTimeDelta"]  = (float) (Window::GetFrameTime() * 1.0f);
        uniformBuffer["iFrame"]      = (float) _frameCounter;

        uniformBuffer["iMouse"] = glm::vec4{Input::GetMousePosition().x, Input::GetMousePosition().y, lastMousePos.x, lastMousePos.y};

        SDL_PushGPUVertexUniformData(cmd_buffer, 0, uniformBuffer.getBufferPointer(), uniformBuffer.getBufferSize());

        std::vector<SDL_GPUTextureSamplerBinding> texture_sampler_bindings(fragShader.samplerCount, SDL_GPUTextureSamplerBinding{
            .texture = fs.texture.gpuTexture,
            .sampler = fs.texture.gpuSampler,
        });

        auto framebuffer = Renderer::GetFramebuffer("primaryFramebuffer");

        texture_sampler_bindings[0].texture = framebuffer->fbContent;
        SDL_BindGPUFragmentSamplers(render_pass, 0, texture_sampler_bindings.data(), fragShader.samplerCount);
        SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
    }

    _frameCounter++;

    SDL_EndGPURenderPass(render_pass);

    SDL_PopGPUDebugGroup(cmd_buffer);
}

UniformBuffer &ShaderRenderPass::getUniformBuffer() {
    return uniformBuffer;
}