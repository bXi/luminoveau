#include "spriterenderpass.h"

#include <utility>

#include "SDL3/SDL_gpu.h"

void SpriteRenderPass::release() {
    m_depth_texture.release(Window::GetDevice());
    SDL_ReleaseGPUGraphicsPipeline(Window::GetDevice(), m_pipeline);
    SDL_Log("%s: released graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());
}

bool SpriteRenderPass::init(
    SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name
) {
    passname = std::move(name);

    m_depth_texture = AssetHandler::CreateDepthTarget(Window::GetDevice(), surface_width, surface_height);

    auto vertShader = AssetHandler::GetShader("assets/shaders/sprite.vert", 0, 2, 0, 0);
    auto fragShader = AssetHandler::GetShader("assets/shaders/sprite.frag", 1, 1, 0, 0);

    SDL_GPUShader *vertex_shader   = vertShader.shader;
    SDL_GPUShader *fragment_shader = fragShader.shader;

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
                .padding1 = 0,
                .padding2 = 0,
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
                .padding1 = 0,
                .padding2 = 0,
                .padding3 = 0,
            },
        .target_info =
            {
                .color_target_descriptions = &color_target_description,
                .num_color_targets = 1,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                .has_depth_stencil_target = true,
                .padding1 = 0,
                .padding2 = 0,
                .padding3 = 0,
            },
        .props = 0,
    };
    m_pipeline = SDL_CreateGPUGraphicsPipeline(Window::GetDevice(), &pipeline_create_info);

    if (!m_pipeline) {
        throw std::runtime_error(Helpers::TextFormat("%s: failed to create graphics pipeline: %s", CURRENT_METHOD(), SDL_GetError()));
    }

    SDL_Log("%s: created graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());

    SDL_ReleaseGPUShader(Window::GetDevice(), vertex_shader);
    SDL_ReleaseGPUShader(Window::GetDevice(), fragment_shader);

    return true;
}

void SpriteRenderPass::render(
    SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
) {


    SDL_PushGPUDebugGroup(cmd_buffer, CURRENT_METHOD());

    SDL_GPUColorTargetInfo color_target_info{
        .texture = target_texture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = color_target_info_clear_color,
        .load_op = color_target_info_loadop,
        .store_op = SDL_GPU_STOREOP_STORE,

    };

    SDL_GPUDepthStencilTargetInfo depth_stencil_info{
        .texture = m_depth_texture.gpuTexture,
        .clear_depth = 1.0f,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
    };

    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target_info, 1, &depth_stencil_info);
    assert(render_pass);
    {
        SDL_BindGPUGraphicsPipeline(render_pass, m_pipeline);

        for (const auto &renderable: renderQueue) {
            glm::mat4 z_index_matrix = glm::translate(
                glm::mat4(1.0f),
                //TODO: fix Zindex
                glm::vec3(0.0f, 0.0f, (float) Window::GetZIndex() / (float) INT32_MAX)// + (renderable.z_index * 10000)))
            );
            glm::mat4 size_matrix    = glm::scale(glm::mat4(1.0f), glm::vec3(renderable.size, 1.0f));

            Uniforms uniforms{
                .camera = camera,
                .model = renderable.transform.to_matrix() * z_index_matrix * size_matrix,
                .flipped = glm::vec2(
                    renderable.flipped_horizontally ? -1.0 : 1.0,
                    renderable.flipped_vertically ? -1.0 : 1.0
                ),
                .uv0 = renderable.uv[0],
                .uv1 = renderable.uv[1],
                .uv2 = renderable.uv[2],
                .uv3 = renderable.uv[3],
                .uv4 = renderable.uv[4],
                .uv5 = renderable.uv[5],
                .tintColorR = renderable.tintColor.getRFloat(),
                .tintColorG = renderable.tintColor.getGFloat(),
                .tintColorB = renderable.tintColor.getBFloat(),
                .tintColorA = renderable.tintColor.getAFloat(),
            };

            SDL_PushGPUVertexUniformData(cmd_buffer, 0, &uniforms, sizeof(uniforms));
            auto texture_sampler_binding = SDL_GPUTextureSamplerBinding{
                .texture = renderable.texture.gpuTexture,
                .sampler = renderable.texture.gpuSampler,
            };

            SDL_BindGPUFragmentSamplers(render_pass, 0, &texture_sampler_binding, 1);
            SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
        }
    }
    SDL_EndGPURenderPass(render_pass);

    SDL_PopGPUDebugGroup(cmd_buffer);

}
