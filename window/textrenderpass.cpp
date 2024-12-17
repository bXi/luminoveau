#include "textrenderpass.h"

#include <utility>

#include "SDL3/SDL_gpu.h"
#include <array>

void TextRenderPass::release() {
    m_depth_texture.release(Window::GetDevice());
    SDL_ReleaseGPUGraphicsPipeline(Window::GetDevice(), m_pipeline);
    SDL_Log("%s: released graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());
}

bool TextRenderPass::init(
    SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name
) {
    passname = std::move(name);

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

    SDL_GPUVertexBufferDescription vertex_buffer_description = {
        .slot = 0,
        .pitch = sizeof(RenderableVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };

    SDL_GPUVertexAttribute vertex_attributes[] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0},
        {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = sizeof(float) * 3},
        {.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = sizeof(float) * 7}
    };

    SDL_GPUVertexInputState vertex_input_state = {
        .vertex_buffer_descriptions = &vertex_buffer_description,
        .num_vertex_buffers = 1,
        .vertex_attributes = vertex_attributes,
        .num_vertex_attributes = 3,
    };

    SDL_GPUGraphicsPipelineCreateInfo pipeline_create_info = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state = vertex_input_state,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info = {
            .color_target_descriptions = &color_target_description,
            .num_color_targets = 1,
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_INVALID,
            .has_depth_stencil_target = false,
        },
    };

    m_pipeline = SDL_CreateGPUGraphicsPipeline(Window::GetDevice(), &pipeline_create_info);

    if (!m_pipeline) {
        throw std::runtime_error(Helpers::TextFormat("%s: failed to create graphics pipeline: %s", CURRENT_METHOD(), SDL_GetError()));
    }

    SDL_Log("%s: created graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());

    SDL_ReleaseGPUShader(Window::GetDevice(), vertex_shader);
    SDL_ReleaseGPUShader(Window::GetDevice(), fragment_shader);

    SDL_GPUBufferCreateInfo vbf_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(RenderableVertex) * 6000
    };
    vertex_buffer = SDL_CreateGPUBuffer(m_gpu_device, &vbf_info);

    SDL_GPUBufferCreateInfo ibf_info = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = sizeof(int) * 4000
    };
    index_buffer = SDL_CreateGPUBuffer(m_gpu_device, &ibf_info);

    SDL_GPUTransferBufferCreateInfo tbf_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (sizeof(RenderableVertex) * 6000) + (sizeof(int) * 4000)
    };
    m_transbuf = SDL_CreateGPUTransferBuffer(m_gpu_device, &tbf_info);

    return true;
}

void TextRenderPass::render(
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

    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target_info, 1, nullptr);
    assert(render_pass);
    {
        SDL_BindGPUGraphicsPipeline(render_pass, m_pipeline);

        SDL_GPUBufferBinding vertexBufferBinding{
            .buffer = vertex_buffer, .offset = 0
        };

        SDL_GPUBufferBinding indexBufferBinding{
            .buffer = index_buffer, .offset = 0
        };

        SDL_BindGPUVertexBuffers(render_pass, 0, &vertexBufferBinding, 1);
        SDL_BindGPUIndexBuffer(render_pass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

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
