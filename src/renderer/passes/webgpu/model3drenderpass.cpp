// WebGPU-backend implementation for Model3DRenderPass.
// Compiled only when LUMINOVEAU_WEBGPU_BACKEND is set.
//
// Mirrors the SDL implementation: per-pixel Phong lighting plus a directional shadow map and a
// point-light distance cube. The render/matrix logic is backend-agnostic (IGpu calls); the only
// WebGPU-specific detail is createShaders declaring the fragment's cube sampler via samplerCubeMask.

#include "renderer/passes/model3drenderpass.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "gpu/presets.h"
#include "platform/window/window.h"
#include "assets/shaders_generated.h"

void Model3DRenderPass::createShaders() {
    IGpu& gpu = Renderer::GetGpu();

    GpuShaderCreateInfo vsi{};
    vsi.code                = Luminoveau::Shaders::Model3d_Vert;
    vsi.codeSize            = Luminoveau::Shaders::Model3d_Vert_Size;
    vsi.entrypoint          = "vs_main";
    vsi.stage               = GpuShaderStage::Vertex;
    vsi.uniformBufferCount  = 1;  // InstanceOffset (per-draw base instance)
    vsi.storageBufferCount  = 1;
    vertex_shader = gpu.createShader(vsi);

    GpuShaderCreateInfo fsi{};
    fsi.code                = Luminoveau::Shaders::Model3d_Frag;
    fsi.codeSize            = Luminoveau::Shaders::Model3d_Frag_Size;
    fsi.entrypoint          = "fs_main";
    fsi.stage               = GpuShaderStage::Fragment;
    fsi.samplerCount        = 3;  // model texture (0) + directional shadow (1) + point cube (2)
    fsi.samplerCubeMask     = (1u << 2);  // pair 2 (shadowCube) is a cube texture
    fsi.uniformBufferCount  = 1;  // LightData (per-pixel lighting inputs)
    fragment_shader = gpu.createShader(fsi);

    // Directional shadow depth pass shaders.
    GpuShaderCreateInfo svi{};
    svi.code                = Luminoveau::Shaders::Shadow_Vert;
    svi.codeSize            = Luminoveau::Shaders::Shadow_Vert_Size;
    svi.entrypoint          = "vs_main";
    svi.stage               = GpuShaderStage::Vertex;
    svi.uniformBufferCount  = 1;  // ShadowParams (lightViewProj + baseInstance)
    svi.storageBufferCount  = 1;
    shadow_vert_shader = gpu.createShader(svi);

    GpuShaderCreateInfo sfi{};
    sfi.code                = Luminoveau::Shaders::Shadow_Frag;
    sfi.codeSize            = Luminoveau::Shaders::Shadow_Frag_Size;
    sfi.entrypoint          = "fs_main";
    sfi.stage               = GpuShaderStage::Fragment;
    shadow_frag_shader = gpu.createShader(sfi);

    // Point-light cube shadow shaders.
    GpuShaderCreateInfo csvi{};
    csvi.code                = Luminoveau::Shaders::Shadowcube_Vert;
    csvi.codeSize            = Luminoveau::Shaders::Shadowcube_Vert_Size;
    csvi.entrypoint          = "vs_main";
    csvi.stage               = GpuShaderStage::Vertex;
    csvi.uniformBufferCount  = 1;  // CubeShadowParams (faceViewProj + baseInstance)
    csvi.storageBufferCount  = 1;
    shadowcube_vert_shader = gpu.createShader(csvi);

    GpuShaderCreateInfo csfi{};
    csfi.code                = Luminoveau::Shaders::Shadowcube_Frag;
    csfi.codeSize            = Luminoveau::Shaders::Shadowcube_Frag_Size;
    csfi.entrypoint          = "fs_main";
    csfi.stage               = GpuShaderStage::Fragment;
    csfi.uniformBufferCount  = 1;  // CubeShadowFragParams (lightPos + far)
    shadowcube_frag_shader = gpu.createShader(csfi);

    if (!vertex_shader || !fragment_shader || !shadow_vert_shader || !shadow_frag_shader
        || !shadowcube_vert_shader || !shadowcube_frag_shader) {
        LOG_ERROR("Model3DRenderPass: shader creation failed");
    }
}

bool Model3DRenderPass::init(
    GpuTextureFormat swapchain_texture_format,
    uint32_t width,
    uint32_t height,
    std::string name,
    bool logInit,
    size_t /*capacity*/,
    bool /*forceNoMSAA*/
) {
    passname        = std::move(name);
    surface_width   = width;
    surface_height  = height;

    IGpu& gpu = Renderer::GetGpu();

    {
        GpuTextureCreateInfo depthInfo{};
        depthInfo.width         = width  ? width  : 1;
        depthInfo.height        = height ? height : 1;
        depthInfo.depthOrLayers = 1;
        depthInfo.numLevels     = 1;
        depthInfo.format        = GpuTextureFormat::D32_Float;
        depthInfo.sampleCount   = GpuSampleCount::x1;
        depthInfo.usage         = GpuTextureUsage::DepthStencilTarget;
        depth_texture = gpu.createTexture(depthInfo);
        if (!depth_texture) {
            LOG_ERROR("Model3DRenderPass: depth texture creation failed");
            return false;
        }
    }

    uniformBuffer         = gpu.createBuffer({ sizeof(SceneUniforms), GpuBufferUsage::StorageRead });
    uniformTransferBuffer = gpu.createTransferBuffer({ sizeof(SceneUniforms), GpuTransferUsage::Upload });
    if (!uniformBuffer || !uniformTransferBuffer) {
        LOG_ERROR("Model3DRenderPass: uniform buffer creation failed");
        return false;
    }

    createShaders();

    static GpuVertexAttribute attrs[4] = {
        { .location = 0, .binding = 0, .format = GpuVertexElementFormat::Float3, .offset = 0  },
        { .location = 1, .binding = 0, .format = GpuVertexElementFormat::Float3, .offset = 12 },
        { .location = 2, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 24 },
        { .location = 3, .binding = 0, .format = GpuVertexElementFormat::Float4, .offset = 32 },
    };
    static GpuVertexBinding vbind = { .binding = 0, .stride = sizeof(Vertex3D), .instanceStepping = false };

    GpuGraphicsPipelineCreateInfo pci{};
    pci.vertexShader             = vertex_shader;
    pci.fragmentShader           = fragment_shader;
    pci.attributes               = attrs;
    pci.attributeCount           = 4;
    pci.bindings                 = &vbind;
    pci.bindingCount             = 1;
    pci.fillMode                 = GpuFillMode::Fill;
    pci.cullMode                 = GpuCullMode::None;
    pci.frontFace                = GpuFrontFace::CounterClockwise;
    pci.colorTargetFormat        = swapchain_texture_format;
    pci.blend                    = GpuPresets::AlphaBlendKeepDstAlpha;
    pci.hasDepthTarget           = true;
    pci.depthTargetFormat        = GpuTextureFormat::D32_Float;
    pci.sampleCount              = GpuSampleCount::x1;
    pci.vertexStorageBufferCount = 1;
    m_pipeline = gpu.createGraphicsPipeline(pci);
    if (!m_pipeline) {
        LOG_ERROR("Model3DRenderPass: graphics pipeline creation failed");
        return false;
    }

    // ── Directional shadow resources ─────────────────────────────────────────
    {
        GpuTextureCreateInfo sc{};
        sc.width  = kShadowRes;
        sc.height = kShadowRes;
        sc.format = GpuTextureFormat::R32_Float;
        sc.usage  = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
        shadowColorTex = gpu.createTexture(sc);

        GpuTextureCreateInfo sd{};
        sd.width  = kShadowRes;
        sd.height = kShadowRes;
        sd.format = GpuTextureFormat::D32_Float;
        sd.usage  = GpuTextureUsage::DepthStencilTarget;
        shadowDepthTex = gpu.createTexture(sd);

        GpuSamplerCreateInfo ss{};
        ss.minFilter = GpuFilter::Nearest; ss.magFilter = GpuFilter::Nearest; ss.mipFilter = GpuFilter::Nearest;
        ss.addressU  = GpuSamplerAddressMode::ClampToEdge;
        ss.addressV  = GpuSamplerAddressMode::ClampToEdge;
        ss.addressW  = GpuSamplerAddressMode::ClampToEdge;
        shadowSampler = gpu.createSampler(ss);

        GpuGraphicsPipelineCreateInfo spci{};
        spci.vertexShader             = shadow_vert_shader;
        spci.fragmentShader           = shadow_frag_shader;
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
        spci.sampleCount              = GpuSampleCount::x1;
        spci.vertexStorageBufferCount = 1;
        m_shadowPipeline = gpu.createGraphicsPipeline(spci);
        if (!shadowColorTex || !shadowDepthTex || !m_shadowPipeline) {
            LOG_ERROR("Model3DRenderPass: shadow resource creation failed");
            return false;
        }
    }

    // ── Point-light cube shadow resources ────────────────────────────────────
    {
        GpuTextureCreateInfo cc{};
        cc.width         = kCubeShadowRes;
        cc.height        = kCubeShadowRes;
        cc.depthOrLayers = 6;                       // cube = 6 faces
        cc.format        = GpuTextureFormat::R32_Float;
        cc.usage         = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
        cc.type          = GpuTextureType::TexCube;
        shadowCubeTex = gpu.createTexture(cc);

        GpuTextureCreateInfo cd{};
        cd.width  = kCubeShadowRes;
        cd.height = kCubeShadowRes;
        cd.format = GpuTextureFormat::D32_Float;
        cd.usage  = GpuTextureUsage::DepthStencilTarget;
        shadowCubeDepthTex = gpu.createTexture(cd);   // reused per face

        GpuSamplerCreateInfo cs{};
        cs.minFilter = GpuFilter::Nearest; cs.magFilter = GpuFilter::Nearest; cs.mipFilter = GpuFilter::Nearest;
        cs.addressU  = GpuSamplerAddressMode::ClampToEdge;
        cs.addressV  = GpuSamplerAddressMode::ClampToEdge;
        cs.addressW  = GpuSamplerAddressMode::ClampToEdge;
        shadowCubeSampler = gpu.createSampler(cs);

        GpuGraphicsPipelineCreateInfo cpci{};
        cpci.vertexShader             = shadowcube_vert_shader;
        cpci.fragmentShader           = shadowcube_frag_shader;
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
        cpci.sampleCount              = GpuSampleCount::x1;
        cpci.vertexStorageBufferCount = 1;
        m_cubeShadowPipeline = gpu.createGraphicsPipeline(cpci);
        if (!shadowCubeTex || !shadowCubeDepthTex || !m_cubeShadowPipeline) {
            LOG_ERROR("Model3DRenderPass: cube shadow resource creation failed");
            return false;
        }
    }

    if (logInit) {
        LOG_INFO("Model3DRenderPass initialized: {}", passname);
    }
    return true;
}

void Model3DRenderPass::release(bool logRelease) {
    IGpu& gpu = Renderer::GetGpu();

    if (depth_texture)         { gpu.releaseTexture(depth_texture);                 depth_texture         = 0; }
    if (uniformBuffer)         { gpu.releaseBuffer(uniformBuffer);                  uniformBuffer         = 0; }
    if (uniformTransferBuffer) { gpu.releaseTransferBuffer(uniformTransferBuffer);  uniformTransferBuffer = 0; }
    if (m_pipeline)            { gpu.releaseGraphicsPipeline(m_pipeline);           m_pipeline            = 0; }
    if (vertex_shader)         { gpu.releaseShader(vertex_shader);                  vertex_shader         = 0; }
    if (fragment_shader)       { gpu.releaseShader(fragment_shader);                fragment_shader       = 0; }

    if (m_shadowPipeline)      { gpu.releaseGraphicsPipeline(m_shadowPipeline);     m_shadowPipeline      = 0; }
    if (shadow_vert_shader)    { gpu.releaseShader(shadow_vert_shader);             shadow_vert_shader    = 0; }
    if (shadow_frag_shader)    { gpu.releaseShader(shadow_frag_shader);             shadow_frag_shader    = 0; }
    if (shadowColorTex)        { gpu.releaseTexture(shadowColorTex);                shadowColorTex        = 0; }
    if (shadowDepthTex)        { gpu.releaseTexture(shadowDepthTex);                shadowDepthTex        = 0; }
    if (shadowSampler)         { gpu.releaseSampler(shadowSampler);                 shadowSampler         = 0; }

    if (m_cubeShadowPipeline)  { gpu.releaseGraphicsPipeline(m_cubeShadowPipeline); m_cubeShadowPipeline  = 0; }
    if (shadowcube_vert_shader){ gpu.releaseShader(shadowcube_vert_shader);         shadowcube_vert_shader = 0; }
    if (shadowcube_frag_shader){ gpu.releaseShader(shadowcube_frag_shader);         shadowcube_frag_shader = 0; }
    if (shadowCubeTex)         { gpu.releaseTexture(shadowCubeTex);                 shadowCubeTex         = 0; }
    if (shadowCubeDepthTex)    { gpu.releaseTexture(shadowCubeDepthTex);            shadowCubeDepthTex    = 0; }
    if (shadowCubeSampler)     { gpu.releaseSampler(shadowCubeSampler);             shadowCubeSampler     = 0; }

    if (logRelease) {
        LOG_INFO("Released 3D model render pass");
    }
}

void Model3DRenderPass::uploadModelToGPU(ModelAsset* model) {
    if (!model || model->vertices.empty() || model->indices.empty()) return;
    if (model->vertexBuffer && model->indexBuffer) return;  // already uploaded

    IGpu& gpu = Renderer::GetGpu();
    uint32_t vSize = static_cast<uint32_t>(model->vertices.size() * sizeof(Vertex3D));
    uint32_t iSize = static_cast<uint32_t>(model->indices.size()  * sizeof(uint32_t));

    model->vertexBuffer = gpu.createBuffer({ vSize, GpuBufferUsage::Vertex });
    model->indexBuffer  = gpu.createBuffer({ iSize, GpuBufferUsage::Index  });

    GpuTransferBufferHandle vXfer = gpu.createTransferBuffer({ vSize, GpuTransferUsage::Upload });
    std::memcpy(gpu.mapTransferBuffer(vXfer, false), model->vertices.data(), vSize);
    gpu.unmapTransferBuffer(vXfer);

    GpuTransferBufferHandle iXfer = gpu.createTransferBuffer({ iSize, GpuTransferUsage::Upload });
    std::memcpy(gpu.mapTransferBuffer(iXfer, false), model->indices.data(), iSize);
    gpu.unmapTransferBuffer(iXfer);

    GpuCmdBufferHandle cmd = gpu.acquireCommandBuffer();
    gpu.uploadToBuffer(cmd, vXfer, 0, model->vertexBuffer, 0, vSize);
    gpu.uploadToBuffer(cmd, iXfer, 0, model->indexBuffer,  0, iSize);
    gpu.submitCommandBuffer(cmd);
    gpu.waitIdle();
    gpu.releaseTransferBuffer(vXfer);
    gpu.releaseTransferBuffer(iXfer);
}

void Model3DRenderPass::render(
    GpuCmdBufferHandle cmdBuffer,
    GpuTextureHandle targetTexture,
    const glm::mat4& /*camera_unused*/
) {
    IGpu& gpu = Renderer::GetGpu();

    Camera3D& camera                   = Scene::GetCamera();
    std::vector<ModelInstance>& models = Scene::GetModels();
    std::vector<Light>& lights         = Scene::GetLights();
    Color ambient                      = Scene::GetAmbientLight();

    SceneUniforms u{};
    LightData     lightData{};
    if (!models.empty() && m_pipeline) {
        float aspect    = (float)Window::GetWidth() / (float)Window::GetHeight();
        u.viewProj      = camera.GetViewProjectionMatrix(aspect);
        u.modelCount    = std::min((int)models.size(), 16);
        for (int i = 0; i < u.modelCount; ++i) u.models[i] = models[i].GetModelMatrix();
        u.cameraPos     = glm::vec4(camera.position.x, camera.position.y, camera.position.z, 1.0f);
        u.ambientLight  = glm::vec4(ambient.r / 255.f, ambient.g / 255.f, ambient.b / 255.f, ambient.a / 255.f);
        u.lightCount    = std::min((int)lights.size(), 4);
        for (int i = 0; i < u.lightCount; ++i) {
            const Light& L = lights[i];
            if (L.type == LightType::Directional)
                u.lightPositions[i] = glm::vec4(L.direction.x, L.direction.y, L.direction.z, (float)L.type);
            else
                u.lightPositions[i] = glm::vec4(L.position.x, L.position.y, L.position.z, (float)L.type);
            u.lightColors[i] = glm::vec4(L.color.r / 255.f, L.color.g / 255.f, L.color.b / 255.f, L.intensity);
            u.lightParams[i] = glm::vec4(L.constant, L.linear, L.quadratic, 0.0f);
        }

        // Directional shadow caster = the first directional light. Same zero-to-one ortho matrix
        // drives the shadow render (shadow.vert) and the lookup (model3d.frag). See SDL impl.
        int       shadowLight = -1;
        glm::mat4 lightViewProj(1.0f);
        for (int i = 0; i < u.lightCount; ++i)
            if (lights[i].type == LightType::Directional) { shadowLight = i; break; }
        if (shadowLight >= 0) {
            glm::vec3 dir = glm::normalize(glm::vec3(lights[shadowLight].direction.x,
                                                     lights[shadowLight].direction.y,
                                                     lights[shadowLight].direction.z));
            const glm::vec3 center(0.0f);
            const float dist = 45.0f, ext = 29.0f, nearP = 12.0f, farP = 82.0f;
            // Directional light vectors point TOWARD the light, so it sits in +dir.
            glm::vec3 eye = center + dir * dist;
            glm::vec3 up  = (std::fabs(dir.y) > 0.95f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            lightViewProj = glm::orthoRH_ZO(-ext, ext, -ext, ext, nearP, farP)
                          * glm::lookAtRH(eye, center, up);
        }

        // Point (cube) shadow caster = the first point light.
        int       pointShadowLight = -1;
        glm::vec3 pointPos(0.0f);
        for (int i = 0; i < u.lightCount; ++i)
            if (lights[i].type == LightType::Point) {
                pointShadowLight = i;
                pointPos = glm::vec3(lights[i].position.x, lights[i].position.y, lights[i].position.z);
                break;
            }

        // Light data packed for the per-pixel fragment shader (LightData uniform).
        lightData.shadowViewProj   = lightViewProj;
        lightData.shadowLight      = shadowLight;
        lightData.pointShadowLight = pointShadowLight;
        lightData.pointLightPosFar = glm::vec4(pointPos, kPointFar);
        lightData.cameraPos    = u.cameraPos;
        lightData.ambientLight = u.ambientLight;
        lightData.lightCount   = u.lightCount;
        for (int i = 0; i < u.lightCount; ++i) {
            lightData.lightPositions[i] = u.lightPositions[i];
            lightData.lightColors[i]    = u.lightColors[i];
            lightData.lightParams[i]    = u.lightParams[i];
        }

        if (uniformTransferBuffer && uniformBuffer) {
            std::memcpy(gpu.mapTransferBuffer(uniformTransferBuffer, false), &u, sizeof(u));
            gpu.unmapTransferBuffer(uniformTransferBuffer);
            gpu.uploadToBuffer(cmdBuffer, uniformTransferBuffer, 0, uniformBuffer, 0, sizeof(u));
        }

        for (auto& inst : models) if (inst.model) uploadModelToGPU(inst.model);

        // ── Directional shadow depth pass ──────────────────────────────────────
        if (shadowLight >= 0 && m_shadowPipeline) {
            GpuColorTargetInfo sct{};
            sct.texture = shadowColorTex;
            sct.loadOp  = GpuLoadOp::Clear;
            sct.storeOp = GpuStoreOp::Store;
            sct.clearR  = 1.0f; sct.clearG = 1.0f; sct.clearB = 1.0f; sct.clearA = 1.0f;  // far

            GpuDepthStencilTargetInfo sdt{};
            sdt.texture    = shadowDepthTex;
            sdt.loadOp     = GpuLoadOp::Clear;
            sdt.storeOp    = GpuStoreOp::DontCare;
            sdt.clearDepth = 1.0f;

            GpuRenderPassHandle sp = gpu.beginRenderPass(cmdBuffer, &sct, 1, &sdt);
            gpu.setViewport(sp, 0.0f, 0.0f, (float)kShadowRes, (float)kShadowRes, 0.0f, 1.0f);
            gpu.bindGraphicsPipeline(sp, m_shadowPipeline);
            gpu.bindVertexStorageBuffers(sp, 0, &uniformBuffer, 1);

            const size_t maxM = std::min(models.size(), static_cast<size_t>(16));
            size_t rs = 0;
            while (rs < maxM) {
                const ModelAsset* mesh = models[rs].model;
                size_t re = rs + 1;
                while (re < maxM && models[re].model == mesh) ++re;
                uint32_t rc = static_cast<uint32_t>(re - rs);
                if (mesh && mesh->vertexBuffer && mesh->indexBuffer) {
                    GpuBufferBinding vb{ mesh->vertexBuffer, 0 };
                    gpu.bindVertexBuffers(sp, 0, &vb, 1);
                    GpuBufferBinding ib{ mesh->indexBuffer, 0 };
                    gpu.bindIndexBuffer(sp, ib, false);

                    ShadowParams spp{};
                    spp.lightViewProj = lightViewProj;
                    spp.baseInstance  = static_cast<uint32_t>(rs);
                    gpu.pushVertexUniformData(cmdBuffer, 0, &spp, sizeof(spp));

                    gpu.drawIndexedPrimitives(sp, static_cast<uint32_t>(mesh->indices.size()), rc, 0, 0, 0);
                }
                rs = re;
            }
            gpu.endRenderPass(sp);
        }

        // ── Point-light cube shadow pass (6 faces) ─────────────────────────────
        if (pointShadowLight >= 0 && m_cubeShadowPipeline) {
            // LH view + projection with D3D face orientations, matching texture_cube sampling.
            // Layer order +X,-X,+Y,-Y,+Z,-Z.
            glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(90.0f), 1.0f, 0.5f, kPointFar);
            struct Face { glm::vec3 dir, up; };
            const Face faces[6] = {
                {{ 1, 0, 0}, {0,  1,  0}},   // +X
                {{-1, 0, 0}, {0,  1,  0}},   // -X
                {{ 0, 1, 0}, {0,  0, -1}},   // +Y
                {{ 0,-1, 0}, {0,  0,  1}},   // -Y
                {{ 0, 0, 1}, {0,  1,  0}},   // +Z
                {{ 0, 0,-1}, {0,  1,  0}},   // -Z
            };
            CubeShadowFragParams cfp{};
            cfp.lightPosFar = glm::vec4(pointPos, kPointFar);

            for (int f = 0; f < 6; ++f) {
                glm::mat4 faceVP = proj * glm::lookAtLH(pointPos, pointPos + faces[f].dir, faces[f].up);

                GpuColorTargetInfo cct{};
                cct.texture = shadowCubeTex;
                cct.layer   = static_cast<uint32_t>(f);     // render into cube face f
                cct.loadOp  = GpuLoadOp::Clear;
                cct.storeOp = GpuStoreOp::Store;
                cct.clearR  = 1.0f; cct.clearG = 1.0f; cct.clearB = 1.0f; cct.clearA = 1.0f;  // far

                GpuDepthStencilTargetInfo cdt{};
                cdt.texture    = shadowCubeDepthTex;
                cdt.loadOp     = GpuLoadOp::Clear;
                cdt.storeOp    = GpuStoreOp::DontCare;
                cdt.clearDepth = 1.0f;

                GpuRenderPassHandle cp = gpu.beginRenderPass(cmdBuffer, &cct, 1, &cdt);
                gpu.setViewport(cp, 0.0f, 0.0f, (float)kCubeShadowRes, (float)kCubeShadowRes, 0.0f, 1.0f);
                gpu.bindGraphicsPipeline(cp, m_cubeShadowPipeline);
                gpu.bindVertexStorageBuffers(cp, 0, &uniformBuffer, 1);
                gpu.pushFragmentUniformData(cmdBuffer, 0, &cfp, sizeof(cfp));

                const size_t maxM = std::min(models.size(), static_cast<size_t>(16));
                size_t rs = 0;
                while (rs < maxM) {
                    const ModelAsset* mesh = models[rs].model;
                    size_t re = rs + 1;
                    while (re < maxM && models[re].model == mesh) ++re;
                    uint32_t rc = static_cast<uint32_t>(re - rs);
                    if (mesh && mesh->vertexBuffer && mesh->indexBuffer) {
                        GpuBufferBinding vb{ mesh->vertexBuffer, 0 };
                        gpu.bindVertexBuffers(cp, 0, &vb, 1);
                        GpuBufferBinding ib{ mesh->indexBuffer, 0 };
                        gpu.bindIndexBuffer(cp, ib, false);

                        CubeShadowParams csp{};
                        csp.faceViewProj = faceVP;
                        csp.baseInstance = static_cast<uint32_t>(rs);
                        gpu.pushVertexUniformData(cmdBuffer, 0, &csp, sizeof(csp));

                        gpu.drawIndexedPrimitives(cp, static_cast<uint32_t>(mesh->indices.size()), rc, 0, 0, 0);
                    }
                    rs = re;
                }
                gpu.endRenderPass(cp);
            }
        }
    }

    bool shouldResolve = (renderTargetResolve != 0);

    GpuColorTargetInfo ct{};
    ct.texture        = targetTexture;
    ct.resolveTexture = renderTargetResolve;
    ct.loadOp         = color_target_info_loadop;
    ct.storeOp        = shouldResolve ? GpuStoreOp::Resolve : GpuStoreOp::Store;
    ct.clearR         = color_target_clear_r;
    ct.clearG         = color_target_clear_g;
    ct.clearB         = color_target_clear_b;
    ct.clearA         = color_target_clear_a;

    GpuDepthStencilTargetInfo dt{};
    dt.texture    = renderTargetDepth ? renderTargetDepth : depth_texture;
    dt.loadOp     = GpuLoadOp::Clear;
    dt.storeOp    = GpuStoreOp::Store;
    dt.clearDepth = 1.0f;

    GpuRenderPassHandle rp = gpu.beginRenderPass(cmdBuffer, &ct, 1, &dt);
    render_pass = rp;

    gpu.setViewport(rp, 0.0f, 0.0f,
                    (float)Window::GetPhysicalWidth(), (float)Window::GetPhysicalHeight(),
                    0.0f, 1.0f);

    if (models.empty() || !m_pipeline) {
        gpu.endRenderPass(rp);
        return;
    }

    gpu.bindGraphicsPipeline(rp, m_pipeline);
    gpu.bindVertexStorageBuffers(rp, 0, &uniformBuffer, 1);

    // Lighting inputs for the per-pixel fragment shader (fragment uniform slot 0).
    gpu.pushFragmentUniformData(cmdBuffer, 0, &lightData, sizeof(lightData));

    GpuSamplerHandle sampler = Renderer::GetSampler(ScaleMode::Linear);

    auto effectiveTexture = [](const ModelInstance& m) -> GpuTextureHandle {
        if (m.textureOverride.gpuTexture)            return m.textureOverride.gpuTexture;
        if (m.model && m.model->texture.gpuTexture)  return m.model->texture.gpuTexture;
        return Renderer::WhitePixel().gpuTexture;
    };

    // Draw contiguous runs sharing the same mesh + texture as one instanced call.
    const size_t maxModels = std::min(models.size(), static_cast<size_t>(16));
    size_t runStart = 0;
    while (runStart < maxModels) {
        const ModelAsset* mesh = models[runStart].model;
        GpuTextureHandle tex   = effectiveTexture(models[runStart]);

        size_t runEnd = runStart + 1;
        while (runEnd < maxModels && models[runEnd].model == mesh
               && effectiveTexture(models[runEnd]) == tex) {
            ++runEnd;
        }
        const uint32_t runCount = static_cast<uint32_t>(runEnd - runStart);

        if (mesh && mesh->vertexBuffer && mesh->indexBuffer) {
            GpuBufferBinding vb{ mesh->vertexBuffer, 0 };
            gpu.bindVertexBuffers(rp, 0, &vb, 1);

            GpuBufferBinding ib{ mesh->indexBuffer, 0 };
            gpu.bindIndexBuffer(rp, ib, /*use16BitIndices=*/false);

            GpuTextureSamplerBinding tsb[3] = {
                { tex, sampler },
                { shadowColorTex, shadowSampler },       // directional shadow map at binding 1
                { shadowCubeTex,  shadowCubeSampler },   // point-light cube shadow at binding 2
            };
            gpu.bindFragmentSamplers(rp, 0, tsb, 3);

            uint32_t baseInstance[8] = {};
            baseInstance[0] = static_cast<uint32_t>(runStart);
            gpu.pushVertexUniformData(cmdBuffer, 0, baseInstance, 32);

            gpu.drawIndexedPrimitives(rp,
                static_cast<uint32_t>(mesh->indices.size()),
                runCount, 0, 0, 0);
        }

        runStart = runEnd;
    }

    gpu.endRenderPass(rp);
}
