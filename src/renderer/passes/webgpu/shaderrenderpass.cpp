// WebGPU-backend implementation for ShaderRenderPass.
// Compiled only when LUMINOVEAU_WEBGPU_BACKEND is set.
//
// **Same three steps as the SDL implementation, and deliberately so** — see
// `renderer/passes/sdl/shaderrenderpass.cpp`, which is the reference:
//
//   1. crop the live window region out of the desktop-sized `fbContent` into a window-sized
//      `_inputTexture`,
//   2. run the user's shader `_inputTexture` → `_resultTexture`,
//   3. composite the result back over `fbContent`.
//
// Two things differ, both forced by the backend rather than chosen:
//
// **The blit shaders are WGSL written here, not the engine's embedded SPIR-V.** Steps 1 and 3 on
// SDL use `Lumi::Shaders::FULLSCREEN_QUAD_*`, which are SPIR-V blobs; WebGPU takes WGSL. Rather
// than transpile them, the pair below is written directly against what these two steps actually
// need — a quad from `vertex_index` and a UV scale for the crop — so the sprawling
// camera/model/uv0..uv5/tint uniform block the SDL built-ins carry is not reproduced. Nothing
// else consumes it.
//
// **Uniform reflection comes off the `ShaderAsset`, not the shader cache.** `Shaders::
// GetShaderMetadata` is an SDL-only facility backed by the on-disk SPIR-V cache. On this backend
// `AssetHandler::GetShader` already reflects offsets and sizes into the asset at load time
// (`assets/webgpu/assethandler.cpp`), so the buffer is built from there and the result is the
// same: the game writes `pass->GetUniformBuffer()["hardScan"] = x` by name either way.

#include "renderer/passes/shaderrenderpass.h"

#include "core/enginestate/enginestate.h"
#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "gpu/presets.h"
#include "platform/input/input.h"
#include "platform/window/window.h"
#include "util/helpers.h"

#include <array>
#include <cstring>
#include <vector>

// ── Embedded WGSL: the crop/composite blit ───────────────────────────────────
//
// A full-screen quad built from `vertex_index`, so no vertex buffer is bound and the pipeline
// declares no vertex input at all. `uvScale` crops: step 1 passes the window's fraction of the
// desktop-sized framebuffer, step 3 passes (1, 1).
//
// The V flip is in the UV derivation rather than in the positions — WebGPU's texture origin is
// top-left while clip space is Y-up, so `uv.y` counts down from 1 as `pos.y` counts up.
static constexpr const char *BLIT_VERT_WGSL = R"(
struct BlitUniforms {
    uvScale : vec2<f32>,
    pad     : vec2<f32>,
}

@group(0) @binding(0) var<uniform> blit : BlitUniforms;

struct VertOut {
    @builtin(position) position : vec4<f32>,
    @location(0)       uv       : vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex : u32) -> VertOut {
    var corners = array<vec2<f32>, 6>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 1.0, -1.0),
        vec2<f32>(-1.0,  1.0),
        vec2<f32>(-1.0,  1.0),
        vec2<f32>( 1.0, -1.0),
        vec2<f32>( 1.0,  1.0),
    );

    let p = corners[vertexIndex];

    var out : VertOut;
    out.position = vec4<f32>(p, 0.0, 1.0);
    out.uv       = vec2<f32>((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5) * blit.uvScale;
    return out;
}
)";

// Sampler before texture, matching `_makeSamplerBGL(..., texFirst=false, ...)` in the backend and
// the pair-swap `AssetHandler` applies to Tint's output for game shaders.
static constexpr const char *BLIT_FRAG_WGSL = R"(
@group(2) @binding(0) var gSampler : sampler;
@group(2) @binding(1) var gTexture : texture_2d<f32>;

@fragment
fn fs_main(@location(0) uv : vec2<f32>) -> @location(0) vec4<f32> {
    return textureSample(gTexture, gSampler, uv);
}
)";

namespace {

struct BlitUniforms {
    float uvScaleX = 1.0f;
    float uvScaleY = 1.0f;
    float pad[2]   = { 0.0f, 0.0f };
};

} // namespace

// ── reflection ───────────────────────────────────────────────────────────────

void ShaderRenderPass::_loadUniformsFromShader(const std::vector<uint8_t> & /*spirvBinary*/) {
    for (const auto &[name, offset] : vertShader.uniformOffsets) {
        auto it = vertShader.uniformSizes.find(name);
        if (it == vertShader.uniformSizes.end())
            continue;
        _uniformBuffer.AddVariable(name, it->second, offset);
    }
}

void ShaderRenderPass::_loadSamplerNamesFromShader(const std::vector<uint8_t> & /*spirvBinary*/) {
    // **Reflected names are not available on this backend, and only the framebuffer mapping wants
    // them.** `AssetHandler` reflects counts and uniform offsets but not sampler names, so a pass
    // that routes a named sampler at a named framebuffer cannot be honoured here. Every sampler is
    // bound to `_inputTexture` instead, which is what an unmapped sampler gets on SDL too — so a
    // shader that does not use the mapping behaves identically, and one that does is told rather
    // than silently drawing the wrong texture.
    _foundSamplers.clear();
    if (!fragShader.frameBufferToSamplerMapping.empty()) {
        LOG_WARNING("ShaderRenderPass '{}': frameBufferToSamplerMapping is not supported on the "
                    "WebGPU backend (sampler names are not reflected); all samplers read the pass "
                    "input",
            _passname.c_str());
    }
}

// ── lifecycle ────────────────────────────────────────────────────────────────

bool ShaderRenderPass::Init(GpuTextureFormat swapchainTextureFormat, uint32_t surfaceWidth,
    uint32_t surfaceHeight, std::string name, bool logInit, size_t /*capacity*/,
    bool /*forceNoMSAA*/) {

    _passname      = std::move(name);
    _desktopWidth  = surfaceWidth;
    _desktopHeight = surfaceHeight;

    IGpu &gpu = Renderer::GetGpu();

    _vertexShader   = vertShader.gpuShader;
    _fragmentShader = fragShader.gpuShader;

    if (!_vertexShader || !_fragmentShader) {
        LOG_ERROR("ShaderRenderPass '{}': missing shader, pass not created", _passname.c_str());
        return false;
    }

    _loadUniformsFromShader(vertShader.fileData);
    _loadSamplerNamesFromShader(fragShader.fileData);

    const vf2d size = Window::GetPhysicalSize();
    _resultTexture  = AssetHandler::CreateEmptyTexture(size).gpuTexture;
    _inputTexture   = AssetHandler::CreateEmptyTexture(size).gpuTexture;

    {
        GpuGraphicsPipelineCreateInfo pci {};
        pci.vertexShader      = _vertexShader;
        pci.fragmentShader    = _fragmentShader;
        pci.fillMode          = GpuFillMode::Fill;
        pci.cullMode          = GpuCullMode::None;
        pci.frontFace         = GpuFrontFace::CounterClockwise;
        pci.colorTargetFormat = swapchainTextureFormat;
        pci.blend             = GpuPresets::AlphaBlend;
        pci.hasDepthTarget    = false;
        pci.sampleCount       = GpuSampleCount::X1;
        _pipeline             = gpu.CreateGraphicsPipeline(pci);
        if (!_pipeline) {
            LOG_CRITICAL("failed to create user shader graphics pipeline");
            return false;
        }
    }

    if (!_finalRenderVertexShader) {
        GpuShaderCreateInfo vsi {};
        vsi.code                 = reinterpret_cast<const uint8_t *>(BLIT_VERT_WGSL);
        vsi.codeSize             = std::strlen(BLIT_VERT_WGSL);
        vsi.entrypoint           = "vs_main";
        vsi.stage                = GpuShaderStage::Vertex;
        vsi.samplerCount         = 0;
        vsi.uniformBufferCount   = 1;
        vsi.storageBufferCount   = 0;
        vsi.storageTextureCount  = 0;
        _finalRenderVertexShader = gpu.CreateShader(vsi);
    }

    if (!_finalRenderFragmentShader) {
        GpuShaderCreateInfo fsi {};
        fsi.code                   = reinterpret_cast<const uint8_t *>(BLIT_FRAG_WGSL);
        fsi.codeSize               = std::strlen(BLIT_FRAG_WGSL);
        fsi.entrypoint             = "fs_main";
        fsi.stage                  = GpuShaderStage::Fragment;
        fsi.samplerCount           = 1;
        fsi.uniformBufferCount     = 0;
        fsi.storageBufferCount     = 0;
        fsi.storageTextureCount    = 0;
        _finalRenderFragmentShader = gpu.CreateShader(fsi);
    }

    if (!_finalRenderVertexShader || !_finalRenderFragmentShader) {
        LOG_CRITICAL("ShaderRenderPass '{}': blit shaders failed to compile", _passname.c_str());
        return false;
    }

    if (!_finalRenderPipeline) {
        GpuGraphicsPipelineCreateInfo pci {};
        pci.vertexShader      = _finalRenderVertexShader;
        pci.fragmentShader    = _finalRenderFragmentShader;
        pci.fillMode          = GpuFillMode::Fill;
        pci.cullMode          = GpuCullMode::None;
        pci.frontFace         = GpuFrontFace::CounterClockwise;
        pci.colorTargetFormat = gpu.GetSwapchainFormat();
        pci.blend             = GpuPresets::AlphaBlendKeepDstAlpha;
        pci.hasDepthTarget    = false;
        pci.sampleCount       = GpuSampleCount::X1;
        _finalRenderPipeline  = gpu.CreateGraphicsPipeline(pci);
        if (!_finalRenderPipeline) {
            LOG_CRITICAL("failed to create final render graphics pipeline");
            return false;
        }
    }

    if (logInit) {
        LOG_INFO("created graphics pipeline: {}", _passname.c_str());
    }
    return true;
}

void ShaderRenderPass::Release(bool logRelease) {
    IGpu &gpu = Renderer::GetGpu();

    if (_pipeline) {
        gpu.ReleaseGraphicsPipeline(_pipeline);
        _pipeline = 0;
    }
    if (_resultTexture) {
        gpu.ReleaseTexture(_resultTexture);
        _resultTexture = 0;
    }
    if (_inputTexture) {
        gpu.ReleaseTexture(_inputTexture);
        _inputTexture = 0;
    }
    if (_finalRenderPipeline) {
        gpu.ReleaseGraphicsPipeline(_finalRenderPipeline);
        _finalRenderPipeline = 0;
    }
    if (_finalRenderVertexShader) {
        gpu.ReleaseShader(_finalRenderVertexShader);
        _finalRenderVertexShader = 0;
    }
    if (_finalRenderFragmentShader) {
        gpu.ReleaseShader(_finalRenderFragmentShader);
        _finalRenderFragmentShader = 0;
    }

    if (logRelease) {
        LOG_INFO("Released graphics pipeline: {}", _passname.c_str());
    }
}

void ShaderRenderPass::OnResize(uint32_t surfaceWidth, uint32_t surfaceHeight) {
    if (surfaceWidth == 0 || surfaceHeight == 0)
        return;

    _desktopWidth  = surfaceWidth;
    _desktopHeight = surfaceHeight;

    // Window-sized, so they have to follow the window rather than stay at whatever Init() saw —
    // otherwise step 2 runs the user shader over a texture that no longer covers the window.
    IGpu       &gpu  = Renderer::GetGpu();
    const vf2d  size = Window::GetPhysicalSize();
    if (size.x <= 0.0f || size.y <= 0.0f)
        return;

    if (_resultTexture)
        gpu.ReleaseTexture(_resultTexture);
    if (_inputTexture)
        gpu.ReleaseTexture(_inputTexture);
    _resultTexture = AssetHandler::CreateEmptyTexture(size).gpuTexture;
    _inputTexture  = AssetHandler::CreateEmptyTexture(size).gpuTexture;
}

UniformBuffer &ShaderRenderPass::GetUniformBuffer() {
    return _uniformBuffer;
}

// ── render ───────────────────────────────────────────────────────────────────

void ShaderRenderPass::_renderShaderOutputToFramebuffer(GpuCmdBufferHandle cmdBuffer,
    GpuTextureHandle targetTexture, GpuTextureHandle resultTexture, const glm::mat4 & /*camera*/) {
    IGpu &gpu = Renderer::GetGpu();

    GpuColorTargetInfo ct {};
    ct.texture             = targetTexture;
    ct.loadOp              = GpuLoadOp::Clear;
    ct.storeOp             = GpuStoreOp::Store;
    ct.clearA              = 1.0f;
    GpuRenderPassHandle rp = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);

    gpu.SetViewport(rp, 0.0f, 0.0f,
        (float)Window::GetPhysicalWidth(), (float)Window::GetPhysicalHeight(), 0.0f, 1.0f);
    gpu.BindGraphicsPipeline(rp, _finalRenderPipeline);

    BlitUniforms u {};
    gpu.PushVertexUniformData(cmdBuffer, 0, &u, sizeof(u));

    GpuTextureSamplerBinding tsb { resultTexture, Renderer::GetSampler(ScaleMode::Linear) };
    gpu.BindFragmentSamplers(rp, 0, &tsb, 1);
    gpu.DrawPrimitives(rp, 6, 1, 0, 0);
    gpu.EndRenderPass(rp);
}

void ShaderRenderPass::Render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle /*targetTexture*/, const glm::mat4 &camera) {
    IGpu &gpu = Renderer::GetGpu();
    auto *framebuffer = Renderer::GetFramebuffer("primaryFramebuffer");

    if (!_pipeline || !_finalRenderPipeline || !_inputTexture || !_resultTexture || !framebuffer) {
        return;
    }

    const float physW = (float)Window::GetPhysicalWidth();
    const float physH = (float)Window::GetPhysicalHeight();

    // STEP 1: crop the window region of the desktop-sized framebuffer into _inputTexture.
    {
        GpuColorTargetInfo ct {};
        ct.texture = _inputTexture;
        ct.loadOp  = GpuLoadOp::Clear;
        ct.storeOp = GpuStoreOp::Store;
        ct.clearA  = 1.0f;

        GpuRenderPassHandle copyPass = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
        gpu.SetViewport(copyPass, 0.0f, 0.0f, physW, physH, 0.0f, 1.0f);
        gpu.BindGraphicsPipeline(copyPass, _finalRenderPipeline);

        BlitUniforms u {};
        u.uvScaleX = _desktopWidth ? physW / (float)_desktopWidth : 1.0f;
        u.uvScaleY = _desktopHeight ? physH / (float)_desktopHeight : 1.0f;
        gpu.PushVertexUniformData(cmdBuffer, 0, &u, sizeof(u));

        GpuTextureSamplerBinding binding { framebuffer->fbContent,
            Renderer::GetSampler(ScaleMode::Linear) };
        gpu.BindFragmentSamplers(copyPass, 0, &binding, 1);

        gpu.DrawPrimitives(copyPass, 6, 1, 0, 0);
        gpu.EndRenderPass(copyPass);
    }

    // STEP 2: run the user shader, _inputTexture → _resultTexture.
    {
        GpuColorTargetInfo ct {};
        ct.texture = _resultTexture;
        ct.loadOp  = GpuLoadOp::Load;
        ct.storeOp = GpuStoreOp::Store;
        ct.clearR  = colorTargetClearR;
        ct.clearG  = colorTargetClearG;
        ct.clearB  = colorTargetClearB;
        ct.clearA  = colorTargetClearA;

        GpuRenderPassHandle rp = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);
        renderPass             = rp;

        gpu.SetViewport(rp, 0.0f, 0.0f, physW, physH, 0.0f, 1.0f);

        if (scissorEnabled) {
            gpu.SetScissor(rp, scissorX, scissorY, scissorW, scissorH);
            scissorEnabled = false;
        }

        gpu.BindGraphicsPipeline(rp, _pipeline);

        if (Input::MouseButtonDown(SDL_BUTTON_LEFT)) {
            _lastMousePos = Input::GetMousePosition();
        }

        // The same names the SDL path writes. A shader that does not declare one simply never
        // receives it — `UniformBuffer` drops assignments to names it has no offset for.
        //
        // **The quad sits at z = 0, where the SDL path uses 0.1, and that one digit is the whole
        // difference between a picture and a black screen here.**
        //
        // `_camera` is `glm::ortho(l, r, b, t)`, whose four-argument form sets `Result[2][2] = -1`
        // — so a view-space z of 0.1 becomes a clip z of -0.1. OpenGL and Vulkan clip to z in
        // [-1, 1] and pass it; WebGPU clips to [0, 1] and throws the entire quad away before
        // rasterisation. Nothing reports this: clipping is not an error, so the pipeline is valid,
        // every draw is accepted, and `_resultTexture` simply never gets written.
        //
        // Zero is safe rather than arbitrary: this pass has no depth target (`hasDepthTarget` is
        // false on `_pipeline`), so the value orders nothing and only has to survive the clip.
        glm::mat4 model = glm::mat4(
            (float)Window::GetWidth(), 0.0f, 0.0f, 0.0f,
            0.0f, (float)Window::GetHeight(), 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);

        _uniformBuffer["model"]   = model;
        _uniformBuffer["camera"]  = camera;
        _uniformBuffer["flipped"] = glm::vec2(1.0, 1.0);
        _uniformBuffer["uv"]      = std::array<glm::vec2, 6> {
            glm::vec2(1.0, 1.0),
            glm::vec2(0.0, 1.0),
            glm::vec2(1.0, 0.0),
            glm::vec2(0.0, 1.0),
            glm::vec2(0.0, 0.0),
            glm::vec2(1.0, 0.0),
        };
        _uniformBuffer["tintColor"]   = Color(WHITE).AsVec4();
        _uniformBuffer["iResolution"] = glm::vec3 { physW, physH, 0.0f };
        _uniformBuffer["iTime"]       = (float)Window::GetRunTime();
        _uniformBuffer["iTimeDelta"]  = (float)(Window::GetFrameTime() * 1.0f);
        _uniformBuffer["iFrame"]      = (float)EngineState::frameCount;
        _uniformBuffer["iMouse"]      = glm::vec4 { Input::GetMousePosition().x,
            Input::GetMousePosition().y, _lastMousePos.x, _lastMousePos.y };

        gpu.PushVertexUniformData(cmdBuffer, 0,
            _uniformBuffer.GetBufferPointer(), _uniformBuffer.GetBufferSize());

        const uint32_t samplerCount = fragShader.samplerCount ? fragShader.samplerCount : 1u;
        std::vector<GpuTextureSamplerBinding> tsbs(samplerCount);
        for (auto &tsb : tsbs) {
            tsb.texture = _inputTexture;
            tsb.sampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode());
        }
        gpu.BindFragmentSamplers(rp, 0, tsbs.data(), samplerCount);

        gpu.DrawPrimitives(rp, 6, 1, 0, 0);
        gpu.EndRenderPass(rp);
    }

    // STEP 3: composite the result back over the framebuffer.
    _renderShaderOutputToFramebuffer(cmdBuffer, framebuffer->fbContent, _resultTexture, camera);
}
