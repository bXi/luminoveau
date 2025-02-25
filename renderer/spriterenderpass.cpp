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

        const int thread_count = thread_pool.workers.size();
        std::vector<std::vector<PreppedSprite>> thread_prepped(thread_count);
        float w = (float)Window::GetWidth(), h = (float)Window::GetHeight();
        size_t chunk_size = (renderQueue.size() + thread_count - 1) / thread_count;

        for (int i = 0; i < thread_count; ++i) {
            size_t start = i * chunk_size;
            size_t end = std::min(start + chunk_size, renderQueue.size());
            thread_pool.enqueue([this, start, end, &thread_prepped, i, w, h, &camera]() {
                PrepSprites(this->renderQueue, start, end, thread_prepped[i], w, h, camera);
            });
        }

        thread_pool.wait_all(); // Wait for all tasks

        std::vector<PreppedSprite> final_prepped;
        final_prepped.reserve(renderQueue.size());
        for (auto& tp : thread_prepped) {
            final_prepped.insert(final_prepped.end(), tp.begin(), tp.end());
        }
        // Tight submit loop
        SDL_GPUTexture *last_texture = nullptr;
        for (const auto& p : final_prepped) {
            SDL_PushGPUVertexUniformData(cmd_buffer, 0, &p.uniforms, sizeof(Uniforms));
            if (p.texture != last_texture) {
                SDL_GPUTextureSamplerBinding binding = { p.texture, p.sampler };
                SDL_BindGPUFragmentSamplers(render_pass, 0, &binding, 1);
                last_texture = p.texture;
            }
            SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
        }
    }
    SDL_EndGPURenderPass(render_pass);
    #ifdef LUMIDEBUG
    SDL_PopGPUDebugGroup(cmd_buffer);
    #endif
}




void SpriteRenderPass::PrepSprites(const std::vector<Renderable>& _renderQueue, size_t start, size_t end,
                 std::vector<PreppedSprite>& prepped, float w, float h, const glm::mat4& camera) {
    prepped.resize(end - start); // Pre-size to avoid push_back overhead
    size_t idx = 0;
    for (size_t i = start; i < end; ++i) {
        const auto& r = _renderQueue[i];
        bool visible = !(r.transform.position.x > w || r.transform.position.y > h ||
                         r.transform.position.x + r.size.x < 0.f || r.transform.position.y + r.size.y < 0.f);
        if (!visible) continue;

        prepped[idx] = {
            .uniforms = {
                .camera = camera,
                .model = r.model,
                .flipped = r.flipped,
                .uv0 = r.uv[0], .uv1 = r.uv[1], .uv2 = r.uv[2],
                .uv3 = r.uv[3], .uv4 = r.uv[4], .uv5 = r.uv[5],
                .tintColorR = static_cast<float>(r.tintColor.r) / 255.0f,
                .tintColorG = static_cast<float>(r.tintColor.g) / 255.0f,
                .tintColorB = static_cast<float>(r.tintColor.b) / 255.0f,
                .tintColorA = static_cast<float>(r.tintColor.a) / 255.0f
            },
            .texture = r.texture.gpuTexture,
            .sampler = r.texture.gpuSampler
        };
        idx++;
    }
    prepped.resize(idx); // Shrink to actual visible count
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
