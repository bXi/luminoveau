// WebGPU-specific initialisation for Renderer::_initRendering().
// Compiled only when LUMINOVEAU_WEBGPU_BACKEND is set (see cmake/Sources.cmake).

#include "renderer/renderer.h"

#include "gpu/backends/webgpu/WebGpuGpuBackend.h"
#include "assets/assethandler.h"
#include "core/log/log.h"
#include "gpu/presets.h"
#include "platform/input/input.h"
#include "renderer/passes/spriterenderpass.h"
#include "renderer/passes/model3drenderpass.h"
#include "draw/particles.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef LUMINOVEAU_WITH_IMGUI
#include "integrations/imgui/imgui_integration.h"
#endif

#include <cstring>

// ── Embedded WGSL: Render-To-Texture fullscreen quad ─────────────────────────
// Fullscreen-quad vertex shader (WGSL).
// group(0) binding(0) = vertex uniform block (camera + model + UVs).
// Struct padded to 208 bytes (multiple of 16) as required by WebGPU.
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

void Renderer::_initRendering() {
    auto* webgpuBackend = new WebGpuGpuBackend();
    m_gpu.reset(webgpuBackend);
    if (!m_gpu->init(Window::GetWindow())) {
        LOG_CRITICAL("WebGpuGpuBackend::init() failed");
        return;
    }
    // Publish the canvas dims now (before first frame is acquired) so anything
    // running during state-init reads the real swapchain size via Window::GetWidth/Height
    // and Renderer::GetCanvasWidth/Height instead of stale SDL window-creation dims.
    Renderer::SetCanvasSize(webgpuBackend->getInitialCanvasWidth(),
                            webgpuBackend->getInitialCanvasHeight());

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

    // Primary framebuffer is sized to the display so the canvas can grow without ever
    // recreating the texture (matches SDL behavior). Render passes draw into the top-left
    // physical-pixel region; the blit samples that region only.
    uint32_t dw = 0, dh = 0;
    Window::GetDisplayBounds(dw, dh);
    int fbWidth  = (int)dw;
    int fbHeight = (int)dh;
    if (fbWidth <= 0 || fbHeight <= 0) { fbWidth = 1280; fbHeight = 720; }

    auto *framebuffer = new FrameBuffer;
    framebuffer->fbContent = AssetHandler::CreateEmptyTexture({(float)fbWidth, (float)fbHeight}).gpuTexture;
    framebuffer->width  = fbWidth;
    framebuffer->height = fbHeight;

    framebuffer->renderpasses.emplace_back("3dmodels", new Model3DRenderPass());
    framebuffer->renderpasses.back().second->color_target_info_loadop = GpuLoadOp::Clear;
    framebuffer->renderpasses.back().second->color_target_clear_r = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_g = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_b = 0.f;
    framebuffer->renderpasses.back().second->color_target_clear_a = 1.f;
    framebuffer->renderpasses.emplace_back("2dsprites", new SpriteRenderPass());
    framebuffer->renderpasses.back().second->color_target_info_loadop = GpuLoadOp::Load;

    frameBuffers.emplace_back("primaryFramebuffer", framebuffer);

    for (auto &[fbName, fb]: frameBuffers) {
        for (auto &[rpName, rp]: fb->renderpasses) {
            rp->init(m_gpu->getSwapchainFormat(), fbWidth, fbHeight, rpName, true, 0, false);
        }
    }
    // Wait for pipeline compilation before first frame.
    m_gpu->waitIdle();

    _whitePixelTexture = AssetHandler::CreateWhitePixel();

    Particles::Init();

#ifdef LUMINOVEAU_WITH_IMGUI
    ImGuiIntegration::InitRenderer(Window::GetWindow());
#endif
}


// ─── CreateComputePipelineAsset (WebGPU backend) ──────────────────────────────
// Runtime path: load pre-transpiled WGSL + reflect via GLSL→SPIRV (glslang)
// to derive thread counts and resource binding counts that WebGPU BGL requires.

#include "file/filehandler.h"
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_cross.hpp>

#include <memory>
#include <regex>
#include <string>
#include <vector>

ComputePipelineAsset Renderer::CreateComputePipelineAsset(const std::string& shaderPath) {
    // Load pre-transpiled WGSL
    std::string wgslPath = "/_transpiled/" + shaderPath + ".wgsl";
    std::string wgsl = FileHandler::ReadTextFile(wgslPath);
    if (wgsl.empty()) {
        LOG_WARNING("Compute WGSL not found for '{}' (shader unsupported on this backend?)", shaderPath.c_str());
        return {};
    }

    // Remap @group(Nu) numbers from SDL_GPU convention to WebGPU backend layout.
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

    // Load GLSL → SPIR-V for reflection
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

    ComputePipelineAsset asset;
    asset.filename = shaderPath;
    try {
        spirv_cross::Compiler spvComp(spirv);
        auto spvRes = spvComp.get_shader_resources();

        auto entry = spvComp.get_entry_points_and_stages();
        if (!entry.empty()) {
            auto wg  = spvComp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 0);
            auto wgY = spvComp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 1);
            auto wgZ = spvComp.get_execution_mode_argument(spv::ExecutionModeLocalSize, 2);
            asset.threadcount_x = wg  ? wg  : 1;
            asset.threadcount_y = wgY ? wgY : 1;
            asset.threadcount_z = wgZ ? wgZ : 1;
        }

        asset.num_samplers        = static_cast<uint32_t>(spvRes.sampled_images.size());
        asset.num_uniform_buffers = static_cast<uint32_t>(spvRes.uniform_buffers.size());

        uint32_t roSSBO = 0, rwSSBO = 0;
        for (const auto& sb : spvRes.storage_buffers) {
            auto flags = spvComp.get_buffer_block_flags(sb.id);
            if (flags.get(spv::DecorationNonWritable)) ++roSSBO;
            else ++rwSSBO;
        }
        asset.num_readonly_storage_buffers  = roSSBO;
        asset.num_readwrite_storage_buffers = rwSSBO;

        uint32_t roTex = 0, rwTex = 0;
        for (const auto& si : spvRes.storage_images) {
            auto deco = spvComp.get_decoration_bitset(si.id);
            if (deco.get(spv::DecorationNonWritable)) ++roTex;
            else ++rwTex;
        }
        asset.num_readonly_storage_textures  = roTex;
        asset.num_readwrite_storage_textures = rwTex;
    } catch (const std::exception& e) {
        LOG_ERROR("Compute SPIRV reflection failed ({}): {}", shaderPath.c_str(), e.what());
        return {};
    }

    auto wgslFormatToGpu = [](const std::string& f) -> GpuTextureFormat {
        if (f == "rgba8unorm")  return GpuTextureFormat::R8G8B8A8_Unorm;
        if (f == "rgba16float") return GpuTextureFormat::R16G16B16A16_Float;
        if (f == "rgba32float") return GpuTextureFormat::R32G32B32A32_Float;
        if (f == "r32float")    return GpuTextureFormat::R32_Float;
        if (f == "bgra8unorm")  return GpuTextureFormat::B8G8R8A8_Unorm;
        return GpuTextureFormat::R8G8B8A8_Unorm;
    };
    std::vector<GpuTextureFormat> roTexFormats(asset.num_readonly_storage_textures,  GpuTextureFormat::R8G8B8A8_Unorm);
    std::vector<GpuTextureFormat> rwTexFormats(asset.num_readwrite_storage_textures, GpuTextureFormat::R8G8B8A8_Unorm);
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
                rwTexFormats[bnd] = fmt;
                if (rwTexWriteOnly) rwTexWriteOnly[bnd] = (mode == "write");
            }
        }
    }

    GpuComputePipelineCreateInfo ci;
    ci.code                             = reinterpret_cast<const uint8_t*>(wgsl.c_str());
    ci.codeSize                         = wgsl.size();
    ci.entrypoint                       = "main";
    ci.threadCountX                     = asset.threadcount_x;
    ci.threadCountY                     = asset.threadcount_y;
    ci.threadCountZ                     = asset.threadcount_z;
    ci.samplerCount                     = asset.num_samplers;
    ci.readonlyStorageTextureCount      = asset.num_readonly_storage_textures;
    ci.readwriteStorageTextureCount     = asset.num_readwrite_storage_textures;
    ci.readonlyStorageBufferCount       = asset.num_readonly_storage_buffers;
    ci.readwriteStorageBufferCount      = asset.num_readwrite_storage_buffers;
    ci.uniformBufferCount               = asset.num_uniform_buffers;
    ci.readonlyStorageTextureFormats    = roTexFormats.empty() ? nullptr : roTexFormats.data();
    ci.readwriteStorageTextureFormats   = rwTexFormats.empty() ? nullptr : rwTexFormats.data();
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
}
