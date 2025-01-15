#include "rendererhandler.h"

#include <stdexcept>
#include "audio/audiohandler.h"

#include "assethandler/assethandler.h"
#include "draw/drawhandler.h"

#include "utils/helpers.h"

#include "renderpass.h"
#include "spriterenderpass.h"
#include "shaderrenderpass.h"
#include "shaderhandler.h"

void Renderer::_initRendering() {

    bool enableGPUDebug = false;
    #ifdef LUMIDEBUG
    enableGPUDebug = true;
    #endif

    m_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, enableGPUDebug, nullptr);
    if (!m_device) {
        SDL_Log("%s: failed to create gpu device: %s", CURRENT_METHOD(), SDL_GetError());
        SDL_DestroyWindow(Window::GetWindow());
        return;
    }

    SDL_Log("%s: using graphics backend: %s", CURRENT_METHOD(), SDL_GetGPUDeviceDriver(m_device));

    if (!SDL_ClaimWindowForGPUDevice(m_device, Window::GetWindow())) {
        SDL_Log("%s: failed to claim window for gpu device: %s", CURRENT_METHOD(), SDL_GetError());
        return;
    }
    SDL_Log("%s: claimed window for gpu device", CURRENT_METHOD());

    Shaders::Init();

    _samplers[ScaleMode::NEAREST] = SDL_CreateGPUSampler(m_device, &GPUstructs::nearestSamplerCreateInfo);
    _samplers[ScaleMode::LINEAR]  = SDL_CreateGPUSampler(m_device, &GPUstructs::linearSamplerCreateInfo);

    m_camera = glm::ortho(0.0f, (float) Window::GetWidth(), float(Window::GetHeight()), 0.0f);

    fs = AssetHandler::CreateEmptyTexture({ 1,1 });

    SDL_GPUShaderCreateInfo rttVertexShaderInfo = {
        .code_size = SpriteRenderPass::sprite_vert_bin_len,
        .code = SpriteRenderPass::sprite_vert_bin,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 2,
    };

    SDL_GPUShaderCreateInfo rttFragmentShaderInfo = {
        .code_size = SpriteRenderPass::sprite_frag_bin_len,
        .code = SpriteRenderPass::sprite_frag_bin,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 1,
    };

    rtt_vertex_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &rttVertexShaderInfo);
    rtt_fragment_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &rttFragmentShaderInfo);

    auto *framebuffer = new FrameBuffer;

    framebuffer->renderpasses.emplace_back("2dsprites", new SpriteRenderPass(m_device));
    framebuffer->renderpasses.begin()->second->color_target_info_loadop      = SDL_GPU_LOADOP_CLEAR;
    framebuffer->renderpasses.begin()->second->color_target_info_clear_color = SDL_FColor(BLACK);

    framebuffer->fbContent = AssetHandler::CreateEmptyTexture(Window::GetSize()).gpuTexture;
    frameBuffers.emplace_back("primaryFramebuffer", framebuffer);

    for (auto &[_fbName, _framebuffer]: frameBuffers) {

        SDL_SetGPUTextureName(Renderer::GetDevice(), _framebuffer->fbContent, Helpers::TextFormat("Renderer: framebuffer %s", _fbName.c_str()));
        for (auto &[passname, renderpass]: _framebuffer->renderpasses) {
            if (!renderpass->init(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow()), Window::GetWidth(), Window::GetHeight(),
                                  passname)) {
                SDL_Log("%s: renderpass (%s) failed to init()", CURRENT_METHOD(), passname.c_str());
            }
        }
    }

    #ifdef ADD_IMGUI
    ImGui_ImplSDL3_InitForOther(Window::GetWindow());
    ImGui_ImplSDLGPU3_Init(m_device, SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow()));
    #endif
}

void Renderer::_close() {
}

void Renderer::_onResize() {
    m_camera = glm::ortho(0.0f, (float) Window::GetWidth(), float(Window::GetHeight()), 0.0f);

    for (auto &[_fbName, _framebuffer]: frameBuffers) {
        SDL_ReleaseGPUTexture(m_device, _framebuffer->fbContent);
        _framebuffer->fbContent = AssetHandler::CreateEmptyTexture(Window::GetSize()).gpuTexture;
        SDL_SetGPUTextureName(Renderer::GetDevice(), _framebuffer->fbContent, Helpers::TextFormat("Renderer: framebuffer %s", _fbName.c_str()));
    }
}

void Renderer::_clearBackground(Color color) {
    //    m_cmdbuf = SDL_AcquireGPUCommandBuffer(m_device);
    //
    //    SDL_GPUTexture *swapchain_texture = nullptr;
    //
    //    if (!SDL_AcquireGPUSwapchainTexture(m_cmdbuf, Window::GetWindow(), &swapchain_texture, nullptr, nullptr)) {
    //        SDL_Log("Renderer::render: failed to acquire gpu swapchain texture: %s", SDL_GetError());
    //        return;
    //    }
    //
    //    SDL_GPUColorTargetInfo color_target_info{
    //        .texture = swapchain_texture,
    //        .mip_level = 0,
    //        .layer_or_depth_plane = 0,
    //        .clear_color = {.r = color.getRFloat(), .g = color.getGFloat(), .b = color.getBFloat(), .a = color.getAFloat()},
    //        .load_op = SDL_GPU_LOADOP_CLEAR,
    //        .store_op = SDL_GPU_STOREOP_STORE,
    //    };
    //
    //    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(m_cmdbuf, &color_target_info, 1, nullptr);
    //
    //    SDL_EndGPURenderPass(render_pass);
}

void Renderer::_startFrame() const {

#ifdef ADD_IMGUI
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
#endif
}

void Renderer::_endFrame() {

    m_cmdbuf = SDL_AcquireGPUCommandBuffer(m_device);
    if (!m_cmdbuf) {
        SDL_Log("Renderer::StartFrame: failed to acquire gpu command buffer: %s", SDL_GetError());
        return;
    }

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(m_cmdbuf, Window::GetWindow(), &swapchain_texture, nullptr, nullptr)) {
        SDL_Log("Renderer::render: failed to acquire gpu swapchain texture: %s", SDL_GetError());
        return;
    }

    if (swapchain_texture) {

        SDL_SetGPUTextureName(Renderer::GetDevice(), swapchain_texture, "Renderer: swapchain_texture");

        for (auto &[fbName, framebuffer]: frameBuffers) {
            for (auto &[passname, renderpass]: framebuffer->renderpasses) {
                renderpass->render(m_cmdbuf, framebuffer->fbContent, m_camera);
            }
        }

        renderFrameBuffer(m_cmdbuf);

#ifdef ADD_IMGUI

        #ifdef LUMIDEBUG
        SDL_PushGPUDebugGroup(m_cmdbuf, "[Lumi] ImGuiRenderPass::render");
        #endif
        ImGui::Render();
        {
            auto copy_pass = SDL_BeginGPUCopyPass(m_cmdbuf);
            ImGui_ImplSDLGPU3_UploadDrawData(ImGui::GetDrawData(), copy_pass);
            SDL_EndGPUCopyPass(copy_pass);
        }
        {
            SDL_GPUColorTargetInfo color_target_info = {};
            color_target_info.texture              = swapchain_texture;
            color_target_info.mip_level            = 0;
            color_target_info.layer_or_depth_plane = 0;
            color_target_info.clear_color          = {0.25f, 0.25f, 0.25f, 0.0f};
            color_target_info.load_op              = SDL_GPU_LOADOP_LOAD;
            color_target_info.store_op             = SDL_GPU_STOREOP_STORE;

            auto render_pass = SDL_BeginGPURenderPass(m_cmdbuf, &color_target_info, 1, nullptr);

            ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), m_cmdbuf, render_pass);

            SDL_EndGPURenderPass(render_pass);
        }
        #ifdef LUMIDEBUG
        SDL_PopGPUDebugGroup(m_cmdbuf);
        #endif
#endif
    } else {
        // don't have a swapchain. just end imgui
        #ifdef ADD_IMGUI
        ImGui::EndFrame();
        #endif
    }

    SDL_SubmitGPUCommandBuffer(m_cmdbuf);

    for (auto &[fbName, framebuffer]: frameBuffers) {
        for (auto &[passname, renderpass]: framebuffer->renderpasses) {
            renderpass->resetRenderQueue();
        }
    }
    m_cmdbuf = nullptr;
}

void Renderer::_reset() {
    for (auto &[fbName, framebuffer]: frameBuffers) {
        for (auto &[passname, renderpass]: framebuffer->renderpasses) {

            renderpass->release();

            SDL_WaitForGPUIdle(m_device);

            if (!renderpass->init(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow()), Window::GetWidth(), Window::GetHeight(),
                                  passname)) {
                SDL_Log("%s: renderpass (%s) failed to init()", CURRENT_METHOD(), passname.c_str());
            }
        }
    }
}

SDL_GPUDevice *Renderer::_getDevice() {
    return m_device;
}

void Renderer::_addToRenderQueue(const std::string &passname, const Renderable &renderable) {
    for (auto &[fbName, framebuffer]: frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
                               [&passname](const std::pair<std::string, RenderPass *> &entry) {
                                   return entry.first == passname;
                               });

        if (it != framebuffer->renderpasses.end()) {
            it->second->addToRenderQueue(renderable);
        }
    }
}

void Renderer::_addShaderPass(const std::string &passname, const ShaderAsset &vertShader, const ShaderAsset &fragShader,
                              std::vector<std::string> targetBuffers) {

    auto shaderPass = new ShaderRenderPass(m_device);

    shaderPass->vertShader = vertShader;
    shaderPass->fragShader = fragShader;

    bool succes = shaderPass->init(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow()), Window::GetWidth(), Window::GetHeight(),
                                   passname);
    if (succes) {
        if (targetBuffers.empty()) {
            targetBuffers.emplace_back("primaryFramebuffer");
        }

        for (auto buffername: targetBuffers) {
            auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(),
                                   [&buffername](const std::pair<std::string, FrameBuffer *> &entry) {
                                       return entry.first == buffername;
                                   });

            if (it != frameBuffers.end()) {
                it->second->renderpasses.emplace_back(passname, shaderPass);
            }
        }
    } else {
        SDL_Log("%s: failed to create shaderpass: %s", CURRENT_METHOD(), passname.c_str());
    }
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCode"

UniformBuffer &Renderer::_getUniformBuffer(const std::string &passname) {
    for (auto &[fbName, framebuffer]: frameBuffers) {

        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
                               [&passname](const std::pair<std::string, RenderPass *> &entry) {
                                   return entry.first == passname;
                               });

        if (it != framebuffer->renderpasses.end()) {
            return it->second->getUniformBuffer();
        }
    }

    //this section of code should never be hit because every renderpass has a buffer attached to it
    assert(false && "UniformBuffer not found");
    static UniformBuffer dummyBuffer;
    return dummyBuffer;
}

#pragma clang diagnostic pop

void Renderer::renderFrameBuffer(SDL_GPUCommandBuffer *cmd_buffer) {



    auto swapchain_texture_format = SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow());

    SDL_GPUColorTargetDescription color_target_descriptions = {
        .format = swapchain_texture_format,
        .blend_state = GPUstructs::defaultBlendState,
    };

    SDL_GPUGraphicsPipelineCreateInfo rtt_pipeline_create_info{
        .vertex_shader = rtt_vertex_shader,
        .fragment_shader = rtt_fragment_shader,
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
                .color_target_descriptions = &color_target_descriptions,
                .num_color_targets = 1,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                .has_depth_stencil_target = false,
            },
        .props = 0,
    };

    m_rendertotexturepipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &rtt_pipeline_create_info);

    auto *framebuffer = Renderer::GetFramebuffer("primaryFramebuffer")->fbContent;

    SDL_GPUColorTargetInfo sdlGpuColorTargetInfo = {
        .texture = swapchain_texture,
        .clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };

    SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(cmd_buffer, &sdlGpuColorTargetInfo, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(renderPass, m_rendertotexturepipeline);

    Renderable renderable = {
        .texture = fs,
        .size = glm::vec2(Window::GetWidth(), Window::GetHeight()),
        .transform = {
            .position = glm::vec2(0.0, 0.0),
        },
    };

    glm::mat4 z_index_matrix = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 0.0f, 0.0f)// + (renderable.z_index * 10000)))
    );
    glm::mat4 size_matrix    = glm::scale(glm::mat4(1.0f), glm::vec3(renderable.size, 1.0f));

    Uniforms rtt_uniforms{
        .camera = m_camera,
        .model = renderable.transform.to_matrix() * z_index_matrix * size_matrix,
        .flipped = glm::vec2(1.0, 1.0),
    };

    SDL_PushGPUVertexUniformData(cmd_buffer, 0, &rtt_uniforms, sizeof(rtt_uniforms));
    auto rtt_texture_sampler_binding = SDL_GPUTextureSamplerBinding{
        .texture = framebuffer,
        .sampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode()),
    };
    SDL_BindGPUFragmentSamplers(renderPass, 0, &rtt_texture_sampler_binding, 1);
    SDL_DrawGPUPrimitives(renderPass, 6, 1, 0, 0);
    SDL_EndGPURenderPass(renderPass);
    SDL_ReleaseGPUGraphicsPipeline(m_device, m_rendertotexturepipeline);

}

FrameBuffer *Renderer::_getFramebuffer(std::string fbname) {
    auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(), [&fbname](const auto &pair) {
        return pair.first == fbname;
    });

    FrameBuffer *framebuffer = nullptr;

    if (it != frameBuffers.end()) {
        framebuffer = it->second;
    }

    return framebuffer;
}

void Renderer::_createFrameBuffer(const std::string &fbname) {
    auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(), [&fbname](const auto &pair) {
        return pair.first == fbname;
    });

    if (it == frameBuffers.end()) {
        auto *framebuffer = new FrameBuffer;
        framebuffer->fbContent = AssetHandler::CreateEmptyTexture(Window::GetSize()).gpuTexture;
        frameBuffers.emplace_back(fbname, framebuffer);

        SDL_Log("%s: created framebuffer: %s", CURRENT_METHOD(), fbname.c_str());
    }
}

void Renderer::_attachRenderPassToFrameBuffer(RenderPass *renderPass, const std::string &passname, const std::string &fbName) {
    auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(), [&fbName](const auto &pair) {
        return pair.first == fbName;
    });

    if (it != frameBuffers.end()) {
        it->second->renderpasses.emplace_back(passname, renderPass);

        SDL_Log("%s: attached renderpass %s to framebuffer: %s", CURRENT_METHOD(), passname.c_str(), fbName.c_str());
    }
}

SDL_GPUSampler *Renderer::_getSampler(ScaleMode scaleMode) {
    return _samplers[scaleMode];
}