#include "spriterenderpass.h"

#include <utility>

#include "SDL3/SDL_gpu.h"

void SpriteRenderPass::release() {
    m_depth_texture.release(Renderer::GetDevice());
    SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), m_pipeline);
    SDL_Log("%s: released graphics pipeline: %s", CURRENT_METHOD(), passname.c_str());
}

bool SpriteRenderPass::init(
    SDL_GPUTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name
) {
    passname = std::move(name);

    m_depth_texture = AssetHandler::CreateDepthTarget(Renderer::GetDevice(), surface_width, surface_height);

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
                .has_depth_stencil_target = true,
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

void SpriteRenderPass::render(
    SDL_GPUCommandBuffer *cmd_buffer, SDL_GPUTexture *target_texture, const glm::mat4 &camera
) {
    #ifdef LUMIDEBUG
    SDL_PushGPUDebugGroup(cmd_buffer, CURRENT_METHOD());
    #endif
    SDL_GPUColorTargetInfo color_target_info{
        .texture = target_texture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = color_target_info_clear_color,
        .load_op = color_target_info_loadop,
        .store_op = SDL_GPU_STOREOP_STORE,
        .cycle = false
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
            if (renderable.transform.position.x > (float)Window::GetWidth() || renderable.transform.position.y > (float)Window::GetHeight() ||
            renderable.transform.position.x + (float)renderable.size.x < 0.f || renderable.transform.position.y + (float)renderable.size.y < 0.f) continue;

            Uniforms uniforms{
                .camera = camera,
                .model = renderable.model,
                .flipped = renderable.flipped,
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
    #ifdef LUMIDEBUG
    SDL_PopGPUDebugGroup(cmd_buffer);
    #endif
}

void SpriteRenderPass::createShaders() {
    SDL_GPUShaderCreateInfo vertexShaderInfo = {
        .code_size = sprite_vert_bin_len,
        .code = sprite_vert_bin,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 2,
    };

    vertex_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &vertexShaderInfo);

    if (!vertex_shader) {
        throw std::runtime_error(
            Helpers::TextFormat("%s: failed to create vertex shader for: %s (%s)", CURRENT_METHOD(), passname.c_str(), SDL_GetError()));
    }

    SDL_GPUShaderCreateInfo fragmentShaderInfo = {
        .code_size = sprite_frag_bin_len,
        .code = sprite_frag_bin,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 1,
    };

    fragment_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &fragmentShaderInfo);

    if (!fragment_shader) {
        throw std::runtime_error(
            Helpers::TextFormat("%s: failed to create fragment shader for: %s (%s)", CURRENT_METHOD(), passname.c_str(), SDL_GetError()));
    }
}
