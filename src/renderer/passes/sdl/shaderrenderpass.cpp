// SDL-backend implementation for ShaderRenderPass.
// Compiled only when LUMINOVEAU_WEBGPU_BACKEND is NOT set.

#include "renderer/passes/shaderrenderpass.h"
#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "gpu/presets.h"
#include "platform/window/window.h"

#include "assets/shaders_generated.h"
#include "renderer/passes/spriterenderpass.h"
#include "renderer/sdl/shaders_sdl.h"
#include "platform/input/input.h"
#include "core/enginestate/enginestate.h"
#include "util/helpers.h"

void ShaderRenderPass::release(bool logRelease) {
    IGpu& gpu = Renderer::GetGpu();

    if (m_depth_texture.gpuTexture)    { gpu.releaseTexture(m_depth_texture.gpuTexture);    m_depth_texture.gpuTexture = 0; }
    if (m_pipeline)                    { gpu.releaseGraphicsPipeline(m_pipeline);           m_pipeline                 = 0; }
    if (resultTexture)                 { gpu.releaseTexture(resultTexture);                 resultTexture              = 0; }
    if (inputTexture)                  { gpu.releaseTexture(inputTexture);                  inputTexture               = 0; }
    if (fs.texture.gpuTexture)         { gpu.releaseTexture(fs.texture.gpuTexture);         fs.texture.gpuTexture      = 0; }
    if (finalrender_pipeline)          { gpu.releaseGraphicsPipeline(finalrender_pipeline); finalrender_pipeline       = 0; }
    if (finalrender_vertex_shader)     { gpu.releaseShader(finalrender_vertex_shader);      finalrender_vertex_shader  = 0; }
    if (finalrender_fragment_shader)   { gpu.releaseShader(finalrender_fragment_shader);    finalrender_fragment_shader = 0; }

    if (logRelease) {
        LOG_INFO("Released graphics pipeline: {}", passname.c_str());
    }
}

void ShaderRenderPass::_loadUniformsFromShader(const std::vector<uint8_t> &/*spirvBinary*/) {
    ShaderMetadata metadata = Shaders::GetShaderMetadata(vertShader.shaderFilename);
    for (const auto& [name, offset] : metadata.uniform_offsets) {
        size_t size = metadata.uniform_sizes.at(name);
        uniformBuffer.addVariable(name, size, offset);
    }
}

void ShaderRenderPass::_loadSamplerNamesFromShader(const std::vector<uint8_t> &/*spirvBinary*/) {
    ShaderMetadata metadata = Shaders::GetShaderMetadata(fragShader.shaderFilename);
    foundSamplers = metadata.sampler_names;
}

bool ShaderRenderPass::init(
    GpuTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name, bool logInit,
    size_t /*capacity*/, bool /*forceNoMSAA*/) {

    passname = std::move(name);
    m_desktop_width  = surface_width;
    m_desktop_height = surface_height;

    IGpu& gpu = Renderer::GetGpu();

    vertex_shader   = vertShader.gpuShader;
    fragment_shader = fragShader.gpuShader;

    _loadUniformsFromShader(vertShader.fileData);
    _loadSamplerNamesFromShader(fragShader.fileData);

    // resultTexture / inputTexture are window-sized (physical pixels).
    resultTexture = AssetHandler::CreateEmptyTexture(Window::GetPhysicalSize()).gpuTexture;
    inputTexture  = AssetHandler::CreateEmptyTexture(Window::GetPhysicalSize()).gpuTexture;

    fs.texture = AssetHandler::CreateEmptyTexture({1, 1});
    fs.x = 0.f; fs.y = 0.f;
    fs.r = 255; fs.g = 255; fs.b = 255; fs.a = 255;

    {
        GpuGraphicsPipelineCreateInfo pci{};
        pci.vertexShader      = vertex_shader;
        pci.fragmentShader    = fragment_shader;
        pci.fillMode          = GpuFillMode::Fill;
        pci.cullMode          = GpuCullMode::None;
        pci.frontFace         = GpuFrontFace::CounterClockwise;
        pci.colorTargetFormat = swapchain_texture_format;
        pci.blend             = GpuPresets::AlphaBlend;
        pci.hasDepthTarget    = false;
        pci.sampleCount       = GpuSampleCount::x1;
        // NOTE: MRT (one per sampler binding) not currently expressible via IGpu — single
        // target used here. If a shader needs MRT, IGpu will need expansion.
        m_pipeline = gpu.createGraphicsPipeline(pci);
        if (!m_pipeline) {
            LOG_CRITICAL("failed to create user shader graphics pipeline");
        }
    }

    #if defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        const char* builtinEntryPoint = "main0";
    #else
        const char* builtinEntryPoint = "main";
    #endif

    if (!finalrender_vertex_shader) {
        GpuShaderCreateInfo vsi{};
        vsi.code                = Luminoveau::Shaders::FullscreenQuad_Vert;
        vsi.codeSize            = Luminoveau::Shaders::FullscreenQuad_Vert_Size;
        vsi.entrypoint          = builtinEntryPoint;
        vsi.stage               = GpuShaderStage::Vertex;
        vsi.samplerCount        = 0;
        vsi.uniformBufferCount  = 1;
        vsi.storageBufferCount  = 0;
        vsi.storageTextureCount = 0;
        finalrender_vertex_shader = gpu.createShader(vsi);
    }

    if (!finalrender_fragment_shader) {
        GpuShaderCreateInfo fsi{};
        fsi.code                = Luminoveau::Shaders::FullscreenQuad_Frag;
        fsi.codeSize            = Luminoveau::Shaders::FullscreenQuad_Frag_Size;
        fsi.entrypoint          = builtinEntryPoint;
        fsi.stage               = GpuShaderStage::Fragment;
        fsi.samplerCount        = 1;
        fsi.uniformBufferCount  = 0;
        fsi.storageBufferCount  = 0;
        fsi.storageTextureCount = 0;
        finalrender_fragment_shader = gpu.createShader(fsi);
    }

    if (!finalrender_pipeline) {
        GpuGraphicsPipelineCreateInfo pci{};
        pci.vertexShader      = finalrender_vertex_shader;
        pci.fragmentShader    = finalrender_fragment_shader;
        pci.fillMode          = GpuFillMode::Fill;
        pci.cullMode          = GpuCullMode::None;
        pci.frontFace         = GpuFrontFace::CounterClockwise;
        pci.colorTargetFormat = gpu.getSwapchainFormat();
        pci.blend             = GpuPresets::AlphaBlendKeepDstAlpha;
        pci.hasDepthTarget    = false;
        pci.sampleCount       = GpuSampleCount::x1;
        finalrender_pipeline = gpu.createGraphicsPipeline(pci);
        if (!finalrender_pipeline) {
            LOG_CRITICAL("failed to create final render graphics pipeline");
        }
    }

    if (logInit) {
        LOG_INFO("created graphics pipeline: {}", passname.c_str());
    }
    return true;
}

void ShaderRenderPass::render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle /*targetTexture*/, const glm::mat4 &camera
) {
    IGpu& gpu = Renderer::GetGpu();
    auto framebuffer = Renderer::GetFramebuffer("primaryFramebuffer");

    // STEP 1: Copy window region of desktop-sized framebuffer to window-sized inputTexture.
    {
        GpuColorTargetInfo ct{};
        ct.texture = inputTexture;
        ct.loadOp  = GpuLoadOp::Clear;
        ct.storeOp = GpuStoreOp::Store;
        ct.clearA  = 1.0f;
        GpuRenderPassHandle copy_pass = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);

        gpu.setViewport(copy_pass, 0.0f, 0.0f,
                        (float)Window::GetPhysicalWidth(), (float)Window::GetPhysicalHeight(),
                        0.0f, 1.0f);
        gpu.bindGraphicsPipeline(copy_pass, finalrender_pipeline);

        glm::mat4 model = glm::mat4(
            (float)Window::GetWidth(), 0.0f, 0.0f, 0.0f,
            0.0f, (float)Window::GetHeight(), 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        );

        float uMax = (float)Window::GetPhysicalWidth()  / (float)m_desktop_width;
        float vMax = (float)Window::GetPhysicalHeight() / (float)m_desktop_height;

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
            float tintColorR;
            float tintColorG;
            float tintColorB;
            float tintColorA;
        };

        Uniforms copy_uniforms{
            .camera     = camera,
            .model      = model,
            .flipped    = glm::vec2(1.0, 1.0),
            .uv0        = glm::vec2(uMax, vMax),
            .uv1        = glm::vec2(0.0,  vMax),
            .uv2        = glm::vec2(uMax, 0.0),
            .uv3        = glm::vec2(0.0,  vMax),
            .uv4        = glm::vec2(0.0,  0.0),
            .uv5        = glm::vec2(uMax, 0.0),
            .tintColorR = 1.0f,
            .tintColorG = 1.0f,
            .tintColorB = 1.0f,
            .tintColorA = 1.0f
        };
        gpu.pushVertexUniformData(cmdBuffer, 0, &copy_uniforms, sizeof(copy_uniforms));

        GpuTextureSamplerBinding binding{ framebuffer->fbContent, Renderer::GetSampler(ScaleMode::Linear) };
        gpu.bindFragmentSamplers(copy_pass, 0, &binding, 1);

        gpu.drawPrimitives(copy_pass, 6, 1, 0, 0);
        gpu.endRenderPass(copy_pass);
    }

    // STEP 2: Run user shader inputTexture → resultTexture.
    {
        std::vector<GpuColorTargetInfo> color_target_info(fragShader.samplerCount);
        for (auto& ct : color_target_info) {
            ct.texture = resultTexture;
            ct.loadOp  = GpuLoadOp::Load;
            ct.storeOp = GpuStoreOp::Store;
            ct.clearR  = color_target_clear_r;
            ct.clearG  = color_target_clear_g;
            ct.clearB  = color_target_clear_b;
            ct.clearA  = color_target_clear_a;
        }

        GpuRenderPassHandle rp = gpu.beginRenderPass(cmdBuffer, color_target_info.data(),
                                                    static_cast<uint32_t>(color_target_info.size()), nullptr);
        render_pass = rp;

        gpu.setViewport(rp, 0.0f, 0.0f,
                        (float)Window::GetPhysicalWidth(), (float)Window::GetPhysicalHeight(),
                        0.0f, 1.0f);

        if (_scissorEnabled) {
            gpu.setScissor(rp, _scissorX, _scissorY, _scissorW, _scissorH);
            _scissorEnabled = false;
        }

        gpu.bindGraphicsPipeline(rp, m_pipeline);

        if (Input::MouseButtonDown(SDL_BUTTON_LEFT)) {
            lastMousePos = Input::GetMousePosition();
        }

        glm::mat4 model = glm::mat4(
            (float)Window::GetWidth(), 0.0f, 0.0f, 0.0f,
            0.0f, (float)Window::GetHeight(), 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.1f, 1.0f
        );

        uniformBuffer["model"]     = model;
        uniformBuffer["camera"]    = camera;
        uniformBuffer["flipped"]   = glm::vec2(1.0, 1.0);
        uniformBuffer["uv"]        = std::array<glm::vec2, 6>{
            glm::vec2(1.0, 1.0),
            glm::vec2(0.0, 1.0),
            glm::vec2(1.0, 0.0),
            glm::vec2(0.0, 1.0),
            glm::vec2(0.0, 0.0),
            glm::vec2(1.0, 0.0),
        };
        uniformBuffer["tintColor"]   = Color(WHITE).asVec4();
        uniformBuffer["iResolution"] = glm::vec3{(float)Window::GetPhysicalWidth(), (float)Window::GetPhysicalHeight(), 0.0f};
        uniformBuffer["iTime"]       = (float)Window::GetRunTime();
        uniformBuffer["iTimeDelta"]  = (float)(Window::GetFrameTime() * 1.0f);
        uniformBuffer["iFrame"]      = (float)EngineState::_frameCount;
        uniformBuffer["iMouse"]      = glm::vec4{Input::GetMousePosition().x, Input::GetMousePosition().y, lastMousePos.x, lastMousePos.y};

        gpu.pushVertexUniformData(cmdBuffer, 0, uniformBuffer.getBufferPointer(), uniformBuffer.getBufferSize());

        std::vector<GpuTextureSamplerBinding> tsbs(fragShader.samplerCount);
        for (auto& tsb : tsbs) {
            tsb.texture = inputTexture;
            tsb.sampler = Renderer::GetSampler(AssetHandler::GetDefaultTextureScaleMode());
        }
        int i = 0;
        for (const auto& sampler : foundSamplers) {
            if (fragShader.frameBufferToSamplerMapping.contains(sampler)) {
                auto fb = Renderer::GetFramebuffer(fragShader.frameBufferToSamplerMapping[sampler]);
                tsbs[i].texture = fb->fbContent;
            }
            i++;
        }
        gpu.bindFragmentSamplers(rp, 0, tsbs.data(), fragShader.samplerCount);
        gpu.drawPrimitives(rp, 6, 1, 0, 0);
        gpu.endRenderPass(rp);
    }

    _renderShaderOutputToFramebuffer(cmdBuffer, framebuffer->fbContent, resultTexture, camera);
}

UniformBuffer &ShaderRenderPass::getUniformBuffer() {
    return uniformBuffer;
}

void ShaderRenderPass::_renderShaderOutputToFramebuffer(GpuCmdBufferHandle cmd_buffer, GpuTextureHandle target_texture, GpuTextureHandle result_texture, const glm::mat4 &camera) {
    if (!fragment_shader) {
        LOG_CRITICAL("missing fragment shader for: {}", passname.c_str());
    }

    IGpu& gpu = Renderer::GetGpu();

    GpuColorTargetInfo ct{};
    ct.texture = target_texture;
    ct.loadOp  = GpuLoadOp::Clear;
    ct.storeOp = GpuStoreOp::Store;
    ct.clearA  = 1.0f;
    GpuRenderPassHandle rp = gpu.beginRenderPass(cmd_buffer, &ct, 1, nullptr);

    gpu.setViewport(rp, 0.0f, 0.0f,
                    (float)Window::GetPhysicalWidth(), (float)Window::GetPhysicalHeight(),
                    0.0f, 1.0f);

    gpu.bindGraphicsPipeline(rp, finalrender_pipeline);

    glm::mat4 model = glm::mat4(
        (float)Window::GetWidth(), 0.0f, 0.0f, 0.0f,
        0.0f, (float)Window::GetHeight(), 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

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
        float tintColorR;
        float tintColorG;
        float tintColorB;
        float tintColorA;
    };

    Uniforms finalrender_uniforms{
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
    gpu.pushVertexUniformData(cmd_buffer, 0, &finalrender_uniforms, sizeof(finalrender_uniforms));

    GpuTextureSamplerBinding tsb{ result_texture, Renderer::GetSampler(ScaleMode::Linear) };
    gpu.bindFragmentSamplers(rp, 0, &tsb, 1);
    gpu.drawPrimitives(rp, 6, 1, 0, 0);
    gpu.endRenderPass(rp);
}
