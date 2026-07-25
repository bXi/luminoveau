// SDL-specific initialisation for Renderer::_initRendering().
// Compiled only when LUMINOVEAU_WEBGPU_BACKEND is NOT set (see cmake/Sources.cmake).

#include "renderer/renderer.h"

#include "gpu/backends/sdl/SdlGpuBackend.h"
#include "gpu/backends/sdl/sdlgpu.h"
#include "assets/assethandler.h"
#include "assets/shaders_generated.h"
#include "core/log/log.h"
#include "gpu/presets.h"
#include "renderer/passes/spriterenderpass.h"
#include "renderer/passes/model3drenderpass.h"
#include "renderer/shaders.h"
#include "util/helpers.h"
#include "draw/particles.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#ifdef LUMINOVEAU_WITH_IMGUI
#include "integrations/imgui/imgui_integration.h"
#endif

void Renderer::_initRendering() {
    bool enableGPUDebug = false;
#ifdef LUMIDEBUG
    SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_VERBOSE);
    enableGPUDebug = true;
#endif

    // Select shader format and driver based on build configuration
    SDL_GPUShaderFormat shaderFormat;
    const char         *preferredDriver = nullptr;

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, enableGPUDebug);

#if defined(__ANDROID__)
    // Android: Force Vulkan with reduced features for broader device compatibility
    shaderFormat    = SDL_GPU_SHADERFORMAT_SPIRV;
    preferredDriver = "vulkan";
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);
    LOG_INFO("Using SPIR-V shaders (Android - Vulkan with reduced features)");
#elif defined(LUMINOVEAU_SHADER_BACKEND_DXIL)
    shaderFormat    = SDL_GPU_SHADERFORMAT_DXIL;
    preferredDriver = "direct3d12";
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
    LOG_INFO("Using DXIL shaders (DirectX 12 SM6.0)");
#elif defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
    shaderFormat    = SDL_GPU_SHADERFORMAT_METALLIB;
    preferredDriver = "metal";
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_METALLIB_BOOLEAN, true);
    LOG_INFO("Using Metal shaders (metallib)");
#else
    shaderFormat    = SDL_GPU_SHADERFORMAT_SPIRV;
    preferredDriver = "vulkan";
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    LOG_INFO("Using SPIR-V shaders (Vulkan)");
#endif

    SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, preferredDriver);

    _device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    if (!_device) {
        LOG_ERROR("Failed to create GPU device: {}", SDL_GetError());
        SDL_DestroyWindow(Window::GetWindow());
        return;
    }

    if (!SDL_ClaimWindowForGPUDevice(_device, Window::GetWindow())) {
        LOG_ERROR("Failed to claim window for GPU device: {}", SDL_GetError());
        return;
    }
    LOG_INFO("Claimed window for GPU device");

    SDL_SetGPUAllowedFramesInFlight(_device, 1);

    _gpu = std::make_unique<SdlGpuBackend>(_device);
    _gpu->Init(Window::GetWindow());

    Shaders::Init();

    GpuSamplerCreateInfo nearestInfo {
        .minFilter = GpuFilter::Nearest,
        .magFilter = GpuFilter::Nearest,
        .mipFilter = GpuFilter::Nearest,
        .addressU  = GpuSamplerAddressMode::Repeat,
        .addressV  = GpuSamplerAddressMode::Repeat,
        .addressW  = GpuSamplerAddressMode::Repeat,
    };
    GpuSamplerCreateInfo linearInfo {
        .minFilter = GpuFilter::Linear,
        .magFilter = GpuFilter::Linear,
        .mipFilter = GpuFilter::Linear,
        .addressU  = GpuSamplerAddressMode::ClampToEdge,
        .addressV  = GpuSamplerAddressMode::ClampToEdge,
        .addressW  = GpuSamplerAddressMode::ClampToEdge,
    };
    _samplers[ScaleMode::Nearest] = _gpu->CreateSampler(nearestInfo);
    _samplers[ScaleMode::Linear]  = _gpu->CreateSampler(linearInfo);

    _camera = glm::ortho(0.0f, (float)Window::GetWidth(), float(Window::GetHeight()), 0.0f);

    _fs = AssetHandler::CreateEmptyTexture({ 1, 1 });

// spirv-cross renames "main" to "main0" in MSL (reserved keyword)
#if defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
    const char *shaderEntryPoint = "main0";
#else
    const char *shaderEntryPoint = "main";
#endif

    SDL_GPUShaderCreateInfo rttVertexShaderInfo = {
        .code_size            = Lumi::Shaders::FULLSCREEN_QUAD_VERT_SIZE,
        .code                 = Lumi::Shaders::FULLSCREEN_QUAD_VERT,
        .entrypoint           = shaderEntryPoint,
        .format               = shaderFormat,
        .stage                = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers         = 0,
        .num_storage_textures = 0,
        .num_storage_buffers  = 0,
        .num_uniform_buffers  = 1,
    };

    SDL_GPUShaderCreateInfo rttFragmentShaderInfo = {
        .code_size            = Lumi::Shaders::FULLSCREEN_QUAD_FRAG_SIZE,
        .code                 = Lumi::Shaders::FULLSCREEN_QUAD_FRAG,
        .entrypoint           = shaderEntryPoint,
        .format               = shaderFormat,
        .stage                = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers         = 1,
        .num_storage_textures = 0,
        .num_storage_buffers  = 0,
        .num_uniform_buffers  = 0,
    };

    _rttVertexShader   = reinterpret_cast<GpuShaderHandle>(SDL_CreateGPUShader(Renderer::GetDevice(), &rttVertexShaderInfo));
    _rttFragmentShader = reinterpret_cast<GpuShaderHandle>(SDL_CreateGPUShader(Renderer::GetDevice(), &rttFragmentShaderInfo));

    if (!_rttVertexShader || !_rttFragmentShader) {
        LOG_CRITICAL("Failed to create RTT shaders: {}", SDL_GetError());
        return;
    }

    auto *framebuffer = new FrameBuffer;

    // Primary framebuffer is sized to the full desktop so the window can grow
    // (fullscreen, resize) without ever recreating the texture. Render passes draw
    // into the top-left window-physical region; the blit samples that region only.
    uint32_t desktopWidth = 0, desktopHeight = 0;
    Window::GetDisplayBounds(desktopWidth, desktopHeight);
    if (desktopWidth == 0 || desktopHeight == 0) {
        desktopWidth  = 3840;
        desktopHeight = 2160;
    }

    LOG_INFO("Creating primary framebuffer at desktop size: {}x{}", desktopWidth, desktopHeight);

    framebuffer->renderpasses.emplace_back("3dmodels", new Model3DRenderPass());
    framebuffer->renderpasses.back().second->colorTargetInfoLoadOp = GpuLoadOp::Clear;
    framebuffer->renderpasses.back().second->colorTargetClearR     = 0.f;
    framebuffer->renderpasses.back().second->colorTargetClearG     = 0.f;
    framebuffer->renderpasses.back().second->colorTargetClearB     = 0.f;
    framebuffer->renderpasses.back().second->colorTargetClearA     = 1.f;

    framebuffer->renderpasses.emplace_back("2dsprites", new SpriteRenderPass());
    framebuffer->renderpasses.back().second->colorTargetInfoLoadOp = GpuLoadOp::Load;
    framebuffer->renderpasses.back().second->colorTargetClearR     = 0.f;
    framebuffer->renderpasses.back().second->colorTargetClearG     = 0.f;
    framebuffer->renderpasses.back().second->colorTargetClearB     = 0.f;
    framebuffer->renderpasses.back().second->colorTargetClearA     = 1.f;

    framebuffer->fbContent = AssetHandler::CreateEmptyTexture({ (float)desktopWidth, (float)desktopHeight }).gpuTexture;
    framebuffer->width     = desktopWidth;
    framebuffer->height    = desktopHeight;
    _frameBuffers.emplace_back("primaryFramebuffer", framebuffer);

    for (auto &[_fbName, _framebuffer] : _frameBuffers) {
        SDL_SetGPUTextureName(Renderer::GetDevice(), reinterpret_cast<SDL_GPUTexture *>(_framebuffer->fbContent),
            Helpers::TextFormat("Renderer: framebuffer %s", _fbName.c_str()));
        for (auto &[_passname, renderpass] : _framebuffer->renderpasses) {
            if (!renderpass->Init(fromSDL(SDL_GetGPUSwapchainTextureFormat(_device, Window::GetWindow())),
                    _framebuffer->width, _framebuffer->height,
                    _passname)) {
                LOG_ERROR("Renderpass ({}) failed to init()", _passname.c_str());
            }
        }
    }

    auto swapchainTextureFormat = SDL_GetGPUSwapchainTextureFormat(_device, Window::GetWindow());

    SDL_GPUColorTargetDescription colorTargetDescriptions = {
        .format      = swapchainTextureFormat,
        .blend_state = toSDL(GpuPresets::AlphaBlendKeepDstAlpha),
    };

    SDL_GPUGraphicsPipelineCreateInfo rttPipelineCreateInfo = {
        .vertex_shader      = reinterpret_cast<SDL_GPUShader *>(_rttVertexShader),
        .fragment_shader    = reinterpret_cast<SDL_GPUShader *>(_rttFragmentShader),
        .vertex_input_state = {
            .vertex_buffer_descriptions = nullptr,
            .num_vertex_buffers         = 0,
            .vertex_attributes          = nullptr,
            .num_vertex_attributes      = 0,
        },
        .primitive_type      = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state    = DEFAULT_RASTERIZER_STATE,
        .multisample_state   = {},
        .depth_stencil_state = {
            .compare_op          = SDL_GPU_COMPAREOP_LESS,
            .back_stencil_state  = {},
            .front_stencil_state = {},
            .compare_mask        = 0,
            .write_mask          = 0,
            .enable_depth_test   = false,
            .enable_depth_write  = false,
            .enable_stencil_test = false,
        },
        .target_info = {
            .color_target_descriptions = &colorTargetDescriptions,
            .num_color_targets         = 1,
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_INVALID,
            .has_depth_stencil_target  = false,
        },
        .props = 0,
    };

    _renderToTexturePipeline = reinterpret_cast<GpuGraphicsPipelineHandle>(
        SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &rttPipelineCreateInfo));

    colorTargetDescriptions.blend_state = {
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .color_blend_op        = SDL_GPU_BLENDOP_ADD,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .alpha_blend_op        = SDL_GPU_BLENDOP_ADD,
        .enable_blend          = true,
    };
    _renderToTexturePipelineAdditive = reinterpret_cast<GpuGraphicsPipelineHandle>(
        SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &rttPipelineCreateInfo));

    _whitePixelTexture = AssetHandler::CreateWhitePixel();

    Particles::Init();

#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::InitRenderer(Window::GetWindow());
#endif
}

// ─── CreateComputePipelineAsset (SDL backend) ─────────────────────────────────
// Uses the precompiled SPIRV pipeline produced by the Shaders subsystem.

ComputePipelineAsset Renderer::CreateComputePipelineAsset(const std::string &shaderPath) {
    return Shaders::CreateComputePipeline(shaderPath);
}
