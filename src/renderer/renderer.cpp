#include "renderer.h"

#include <stdexcept>
#include <thread>
#include <regex>
#include "platform/audio/audio.h"
#ifdef LUMINOVEAU_WEBGPU_BACKEND
#include "gpu/backends/webgpu/WebGpuGpuBackend.h"
#else
#include "gpu/backends/sdl/SdlGpuBackend.h"
#endif

#include "assets/assethandler.h"
#include "assets/shaders_generated.h"

#include "util/helpers.h"
#include "core/log/log.h"

#include "gpu/renderpass.h"
#include "gpu/presets.h"

#include "renderer/passes/spriterenderpass.h"
#include "renderer/passes/model3drenderpass.h"
#include "renderer/passes/shaderrenderpass.h"

#include "compute.h"
#include "draw/particles.h"
#ifdef LUMINOVEAU_WEBGPU_BACKEND
#include "platform/input/input.h"
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifndef LUMINOVEAU_WEBGPU_BACKEND
#include "gpu/geometry/geometry2d.h"
#include "draw/draw.h"
#include "shaders.h"
#else
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_cross.hpp>
#include "file/filehandler.h"
#endif

#ifndef LUMINOVEAU_WEBGPU_BACKEND
#include <SDL3_image/SDL_image.h>
#endif

#ifdef LUMINOVEAU_WITH_RMLUI
#include "integrations/rmlui/rmlui.h"
#include "integrations/rmlui/rmluibackend.h"
#endif

#ifdef LUMINOVEAU_WITH_IMGUI
#include "integrations/imgui/imgui_integration.h"
#endif

#ifdef LUMINOVEAU_WEBGPU_BACKEND
// Fullscreen-quad vertex shader (WGSL).
// group(0) binding(0) = vertex uniform block (camera + model + UVs).
// Struct is padded to 208 bytes (multiple of 16) as required by WebGPU.
static constexpr const char* kRttVertWGSL = R"(
struct Uniforms {
    camera  : mat4x4<f32>,
    model   : mat4x4<f32>,
    flipped : vec2<f32>,
    uv0     : vec2<f32>,
    uv1     : vec2<f32>,
    uv2     : vec2<f32>,
    uv3     : vec2<f32>,
    uv4     : vec2<f32>,
    uv5     : vec2<f32>,
    tintR   : f32,
    tintG   : f32,
    tintB   : f32,
    tintA   : f32,
    _pad    : vec2<f32>,
}
@group(0) @binding(0) var<uniform> u : Uniforms;

struct VertOut {
    @builtin(position) pos : vec4<f32>,
    @location(0) uv        : vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vi : u32) -> VertOut {
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(1.0, 1.0), vec2<f32>(0.0, 1.0), vec2<f32>(1.0, 0.0),
        vec2<f32>(0.0, 1.0), vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0)
    );
    var uvs = array<vec2<f32>, 6>(u.uv0, u.uv1, u.uv2, u.uv3, u.uv4, u.uv5);
    let world = u.model * vec4<f32>(positions[vi], 0.0, 1.0);
    var o : VertOut;
    o.pos = u.camera * world;
    o.uv  = uvs[vi];
    return o;
}
)";

// Fullscreen-quad fragment shader (WGSL).
// group(2) = frag sampler+tex pairs (per WebGPU backend convention).
static constexpr const char* kRttFragWGSL = R"(
@group(2) @binding(0) var samp : sampler;
@group(2) @binding(1) var tex  : texture_2d<f32>;

struct VertOut {
    @builtin(position) pos : vec4<f32>,
    @location(0) uv        : vec2<f32>,
}

@fragment
fn fs_main(in : VertOut) -> @location(0) vec4<f32> {
    return textureSample(tex, samp, in.uv);
}
)";
#endif // LUMINOVEAU_WEBGPU_BACKEND

void Renderer::_initRendering() {

#ifdef LUMINOVEAU_WEBGPU_BACKEND

    m_gpu = std::make_unique<WebGpuGpuBackend>();
    if (!m_gpu->init(Window::GetWindow())) {
        LOG_CRITICAL("WebGpuGpuBackend::init() failed");
        return;
    }

    GpuSamplerCreateInfo nearestInfo{
        .minFilter = GpuFilter::Nearest, .magFilter = GpuFilter::Nearest, .mipFilter = GpuFilter::Nearest,
        .addressU  = GpuSamplerAddressMode::Repeat, .addressV = GpuSamplerAddressMode::Repeat, .addressW = GpuSamplerAddressMode::Repeat,
    };
    GpuSamplerCreateInfo linearInfo{
        .minFilter = GpuFilter::Linear, .magFilter = GpuFilter::Linear, .mipFilter = GpuFilter::Linear,
        .addressU  = GpuSamplerAddressMode::ClampToEdge, .addressV = GpuSamplerAddressMode::ClampToEdge, .addressW = GpuSamplerAddressMode::ClampToEdge,
    };
    _samplers[ScaleMode::Nearest] = m_gpu->createSampler(nearestInfo);
    _samplers[ScaleMode::Linear]  = m_gpu->createSampler(linearInfo);

    m_camera = glm::ortho(0.0f, (float)Window::GetWidth(), (float)Window::GetHeight(), 0.0f);

    fs = AssetHandler::CreateEmptyTexture({1, 1});

    GpuShaderCreateInfo rttVertInfo{};
    rttVertInfo.code               = reinterpret_cast<const uint8_t*>(kRttVertWGSL);
    rttVertInfo.codeSize           = strlen(kRttVertWGSL);
    rttVertInfo.entrypoint         = "vs_main";
    rttVertInfo.stage              = GpuShaderStage::Vertex;
    rttVertInfo.uniformBufferCount = 1;
    rtt_vertex_shader = m_gpu->createShader(rttVertInfo);

    GpuShaderCreateInfo rttFragInfo{};
    rttFragInfo.code          = reinterpret_cast<const uint8_t*>(kRttFragWGSL);
    rttFragInfo.codeSize      = strlen(kRttFragWGSL);
    rttFragInfo.entrypoint    = "fs_main";
    rttFragInfo.stage         = GpuShaderStage::Fragment;
    rttFragInfo.samplerCount  = 1;
    rtt_fragment_shader = m_gpu->createShader(rttFragInfo);

    if (!rtt_vertex_shader || !rtt_fragment_shader) {
        LOG_CRITICAL("Failed to create WGSL RTT shaders");
        return;
    }

    GpuGraphicsPipelineCreateInfo rttPipeInfo{};
    rttPipeInfo.vertexShader      = rtt_vertex_shader;
    rttPipeInfo.fragmentShader    = rtt_fragment_shader;
    rttPipeInfo.colorTargetFormat = m_gpu->getSwapchainFormat();
    rttPipeInfo.blend             = GpuPresets::AlphaBlendKeepDstAlpha;
    rttPipeInfo.hasDepthTarget    = false;
    m_rendertotexturepipeline = m_gpu->createGraphicsPipeline(rttPipeInfo);

    GpuGraphicsPipelineCreateInfo rttAddPipeInfo = rttPipeInfo;
    rttAddPipeInfo.blend = {
        .blendEnabled   = true,
        .srcColorFactor = GpuBlendFactor::One,
        .dstColorFactor = GpuBlendFactor::One,
        .colorOp        = GpuBlendOp::Add,
        .srcAlphaFactor = GpuBlendFactor::One,
        .dstAlphaFactor = GpuBlendFactor::One,
        .alphaOp        = GpuBlendOp::Add,
    };
    m_rendertotexturepipeline_additive = m_gpu->createGraphicsPipeline(rttAddPipeInfo);

    // For Native mode, framebuffer matches canvas pixels exactly.
    // For Contain/Fill/Stretch, use the configured fixed render resolution.
    int fbWidth, fbHeight;
    if (Window::GetWebGpuScaleMode() == WebGpuScaleMode::Native) {
        SDL_GetWindowSizeInPixels(Window::GetWindow(), &fbWidth, &fbHeight);
        if (fbWidth <= 0 || fbHeight <= 0) { fbWidth = 1280; fbHeight = 720; }
    } else {
        fbWidth  = Window::GetWebGpuRenderWidth();
        fbHeight = Window::GetWebGpuRenderHeight();
        if (fbWidth <= 0 || fbHeight <= 0) { fbWidth = 1280; fbHeight = 720; }
    }

    auto *framebuffer = new FrameBuffer;
    framebuffer->fbContent = AssetHandler::CreateEmptyTexture({(float)fbWidth, (float)fbHeight}).gpuTexture;
    framebuffer->width  = fbWidth;
    framebuffer->height = fbHeight;

    // Add render passes for WebGPU
    framebuffer->renderpasses.emplace_back("3dmodels", new Model3DRenderPass());
    framebuffer->renderpasses.back().second->color_target_info_loadop = GpuLoadOp::Clear;
    framebuffer->renderpasses.back().second->color_target_clear_r = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_g = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_b = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_a = 1.f;
    framebuffer->renderpasses.emplace_back("2dsprites", new SpriteRenderPass());
    framebuffer->renderpasses.back().second->color_target_info_loadop = GpuLoadOp::Load;

    frameBuffers.emplace_back("primaryFramebuffer", framebuffer);

    // Init render passes for WebGPU
    for (auto &[fbName, fb]: frameBuffers) {
        for (auto &[rpName, rp]: fb->renderpasses) {
            rp->init(m_gpu->getSwapchainFormat(), fbWidth, fbHeight, rpName, true, 0, false);
        }
    }
    // Wait for pipeline compilation before first frame.
    m_gpu->waitIdle();

    _whitePixelTexture = AssetHandler::CreateWhitePixel();

    Particles::Init();

#else // SDL path ────────────────────────────────────────────────────────────

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
        // Android: Force Vulkan with reduced features for broader device compatibility
        shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
        preferredDriver = "vulkan";
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);
        LOG_INFO("Using SPIR-V shaders (Android - Vulkan with reduced features)");

    #elif defined(LUMINOVEAU_SHADER_BACKEND_DXIL)
        // DirectX 12 with DXIL SM6.0 (Windows)
        shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
        preferredDriver = "direct3d12";
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
        LOG_INFO("Using DXIL shaders (DirectX 12 SM6.0)");

    #elif defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        // Metal (macOS/iOS) with pre-compiled metallib bytecode
        shaderFormat = SDL_GPU_SHADERFORMAT_METALLIB;
        preferredDriver = "metal";
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_METALLIB_BOOLEAN, true);
        LOG_INFO("Using Metal shaders (metallib)");

    #else
        // Default: Vulkan with SPIR-V
        shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
        preferredDriver = "vulkan";
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


    if (!SDL_ClaimWindowForGPUDevice(m_device, Window::GetWindow())) {
        LOG_ERROR("Failed to claim window for GPU device: {}", SDL_GetError());
        return;
    }
    LOG_INFO("Claimed window for GPU device");

    SDL_SetGPUAllowedFramesInFlight(m_device, 1);

    m_gpu = std::make_unique<SdlGpuBackend>(m_device);
    m_gpu->init(Window::GetWindow());

    Shaders::Init();

    GpuSamplerCreateInfo nearestInfo{
        .minFilter = GpuFilter::Nearest, .magFilter = GpuFilter::Nearest, .mipFilter = GpuFilter::Nearest,
        .addressU  = GpuSamplerAddressMode::Repeat, .addressV = GpuSamplerAddressMode::Repeat, .addressW = GpuSamplerAddressMode::Repeat,
    };
    GpuSamplerCreateInfo linearInfo{
        .minFilter = GpuFilter::Linear,  .magFilter = GpuFilter::Linear,  .mipFilter = GpuFilter::Linear,
        .addressU  = GpuSamplerAddressMode::ClampToEdge, .addressV = GpuSamplerAddressMode::ClampToEdge, .addressW = GpuSamplerAddressMode::ClampToEdge,
    };
    _samplers[ScaleMode::Nearest] = m_gpu->createSampler(nearestInfo);
    _samplers[ScaleMode::Linear]  = m_gpu->createSampler(linearInfo);

    m_camera = glm::ortho(0.0f, (float) Window::GetWidth(), float(Window::GetHeight()), 0.0f);

    fs = AssetHandler::CreateEmptyTexture({1, 1});

    // spirv-cross renames "main" to "main0" in MSL (reserved keyword)
    #if defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        const char* shaderEntryPoint = "main0";
    #else
        const char* shaderEntryPoint = "main";
    #endif

    SDL_GPUShaderCreateInfo rttVertexShaderInfo = {
        .code_size = Luminoveau::Shaders::FullscreenQuad_Vert_Size,
        .code = Luminoveau::Shaders::FullscreenQuad_Vert,
        .entrypoint = shaderEntryPoint,
        .format = shaderFormat,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 1,
    };

    SDL_GPUShaderCreateInfo rttFragmentShaderInfo = {
        .code_size = Luminoveau::Shaders::FullscreenQuad_Frag_Size,
        .code = Luminoveau::Shaders::FullscreenQuad_Frag,
        .entrypoint = shaderEntryPoint,
        .format = shaderFormat,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 0,
    };

    rtt_vertex_shader   = reinterpret_cast<GpuShaderHandle>(SDL_CreateGPUShader(Renderer::GetDevice(), &rttVertexShaderInfo));
    rtt_fragment_shader = reinterpret_cast<GpuShaderHandle>(SDL_CreateGPUShader(Renderer::GetDevice(), &rttFragmentShaderInfo));

    if (!rtt_vertex_shader || !rtt_fragment_shader) {
        LOG_CRITICAL("Failed to create RTT shaders: {}", SDL_GetError());
        return;
    }

    auto *framebuffer = new FrameBuffer;

    // Get the primary display's size in PHYSICAL pixels for creating framebuffers
    // On HiDPI/Retina displays, w * pixel_density gives the actual device resolution
    // while w/h alone give logical points (which would be too small for the framebuffer)
    SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(primaryDisplay);
    int desktopWidth = displayMode ? (int)(displayMode->w * displayMode->pixel_density) : 3840;
    int desktopHeight = displayMode ? (int)(displayMode->h * displayMode->pixel_density) : 2160;

    LOG_INFO("Creating framebuffers at desktop size: {}x{}", desktopWidth, desktopHeight);

    // Add 3D model render pass (render first)
    framebuffer->renderpasses.emplace_back("3dmodels", new Model3DRenderPass(m_device));
    framebuffer->renderpasses.back().second->color_target_info_loadop = GpuLoadOp::Clear;
    framebuffer->renderpasses.back().second->color_target_clear_r = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_g = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_b = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_a = 1.f;

    // Add 2D sprite pass (render on top of 3D)
    framebuffer->renderpasses.emplace_back("2dsprites", new SpriteRenderPass(m_device));
    framebuffer->renderpasses.back().second->color_target_info_loadop = GpuLoadOp::Load;
    framebuffer->renderpasses.back().second->color_target_clear_r = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_g = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_b = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_a = 1.f;

    // Create framebuffer at desktop size, not window size
    framebuffer->fbContent = AssetHandler::CreateEmptyTexture({(float)desktopWidth, (float)desktopHeight}).gpuTexture;
    framebuffer->width = desktopWidth;
    framebuffer->height = desktopHeight;
    frameBuffers.emplace_back("primaryFramebuffer", framebuffer);

    for (auto &[_fbName, _framebuffer]: frameBuffers) {

        SDL_SetGPUTextureName(Renderer::GetDevice(), reinterpret_cast<SDL_GPUTexture*>(_framebuffer->fbContent), Helpers::TextFormat("Renderer: framebuffer %s", _fbName.c_str()));
        for (auto &[passname, renderpass]: _framebuffer->renderpasses) {
            // Initialize render passes at desktop size, not window size
            if (!renderpass->init(fromSDL(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow())),
                                  _framebuffer->width, _framebuffer->height,
                                  passname)) {
                LOG_ERROR("Renderpass ({}) failed to init()", passname.c_str());
            }
        }
    }

    auto swapchain_texture_format = SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow());

    SDL_GPUColorTargetDescription color_target_descriptions = {
        .format      = swapchain_texture_format,
        .blend_state = toSDL(GpuPresets::AlphaBlendKeepDstAlpha),
    };

    SDL_GPUGraphicsPipelineCreateInfo rtt_pipeline_create_info = {
        .vertex_shader   = reinterpret_cast<SDL_GPUShader*>(rtt_vertex_shader),
        .fragment_shader = reinterpret_cast<SDL_GPUShader*>(rtt_fragment_shader),
        .vertex_input_state =
            {
                .vertex_buffer_descriptions = nullptr,
                .num_vertex_buffers         = 0,
                .vertex_attributes          = nullptr,
                .num_vertex_attributes      = 0,
            },
        .primitive_type    = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state  = SDL_DefaultRasterizerState,
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

    m_rendertotexturepipeline = reinterpret_cast<GpuGraphicsPipelineHandle>(
        SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &rtt_pipeline_create_info));

    color_target_descriptions.blend_state = {
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .color_blend_op        = SDL_GPU_BLENDOP_ADD,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .alpha_blend_op        = SDL_GPU_BLENDOP_ADD,
        .enable_blend          = true,
    };
    m_rendertotexturepipeline_additive = reinterpret_cast<GpuGraphicsPipelineHandle>(
        SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &rtt_pipeline_create_info));

    _whitePixelTexture = AssetHandler::CreateWhitePixel();

    Particles::Init();

#endif // LUMINOVEAU_WEBGPU_BACKEND

#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::InitRenderer(Window::GetWindow());
#endif
}

void Renderer::_close() {
    if (!m_gpu) {
        return;
    }

    LOG_INFO("Closing renderer");

#ifndef LUMINOVEAU_WEBGPU_BACKEND
    Particles::Quit();
    SDL_WaitForGPUIdle(m_device);

    if (_pendingScreenshotData.transferBuffer) {
        SDL_ReleaseGPUTransferBuffer(m_device, _pendingScreenshotData.transferBuffer);
        _pendingScreenshotData = {};
    }
#else
    Particles::Quit();
    m_gpu->waitIdle();
#endif

    for (auto &[fbName, framebuffer]: frameBuffers) {
        for (auto &[passname, renderpass]: framebuffer->renderpasses) {
            renderpass->release(false);
            delete renderpass;
        }
        framebuffer->renderpasses.clear();

        if (framebuffer->fbContent)     m_gpu->releaseTexture(framebuffer->fbContent);
        if (framebuffer->fbContentMSAA) m_gpu->releaseTexture(framebuffer->fbContentMSAA);
        if (framebuffer->fbDepthMSAA)   m_gpu->releaseTexture(framebuffer->fbDepthMSAA);
        delete framebuffer;
    }
    frameBuffers.clear();

    for (auto& [mode, sampler] : _samplers) {
        if (sampler) m_gpu->releaseSampler(sampler);
    }
    _samplers.clear();

    if (m_rendertotexturepipeline)          m_gpu->releaseGraphicsPipeline(m_rendertotexturepipeline);
    if (m_rendertotexturepipeline_additive) m_gpu->releaseGraphicsPipeline(m_rendertotexturepipeline_additive);
    if (rtt_vertex_shader)                  m_gpu->releaseShader(rtt_vertex_shader);
    if (rtt_fragment_shader)                m_gpu->releaseShader(rtt_fragment_shader);
    m_rendertotexturepipeline = m_rendertotexturepipeline_additive = 0;
    rtt_vertex_shader = rtt_fragment_shader = 0;

#ifndef LUMINOVEAU_WEBGPU_BACKEND
    Shaders::Quit();
#endif

    AssetHandler::Cleanup();

#ifndef LUMINOVEAU_WEBGPU_BACKEND
    Geometry2DFactory::ReleaseAll();
    SDL_DestroyGPUDevice(m_device);
    m_device = nullptr;
#else
    m_gpu->shutdown();
#endif

    m_gpu.reset();
    m_cmdbuf = 0;
    swapchain_texture = 0;

    LOG_INFO("Renderer closed");
}

ComputePipelineAsset Renderer::CreateComputePipelineAsset(const std::string& shaderPath) {
#ifndef LUMINOVEAU_WEBGPU_BACKEND
    return Shaders::CreateComputePipeline(GetDevice(), shaderPath);
#else
    // Load pre-transpiled WGSL
    std::string wgslPath = "/_transpiled/" + shaderPath + ".wgsl";
    std::string wgsl = FileHandler::ReadTextFile(wgslPath);
    if (wgsl.empty()) {
        LOG_WARNING("Compute WGSL not found for '{}' (shader unsupported on this backend?)", shaderPath.c_str());
        return {};
    }

    // Remap @group(Nu) numbers from SDL_GPU convention to WebGPU backend layout.
    // Source GLSL uses SDL_GPU: set 0 = RO storage, set 1 = RW storage, set 2 = uniforms.
    // WebGpuGpuBackend pipeline layout: group 0 = uniforms, group 1 = RO storage, group 2 = RW storage.
    // Cycle: 0 -> 1, 1 -> 2, 2 -> 0. Use placeholders to avoid mid-swap collisions.
    {
        auto replaceAll = [&](const std::string& from, const std::string& to) {
            size_t pos = 0;
            while ((pos = wgsl.find(from, pos)) != std::string::npos) {
                wgsl.replace(pos, from.size(), to);
                pos += to.size();
            }
        };
        replaceAll("@group(0u)", "@group(__g1__)");
        replaceAll("@group(1u)", "@group(__g2__)");
        replaceAll("@group(2u)", "@group(__g0__)");
        replaceAll("__g0__", "0u");
        replaceAll("__g1__", "1u");
        replaceAll("__g2__", "2u");
    }

    // Load GLSL → SPIR-V for reflection (thread counts, resource counts)
    std::string glslSource = FileHandler::ReadTextFile(shaderPath);
    if (glslSource.empty()) {
        LOG_ERROR("Compute GLSL source not found: {}", shaderPath.c_str());
        return {};
    }

    glslang::InitializeProcess();
    glslang::TShader shader(EShLangCompute);
    const char* src = glslSource.c_str();
    shader.setStrings(&src, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    TBuiltInResource res{};
    res.maxComputeWorkGroupCountX = 65535; res.maxComputeWorkGroupCountY = 65535;
    res.maxComputeWorkGroupCountZ = 65535; res.maxComputeWorkGroupSizeX = 1024;
    res.maxComputeWorkGroupSizeY = 1024; res.maxComputeWorkGroupSizeZ = 64;
    res.maxComputeUniformComponents = 1024; res.maxComputeTextureImageUnits = 16;
    res.maxComputeImageUniforms = 8; res.maxComputeAtomicCounters = 8;
    res.maxComputeAtomicCounterBuffers = 1;
    res.limits.nonInductiveForLoops = 1; res.limits.whileLoops = 1;
    res.limits.doWhileLoops = 1; res.limits.generalUniformIndexing = 1;
    res.limits.generalVariableIndexing = 1; res.limits.generalSamplerIndexing = 1;
    res.limits.generalAttributeMatrixVectorIndexing = 1;
    res.limits.generalVaryingIndexing = 1;
    res.limits.generalConstantMatrixVectorIndexing = 1;
    res.maxCombinedTextureImageUnits = 80;

    std::vector<uint32_t> spirv;
    if (!shader.parse(&res, 450, EProfile::ECoreProfile, false, true, EShMsgDefault)) {
        LOG_ERROR("Compute shader parse failed ({}): {}", shaderPath.c_str(), shader.getInfoLog());
        glslang::FinalizeProcess();
        return {};
    }
    glslang::TProgram prog;
    prog.addShader(&shader);
    if (!prog.link(EShMsgDefault)) {
        LOG_ERROR("Compute shader link failed ({}): {}", shaderPath.c_str(), prog.getInfoLog());
        glslang::FinalizeProcess();
        return {};
    }
    glslang::GlslangToSpv(*prog.getIntermediate(EShLangCompute), spirv);
    glslang::FinalizeProcess();

    // Reflect SPIR-V
    ComputePipelineAsset asset;
    asset.filename = shaderPath;
    try {
        spirv_cross::Compiler spvComp(spirv);
        auto spvRes = spvComp.get_shader_resources();

        auto entry = spvComp.get_entry_points_and_stages();
        if (!entry.empty()) {
            auto wg = spvComp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 0);
            auto wgY = spvComp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 1);
            auto wgZ = spvComp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 2);
            asset.threadcount_x = wg  ? wg  : 1;
            asset.threadcount_y = wgY ? wgY : 1;
            asset.threadcount_z = wgZ ? wgZ : 1;
        }

        asset.num_samplers                   = static_cast<uint32_t>(spvRes.sampled_images.size());
        asset.num_uniform_buffers            = static_cast<uint32_t>(spvRes.uniform_buffers.size());

        // Distinguish readonly vs readwrite storage buffers via NonWritable decoration.
        // Buffer-level `readonly` qualifier in GLSL maps to NonWritable on the variable.
        uint32_t roSSBO = 0, rwSSBO = 0;
        for (const auto& sb : spvRes.storage_buffers) {
            auto flags = spvComp.get_buffer_block_flags(sb.id);
            if (flags.get(spv::DecorationNonWritable)) ++roSSBO;
            else ++rwSSBO;
        }
        asset.num_readonly_storage_buffers   = roSSBO;
        asset.num_readwrite_storage_buffers  = rwSSBO;

        // Storage textures: NonWritable = readonly, NonReadable = writeonly, neither = readwrite
        uint32_t roTex = 0, rwTex = 0;
        for (const auto& si : spvRes.storage_images) {
            auto deco = spvComp.get_decoration_bitset(si.id);
            if (deco.get(spv::DecorationNonWritable)) ++roTex;
            else ++rwTex;
        }
        asset.num_readonly_storage_textures  = roTex;
        asset.num_readwrite_storage_textures = rwTex;
        // Stash writeonly mask per RW texture binding; consumed by createComputePipeline below.
        // SPIR-V binding numbers within set 1 (RW set) drive the index.
        // (We only need this for the WebGPU BGL access mode.)
    } catch (const std::exception& e) {
        LOG_ERROR("Compute SPIRV reflection failed ({}): {}", shaderPath.c_str(), e.what());
        return {};
    }

    // Parse WGSL for storage texture formats per (group, binding). Required by WebGPU BGL —
    // the format declared in the BGL entry must match the WGSL texture_storage_2d<FORMAT,...> arg.
    auto wgslFormatToGpu = [](const std::string& f) -> GpuTextureFormat {
        if (f == "rgba8unorm")     return GpuTextureFormat::R8G8B8A8_Unorm;
        if (f == "rgba16float")    return GpuTextureFormat::R16G16B16A16_Float;
        if (f == "rgba32float")    return GpuTextureFormat::R32G32B32A32_Float;
        if (f == "r32float")       return GpuTextureFormat::R32_Float;
        if (f == "bgra8unorm")     return GpuTextureFormat::B8G8R8A8_Unorm;
        return GpuTextureFormat::R8G8B8A8_Unorm;
    };
    std::vector<GpuTextureFormat> roTexFormats(asset.num_readonly_storage_textures,  GpuTextureFormat::R8G8B8A8_Unorm);
    std::vector<GpuTextureFormat> rwTexFormats(asset.num_readwrite_storage_textures, GpuTextureFormat::R8G8B8A8_Unorm);
    // Use plain bool array (not std::vector<bool> — needs raw bool* for the create-info pointer).
    std::unique_ptr<bool[]> rwTexWriteOnly(asset.num_readwrite_storage_textures > 0
                                            ? new bool[asset.num_readwrite_storage_textures]()
                                            : nullptr);
    {
        std::regex re(R"(@group\((\d+)u?\)\s*@binding\((\d+)u?\)\s*var\s*(?:<[^>]*>\s*)?\w+\s*:\s*texture_storage_2d<(\w+)\s*,\s*(read|read_write|write)>)");
        auto begin = std::sregex_iterator(wgsl.begin(), wgsl.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            uint32_t grp = std::stoul((*it)[1].str());
            uint32_t bnd = std::stoul((*it)[2].str());
            GpuTextureFormat fmt = wgslFormatToGpu((*it)[3].str());
            std::string mode = (*it)[4].str();
            if (grp == 1 && bnd < roTexFormats.size()) roTexFormats[bnd] = fmt;
            if (grp == 2 && bnd < rwTexFormats.size()) {
                rwTexFormats[bnd]   = fmt;
                if (rwTexWriteOnly) rwTexWriteOnly[bnd] = (mode == "write");
            }
        }
    }

    // Create compute pipeline from WGSL
    GpuComputePipelineCreateInfo ci;
    ci.code                           = reinterpret_cast<const uint8_t*>(wgsl.c_str());
    ci.codeSize                       = wgsl.size();
    ci.entrypoint                     = "main";
    ci.threadCountX                   = asset.threadcount_x;
    ci.threadCountY                   = asset.threadcount_y;
    ci.threadCountZ                   = asset.threadcount_z;
    ci.samplerCount                   = asset.num_samplers;
    ci.readonlyStorageTextureCount    = asset.num_readonly_storage_textures;
    ci.readwriteStorageTextureCount   = asset.num_readwrite_storage_textures;
    ci.readonlyStorageBufferCount     = asset.num_readonly_storage_buffers;
    ci.readwriteStorageBufferCount    = asset.num_readwrite_storage_buffers;
    ci.uniformBufferCount             = asset.num_uniform_buffers;
    ci.readonlyStorageTextureFormats  = roTexFormats.empty() ? nullptr : roTexFormats.data();
    ci.readwriteStorageTextureFormats = rwTexFormats.empty() ? nullptr : rwTexFormats.data();
    ci.readwriteStorageTextureWriteOnly = rwTexWriteOnly.get();

    asset.pipeline = GetGpu().createComputePipeline(ci);
    if (!asset.pipeline) {
        LOG_ERROR("Compute pipeline creation failed for '{}'", shaderPath.c_str());
        return {};
    }

    LOG_INFO("Created compute pipeline: {} ({}x{}x{}, {} storage bufs, {} uniforms)",
             shaderPath.c_str(), asset.threadcount_x, asset.threadcount_y, asset.threadcount_z,
             asset.num_readwrite_storage_buffers, asset.num_uniform_buffers);
    return asset;
#endif
}

void Renderer::_updateCameraProjection() {
#ifdef LUMINOVEAU_WEBGPU_BACKEND
    if (Window::GetWebGpuScaleMode() == WebGpuScaleMode::Native &&
        m_canvasWidth > 0 && m_canvasHeight > 0) {
        m_camera = glm::ortho(0.0f, (float)m_canvasWidth, (float)m_canvasHeight, 0.0f);
        return;
    }
#endif
    m_camera = glm::ortho(0.0f, (float)Window::GetWidth(), (float)Window::GetHeight(), 0.0f);
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
    if (!m_gpu) return;

#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::NewFrame();
#endif
}

void Renderer::_endFrame() {
    if (!m_gpu) return;

    // ── SDL-only pre-frame work ───────────────────────────────────────────────
#ifndef LUMINOVEAU_WEBGPU_BACKEND
    Draw::FlushPixels();
    Input::GetVirtualControls().Render();
    _processPendingScreenshot();
#endif

    // ── Acquire command buffer and swapchain (IGpu interface) ─────────────────
    m_cmdbuf = m_gpu->acquireCommandBuffer();
    if (!m_cmdbuf) {
        LOG_WARNING("Failed to acquire GPU command buffer");
#ifndef LUMINOVEAU_WEBGPU_BACKEND
#ifdef LUMINOVEAU_WITH_IMGUI
        ImGuiIntegration::EndFrame();
#endif
#endif
        return;
    }

    uint32_t scWidth = 0, scHeight = 0;
    swapchain_texture = m_gpu->acquireSwapchainTexture(m_cmdbuf, scWidth, scHeight);
    if (scWidth > 0 && scHeight > 0 && (scWidth != m_canvasWidth || scHeight != m_canvasHeight)) {
        m_canvasWidth  = scWidth;
        m_canvasHeight = scHeight;
#ifdef LUMINOVEAU_WEBGPU_BACKEND
        // Native mode: render passes work in canvas space — update camera AND rebuild FBs/passes
        // so intermediate render targets follow the new canvas size.
        if (Window::GetWebGpuScaleMode() == WebGpuScaleMode::Native) {
            _updateCameraProjection();
            m_pendingReset = true;
        }
#endif
    }

    if (!swapchain_texture) {
#ifndef LUMINOVEAU_WEBGPU_BACKEND
        Compute::_Reset();
        Draw::ResetEffectStore();
#endif
#ifdef LUMINOVEAU_WITH_IMGUI
        ImGuiIntegration::EndFrame();
#endif
        m_gpu->submitCommandBuffer(m_cmdbuf);
        for (auto &[fbName, framebuffer]: frameBuffers) {
            for (auto &[passname, renderpass]: framebuffer->renderpasses) {
                renderpass->resetRenderQueue();
            }
        }
        m_cmdbuf = 0;
        return;
    }

    // ── SDL-only: Draw pixel textures, particles, compute ────────────────────
#ifndef LUMINOVEAU_WEBGPU_BACKEND
    Draw::ReleaseFramePixelTextures();

    bool useMSAA = (currentSampleCount > GpuSampleCount::x1);

    auto runPasses = [&](bool preCompute) {
        for (auto &[fbName, framebuffer]: frameBuffers) {
            if (framebuffer->preComputeFlush != preCompute) continue;
            bool useThisMSAA = useMSAA && framebuffer->fbContentMSAA != 0;
            GpuTextureHandle renderTarget = useThisMSAA ? framebuffer->fbContentMSAA : framebuffer->fbContent;
            GpuTextureHandle depthTarget  = useThisMSAA ? framebuffer->fbDepthMSAA   : 0;
            for (size_t i = 0; i < framebuffer->renderpasses.size(); i++) {
                auto& [passname, renderpass] = framebuffer->renderpasses[i];
                if (i > 0) renderpass->color_target_info_loadop = GpuLoadOp::Load;
                renderpass->renderTargetDepth = depthTarget;
                bool isLastPass = (i == framebuffer->renderpasses.size() - 1);
                bool nextNeedsResolved = useThisMSAA && !isLastPass &&
                    framebuffer->renderpasses[i + 1].second->needsResolvedInput();
                renderpass->renderTargetResolve = (useThisMSAA && (isLastPass || nextNeedsResolved)) ? framebuffer->fbContent : 0;
                renderpass->render(m_cmdbuf, renderTarget, m_camera);
            }
        }
    };

    auto* sdlCmdBuf = reinterpret_cast<SDL_GPUCommandBuffer*>(m_cmdbuf);
    auto* sdlSwapchain = reinterpret_cast<SDL_GPUTexture*>(swapchain_texture);

    runPasses(true);
    Particles::_PrepareFrame(m_cmdbuf);
    Compute::_ExecuteQueued(m_cmdbuf);
    Compute::_Reset();
    runPasses(false);
#endif

#ifdef LUMINOVEAU_WEBGPU_BACKEND
    Particles::_PrepareFrame(m_cmdbuf);
    Compute::_ExecuteQueued(m_cmdbuf);
    Compute::_Reset();

    for (auto &[fbName, framebuffer]: frameBuffers) {
        GpuTextureHandle renderTarget = framebuffer->fbContent;
        // Per-FB camera: map (0..fb.width, fb.height..0) so draws into smaller render targets
        // (e.g. LightToy intermediates) hit the correct texel position regardless of canvas size.
        glm::mat4 fbCamera = (framebuffer->width == (uint32_t)Window::GetWidth() &&
                              framebuffer->height == (uint32_t)Window::GetHeight())
                              ? m_camera
                              : glm::ortho(0.0f, (float)framebuffer->width,
                                           (float)framebuffer->height, 0.0f);
        for (size_t i = 0; i < framebuffer->renderpasses.size(); i++) {
            auto& [passname, renderpass] = framebuffer->renderpasses[i];
            if (i > 0) renderpass->color_target_info_loadop = GpuLoadOp::Load;
            renderpass->render(m_cmdbuf, renderTarget, fbCamera);
        }
    }
#endif

    // ── Blit framebuffer to swapchain (IGpu interface) ────────────────────────
    renderFrameBuffer(m_cmdbuf);

    // ── SDL-only: UI overlays ─────────────────────────────────────────────────
#ifndef LUMINOVEAU_WEBGPU_BACKEND
#ifdef LUMINOVEAU_WITH_RMLUI
    RmlUI::Backend::BeginFrame(sdlCmdBuf, sdlSwapchain,
                               static_cast<uint32_t>(Window::GetWidth()),
                               static_cast<uint32_t>(Window::GetHeight()));
    RmlUI::Render();
    RmlUI::Backend::EndFrame();
#endif
#endif

    // ── ImGui: both SDL and WebGPU ────────────────────────────────────────────
#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::RenderFrame(m_cmdbuf, swapchain_texture);
#endif

#ifndef LUMINOVEAU_WEBGPU_BACKEND

    if (Window::HasPendingScreenshot()) {
        std::string filename = Window::GetAndClearPendingScreenshot();
        if (filename.empty()) {
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            filename = Helpers::TextFormat("screenshot_%lld.png", timestamp);
        }
        if (!filename.ends_with(".png")) {
            if (filename.ends_with(".bmp")) filename = filename.substr(0, filename.length() - 4) + ".png";
            else                            filename += ".png";
        }
        int width = Window::GetPhysicalWidth();
        int height = Window::GetPhysicalHeight();
        size_t dataSize = width * height * 4;
        SDL_GPUTransferBufferCreateInfo transferInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
            .size = (Uint32)dataSize
        };
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(m_device, &transferInfo);
        if (transferBuffer) {
            SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(sdlCmdBuf);
            SDL_GPUTextureRegion srcRegion = {
                .texture = sdlSwapchain, .mip_level = 0, .layer = 0,
                .x = 0, .y = 0, .z = 0, .w = (Uint32)width, .h = (Uint32)height, .d = 1
            };
            SDL_GPUTextureTransferInfo dstInfo = {
                .transfer_buffer = transferBuffer, .offset = 0,
                .pixels_per_row = (Uint32)width, .rows_per_layer = (Uint32)height
            };
            SDL_DownloadFromGPUTexture(copyPass, &srcRegion, &dstInfo);
            SDL_EndGPUCopyPass(copyPass);
            m_gpu->submitCommandBuffer(m_cmdbuf);
            _pendingScreenshotData.filename       = filename;
            _pendingScreenshotData.transferBuffer = transferBuffer;
            _pendingScreenshotData.width          = width;
            _pendingScreenshotData.height         = height;
            _pendingScreenshotData.dataSize       = dataSize;
            for (auto &[fbName, framebuffer]: frameBuffers) {
                for (auto &[passname, renderpass]: framebuffer->renderpasses) {
                    renderpass->resetRenderQueue();
                }
            }
            Draw::ResetEffectStore();
            m_cmdbuf = 0;
            return;
        } else {
            LOG_ERROR("Failed to create transfer buffer for screenshot");
        }
    }
#endif

    // ── Submit and reset ──────────────────────────────────────────────────────
    m_gpu->submitCommandBuffer(m_cmdbuf);
    m_gpu->presentSwapchain();
    for (auto &[fbName, framebuffer]: frameBuffers) {
        for (auto &[passname, renderpass]: framebuffer->renderpasses) {
            renderpass->resetRenderQueue();
        }
    }
#ifndef LUMINOVEAU_WEBGPU_BACKEND
    Draw::ResetEffectStore();
#endif
    m_cmdbuf = 0;
}

#ifndef LUMINOVEAU_WEBGPU_BACKEND
void Renderer::_processPendingScreenshot() {
    // Check if we have a pending screenshot to process
    if (!_pendingScreenshotData.transferBuffer) {
        return;
    }

    // Wait for GPU to finish the copy from previous frame
    SDL_WaitForGPUIdle(m_device);

    // Map and read data
    void* gpuData = SDL_MapGPUTransferBuffer(m_device, _pendingScreenshotData.transferBuffer, false);
    if (gpuData) {
        // Copy pixel data to a buffer we can pass to the background thread
        unsigned char* pixelCopy = (unsigned char*)malloc(_pendingScreenshotData.dataSize);
        if (pixelCopy) {
            memcpy(pixelCopy, gpuData, _pendingScreenshotData.dataSize);

            // Capture data for thread (copy by value)
            std::string filename = _pendingScreenshotData.filename;
            int width = _pendingScreenshotData.width;
            int height = _pendingScreenshotData.height;
            size_t dataSize = _pendingScreenshotData.dataSize;

            // Get pixel format now (can't access SDL state from thread)
            SDL_GPUTextureFormat gpuFormat = SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow());
            SDL_PixelFormat pixelFormat;
            if (gpuFormat == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM) {
                pixelFormat = SDL_PIXELFORMAT_ARGB8888;  // BGRA in memory order
            } else {
                pixelFormat = SDL_PIXELFORMAT_RGBA32;  // Fallback to RGBA
            }

            // Spawn thread to save PNG in background
            std::thread([pixelCopy, filename, width, height, dataSize, pixelFormat]() {
                SDL_Surface* surface = SDL_CreateSurface(width, height, pixelFormat);
                if (surface) {
                    memcpy(surface->pixels, pixelCopy, dataSize);

                    if (IMG_SavePNG(surface, filename.c_str())) {
                        LOG_INFO("Screenshot saved: {}", filename);
                    } else {
                        LOG_ERROR("Failed to save screenshot: {}", SDL_GetError());
                    }

                    SDL_DestroySurface(surface);
                }

                free(pixelCopy);  // Thread owns this memory now
            }).detach();  // Detach so thread runs independently
        }

        SDL_UnmapGPUTransferBuffer(m_device, _pendingScreenshotData.transferBuffer);
    }

    SDL_ReleaseGPUTransferBuffer(m_device, _pendingScreenshotData.transferBuffer);
    _pendingScreenshotData = {};
}
#endif // LUMINOVEAU_WEBGPU_BACKEND

void Renderer::_reset() {
    LOG_INFO("Resetting render passes with MSAA={}", static_cast<int>(currentSampleCount));

    // Get desktop size in physical pixels for render pass re-initialization
    // Compute render dimensions: canvas pixels for WebGPU Native, desktop size elsewhere.
#ifdef LUMINOVEAU_WEBGPU_BACKEND
    int rpWidth = 0, rpHeight = 0;
    if (Window::GetWebGpuScaleMode() == WebGpuScaleMode::Native) {
#ifdef __EMSCRIPTEN__
        // Prefer the last known swapchain size (set by acquireSwapchainTexture).
        // window.innerWidth/Height is read at a different point in the frame and
        // may differ by browser-layout timing during resize animation.
        if (m_canvasWidth > 0 && m_canvasHeight > 0) {
            rpWidth  = (int)m_canvasWidth;
            rpHeight = (int)m_canvasHeight;
        } else {
            rpWidth  = EM_ASM_INT(return window.innerWidth  | 0);
            rpHeight = EM_ASM_INT(return window.innerHeight | 0);
        }
#else
        SDL_GetWindowSizeInPixels(Window::GetWindow(), &rpWidth, &rpHeight);
        if (rpWidth <= 0 || rpHeight <= 0) {
            rpWidth  = (int)m_canvasWidth;
            rpHeight = (int)m_canvasHeight;
        }
#endif
        if (rpWidth <= 0 || rpHeight <= 0) { rpWidth = 1280; rpHeight = 720; }
    } else {
        rpWidth  = Window::GetWebGpuRenderWidth();
        rpHeight = Window::GetWebGpuRenderHeight();
        if (rpWidth <= 0 || rpHeight <= 0) { rpWidth = 1280; rpHeight = 720; }
    }
#else
    SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(primaryDisplay);
    int rpWidth  = displayMode ? (int)(displayMode->w * displayMode->pixel_density) : 0;
    int rpHeight = displayMode ? (int)(displayMode->h * displayMode->pixel_density) : 0;
    if (rpWidth <= 0 || rpHeight <= 0) {
        SDL_GetWindowSizeInPixels(Window::GetWindow(), &rpWidth, &rpHeight);
    }
    if (rpWidth <= 0 || rpHeight <= 0) {
        rpWidth = 1920; rpHeight = 1080;
    }
#endif

    bool useMSAA = (currentSampleCount > GpuSampleCount::x1);
#ifndef LUMINOVEAU_WEBGPU_BACKEND
    SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow());
#endif

#ifdef LUMINOVEAU_WEBGPU_BACKEND
    // Native mode: recreate fbContent textures to match the current canvas size.
    if (Window::GetWebGpuScaleMode() == WebGpuScaleMode::Native) {
        for (auto &[fbName, framebuffer]: frameBuffers) {
            if (framebuffer->fbContent &&
                (framebuffer->width != rpWidth || framebuffer->height != rpHeight)) {
                m_gpu->releaseTexture(framebuffer->fbContent);
                framebuffer->fbContent = AssetHandler::CreateEmptyTexture(
                    {(float)rpWidth, (float)rpHeight}).gpuTexture;
                framebuffer->width  = rpWidth;
                framebuffer->height = rpHeight;
                framebuffer->textureView.width      = rpWidth;
                framebuffer->textureView.height     = rpHeight;
                framebuffer->textureView.gpuTexture = framebuffer->fbContent;
                framebuffer->textureView.gpuSampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode());
            }
        }
    }
#endif

    // Recreate framebuffer MSAA textures if needed
    for (auto &[fbName, framebuffer]: frameBuffers) {
        // Release old MSAA textures
        if (framebuffer->fbContentMSAA) {
            m_gpu->releaseTexture(framebuffer->fbContentMSAA);
            framebuffer->fbContentMSAA = 0;
        }
        if (framebuffer->fbDepthMSAA) {
            m_gpu->releaseTexture(framebuffer->fbDepthMSAA);
            framebuffer->fbDepthMSAA = 0;
        }

        // Create new MSAA textures if MSAA is enabled and this framebuffer supports it.
        // Custom effect render targets (noMSAA=true) always render to plain 1x textures.
        if (useMSAA && !framebuffer->noMSAA) {
#ifdef LUMINOVEAU_WEBGPU_BACKEND
            GpuTextureFormat swapchainFmt = m_gpu->getSwapchainFormat();
#else
            GpuTextureFormat swapchainFmt = fromSDL(swapchainFormat);
#endif
            GpuTextureCreateInfo msaaColorInfo{
                .width         = static_cast<uint32_t>(rpWidth),
                .height        = static_cast<uint32_t>(rpHeight),
                .depthOrLayers = 1, .numLevels = 1,
                .format        = swapchainFmt,
                .sampleCount   = currentSampleCount,
                .usage         = GpuTextureUsage::ColorTarget,
            };
            framebuffer->fbContentMSAA = m_gpu->createTexture(msaaColorInfo);

            GpuTextureCreateInfo msaaDepthInfo{
                .width         = static_cast<uint32_t>(rpWidth),
                .height        = static_cast<uint32_t>(rpHeight),
                .depthOrLayers = 1, .numLevels = 1,
                .format        = GpuTextureFormat::D32_Float,
                .sampleCount   = currentSampleCount,
                .usage         = GpuTextureUsage::DepthStencilTarget,
            };
            framebuffer->fbDepthMSAA = m_gpu->createTexture(msaaDepthInfo);
        }
    }

    for (auto &[fbName, framebuffer]: frameBuffers) {
        for (auto &[passname, renderpass]: framebuffer->renderpasses) {

            renderpass->release(false);

#ifdef LUMINOVEAU_WEBGPU_BACKEND
            m_gpu->waitIdle();
            bool initSuccess = renderpass->init(m_gpu->getSwapchainFormat(),
#else
            SDL_WaitForGPUIdle(m_device);
            bool initSuccess = renderpass->init(fromSDL(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow())),
#endif
                                  rpWidth, rpHeight,
                                  passname, true, 0, framebuffer->noMSAA);
            if (!initSuccess) {
                LOG_ERROR("Renderpass ({}) failed to init()", passname.c_str());
            }
        }
    }

#ifdef LUMINOVEAU_WEBGPU_BACKEND
    // Ensure all pipeline compilations are complete before rendering resumes.
    m_gpu->waitIdle();
#endif

    LOG_INFO("Reset complete");
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
#ifdef LUMINOVEAU_WEBGPU_BACKEND
    LOG_WARNING("AddShaderPass not yet supported on WebGPU backend ({})", passname.c_str());
    return;
#else
    auto shaderPass = new ShaderRenderPass(m_device);

    shaderPass->vertShader = vertShader;
    shaderPass->fragShader = fragShader;

    // Get desktop size in physical pixels for shader pass initialization
    SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(primaryDisplay);
    int desktopWidth = displayMode ? (int)(displayMode->w * displayMode->pixel_density) : 3840;
    int desktopHeight = displayMode ? (int)(displayMode->h * displayMode->pixel_density) : 2160;

    bool succes = shaderPass->init(fromSDL(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow())),
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
#endif // LUMINOVEAU_WEBGPU_BACKEND
}

void Renderer::_removeShaderPass(const std::string &passname) {
    bool found = false;
    RenderPass* passToDelete = nullptr;

    // Find and remove the pass from all framebuffers
    for (auto &[fbName, framebuffer]: frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
                               [&passname](const std::pair<std::string, RenderPass *> &entry) {
                                   return entry.first == passname;
                               });

        if (it != framebuffer->renderpasses.end()) {
            passToDelete = it->second;
            framebuffer->renderpasses.erase(it);
            found = true;
            LOG_INFO("Removed shader pass '{}' from framebuffer '{}'", passname, fbName);
        }
    }

    // Release GPU resources and delete the pass
    if (passToDelete) {
        passToDelete->release(true);  // Log the release
        delete passToDelete;
    }

    if (!found) {
        LOG_WARNING("Shader pass '{}' not found for removal", passname);
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

void Renderer::renderFrameBuffer(GpuCmdBufferHandle cmdBuf) {

    auto *framebufferObj = Renderer::GetFramebuffer("primaryFramebuffer");

    struct UniformsPadded : Uniforms { float _pad[2] = {0.0f, 0.0f}; };
    UniformsPadded rtt_uniforms{};

#ifdef LUMINOVEAU_WEBGPU_BACKEND
    // If the swapchain changed size since _reset() ran, retrigger next frame
    // so _reset() can use the now-current m_canvasWidth/Height.
    if (Window::GetWebGpuScaleMode() == WebGpuScaleMode::Native &&
        framebufferObj && m_canvasWidth > 0 && m_canvasHeight > 0 &&
        ((int)m_canvasWidth  != framebufferObj->width ||
         (int)m_canvasHeight != framebufferObj->height)) {
        m_pendingReset = true;
    }
    {
        if (Input::KeyPressed(SDLK_GRAVE)) {
            auto mouse = Input::GetMousePosition();
            LOG_INFO("renderFrameBuffer diag: fb={}x{} canvas={}x{} Window::GetSize={}x{} mouse=({:.1f},{:.1f}) blitInvScale=({:.3f},{:.3f})",
                framebufferObj ? framebufferObj->width  : -1,
                framebufferObj ? framebufferObj->height : -1,
                (int)m_canvasWidth, (int)m_canvasHeight,
                Window::GetWidth(), Window::GetHeight(),
                mouse.x, mouse.y,
                m_blitInvScaleX, m_blitInvScaleY);
        }
    }
    {
        const float fbW = (float)framebufferObj->width;
        const float fbH = (float)framebufferObj->height;
        const float scW = (float)(m_canvasWidth  > 0 ? m_canvasWidth  : (uint32_t)fbW);
        const float scH = (float)(m_canvasHeight > 0 ? m_canvasHeight : (uint32_t)fbH);

        float blitX = 0.0f, blitY = 0.0f, blitW = scW, blitH = scH;
        float uvX0 = 0.0f,  uvY0 = 0.0f,  uvX1 = 1.0f, uvY1 = 1.0f;

        switch (Window::GetWebGpuScaleMode()) {
        case WebGpuScaleMode::Contain: {
            const float scale = std::min(scW / fbW, scH / fbH);
            blitW = fbW * scale;
            blitH = fbH * scale;
            blitX = (scW - blitW) * 0.5f;
            blitY = (scH - blitH) * 0.5f;
            break;
        }
        case WebGpuScaleMode::Fill: {
            const float scale = std::max(scW / fbW, scH / fbH);
            const float visW  = scW / scale;
            const float visH  = scH / scale;
            uvX0 = (fbW - visW) * 0.5f / fbW;
            uvY0 = (fbH - visH) * 0.5f / fbH;
            uvX1 = uvX0 + visW / fbW;
            uvY1 = uvY0 + visH / fbH;
            break;
        }
        default: break; // Stretch / Native: full quad, full UV
        }

        // Store inverse blit transform for mouse coordinate remapping.
        // Formula: logical = offset + canvas * invScale
        m_blitInvScaleX      = 1.0f;
        m_blitInvScaleY      = 1.0f;
        m_blitLogicalOffsetX = 0.0f;
        m_blitLogicalOffsetY = 0.0f;
        switch (Window::GetWebGpuScaleMode()) {
        case WebGpuScaleMode::Contain: {
            const float sc = std::min(scW / fbW, scH / fbH);
            const float bX = (scW - fbW * sc) * 0.5f;
            const float bY = (scH - fbH * sc) * 0.5f;
            m_blitInvScaleX      = 1.0f / sc;
            m_blitInvScaleY      = 1.0f / sc;
            m_blitLogicalOffsetX = -bX / sc;
            m_blitLogicalOffsetY = -bY / sc;
            break;
        }
        case WebGpuScaleMode::Fill: {
            const float sc = std::max(scW / fbW, scH / fbH);
            m_blitInvScaleX      = 1.0f / sc;
            m_blitInvScaleY      = 1.0f / sc;
            m_blitLogicalOffsetX = (fbW - scW / sc) * 0.5f;
            m_blitLogicalOffsetY = (fbH - scH / sc) * 0.5f;
            break;
        }
        case WebGpuScaleMode::Stretch:
            m_blitInvScaleX = fbW / scW;
            m_blitInvScaleY = fbH / scH;
            break;
        case WebGpuScaleMode::Native:
            break;
        }

        // Blit always uses canvas-space ortho so modes work independently of m_camera
        rtt_uniforms.camera  = glm::ortho(0.0f, scW, scH, 0.0f);
        rtt_uniforms.model   = glm::mat4(
            blitW, 0.0f,  0.0f, 0.0f,
            0.0f,  blitH, 0.0f, 0.0f,
            0.0f,  0.0f,  1.0f, 0.0f,
            blitX, blitY, 0.0f, 1.0f
        );
        rtt_uniforms.flipped = glm::vec2(1.0f, 1.0f);
        // Vertex positions (from WGSL): (1,1),(0,1),(1,0),(0,1),(0,0),(1,0)
        rtt_uniforms.uv0 = glm::vec2(uvX1, uvY1);
        rtt_uniforms.uv1 = glm::vec2(uvX0, uvY1);
        rtt_uniforms.uv2 = glm::vec2(uvX1, uvY0);
        rtt_uniforms.uv3 = glm::vec2(uvX0, uvY1);
        rtt_uniforms.uv4 = glm::vec2(uvX0, uvY0);
        rtt_uniforms.uv5 = glm::vec2(uvX1, uvY0);
    }
#else
    {
        rtt_uniforms.camera  = m_camera;
        rtt_uniforms.model   = glm::mat4(
            Window::GetWidth(),  0.0f,               0.0f, 0.0f,
            0.0f,                Window::GetHeight(), 0.0f, 0.0f,
            0.0f,                0.0f,               1.0f, 0.0f,
            0.0f,                0.0f,               0.0f, 1.0f
        );
        const float uMax     = (float)Window::GetPhysicalWidth()  / (float)framebufferObj->width;
        const float vMax     = (float)Window::GetPhysicalHeight() / (float)framebufferObj->height;
        rtt_uniforms.flipped = glm::vec2(1.0f, 1.0f);
        rtt_uniforms.uv0 = glm::vec2(uMax, vMax);
        rtt_uniforms.uv1 = glm::vec2(0.0f, vMax);
        rtt_uniforms.uv2 = glm::vec2(uMax, 0.0f);
        rtt_uniforms.uv3 = glm::vec2(0.0f, vMax);
        rtt_uniforms.uv4 = glm::vec2(0.0f, 0.0f);
        rtt_uniforms.uv5 = glm::vec2(uMax, 0.0f);
    }
#endif

    GpuColorTargetInfo colorTarget{
        .texture = swapchain_texture,
        .loadOp  = GpuLoadOp::Clear,
        .storeOp = GpuStoreOp::Store,
        .clearR  = 0.0f, .clearG = 0.0f, .clearB = 0.0f, .clearA = 1.0f,
    };

    auto renderPass = m_gpu->beginRenderPass(cmdBuf, &colorTarget, 1, nullptr);
    m_gpu->bindGraphicsPipeline(renderPass, m_rendertotexturepipeline);
    m_gpu->pushVertexUniformData(cmdBuf, 0, &rtt_uniforms, sizeof(rtt_uniforms));
    GpuTextureSamplerBinding binding{ framebufferObj->fbContent, Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode()) };
    m_gpu->bindFragmentSamplers(renderPass, 0, &binding, 1);
    m_gpu->drawPrimitives(renderPass, 6, 1, 0, 0);

    for (const auto& [fbName, fb] : frameBuffers) {
        if (fb->renderToScreen) {
            m_gpu->bindGraphicsPipeline(renderPass, fb->additiveBlend
                ? m_rendertotexturepipeline_additive : m_rendertotexturepipeline);
            m_gpu->pushVertexUniformData(cmdBuf, 0, &rtt_uniforms, sizeof(rtt_uniforms));
            GpuTextureSamplerBinding fbBinding{ fb->fbContent, Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode()) };
            m_gpu->bindFragmentSamplers(renderPass, 0, &fbBinding, 1);
            m_gpu->drawPrimitives(renderPass, 6, 1, 0, 0);
        }
    }

    m_gpu->endRenderPass(renderPass);
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

void Renderer::_createFrameBuffer(const std::string &fbname, uint32_t width, uint32_t height) {
    auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(), [&fbname](const auto &pair) {
        return pair.first == fbname;
    });

    if (it == frameBuffers.end()) {
        int fbWidth, fbHeight;
        if (width > 0 && height > 0) {
            fbWidth  = (int)width;
            fbHeight = (int)height;
        } else {
#ifdef LUMINOVEAU_WEBGPU_BACKEND
            SDL_GetWindowSizeInPixels(Window::GetWindow(), &fbWidth, &fbHeight);
            if (fbWidth <= 0 || fbHeight <= 0) { fbWidth = 1280; fbHeight = 720; }
#else
            SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
            const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(primaryDisplay);
            fbWidth  = displayMode ? (int)(displayMode->w * displayMode->pixel_density) : 3840;
            fbHeight = displayMode ? (int)(displayMode->h * displayMode->pixel_density) : 2160;
#endif
        }

        auto *framebuffer = new FrameBuffer;
        framebuffer->fbContent = AssetHandler::CreateEmptyTexture({(float)fbWidth, (float)fbHeight}).gpuTexture;
        framebuffer->width = fbWidth;
        framebuffer->height = fbHeight;
        framebuffer->fixedSize = (width > 0 && height > 0);
        frameBuffers.emplace_back(fbname, framebuffer);

        framebuffer->textureView.width  = fbWidth;
        framebuffer->textureView.height = fbHeight;
        framebuffer->textureView.gpuTexture = framebuffer->fbContent;
        framebuffer->textureView.gpuSampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode());

        LOG_INFO("Created framebuffer: {} ({}x{})", fbname.c_str(), fbWidth, fbHeight);
    }
}

void Renderer::_createFrameBuffer(const std::string &fbname, uint32_t width, uint32_t height, GpuTextureFormat format) {
    auto it = std::find_if(frameBuffers.begin(), frameBuffers.end(), [&fbname](const auto &pair) {
        return pair.first == fbname;
    });

    if (it == frameBuffers.end()) {
        int fbWidth, fbHeight;
        if (width > 0 && height > 0) {
            fbWidth  = (int)width;
            fbHeight = (int)height;
        } else {
#ifdef LUMINOVEAU_WEBGPU_BACKEND
            SDL_GetWindowSizeInPixels(Window::GetWindow(), &fbWidth, &fbHeight);
            if (fbWidth <= 0 || fbHeight <= 0) { fbWidth = 1280; fbHeight = 720; }
#else
            SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
            const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(primaryDisplay);
            fbWidth  = displayMode ? (int)(displayMode->w * displayMode->pixel_density) : 3840;
            fbHeight = displayMode ? (int)(displayMode->h * displayMode->pixel_density) : 2160;
#endif
        }

        auto *framebuffer = new FrameBuffer;
        framebuffer->fbContent = AssetHandler::CreateEmptyTexture({(float)fbWidth, (float)fbHeight}, format).gpuTexture;
        framebuffer->width = fbWidth;
        framebuffer->height = fbHeight;
        framebuffer->fixedSize = (width > 0 && height > 0);
        frameBuffers.emplace_back(fbname, framebuffer);

        framebuffer->textureView.width  = fbWidth;
        framebuffer->textureView.height = fbHeight;
        framebuffer->textureView.gpuTexture = framebuffer->fbContent;
        framebuffer->textureView.gpuSampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode());

        LOG_INFO("Created framebuffer: {} ({}x{}, custom format)", fbname.c_str(), fbWidth, fbHeight);
    }
}

void Renderer::_setFramebufferRenderToScreen(const std::string& fbName, bool render) {
    auto* framebuffer = _getFramebuffer(fbName);
    if (framebuffer) {
        framebuffer->renderToScreen = render;
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

GpuSamplerHandle Renderer::_getSampler(ScaleMode scaleMode) {
    return _samplers[scaleMode];
}

Texture Renderer::_whitePixel() {
    return _whitePixelTexture;
}

Geometry2D* Renderer::_getQuadGeometry() {
    return Geometry2DFactory::CreateQuad();
}

Geometry2D* Renderer::_getCircleGeometry(int segments) {
    return Geometry2DFactory::CreateCircle(segments);
}

Geometry2D* Renderer::_getRoundedRectGeometry(float cornerRadiusX, float cornerRadiusY, int cornerSegments) {
    return Geometry2DFactory::CreateRoundedRect(cornerRadiusX, cornerRadiusY, cornerSegments);
}

GpuRenderPassHandle Renderer::_getRenderPass(const std::string& passname) {

    GpuRenderPassHandle foundPass = 0;

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

RenderPass* Renderer::_findRenderPass(const std::string& passname) {
    for (auto &[fbName, framebuffer]: frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
                               [&passname](const std::pair<std::string, RenderPass *> &entry) {
                                   return entry.first == passname;
                               });
        if (it != framebuffer->renderpasses.end()) {
            return it->second;
        }
    }
    return nullptr;
}

void Renderer::_setScissorMode(const std::string& passname, const rectf& cliprect) {

        for (auto &[fbName, framebuffer]: frameBuffers) {
        auto it = std::find_if(framebuffer->renderpasses.begin(), framebuffer->renderpasses.end(),
                               [&passname](const std::pair<std::string, RenderPass *> &entry) {
                                   return entry.first == passname;
                               });

        if (it != framebuffer->renderpasses.end()) {
                it->second->_scissorEnabled = true;
                it->second->_scissorX = static_cast<int32_t>(cliprect.x);
                it->second->_scissorY = static_cast<int32_t>(cliprect.y);
                it->second->_scissorW = static_cast<uint32_t>(cliprect.w);
                it->second->_scissorH = static_cast<uint32_t>(cliprect.h);

        }
    }

}

void Renderer::_setSampleCount(GpuSampleCount sampleCount) {
    currentSampleCount = sampleCount;

    _reset();
}

void Renderer::_createSpriteRenderTarget(const std::string& name, const SpriteRenderTargetConfig& config) {
#ifdef LUMINOVEAU_WEBGPU_BACKEND
    std::string framebufferName = name + "_framebuffer";
    if (config.format != GpuTextureFormat::Invalid)
        _createFrameBuffer(framebufferName, config.width, config.height, config.format);
    else
        _createFrameBuffer(framebufferName, config.width, config.height);
    _setFramebufferRenderToScreen(framebufferName, config.renderToScreen);

    auto* fb = _getFramebuffer(framebufferName);
    if (fb) {
        fb->noMSAA          = true;
        fb->preComputeFlush = config.preComputeFlush;
        if (config.renderToScreen && config.blendMode == BlendMode::Additive)
            fb->additiveBlend = true;
    }

    GpuColorTargetBlendState blendState{};
    switch (config.blendMode) {
        case BlendMode::Default:
            blendState = GpuPresets::AlphaBlendKeepDstAlpha;
            break;
        case BlendMode::SrcAlpha:
            blendState = GpuPresets::AlphaBlend;
            break;
        case BlendMode::Additive:
            blendState = {
                .blendEnabled   = true,
                .srcColorFactor = GpuBlendFactor::SrcAlpha,
                .dstColorFactor = GpuBlendFactor::One,
                .colorOp        = GpuBlendOp::Add,
                .srcAlphaFactor = GpuBlendFactor::One,
                .dstAlphaFactor = GpuBlendFactor::One,
                .alphaOp        = GpuBlendOp::Add,
            };
            break;
        case BlendMode::None:
            blendState = {
                .blendEnabled   = false,
                .srcColorFactor = GpuBlendFactor::One,
                .dstColorFactor = GpuBlendFactor::Zero,
                .colorOp        = GpuBlendOp::Add,
                .srcAlphaFactor = GpuBlendFactor::One,
                .dstAlphaFactor = GpuBlendFactor::Zero,
                .alphaOp        = GpuBlendOp::Add,
            };
            break;
    }

    auto* renderPass = new SpriteRenderPass();
    renderPass->UpdateRenderPassBlendState(blendState);

    int rpWidth, rpHeight;
    if (config.width > 0 && config.height > 0) {
        rpWidth  = (int)config.width;
        rpHeight = (int)config.height;
    } else {
        SDL_GetWindowSizeInPixels(Window::GetWindow(), &rpWidth, &rpHeight);
        if (rpWidth <= 0 || rpHeight <= 0) { rpWidth = 1280; rpHeight = 720; }
    }

    GpuTextureFormat passFormat = (config.format != GpuTextureFormat::Invalid)
                                  ? config.format
                                  : m_gpu->getSwapchainFormat();
    renderPass->init(passFormat, rpWidth, rpHeight, name, true,
                     config.maxSprites, /*forceNoMSAA=*/true);

    renderPass->color_target_info_loadop = config.clearOnLoad ? GpuLoadOp::Clear : GpuLoadOp::Load;
    renderPass->color_target_clear_r = config.clearColor.r / 255.0f;
    renderPass->color_target_clear_g = config.clearColor.g / 255.0f;
    renderPass->color_target_clear_b = config.clearColor.b / 255.0f;
    renderPass->color_target_clear_a = config.clearColor.a / 255.0f;

    _attachRenderPassToFrameBuffer(renderPass, name, framebufferName);
    LOG_INFO("Created sprite render target: {}", name.c_str());
    return;
#else
    // Create framebuffer first (use config size if specified, else desktop default)
    std::string framebufferName = name + "_framebuffer";
    if (config.format != GpuTextureFormat::Invalid)
        _createFrameBuffer(framebufferName, config.width, config.height, config.format);
    else
        _createFrameBuffer(framebufferName, config.width, config.height);
    _setFramebufferRenderToScreen(framebufferName, config.renderToScreen);

    // Custom render targets never use MSAA — they are intermediate effect buffers
    // that always render to plain 1x textures.
    auto* fb = _getFramebuffer(framebufferName);
    if (fb) {
        fb->noMSAA          = true;
        fb->preComputeFlush = config.preComputeFlush;
        if (config.renderToScreen && config.blendMode == BlendMode::Additive)
            fb->additiveBlend = true;
    }

    // Convert BlendMode enum to SDL blend state
    SDL_GPUColorTargetBlendState blendState;
    switch (config.blendMode) {
        case BlendMode::Default:
            blendState = toSDL(GpuPresets::AlphaBlendKeepDstAlpha);
            break;
        case BlendMode::SrcAlpha:
            blendState = toSDL(GpuPresets::AlphaBlend);
            break;
        case BlendMode::Additive:
            blendState = {
                .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .color_blend_op = SDL_GPU_BLENDOP_ADD,
                .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                .enable_blend = true,
            };
            break;
        case BlendMode::None:
            blendState = {
                .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                .color_blend_op = SDL_GPU_BLENDOP_ADD,
                .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                .enable_blend = false,
            };
            break;
    }

    // Create and configure render pass
    RenderPass* renderPass = new SpriteRenderPass(m_device);
    static_cast<SpriteRenderPass*>(renderPass)->UpdateRenderPassBlendState(blendState);

    // Determine render pass size — use config size if specified, else desktop size
    int rpWidth, rpHeight;
    if (config.width > 0 && config.height > 0) {
        rpWidth  = (int)config.width;
        rpHeight = (int)config.height;
    } else {
        SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
        const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(primaryDisplay);
        rpWidth  = displayMode ? (int)(displayMode->w * displayMode->pixel_density) : 3840;
        rpHeight = displayMode ? (int)(displayMode->h * displayMode->pixel_density) : 2160;
    }

    renderPass->init(
        config.format != GpuTextureFormat::Invalid
            ? config.format
            : fromSDL(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow())),
        rpWidth,
        rpHeight,
        name,
        true,
        config.maxSprites,
        true  // forceNoMSAA: custom render targets always render to 1x textures
    );

    // Configure load operation and clear color
    renderPass->color_target_info_loadop = config.clearOnLoad ? GpuLoadOp::Clear : GpuLoadOp::Load;
    renderPass->color_target_clear_r = config.clearColor.r / 255.0f;
    renderPass->color_target_clear_g = config.clearColor.g / 255.0f;
    renderPass->color_target_clear_b = config.clearColor.b / 255.0f;
    renderPass->color_target_clear_a = config.clearColor.a / 255.0f;

    // Attach to framebuffer
    _attachRenderPassToFrameBuffer(renderPass, name, framebufferName);

    LOG_INFO("Created sprite render target: {}", name.c_str());
#endif // LUMINOVEAU_WEBGPU_BACKEND
}

void Renderer::_removeSpriteRenderTarget(const std::string& name, bool removeFramebuffer) {
    std::string framebufferName = name + "_framebuffer";

    // Find and remove the render pass from the framebuffer
    auto fbIt = std::find_if(frameBuffers.begin(), frameBuffers.end(),
        [&framebufferName](const auto& pair) {
            return pair.first == framebufferName;
        });

    if (fbIt != frameBuffers.end()) {
        auto& renderpasses = fbIt->second->renderpasses;
        auto passIt = std::find_if(renderpasses.begin(), renderpasses.end(),
            [&name](const auto& pair) {
                return pair.first == name;
            });

        if (passIt != renderpasses.end()) {
            // Release GPU resources
            passIt->second->release();

            // Delete the render pass object
            delete passIt->second;

            // Remove from vector
            renderpasses.erase(passIt);

            LOG_INFO("Removed sprite render target: {}", name.c_str());
        }

        // Optionally remove the framebuffer if requested
        if (removeFramebuffer) {
            // Release framebuffer GPU textures
            if (fbIt->second->fbContent)     m_gpu->releaseTexture(fbIt->second->fbContent);
            if (fbIt->second->fbContentMSAA) m_gpu->releaseTexture(fbIt->second->fbContentMSAA);
            if (fbIt->second->fbDepthMSAA)   m_gpu->releaseTexture(fbIt->second->fbDepthMSAA);

            // Delete framebuffer object
            delete fbIt->second;

            // Remove from vector
            frameBuffers.erase(fbIt);

            LOG_INFO("Removed framebuffer: {}", framebufferName.c_str());
        }
    } else {
        LOG_WARNING("Sprite render target not found: {}", name.c_str());
    }
}
