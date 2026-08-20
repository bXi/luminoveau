// SDL-backend implementation for Model3DRenderPass.
// Compiled only when LUMINOVEAU_WEBGPU_BACKEND is NOT set.

#include "renderer/passes/model3drenderpass.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "gpu/presets.h"
#include "platform/window/window.h"
#include "assets/shaders_generated.h"

#include "gpu/backends/sdl/sdlgpu.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

void Model3DRenderPass::_createShaders() {
    IGpu &gpu = Renderer::GetGpu();

// spirv-cross renames "main" to "main0" in MSL (reserved keyword)
#if defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
    const char *entryPoint = "main0";
#else
    const char *entryPoint = "main";
#endif

    GpuShaderCreateInfo vsi {};
    vsi.code                = Lumi::Shaders::MODEL3D_VERT;
    vsi.codeSize            = Lumi::Shaders::MODEL3D_VERT_SIZE;
    vsi.entrypoint          = entryPoint;
    vsi.stage               = GpuShaderStage::Vertex;
    vsi.samplerCount        = 0;
    vsi.uniformBufferCount  = 1; // InstanceOffset (per-draw base instance)
    vsi.storageBufferCount  = 1; // SceneUniforms at set 0
    vsi.storageTextureCount = 0;
    _vertexShader           = gpu.CreateShader(vsi);

    GpuShaderCreateInfo fsi {};
    fsi.code                = Lumi::Shaders::MODEL3D_FRAG;
    fsi.codeSize            = Lumi::Shaders::MODEL3D_FRAG_SIZE;
    fsi.entrypoint          = entryPoint;
    fsi.stage               = GpuShaderStage::Fragment;
    fsi.samplerCount        = 3; // model texture (0) + directional shadow map (1) + point cube (2)
    fsi.uniformBufferCount  = 1; // LightData (per-pixel lighting inputs)
    fsi.storageBufferCount  = 0;
    fsi.storageTextureCount = 0;
    _fragmentShader         = gpu.CreateShader(fsi);

    // Shadow depth pass shaders.
    GpuShaderCreateInfo svi {};
    svi.code                = Lumi::Shaders::SHADOW_VERT;
    svi.codeSize            = Lumi::Shaders::SHADOW_VERT_SIZE;
    svi.entrypoint          = entryPoint;
    svi.stage               = GpuShaderStage::Vertex;
    svi.samplerCount        = 0;
    svi.uniformBufferCount  = 1; // ShadowParams (lightViewProj + baseInstance)
    svi.storageBufferCount  = 1; // SceneUniforms (models[]) at set 0
    svi.storageTextureCount = 0;
    _shadowVertShader       = gpu.CreateShader(svi);

    GpuShaderCreateInfo sfi {};
    sfi.code                = Lumi::Shaders::SHADOW_FRAG;
    sfi.codeSize            = Lumi::Shaders::SHADOW_FRAG_SIZE;
    sfi.entrypoint          = entryPoint;
    sfi.stage               = GpuShaderStage::Fragment;
    sfi.samplerCount        = 0;
    sfi.uniformBufferCount  = 0;
    sfi.storageBufferCount  = 0;
    sfi.storageTextureCount = 0;
    _shadowFragShader       = gpu.CreateShader(sfi);

    // Cube (point-light) shadow shaders.
    GpuShaderCreateInfo csvi {};
    csvi.code                = Lumi::Shaders::SHADOWCUBE_VERT;
    csvi.codeSize            = Lumi::Shaders::SHADOWCUBE_VERT_SIZE;
    csvi.entrypoint          = entryPoint;
    csvi.stage               = GpuShaderStage::Vertex;
    csvi.samplerCount        = 0;
    csvi.uniformBufferCount  = 1; // CubeShadowParams (faceViewProj + baseInstance)
    csvi.storageBufferCount  = 1; // SceneUniforms (models[]) at set 0
    csvi.storageTextureCount = 0;
    _shadowcubeVertShader    = gpu.CreateShader(csvi);

    GpuShaderCreateInfo csfi {};
    csfi.code                = Lumi::Shaders::SHADOWCUBE_FRAG;
    csfi.codeSize            = Lumi::Shaders::SHADOWCUBE_FRAG_SIZE;
    csfi.entrypoint          = entryPoint;
    csfi.stage               = GpuShaderStage::Fragment;
    csfi.samplerCount        = 0;
    csfi.uniformBufferCount  = 1; // CubeShadowFragParams (lightPos + far)
    csfi.storageBufferCount  = 0;
    csfi.storageTextureCount = 0;
    _shadowcubeFragShader    = gpu.CreateShader(csfi);

    if (!_vertexShader || !_fragmentShader || !_shadowVertShader || !_shadowFragShader
        || !_shadowcubeVertShader || !_shadowcubeFragShader) {
        LOG_ERROR("Model3DRenderPass: shader creation failed");
    }
}

bool Model3DRenderPass::Init(
    GpuTextureFormat swapchainTextureFormat,
    uint32_t         width,
    uint32_t         height,
    std::string      name,
    bool             logInit,
    size_t /*capacity*/,
    bool /*forceNoMSAA*/
) {
    _passname      = std::move(name);
    _surfaceWidth  = width;
    _surfaceHeight = height;

    IGpu &gpu = Renderer::GetGpu();

    {
        GpuTextureCreateInfo depthInfo {};
        depthInfo.width         = width ? width : 1;
        depthInfo.height        = height ? height : 1;
        depthInfo.depthOrLayers = 1;
        depthInfo.numLevels     = 1;
        depthInfo.format        = GpuTextureFormat::D32_Float;
        depthInfo.sampleCount   = GpuSampleCount::X1;
        depthInfo.usage         = GpuTextureUsage::DepthStencilTarget;
        _depthTexture           = gpu.CreateTexture(depthInfo);
        if (!_depthTexture) {
            LOG_ERROR("Model3DRenderPass: depth texture creation failed");
            return false;
        }
    }

    _uniformBuffer         = gpu.CreateBuffer({ sizeof(SceneUniforms), GpuBufferUsage::StorageRead });
    _uniformTransferBuffer = gpu.CreateTransferBuffer({ sizeof(SceneUniforms), GpuTransferUsage::Upload });
    if (!_uniformBuffer || !_uniformTransferBuffer) {
        LOG_ERROR("Model3DRenderPass: uniform buffer creation failed");
        return false;
    }

    _createShaders();
    _currentSampleCount = Renderer::GetSampleCount();

    static GpuVertexAttribute attrs[4] = {
        { .location = 0, .binding = 0, .format = GpuVertexElementFormat::Float3, .offset = 0 },
        { .location = 1, .binding = 0, .format = GpuVertexElementFormat::Float3, .offset = 12 },
        { .location = 2, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 24 },
        { .location = 3, .binding = 0, .format = GpuVertexElementFormat::Float4, .offset = 32 },
    };
    static GpuVertexBinding vbind = { .binding = 0, .stride = sizeof(Vertex3D), .instanceStepping = false };

    GpuGraphicsPipelineCreateInfo pci {};
    pci.vertexShader             = _vertexShader;
    pci.fragmentShader           = _fragmentShader;
    pci.attributes               = attrs;
    pci.attributeCount           = 4;
    pci.bindings                 = &vbind;
    pci.bindingCount             = 1;
    pci.fillMode                 = GpuFillMode::Fill;
    pci.cullMode                 = GpuCullMode::None;
    pci.frontFace                = GpuFrontFace::CounterClockwise;
    pci.colorTargetFormat        = swapchainTextureFormat;
    pci.blend                    = GpuPresets::AlphaBlendKeepDstAlpha;
    pci.hasDepthTarget           = true;
    pci.depthTargetFormat        = GpuTextureFormat::D32_Float;
    pci.sampleCount              = _currentSampleCount;
    pci.vertexStorageBufferCount = 1;
    _pipeline                    = gpu.CreateGraphicsPipeline(pci);
    if (!_pipeline) {
        LOG_ERROR("Failed to create graphics pipeline: {}", SDL_GetError());
        return false;
    }

    // ── Shadow resources (directional caster) ────────────────────────────────
    {
        GpuTextureCreateInfo sc {};
        sc.width        = SHADOW_RES;
        sc.height       = SHADOW_RES;
        sc.format       = GpuTextureFormat::R32_Float;
        sc.usage        = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
        _shadowColorTex = gpu.CreateTexture(sc);

        GpuTextureCreateInfo sd {};
        sd.width        = SHADOW_RES;
        sd.height       = SHADOW_RES;
        sd.format       = GpuTextureFormat::D32_Float;
        sd.usage        = GpuTextureUsage::DepthStencilTarget;
        _shadowDepthTex = gpu.CreateTexture(sd);

        GpuSamplerCreateInfo ss {};
        // Nearest: never linearly average raw depth (that under-reads on sloped surfaces and makes
        // everything self-shadow). Softening is done by PCF in the shader instead.
        ss.minFilter   = GpuFilter::Nearest;
        ss.magFilter   = GpuFilter::Nearest;
        ss.mipFilter   = GpuFilter::Nearest;
        ss.addressU    = GpuSamplerAddressMode::ClampToEdge;
        ss.addressV    = GpuSamplerAddressMode::ClampToEdge;
        ss.addressW    = GpuSamplerAddressMode::ClampToEdge;
        _shadowSampler = gpu.CreateSampler(ss);

        GpuGraphicsPipelineCreateInfo spci {};
        spci.vertexShader             = _shadowVertShader;
        spci.fragmentShader           = _shadowFragShader;
        spci.attributes               = attrs;
        spci.attributeCount           = 4;
        spci.bindings                 = &vbind;
        spci.bindingCount             = 1;
        spci.fillMode                 = GpuFillMode::Fill;
        spci.cullMode                 = GpuCullMode::None;
        spci.frontFace                = GpuFrontFace::CounterClockwise;
        spci.colorTargetFormat        = GpuTextureFormat::R32_Float;
        spci.blend                    = GpuPresets::Opaque;
        spci.hasDepthTarget           = true;
        spci.depthTargetFormat        = GpuTextureFormat::D32_Float;
        spci.sampleCount              = GpuSampleCount::X1;
        spci.vertexStorageBufferCount = 1;
        _shadowPipeline               = gpu.CreateGraphicsPipeline(spci);
        if (!_shadowColorTex || !_shadowDepthTex || !_shadowPipeline) {
            LOG_ERROR("Model3DRenderPass: shadow resource creation failed");
            return false;
        }
    }

    // ── Point-light cube shadow resources ────────────────────────────────────
    {
        GpuTextureCreateInfo cc {};
        cc.width         = CUBE_SHADOW_RES;
        cc.height        = CUBE_SHADOW_RES;
        cc.depthOrLayers = 6; // cube = 6 faces
        cc.format        = GpuTextureFormat::R32_Float;
        cc.usage         = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
        cc.type          = GpuTextureType::TexCube;
        _shadowCubeTex   = gpu.CreateTexture(cc);

        GpuTextureCreateInfo cd {};
        cd.width            = CUBE_SHADOW_RES;
        cd.height           = CUBE_SHADOW_RES;
        cd.format           = GpuTextureFormat::D32_Float;
        cd.usage            = GpuTextureUsage::DepthStencilTarget;
        _shadowCubeDepthTex = gpu.CreateTexture(cd); // reused per face

        GpuSamplerCreateInfo cs {};
        cs.minFilter       = GpuFilter::Nearest;
        cs.magFilter       = GpuFilter::Nearest;
        cs.mipFilter       = GpuFilter::Nearest;
        cs.addressU        = GpuSamplerAddressMode::ClampToEdge;
        cs.addressV        = GpuSamplerAddressMode::ClampToEdge;
        cs.addressW        = GpuSamplerAddressMode::ClampToEdge;
        _shadowCubeSampler = gpu.CreateSampler(cs);

        GpuGraphicsPipelineCreateInfo cpci {};
        cpci.vertexShader             = _shadowcubeVertShader;
        cpci.fragmentShader           = _shadowcubeFragShader;
        cpci.attributes               = attrs;
        cpci.attributeCount           = 4;
        cpci.bindings                 = &vbind;
        cpci.bindingCount             = 1;
        cpci.fillMode                 = GpuFillMode::Fill;
        cpci.cullMode                 = GpuCullMode::None;
        cpci.frontFace                = GpuFrontFace::CounterClockwise;
        cpci.colorTargetFormat        = GpuTextureFormat::R32_Float;
        cpci.blend                    = GpuPresets::Opaque;
        cpci.hasDepthTarget           = true;
        cpci.depthTargetFormat        = GpuTextureFormat::D32_Float;
        cpci.sampleCount              = GpuSampleCount::X1;
        cpci.vertexStorageBufferCount = 1;
        _cubeShadowPipeline           = gpu.CreateGraphicsPipeline(cpci);
        if (!_shadowCubeTex || !_shadowCubeDepthTex || !_cubeShadowPipeline) {
            LOG_ERROR("Model3DRenderPass: cube shadow resource creation failed");
            return false;
        }
    }

    if (logInit) {
        LOG_INFO("Model3DRenderPass initialized: {}", _passname);
    }
    return true;
}

void Model3DRenderPass::Release(bool logRelease) {
    IGpu &gpu = Renderer::GetGpu();

    if (_msaaColorTexture) {
        gpu.ReleaseTexture(_msaaColorTexture);
        _msaaColorTexture = 0;
    }
    if (_msaaDepthTexture) {
        gpu.ReleaseTexture(_msaaDepthTexture);
        _msaaDepthTexture = 0;
    }
    if (_depthTexture) {
        gpu.ReleaseTexture(_depthTexture);
        _depthTexture = 0;
    }
    if (_uniformBuffer) {
        gpu.ReleaseBuffer(_uniformBuffer);
        _uniformBuffer = 0;
    }
    if (_uniformTransferBuffer) {
        gpu.ReleaseTransferBuffer(_uniformTransferBuffer);
        _uniformTransferBuffer = 0;
    }
    if (_pipeline) {
        gpu.ReleaseGraphicsPipeline(_pipeline);
        _pipeline = 0;
    }
    if (_vertexShader) {
        gpu.ReleaseShader(_vertexShader);
        _vertexShader = 0;
    }
    if (_fragmentShader) {
        gpu.ReleaseShader(_fragmentShader);
        _fragmentShader = 0;
    }

    if (_shadowPipeline) {
        gpu.ReleaseGraphicsPipeline(_shadowPipeline);
        _shadowPipeline = 0;
    }
    if (_shadowVertShader) {
        gpu.ReleaseShader(_shadowVertShader);
        _shadowVertShader = 0;
    }
    if (_shadowFragShader) {
        gpu.ReleaseShader(_shadowFragShader);
        _shadowFragShader = 0;
    }
    if (_shadowColorTex) {
        gpu.ReleaseTexture(_shadowColorTex);
        _shadowColorTex = 0;
    }
    if (_shadowDepthTex) {
        gpu.ReleaseTexture(_shadowDepthTex);
        _shadowDepthTex = 0;
    }
    if (_shadowSampler) {
        gpu.ReleaseSampler(_shadowSampler);
        _shadowSampler = 0;
    }

    if (_cubeShadowPipeline) {
        gpu.ReleaseGraphicsPipeline(_cubeShadowPipeline);
        _cubeShadowPipeline = 0;
    }
    if (_shadowcubeVertShader) {
        gpu.ReleaseShader(_shadowcubeVertShader);
        _shadowcubeVertShader = 0;
    }
    if (_shadowcubeFragShader) {
        gpu.ReleaseShader(_shadowcubeFragShader);
        _shadowcubeFragShader = 0;
    }
    if (_shadowCubeTex) {
        gpu.ReleaseTexture(_shadowCubeTex);
        _shadowCubeTex = 0;
    }
    if (_shadowCubeDepthTex) {
        gpu.ReleaseTexture(_shadowCubeDepthTex);
        _shadowCubeDepthTex = 0;
    }
    if (_shadowCubeSampler) {
        gpu.ReleaseSampler(_shadowCubeSampler);
        _shadowCubeSampler = 0;
    }

    if (logRelease) {
        LOG_INFO("Released 3D model render pass");
    }
}

void Model3DRenderPass::_uploadModelToGPU(ModelAsset *model) {
    if (!model || model->vertices.empty() || model->indices.empty())
        return;
    if (model->vertexBuffer && model->indexBuffer)
        return; // already uploaded

    IGpu    &gpu   = Renderer::GetGpu();
    uint32_t vSize = static_cast<uint32_t>(model->vertices.size() * sizeof(Vertex3D));
    uint32_t iSize = static_cast<uint32_t>(model->indices.size() * sizeof(uint32_t));

    model->vertexBuffer = gpu.CreateBuffer({ vSize, GpuBufferUsage::Vertex });
    model->indexBuffer  = gpu.CreateBuffer({ iSize, GpuBufferUsage::Index });

    GpuTransferBufferHandle vXfer = gpu.CreateTransferBuffer({ vSize, GpuTransferUsage::Upload });
    std::memcpy(gpu.MapTransferBuffer(vXfer, false), model->vertices.data(), vSize);
    gpu.UnmapTransferBuffer(vXfer);

    GpuTransferBufferHandle iXfer = gpu.CreateTransferBuffer({ iSize, GpuTransferUsage::Upload });
    std::memcpy(gpu.MapTransferBuffer(iXfer, false), model->indices.data(), iSize);
    gpu.UnmapTransferBuffer(iXfer);

    GpuCmdBufferHandle cmd = gpu.AcquireCommandBuffer();
    gpu.UploadToBuffer(cmd, vXfer, 0, model->vertexBuffer, 0, vSize);
    gpu.UploadToBuffer(cmd, iXfer, 0, model->indexBuffer, 0, iSize);
    gpu.SubmitCommandBuffer(cmd);
    gpu.WaitIdle();
    gpu.ReleaseTransferBuffer(vXfer);
    gpu.ReleaseTransferBuffer(iXfer);
}

void Model3DRenderPass::Render(
    GpuCmdBufferHandle cmdBuffer,
    GpuTextureHandle   targetTexture,
    const glm::mat4 & /*camera_unused*/
) {
    IGpu &gpu = Renderer::GetGpu();

    Camera3D                   &camera  = Scene::GetCamera();
    std::vector<ModelInstance> &models  = Scene::GetModels();
    std::vector<Light>         &lights  = Scene::GetLights();
    Color                       ambient = Scene::GetAmbientLight();

    SceneUniforms u {};
    LightData     lightData {};
    if (!models.empty() && _pipeline) {
        // Matches the viewport, so an off-screen target of a different shape than the window
        // is not projected with the window's aspect and stretched.
        const float aspect = (viewportWidth && viewportHeight)
            ? (float)viewportWidth / (float)viewportHeight
            : (float)Window::GetWidth() / (float)Window::GetHeight();
        u.viewProj   = camera.GetViewProjectionMatrix(aspect);
        u.modelCount = std::min((int)models.size(), 16);
        for (int i = 0; i < u.modelCount; ++i)
            u.models[i] = models[i].GetModelMatrix();
        u.cameraPos    = glm::vec4(camera.position.x, camera.position.y, camera.position.z, 1.0f);
        u.ambientLight = glm::vec4(ambient.r / 255.f, ambient.g / 255.f, ambient.b / 255.f, ambient.a / 255.f);
        u.lightCount   = std::min((int)lights.size(), 4);
        for (int i = 0; i < u.lightCount; ++i) {
            const Light &light = lights[i];
            if (light.type == LightType::Directional)
                u.lightPositions[i] = glm::vec4(light.direction.x, light.direction.y, light.direction.z, (float)light.type);
            else
                u.lightPositions[i] = glm::vec4(light.position.x, light.position.y, light.position.z, (float)light.type);
            u.lightColors[i] = glm::vec4(light.color.r / 255.f, light.color.g / 255.f, light.color.b / 255.f, light.intensity);
            u.lightParams[i] = glm::vec4(light.constant, light.linear, light.quadratic, 0.0f);
        }

        // Directional shadow caster = the first directional light. Build an orthographic
        // view-projection from its direction, fitted around the scene origin. Self-consistent:
        // the same matrix drives the shadow Render (shadow.vert) and the lookup (model3d.frag).
        int       shadowLight = -1;
        glm::mat4 lightViewProj(1.0f);
        for (int i = 0; i < u.lightCount; ++i)
            if (lights[i].type == LightType::Directional) {
                shadowLight = i;
                break;
            }
        if (shadowLight >= 0) {
            glm::vec3       dir = glm::normalize(glm::vec3(lights[shadowLight].direction.x,
                      lights[shadowLight].direction.y,
                      lights[shadowLight].direction.z));
            const glm::vec3 center(0.0f);
            // Fit the frustum tightly around the scene: a loose near/far crushes all depth into a
            // sliver of [0,1], destroying the pillar-vs-floor depth difference the compare needs.
            // ext fits the scene + its cast shadows tightly so the shadow map isn't wasting texels
            // on empty margin (bigger ext = blurrier shadows for a given resolution).
            const float dist = 45.0f, ext = 29.0f, nearP = 12.0f, farP = 82.0f;
            // Directional light vectors point TOWARD the light source (engine convention), so the
            // light sits in the +dir direction — place the shadow camera there looking back.
            glm::vec3 eye = center + dir * dist;
            glm::vec3 up  = (std::fabs(dir.y) > 0.95f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            // Zero-to-one depth ortho (SDL_GPU depth buffers are [0,1]); plain glm::ortho hands
            // back [-1,1] which breaks the shadow pass's depth test (nearest occluder unresolved).
            lightViewProj = glm::orthoRH_ZO(-ext, ext, -ext, ext, nearP, farP)
                * glm::lookAtRH(eye, center, up);
        }

        // Point (cube) shadow caster = the first point light. Its cube map stores linear distance
        // to the light, normalized by POINT_FAR.
        int       pointShadowLight = -1;
        glm::vec3 pointPos(0.0f);
        for (int i = 0; i < u.lightCount; ++i)
            if (lights[i].type == LightType::Point) {
                pointShadowLight = i;
                pointPos         = glm::vec3(lights[i].position.x, lights[i].position.y, lights[i].position.z);
                break;
            }

        // Same light data, packed for the per-pixel fragment shader (LightData cbuffer).
        lightData.shadowViewProj   = lightViewProj;
        lightData.shadowLight      = shadowLight;
        lightData.pointShadowLight = pointShadowLight;
        lightData.pointLightPosFar = glm::vec4(pointPos, POINT_FAR);
        lightData.cameraPos        = u.cameraPos;
        lightData.ambientLight     = u.ambientLight;
        lightData.lightCount       = u.lightCount;
        for (int i = 0; i < u.lightCount; ++i) {
            lightData.lightPositions[i] = u.lightPositions[i];
            lightData.lightColors[i]    = u.lightColors[i];
            lightData.lightParams[i]    = u.lightParams[i];
        }

        if (_uniformTransferBuffer && _uniformBuffer) {
            std::memcpy(gpu.MapTransferBuffer(_uniformTransferBuffer, false), &u, sizeof(u));
            gpu.UnmapTransferBuffer(_uniformTransferBuffer);
            gpu.UploadToBuffer(cmdBuffer, _uniformTransferBuffer, 0, _uniformBuffer, 0, sizeof(u));
        }

        for (auto &inst : models)
            if (inst.model)
                _uploadModelToGPU(inst.model);

        // ── Shadow depth pass: render the scene from the directional light into the shadow map ──
        if (shadowLight >= 0 && _shadowPipeline) {
            GpuColorTargetInfo sct {};
            sct.texture = _shadowColorTex;
            sct.loadOp  = GpuLoadOp::Clear;
            sct.storeOp = GpuStoreOp::Store;
            sct.clearR  = 1.0f;
            sct.clearG  = 1.0f;
            sct.clearB  = 1.0f;
            sct.clearA  = 1.0f; // far

            GpuDepthStencilTargetInfo sdt {};
            sdt.texture    = _shadowDepthTex;
            sdt.loadOp     = GpuLoadOp::Clear;
            sdt.storeOp    = GpuStoreOp::DontCare;
            sdt.clearDepth = 1.0f;

            GpuRenderPassHandle sp = gpu.BeginRenderPass(cmdBuffer, &sct, 1, &sdt);
            gpu.SetViewport(sp, 0.0f, 0.0f, (float)SHADOW_RES, (float)SHADOW_RES, 0.0f, 1.0f);
            gpu.BindGraphicsPipeline(sp, _shadowPipeline);
            gpu.BindVertexStorageBuffers(sp, 0, &_uniformBuffer, 1);

            const size_t maxM = std::min(models.size(), static_cast<size_t>(16));
            size_t       rs   = 0;
            while (rs < maxM) {
                const ModelAsset *mesh = models[rs].model;
                size_t            re   = rs + 1;
                while (re < maxM && models[re].model == mesh)
                    ++re;
                uint32_t rc = static_cast<uint32_t>(re - rs);
                if (mesh && mesh->vertexBuffer && mesh->indexBuffer) {
                    GpuBufferBinding vb { mesh->vertexBuffer, 0 };
                    gpu.BindVertexBuffers(sp, 0, &vb, 1);
                    GpuBufferBinding ib { mesh->indexBuffer, 0 };
                    gpu.BindIndexBuffer(sp, ib, false);

                    ShadowParams spp {};
                    spp.lightViewProj = lightViewProj;
                    spp.baseInstance  = static_cast<uint32_t>(rs);
                    gpu.PushVertexUniformData(cmdBuffer, 0, &spp, sizeof(spp));

                    gpu.DrawIndexedPrimitives(sp, static_cast<uint32_t>(mesh->indices.size()), rc, 0, 0, 0);
                }
                rs = re;
            }
            gpu.EndRenderPass(sp);
        }

        // ── Point-light cube shadow pass: render the scene into the 6 cube faces ──
        if (pointShadowLight >= 0 && _cubeShadowPipeline) {
            // Cube faces must match HLSL TextureCube.Sample's D3D (left-handed) convention, so use
            // LH view + projection and the standard D3D face orientations. Layer order is
            // +X,-X,+Y,-Y,+Z,-Z.
            glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(90.0f), 1.0f, 0.5f, POINT_FAR);
            struct Face {
                glm::vec3 dir, up;
            };
            const Face faces[6] = {
                { { 1, 0, 0 }, { 0, 1, 0 } },  // +X
                { { -1, 0, 0 }, { 0, 1, 0 } }, // -X
                { { 0, 1, 0 }, { 0, 0, -1 } }, // +Y
                { { 0, -1, 0 }, { 0, 0, 1 } }, // -Y
                { { 0, 0, 1 }, { 0, 1, 0 } },  // +Z
                { { 0, 0, -1 }, { 0, 1, 0 } }, // -Z
            };
            CubeShadowFragParams cfp {};
            cfp.lightPosFar = glm::vec4(pointPos, POINT_FAR);

            for (int f = 0; f < 6; ++f) {
                glm::mat4 faceVP = proj * glm::lookAtLH(pointPos, pointPos + faces[f].dir, faces[f].up);

                GpuColorTargetInfo cct {};
                cct.texture = _shadowCubeTex;
                cct.layer   = static_cast<uint32_t>(f); // render into cube face f
                cct.loadOp  = GpuLoadOp::Clear;
                cct.storeOp = GpuStoreOp::Store;
                cct.clearR  = 1.0f;
                cct.clearG  = 1.0f;
                cct.clearB  = 1.0f;
                cct.clearA  = 1.0f; // far

                GpuDepthStencilTargetInfo cdt {};
                cdt.texture    = _shadowCubeDepthTex;
                cdt.loadOp     = GpuLoadOp::Clear;
                cdt.storeOp    = GpuStoreOp::DontCare;
                cdt.clearDepth = 1.0f;

                GpuRenderPassHandle cp = gpu.BeginRenderPass(cmdBuffer, &cct, 1, &cdt);
                gpu.SetViewport(cp, 0.0f, 0.0f, (float)CUBE_SHADOW_RES, (float)CUBE_SHADOW_RES, 0.0f, 1.0f);
                gpu.BindGraphicsPipeline(cp, _cubeShadowPipeline);
                gpu.BindVertexStorageBuffers(cp, 0, &_uniformBuffer, 1);
                gpu.PushFragmentUniformData(cmdBuffer, 0, &cfp, sizeof(cfp));

                const size_t maxM = std::min(models.size(), static_cast<size_t>(16));
                size_t       rs   = 0;
                while (rs < maxM) {
                    const ModelAsset *mesh = models[rs].model;
                    size_t            re   = rs + 1;
                    while (re < maxM && models[re].model == mesh)
                        ++re;
                    uint32_t rc = static_cast<uint32_t>(re - rs);
                    if (mesh && mesh->vertexBuffer && mesh->indexBuffer) {
                        GpuBufferBinding vb { mesh->vertexBuffer, 0 };
                        gpu.BindVertexBuffers(cp, 0, &vb, 1);
                        GpuBufferBinding ib { mesh->indexBuffer, 0 };
                        gpu.BindIndexBuffer(cp, ib, false);

                        CubeShadowParams csp {};
                        csp.faceViewProj = faceVP;
                        csp.baseInstance = static_cast<uint32_t>(rs);
                        gpu.PushVertexUniformData(cmdBuffer, 0, &csp, sizeof(csp));

                        gpu.DrawIndexedPrimitives(cp, static_cast<uint32_t>(mesh->indices.size()), rc, 0, 0, 0);
                    }
                    rs = re;
                }
                gpu.EndRenderPass(cp);
            }
        }
    }

    bool shouldResolve = (renderTargetResolve != 0);

    GpuColorTargetInfo ct {};
    ct.texture        = targetTexture;
    ct.resolveTexture = renderTargetResolve;
    ct.loadOp         = colorTargetInfoLoadOp;
    ct.storeOp        = shouldResolve ? GpuStoreOp::Resolve : GpuStoreOp::Store;
    ct.clearR         = colorTargetClearR;
    ct.clearG         = colorTargetClearG;
    ct.clearB         = colorTargetClearB;
    ct.clearA         = colorTargetClearA;

    GpuDepthStencilTargetInfo dt {};
    dt.texture    = renderTargetDepth ? renderTargetDepth : _depthTexture;
    dt.loadOp     = GpuLoadOp::Clear;
    dt.storeOp    = GpuStoreOp::Store;
    dt.clearDepth = 1.0f;

    GpuRenderPassHandle rp = gpu.BeginRenderPass(cmdBuffer, &ct, 1, &dt);
    renderPass             = rp;

    // Falls back to the window when no viewport is set, which is what every existing caller
    // relies on; an explicit one is how a caller renders into an off-screen target.
    gpu.SetViewport(rp, 0.0f, 0.0f,
        viewportWidth ? (float)viewportWidth : (float)Window::GetPhysicalWidth(),
        viewportHeight ? (float)viewportHeight : (float)Window::GetPhysicalHeight(),
        0.0f, 1.0f);

    if (models.empty() || !_pipeline) {
        gpu.EndRenderPass(rp);
        return;
    }

    gpu.BindGraphicsPipeline(rp, _pipeline);
    gpu.BindVertexStorageBuffers(rp, 0, &_uniformBuffer, 1);

    // Lighting inputs for the per-pixel fragment shader (fragment uniform slot 0).
    gpu.PushFragmentUniformData(cmdBuffer, 0, &lightData, sizeof(lightData));

    GpuSamplerHandle sampler = Renderer::GetSampler(ScaleMode::Linear);

    auto effectiveTexture = [](const ModelInstance &m) -> GpuTextureHandle {
        if (m.textureOverride.gpuTexture)
            return m.textureOverride.gpuTexture;
        if (m.model && m.model->texture.gpuTexture)
            return m.model->texture.gpuTexture;
        return Renderer::WhitePixel().gpuTexture;
    };

    // Draw contiguous runs sharing the same mesh + texture as one instanced call. The shader
    // reads models[instanceIndex + baseInstance], so a run's matrices stay contiguous in the
    // uniform array (uploaded in original order). Capped at 16 — the size of SceneUniforms.models.
    const size_t maxModels = std::min(models.size(), static_cast<size_t>(16));
    size_t       runStart  = 0;
    while (runStart < maxModels) {
        const ModelAsset *mesh = models[runStart].model;
        GpuTextureHandle  tex  = effectiveTexture(models[runStart]);

        size_t runEnd = runStart + 1;
        while (runEnd < maxModels && models[runEnd].model == mesh
            && effectiveTexture(models[runEnd]) == tex) {
            ++runEnd;
        }
        const uint32_t runCount = static_cast<uint32_t>(runEnd - runStart);

        if (mesh && mesh->vertexBuffer && mesh->indexBuffer) {
            GpuBufferBinding vb { mesh->vertexBuffer, 0 };
            gpu.BindVertexBuffers(rp, 0, &vb, 1);

            GpuBufferBinding ib { mesh->indexBuffer, 0 };
            gpu.BindIndexBuffer(rp, ib, /*use16BitIndices=*/false);

            GpuTextureSamplerBinding tsb[3] = {
                { tex, sampler },
                { _shadowColorTex, _shadowSampler },    // directional shadow map at binding 1
                { _shadowCubeTex, _shadowCubeSampler }, // point-light cube shadow at binding 2
            };
            gpu.BindFragmentSamplers(rp, 0, tsb, 3);

            uint32_t baseInstance = static_cast<uint32_t>(runStart);
            gpu.PushVertexUniformData(cmdBuffer, 0, &baseInstance, sizeof(uint32_t));

            gpu.DrawIndexedPrimitives(rp,
                static_cast<uint32_t>(mesh->indices.size()),
                runCount, 0, 0, 0);
        }

        runStart = runEnd;
    }

    gpu.EndRenderPass(rp);
}
