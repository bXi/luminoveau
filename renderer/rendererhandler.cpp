#include "rendererhandler.h"

#include <stdexcept>
#include "audio/audiohandler.h"

#include "assethandler/assethandler.h"
#include "assethandler/shaders_generated.h"
#include "draw/drawhandler.h"

#include "utils/helpers.h"
#include "log/loghandler.h"

#include "renderpass.h"
#include "spriterenderpass.h"
#include "model3drenderpass.h"
#include "shaderrenderpass.h"
#include "shaderhandler.h"

void Renderer::_initRendering() {

    bool enableGPUDebug = false;
    #ifdef LUMIDEBUG
    SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_VERBOSE);
    enableGPUDebug = true;
    #endif

    // Select shader format and driver based on build configuration
    SDL_GPUShaderFormat shaderFormat;
    const char* preferredDriver = nullptr;
    
    // Create device with explicit driver selection
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, enableGPUDebug);
    
    #if defined(__ANDROID__)
        // Android: Force Vulkan (our shaders are SPIRV only)
        shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
        preferredDriver = "vulkan";  // Force Vulkan - OpenGL ES won't work!
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        
        // Disable optional Vulkan features for broader Android device compatibility
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);

        LOG_INFO("Using SPIR-V shaders (Android - Vulkan with reduced features)");
    #elif defined(LUMINOVEAU_SHADER_BACKEND_DXIL)
        // Use DXIL SM6.0 for modern DirectX 12 (PC and Xbox One S)
        shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;  
        preferredDriver = "direct3d12";  // Force DirectX 12
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
        LOG_INFO("Using DXIL shaders (DirectX 12 SM6.0)");
    #elif defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        shaderFormat = SDL_GPU_SHADERFORMAT_METALLIB;
        preferredDriver = "metal";  // Force Metal
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_METALLIB_BOOLEAN, true);
        LOG_INFO("Using Metal shaders");
    #else
        // Default to SPIR-V (Vulkan)
        shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
        preferredDriver = "vulkan";  // Force Vulkan
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        LOG_INFO("Using SPIR-V shaders (Vulkan)");
    #endif
    
    SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, preferredDriver);
    
    m_device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    if (!m_device) {
        LOG_ERROR("Failed to create GPU device: {}", SDL_GetError());
        SDL_DestroyWindow(Window::GetWindow());
        return;
    }

    LOG_INFO("Using graphics backend: {}", SDL_GetGPUDeviceDriver(m_device));

    if (!SDL_ClaimWindowForGPUDevice(m_device, Window::GetWindow())) {
        LOG_ERROR("Failed to claim window for GPU device: {}", SDL_GetError());
        return;
    }
    LOG_INFO("Claimed window for GPU device");

    SDL_SetGPUAllowedFramesInFlight(m_device, 1);

    Shaders::Init();

    _samplers[ScaleMode::NEAREST] = SDL_CreateGPUSampler(m_device, &GPUstructs::nearestSamplerCreateInfo);
    _samplers[ScaleMode::LINEAR]  = SDL_CreateGPUSampler(m_device, &GPUstructs::linearSamplerCreateInfo);

    m_camera = glm::ortho(0.0f, (float) Window::GetWidth(), float(Window::GetHeight()), 0.0f);

    fs = AssetHandler::CreateEmptyTexture({1, 1});

    SDL_GPUShaderCreateInfo rttVertexShaderInfo = {
        .code_size = Luminoveau::Shaders::FullscreenQuad_Vert_Size,
        .code = Luminoveau::Shaders::FullscreenQuad_Vert,
        .entrypoint = "main",
        .format = shaderFormat,  // Use selected format
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 1,  // CameraUniforms at space1
    };

    SDL_GPUShaderCreateInfo rttFragmentShaderInfo = {
        .code_size = Luminoveau::Shaders::FullscreenQuad_Frag_Size,
        .code = Luminoveau::Shaders::FullscreenQuad_Frag,
        .entrypoint = "main",
        .format = shaderFormat,  // Use selected format
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 0,  // No fragment uniforms
    };

    rtt_vertex_shader   = SDL_CreateGPUShader(Renderer::GetDevice(), &rttVertexShaderInfo);
    rtt_fragment_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &rttFragmentShaderInfo);

    if (!rtt_vertex_shader || !rtt_fragment_shader) {
        LOG_CRITICAL("Failed to create RTT shaders: {}", SDL_GetError());
        return;
    }
    LOG_INFO("RTT shaders created successfully");

    auto *framebuffer = new FrameBuffer;

    // Get the primary display's size for creating desktop-sized framebuffers
    SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(primaryDisplay);
    int desktopWidth = displayMode ? displayMode->w : 3840;  // Fallback to 4K if can't get display
    int desktopHeight = displayMode ? displayMode->h : 2160;
    
    LOG_INFO("Creating framebuffers at desktop size: {}x{}", desktopWidth, desktopHeight);

    // Add 3D model render pass (render first)
    framebuffer->renderpasses.emplace_back("3dmodels", new Model3DRenderPass(m_device));
    framebuffer->renderpasses.back().second->color_target_info_loadop = SDL_GPU_LOADOP_CLEAR; // Clear for 3D
    framebuffer->renderpasses.back().second->color_target_info_clear_color = SDL_FColor(0.f, 0.f, 0.f, 1.f);

    // Add 2D sprite pass (render on top of 3D)
    framebuffer->renderpasses.emplace_back("2dsprites", new SpriteRenderPass(m_device));
    framebuffer->renderpasses.back().second->color_target_info_loadop = SDL_GPU_LOADOP_LOAD; // Don't clear, render on top
    framebuffer->renderpasses.back().second->color_target_info_clear_color = SDL_FColor(0.f, 0.f, 0.f, 1.f);

    // Create framebuffer at desktop size, not window size
    framebuffer->fbContent = AssetHandler::CreateEmptyTexture({(float)desktopWidth, (float)desktopHeight}).gpuTexture;
    framebuffer->width = desktopWidth;
    framebuffer->height = desktopHeight;
    frameBuffers.emplace_back("primaryFramebuffer", framebuffer);

    for (auto &[_fbName, _framebuffer]: frameBuffers) {

        SDL_SetGPUTextureName(Renderer::GetDevice(), _framebuffer->fbContent, Helpers::TextFormat("Renderer: framebuffer %s", _fbName.c_str()));
        for (auto &[passname, renderpass]: _framebuffer->renderpasses) {
            // Initialize render passes at desktop size, not window size
            if (!renderpass->init(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow()), 
                                  _framebuffer->width, _framebuffer->height,
                                  passname)) {
                LOG_ERROR("Renderpass ({}) failed to init()", passname.c_str());
            }
        }
    }

    auto swapchain_texture_format = SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow());

    color_target_descriptions = {
        .format      = swapchain_texture_format,
        .blend_state = GPUstructs::defaultBlendState,
    };

    rtt_pipeline_create_info = {
        .vertex_shader   = rtt_vertex_shader,
        .fragment_shader = rtt_fragment_shader,
        .vertex_input_state =
            {
                .vertex_buffer_descriptions = nullptr,
                .num_vertex_buffers         = 0,
                .vertex_attributes          = nullptr,
                .num_vertex_attributes      = 0,
            },
        .primitive_type    = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state  = GPUstructs::defaultRasterizerState,
        .multisample_state = {},
        .depth_stencil_state =
            {
                .compare_op          = SDL_GPU_COMPAREOP_LESS,
                .back_stencil_state  = {},
                .front_stencil_state = {},
                .compare_mask        = 0,
                .write_mask          = 0,
                .enable_depth_test   = false,  // No depth for RTT quad
                .enable_depth_write  = false,
                .enable_stencil_test = false,
            },
        .target_info =
            {
                .color_target_descriptions = &color_target_descriptions,
                .num_color_targets         = 1,
                .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_INVALID,  // No depth!
                .has_depth_stencil_target  = false,
            },
        .props = 0,
    };

    m_rendertotexturepipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &rtt_pipeline_create_info);
    
    _whitePixelTexture = AssetHandler::CreateWhitePixel();

    _whiteCircleTexture = AssetHandler::GetTexture("assets/circle.png");
    _whiteCircleTexture.gpuSampler = GetSampler(ScaleMode::LINEAR);


    #ifdef LUMINOVEAU_WITH_IMGUI
    ImGui_ImplSDL3_InitForSDLGPU(Window::GetWindow());

    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = m_device;
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow());
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);
    #endif
}

void Renderer::_close() {
    // Shutdown SDL_shadercross
    Shaders::Quit();
}

void Renderer::_updateCameraProjection() {
    m_camera = glm::ortho(0.0f, (float) Window::GetWidth(),
                          float(Window::GetHeight()), 0.0f);
}

void Renderer::_onResize() {
    // Update camera projection
    _updateCameraProjection();
    
    // Reset render passes to recreate window-sized textures
    _reset();
}

void Renderer::_clearBackground(Color color) {
    LUMI_UNUSED(color);
    // can ignore this for now
}

void Renderer::_startFrame() const {

#ifdef LUMINOVEAU_WITH_IMGUI
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
#endif
}

void Renderer::_endFrame() {
    Draw::FlushPixels();
    Input::GetVirtualControls().Render(); //Draw as last thing
    m_cmdbuf = SDL_AcquireGPUCommandBuffer(m_device);
    if (!m_cmdbuf) {
        LOG_ERROR("Failed to acquire GPU command buffer: {}", SDL_GetError());
        return;
    }

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(m_cmdbuf, Window::GetWindow(), &swapchain_texture, nullptr, nullptr)) {
        LOG_ERROR("Failed to acquire GPU swapchain texture: {}", SDL_GetError());
        return;
    }

    if (swapchain_texture) {

        SDL_SetGPUTextureName(Renderer::GetDevice(), swapchain_texture, "Renderer: swapchain_texture");

        bool useMSAA = (currentSampleCount > SDL_GPU_SAMPLECOUNT_1);

        for (auto &[fbName, framebuffer]: frameBuffers) {
            // Render all passes to MSAA texture if enabled, otherwise directly to fbContent
            SDL_GPUTexture* renderTarget = useMSAA ? framebuffer->fbContentMSAA : framebuffer->fbContent;
            SDL_GPUTexture* depthTarget = useMSAA ? framebuffer->fbDepthMSAA : nullptr;
            
            // Render all passes
            for (size_t i = 0; i < framebuffer->renderpasses.size(); i++) {
                auto& [passname, renderpass] = framebuffer->renderpasses[i];
                
                // First pass should always clear, subsequent passes should load
                if (i == 0) {
                    renderpass->color_target_info_loadop = SDL_GPU_LOADOP_CLEAR;
                } else {
                    renderpass->color_target_info_loadop = SDL_GPU_LOADOP_LOAD;
                }
                
                // Pass shared depth target
                renderpass->renderTargetDepth = depthTarget;
                
                // Last pass should resolve MSAA to fbContent
                bool isLastPass = (i == framebuffer->renderpasses.size() - 1);
                renderpass->renderTargetResolve = (useMSAA && isLastPass) ? framebuffer->fbContent : nullptr;
                
                renderpass->render(m_cmdbuf, renderTarget, m_camera);
            }
        }

        renderFrameBuffer(m_cmdbuf);

#ifdef LUMINOVEAU_WITH_IMGUI

#ifdef LUMIDEBUG
        SDL_PushGPUDebugGroup(m_cmdbuf, "[Lumi] ImGuiRenderPass::render");
#endif
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        
        // IMPORTANT: PrepareDrawData must be called BEFORE the render pass
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, m_cmdbuf);
        
        {
            SDL_GPUColorTargetInfo color_target_info = {};
            color_target_info.texture              = swapchain_texture;
            color_target_info.mip_level            = 0;
            color_target_info.layer_or_depth_plane = 0;
            color_target_info.clear_color          = {0.25f, 0.25f, 0.25f, 0.0f};
            color_target_info.load_op              = SDL_GPU_LOADOP_LOAD;
            color_target_info.store_op             = SDL_GPU_STOREOP_STORE;

            auto render_pass = SDL_BeginGPURenderPass(m_cmdbuf, &color_target_info, 1, nullptr);

            ImGui_ImplSDLGPU3_RenderDrawData(draw_data, m_cmdbuf, render_pass);

            SDL_EndGPURenderPass(render_pass);
        }
#ifdef LUMIDEBUG
        SDL_PopGPUDebugGroup(m_cmdbuf);
#endif
#endif
    } else {
        // don't have a swapchain. just end imgui
        #ifdef LUMINOVEAU_WITH_IMGUI
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
    LOG_INFO("Resetting render passes with MSAA={}", static_cast<int>(currentSampleCount));
    
    // Get desktop size for render pass re-initialization
    SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(primaryDisplay);
    int desktopWidth = displayMode ? displayMode->w : 3840;
    int desktopHeight = displayMode ? displayMode->h : 2160;

    bool useMSAA = (currentSampleCount > SDL_GPU_SAMPLECOUNT_1);
    SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow());

    // Recreate framebuffer MSAA textures if needed
    for (auto &[fbName, framebuffer]: frameBuffers) {
        // Release old MSAA textures
        if (framebuffer->fbContentMSAA) {
            SDL_ReleaseGPUTexture(m_device, framebuffer->fbContentMSAA);
            framebuffer->fbContentMSAA = nullptr;
        }
        if (framebuffer->fbDepthMSAA) {
            SDL_ReleaseGPUTexture(m_device, framebuffer->fbDepthMSAA);
            framebuffer->fbDepthMSAA = nullptr;
        }

        // Create new MSAA textures if MSAA is enabled
        if (useMSAA) {
            SDL_GPUTextureCreateInfo msaaColorInfo = {
                .type = SDL_GPU_TEXTURETYPE_2D,
                .format = swapchainFormat,
                .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
                .width = static_cast<uint32_t>(desktopWidth),
                .height = static_cast<uint32_t>(desktopHeight),
                .layer_count_or_depth = 1,
                .num_levels = 1,
                .sample_count = currentSampleCount,
            };
            framebuffer->fbContentMSAA = SDL_CreateGPUTexture(m_device, &msaaColorInfo);

            SDL_GPUTextureCreateInfo msaaDepthInfo = {
                .type = SDL_GPU_TEXTURETYPE_2D,
                .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,  // Match 3D pass format
                .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                .width = static_cast<uint32_t>(desktopWidth),
                .height = static_cast<uint32_t>(desktopHeight),
                .layer_count_or_depth = 1,
                .num_levels = 1,
                .sample_count = currentSampleCount,
            };
            framebuffer->fbDepthMSAA = SDL_CreateGPUTexture(m_device, &msaaDepthInfo);
        }
    }

    for (auto &[fbName, framebuffer]: frameBuffers) {
        for (auto &[passname, renderpass]: framebuffer->renderpasses) {

            // Don't log during reset (this is called during window resize)
            renderpass->release(false);

            SDL_WaitForGPUIdle(m_device);

            bool initSuccess = renderpass->init(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow()), 
                                  desktopWidth, desktopHeight,
                                  passname, true);  // Force logging during reset
            if (!initSuccess) {
                LOG_ERROR("Renderpass ({}) failed to init()", passname.c_str());
            }
        }
    }
    
    LOG_INFO("Reset complete");
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

    // Get desktop size for shader pass initialization
    SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(primaryDisplay);
    int desktopWidth = displayMode ? displayMode->w : 3840;
    int desktopHeight = displayMode ? displayMode->h : 2160;

    bool succes = shaderPass->init(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow()), 
                                   desktopWidth, desktopHeight,
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
        LOG_ERROR("Failed to create shaderpass: {}", passname.c_str());
    }
}

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

void Renderer::renderFrameBuffer(SDL_GPUCommandBuffer *cmd_buffer) {

    auto *framebufferObj = Renderer::GetFramebuffer("primaryFramebuffer");
    auto *framebuffer = framebufferObj->fbContent;

    SDL_GPUColorTargetInfo sdlGpuColorTargetInfo = {
        .texture = swapchain_texture,
        .clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op    = SDL_GPU_STOREOP_STORE,
    };

    SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(cmd_buffer, &sdlGpuColorTargetInfo, 1, nullptr);
    
    SDL_BindGPUGraphicsPipeline(renderPass, m_rendertotexturepipeline);

    glm::mat4 model = glm::mat4(
        Window::GetWidth(),  0.0f,              0.0f,  0.0f,    // Column 0
        0.0f,               Window::GetHeight(), 0.0f,  0.0f,    // Column 1
        0.0f,               0.0f,              1.0f,  0.0f,    // Column 2
        0.0f,               0.0f,              0.0f,  1.0f     // Column 3
    );

    // Calculate UV coordinates to sample only the window-sized portion of the desktop-sized texture
    float uMax = (float)Window::GetWidth() / (float)framebufferObj->width;
    float vMax = (float)Window::GetHeight() / (float)framebufferObj->height;

    Uniforms rtt_uniforms{
        .camera = m_camera,
        .model = model,
        .flipped = glm::vec2(1.0, 1.0),
        .uv0 = glm::vec2(uMax, vMax),
        .uv1 = glm::vec2(0.0, vMax),
        .uv2 = glm::vec2(uMax, 0.0),
        .uv3 = glm::vec2(0.0, vMax),
        .uv4 = glm::vec2(0.0, 0.0),
        .uv5 = glm::vec2(uMax, 0.0),
    };

    SDL_PushGPUVertexUniformData(cmd_buffer, 0, &rtt_uniforms, sizeof(rtt_uniforms));
    auto rtt_texture_sampler_binding = SDL_GPUTextureSamplerBinding{
        .texture = framebuffer,
        .sampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode()),
    };
    SDL_BindGPUFragmentSamplers(renderPass, 0, &rtt_texture_sampler_binding, 1);
    SDL_DrawGPUPrimitives(renderPass, 6, 1, 0, 0);

    for (const auto& [fbName, framebuffer] : frameBuffers) {
        if (framebuffer->renderToScreen) {
            SDL_GPUTextureSamplerBinding binding = {
                .texture = framebuffer->fbContent,
                .sampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode()),
            };
            SDL_BindGPUFragmentSamplers(renderPass, 0, &binding, 1);
            SDL_DrawGPUPrimitives(renderPass, 6, 1, 0, 0); // Draw full-screen quad
        }
    }


    SDL_EndGPURenderPass(renderPass);
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

        LOG_INFO("Created framebuffer: {}", fbname.c_str());
    }
}

void Renderer::_setFramebufferRenderToScreen(const std::string& fbName, bool render) {
    auto* framebuffer = _getFramebuffer(fbName);
    if (framebuffer) {
        framebuffer->renderToScreen = render;
        LOG_INFO("Set renderToScreen to {} for framebuffer: {}", render, fbName.c_str());
    } else {
        LOG_WARNING("Framebuffer not found: {}", fbName.c_str());
    }
}

void Renderer::_attachRenderPassToFrameBuffer(RenderPass *renderPass, const std::string &passname, const std::string &fbName) {
    auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(), [&fbName](const auto &pair) {
        return pair.first == fbName;
    });

    if (it != frameBuffers.end()) {
        it->second->renderpasses.emplace_back(passname, renderPass);

        LOG_INFO("Attached renderpass {} to framebuffer: {}", passname.c_str(), fbName.c_str());
    }
}

SDL_GPUSampler *Renderer::_getSampler(ScaleMode scaleMode) {
    return _samplers[scaleMode];
}

Texture Renderer::_whitePixel() {
    return _whitePixelTexture;
}

Texture Renderer::_whiteCircle() {
    return _whiteCircleTexture;
}

SDL_GPURenderPass* Renderer::_getRenderPass(const std::string& passname) {

    SDL_GPURenderPass* foundPass = nullptr;

    for (auto &[fbName, framebuffer]: frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
                               [&passname](const std::pair<std::string, RenderPass *> &entry) {
                                   return entry.first == passname;
                               });

        if (it != framebuffer->renderpasses.end()) {
            foundPass = it->second->render_pass;
        }
    }

    return foundPass;

}

void Renderer::_setScissorMode(const std::string& passname, const rectf& cliprect) {

        for (auto &[fbName, framebuffer]: frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
                               [&passname](const std::pair<std::string, RenderPass *> &entry) {
                                   return entry.first == passname;
                               });

        if (it != framebuffer->renderpasses.end()) {
                it->second->_scissorEnabled = true;
                it->second->_scissorRect->x = cliprect.x;
                it->second->_scissorRect->y = cliprect.y;
                it->second->_scissorRect->w = cliprect.w;
                it->second->_scissorRect->h = cliprect.h;

        }
    }

}

void Renderer::_setSampleCount(SDL_GPUSampleCount sampleCount) {
    currentSampleCount = sampleCount;

    _reset();
}
