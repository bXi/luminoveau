// SDL-backend implementation for ShaderRenderPass.
// Compiled only when LUMINOVEAU_WEBGPU_BACKEND is NOT set.

#include "renderer/passes/shaderrenderpass.h"
#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "gpu/presets.h"
#include "platform/window/window.h"

#include "assets/shaders_generated.h"
#include "renderer/passes/spriterenderpass.h"
#include "renderer/shaders.h"
#include "platform/input/input.h"
#include "core/enginestate/enginestate.h"
#include "util/helpers.h"

void ShaderRenderPass::Release(bool logRelease) {
    IGpu &gpu = Renderer::GetGpu();

    if (_depthTexture.gpuTexture) {
        gpu.ReleaseTexture(_depthTexture.gpuTexture);
        _depthTexture.gpuTexture = 0;
    }
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
    if (_fs.texture.gpuTexture) {
        gpu.ReleaseTexture(_fs.texture.gpuTexture);
        _fs.texture.gpuTexture = 0;
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

    // _resultTexture / _inputTexture are window-sized (physical pixels); recreate them to match
    // the new window size so STEP 2 (the user shader) and the final composite still read/write
    // a texture that actually covers the current window instead of the size at Init() time.
    IGpu   &gpu  = Renderer::GetGpu();
    vf2d    size = Window::GetPhysicalSize();
    if (_resultTexture)
        gpu.ReleaseTexture(_resultTexture);
    if (_inputTexture)
        gpu.ReleaseTexture(_inputTexture);
    _resultTexture = AssetHandler::CreateEmptyTexture(size).gpuTexture;
    _inputTexture  = AssetHandler::CreateEmptyTexture(size).gpuTexture;
}

void ShaderRenderPass::_loadUniformsFromShader(const std::vector<uint8_t> & /*spirvBinary*/) {
    ShaderMetadata metadata = Shaders::GetShaderMetadata(vertShader.shaderFilename);
    for (const auto &[name, offset] : metadata.uniformOffsets) {
        size_t size = metadata.uniformSizes.at(name);
        _uniformBuffer.AddVariable(name, size, offset);
    }
}

void ShaderRenderPass::_loadSamplerNamesFromShader(const std::vector<uint8_t> & /*spirvBinary*/) {
    ShaderMetadata metadata = Shaders::GetShaderMetadata(fragShader.shaderFilename);
    _foundSamplers          = metadata.samplerNames;
}

bool ShaderRenderPass::Init(
    GpuTextureFormat swapchainTextureFormat, uint32_t surfaceWidth, uint32_t surfaceHeight, std::string name, bool logInit,
    size_t /*capacity*/, bool /*forceNoMSAA*/) {

    _passname      = std::move(name);
    _desktopWidth  = surfaceWidth;
    _desktopHeight = surfaceHeight;

    IGpu &gpu = Renderer::GetGpu();

    _vertexShader   = vertShader.gpuShader;
    _fragmentShader = fragShader.gpuShader;

    _loadUniformsFromShader(vertShader.fileData);
    _loadSamplerNamesFromShader(fragShader.fileData);

    // _resultTexture / _inputTexture are window-sized (physical pixels).
    _resultTexture = AssetHandler::CreateEmptyTexture(Window::GetPhysicalSize()).gpuTexture;
    _inputTexture  = AssetHandler::CreateEmptyTexture(Window::GetPhysicalSize()).gpuTexture;

    _fs.texture = AssetHandler::CreateEmptyTexture({ 1, 1 });
    _fs.x       = 0.f;
    _fs.y       = 0.f;
    _fs.r       = 255;
    _fs.g       = 255;
    _fs.b       = 255;
    _fs.a       = 255;

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
        // NOTE: MRT (one per sampler binding) not currently expressible via IGpu — single
        // target used here. If a shader needs MRT, IGpu will need expansion.
        _pipeline = gpu.CreateGraphicsPipeline(pci);
        if (!_pipeline) {
            LOG_CRITICAL("failed to create user shader graphics pipeline");
        }
    }

#if defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
    const char *builtinEntryPoint = "main0";
#else
    const char *builtinEntryPoint = "main";
#endif

    if (!_finalRenderVertexShader) {
        GpuShaderCreateInfo vsi {};
        vsi.code                 = Lumi::Shaders::FULLSCREEN_QUAD_VERT;
        vsi.codeSize             = Lumi::Shaders::FULLSCREEN_QUAD_VERT_SIZE;
        vsi.entrypoint           = builtinEntryPoint;
        vsi.stage                = GpuShaderStage::Vertex;
        vsi.samplerCount         = 0;
        vsi.uniformBufferCount   = 1;
        vsi.storageBufferCount   = 0;
        vsi.storageTextureCount  = 0;
        _finalRenderVertexShader = gpu.CreateShader(vsi);
    }

    if (!_finalRenderFragmentShader) {
        GpuShaderCreateInfo fsi {};
        fsi.code                   = Lumi::Shaders::FULLSCREEN_QUAD_FRAG;
        fsi.codeSize               = Lumi::Shaders::FULLSCREEN_QUAD_FRAG_SIZE;
        fsi.entrypoint             = builtinEntryPoint;
        fsi.stage                  = GpuShaderStage::Fragment;
        fsi.samplerCount           = 1;
        fsi.uniformBufferCount     = 0;
        fsi.storageBufferCount     = 0;
        fsi.storageTextureCount    = 0;
        _finalRenderFragmentShader = gpu.CreateShader(fsi);
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
        }
    }

    if (logInit) {
        LOG_INFO("created graphics pipeline: {}", _passname.c_str());
    }
    return true;
}

void ShaderRenderPass::Render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle /*targetTexture*/, const glm::mat4 &camera) {
    IGpu &gpu         = Renderer::GetGpu();
    auto  framebuffer = Renderer::GetFramebuffer("primaryFramebuffer");

    // STEP 1: Copy window region of desktop-sized framebuffer to window-sized _inputTexture.
    {
        GpuColorTargetInfo ct {};
        ct.texture                   = _inputTexture;
        ct.loadOp                    = GpuLoadOp::Clear;
        ct.storeOp                   = GpuStoreOp::Store;
        ct.clearA                    = 1.0f;
        GpuRenderPassHandle copyPass = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);

        gpu.SetViewport(copyPass, 0.0f, 0.0f,
            (float)Window::GetPhysicalWidth(), (float)Window::GetPhysicalHeight(),
            0.0f, 1.0f);
        gpu.BindGraphicsPipeline(copyPass, _finalRenderPipeline);

        glm::mat4 model = glm::mat4(
            (float)Window::GetWidth(), 0.0f, 0.0f, 0.0f,
            0.0f, (float)Window::GetHeight(), 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);

        float uMax = (float)Window::GetPhysicalWidth() / (float)_desktopWidth;
        float vMax = (float)Window::GetPhysicalHeight() / (float)_desktopHeight;

        struct Uniforms {
            glm::mat4 camera;
            glm::mat4 model;
            glm::vec2 flipped;
            glm::vec2 uv0;
            glm::vec2 uv1;
            glm::vec2 uv2;
            glm::vec2 uv3;
            glm::vec2 uv4;
            glm::vec2 uv5;
            float     tintColorR;
            float     tintColorG;
            float     tintColorB;
            float     tintColorA;
        };

        Uniforms copyUniforms {
            .camera     = camera,
            .model      = model,
            .flipped    = glm::vec2(1.0, 1.0),
            .uv0        = glm::vec2(uMax, vMax),
            .uv1        = glm::vec2(0.0, vMax),
            .uv2        = glm::vec2(uMax, 0.0),
            .uv3        = glm::vec2(0.0, vMax),
            .uv4        = glm::vec2(0.0, 0.0),
            .uv5        = glm::vec2(uMax, 0.0),
            .tintColorR = 1.0f,
            .tintColorG = 1.0f,
            .tintColorB = 1.0f,
            .tintColorA = 1.0f
        };
        gpu.PushVertexUniformData(cmdBuffer, 0, &copyUniforms, sizeof(copyUniforms));

        GpuTextureSamplerBinding binding { framebuffer->fbContent, Renderer::GetSampler(ScaleMode::Linear) };
        gpu.BindFragmentSamplers(copyPass, 0, &binding, 1);

        gpu.DrawPrimitives(copyPass, 6, 1, 0, 0);
        gpu.EndRenderPass(copyPass);
    }

    // STEP 2: Run user shader _inputTexture → _resultTexture.
    {
        std::vector<GpuColorTargetInfo> colorTargetInfo(fragShader.samplerCount);
        for (auto &ct : colorTargetInfo) {
            ct.texture = _resultTexture;
            ct.loadOp  = GpuLoadOp::Load;
            ct.storeOp = GpuStoreOp::Store;
            ct.clearR  = colorTargetClearR;
            ct.clearG  = colorTargetClearG;
            ct.clearB  = colorTargetClearB;
            ct.clearA  = colorTargetClearA;
        }

        GpuRenderPassHandle rp = gpu.BeginRenderPass(cmdBuffer, colorTargetInfo.data(),
            static_cast<uint32_t>(colorTargetInfo.size()), nullptr);
        renderPass             = rp;

        gpu.SetViewport(rp, 0.0f, 0.0f,
            (float)Window::GetPhysicalWidth(), (float)Window::GetPhysicalHeight(),
            0.0f, 1.0f);

        if (scissorEnabled) {
            gpu.SetScissor(rp, scissorX, scissorY, scissorW, scissorH);
            scissorEnabled = false;
        }

        gpu.BindGraphicsPipeline(rp, _pipeline);

        if (Input::MouseButtonDown(SDL_BUTTON_LEFT)) {
            _lastMousePos = Input::GetMousePosition();
        }

        glm::mat4 model = glm::mat4(
            (float)Window::GetWidth(), 0.0f, 0.0f, 0.0f,
            0.0f, (float)Window::GetHeight(), 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.1f, 1.0f);

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
        _uniformBuffer["iResolution"] = glm::vec3 { (float)Window::GetPhysicalWidth(), (float)Window::GetPhysicalHeight(), 0.0f };
        _uniformBuffer["iTime"]       = (float)Window::GetRunTime();
        _uniformBuffer["iTimeDelta"]  = (float)(Window::GetFrameTime() * 1.0f);
        _uniformBuffer["iFrame"]      = (float)EngineState::frameCount;
        _uniformBuffer["iMouse"]      = glm::vec4 { Input::GetMousePosition().x, Input::GetMousePosition().y, _lastMousePos.x, _lastMousePos.y };

        gpu.PushVertexUniformData(cmdBuffer, 0, _uniformBuffer.GetBufferPointer(), _uniformBuffer.GetBufferSize());

        std::vector<GpuTextureSamplerBinding> tsbs(fragShader.samplerCount);
        for (auto &tsb : tsbs) {
            tsb.texture = _inputTexture;
            tsb.sampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode());
        }
        int i = 0;
        for (const auto &sampler : _foundSamplers) {
            if (fragShader.frameBufferToSamplerMapping.contains(sampler)) {
                auto fb         = Renderer::GetFramebuffer(fragShader.frameBufferToSamplerMapping[sampler]);
                tsbs[i].texture = fb->fbContent;
            }
            i++;
        }
        gpu.BindFragmentSamplers(rp, 0, tsbs.data(), fragShader.samplerCount);
        gpu.DrawPrimitives(rp, 6, 1, 0, 0);
        gpu.EndRenderPass(rp);
    }

    _renderShaderOutputToFramebuffer(cmdBuffer, framebuffer->fbContent, _resultTexture, camera);
}

UniformBuffer &ShaderRenderPass::GetUniformBuffer() {
    return _uniformBuffer;
}

void ShaderRenderPass::_renderShaderOutputToFramebuffer(GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, GpuTextureHandle resultTexture, const glm::mat4 &camera) {
    if (!_fragmentShader) {
        LOG_CRITICAL("missing fragment shader for: {}", _passname.c_str());
    }

    IGpu &gpu = Renderer::GetGpu();

    GpuColorTargetInfo ct {};
    ct.texture             = targetTexture;
    ct.loadOp              = GpuLoadOp::Clear;
    ct.storeOp             = GpuStoreOp::Store;
    ct.clearA              = 1.0f;
    GpuRenderPassHandle rp = gpu.BeginRenderPass(cmdBuffer, &ct, 1, nullptr);

    gpu.SetViewport(rp, 0.0f, 0.0f,
        (float)Window::GetPhysicalWidth(), (float)Window::GetPhysicalHeight(),
        0.0f, 1.0f);

    gpu.BindGraphicsPipeline(rp, _finalRenderPipeline);

    glm::mat4 model = glm::mat4(
        (float)Window::GetWidth(), 0.0f, 0.0f, 0.0f,
        0.0f, (float)Window::GetHeight(), 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);

    struct Uniforms {
        glm::mat4 camera;
        glm::mat4 model;
        glm::vec2 flipped;
        glm::vec2 uv0;
        glm::vec2 uv1;
        glm::vec2 uv2;
        glm::vec2 uv3;
        glm::vec2 uv4;
        glm::vec2 uv5;
        float     tintColorR;
        float     tintColorG;
        float     tintColorB;
        float     tintColorA;
    };

    Uniforms finalRenderUniforms {
        .camera     = camera,
        .model      = model,
        .flipped    = glm::vec2(1.0, 1.0),
        .uv0        = glm::vec2(1.0, 1.0),
        .uv1        = glm::vec2(0.0, 1.0),
        .uv2        = glm::vec2(1.0, 0.0),
        .uv3        = glm::vec2(0.0, 1.0),
        .uv4        = glm::vec2(0.0, 0.0),
        .uv5        = glm::vec2(1.0, 0.0),
        .tintColorR = 1.0f,
        .tintColorG = 1.0f,
        .tintColorB = 1.0f,
        .tintColorA = 1.0f
    };
    gpu.PushVertexUniformData(cmdBuffer, 0, &finalRenderUniforms, sizeof(finalRenderUniforms));

    GpuTextureSamplerBinding tsb { resultTexture, Renderer::GetSampler(ScaleMode::Linear) };
    gpu.BindFragmentSamplers(rp, 0, &tsb, 1);
    gpu.DrawPrimitives(rp, 6, 1, 0, 0);
    gpu.EndRenderPass(rp);
}
