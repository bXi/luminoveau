// WebGPU-backend implementation for Model3DRenderPass.
// Compiled only when LUMINOVEAU_WEBGPU_BACKEND is set.

#include "renderer/passes/model3drenderpass.h"

#include <algorithm>
#include <cstring>

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
    vsi.samplerCount        = 0;
    vsi.uniformBufferCount  = 0;
    vsi.storageBufferCount  = 1;
    vsi.storageTextureCount = 0;
    vertex_shader = gpu.createShader(vsi);

    GpuShaderCreateInfo fsi{};
    fsi.code                = Luminoveau::Shaders::Model3d_Frag;
    fsi.codeSize            = Luminoveau::Shaders::Model3d_Frag_Size;
    fsi.entrypoint          = "fs_main";
    fsi.stage               = GpuShaderStage::Fragment;
    fsi.samplerCount        = 1;
    fsi.uniformBufferCount  = 0;
    fsi.storageBufferCount  = 0;
    fsi.storageTextureCount = 0;
    fragment_shader = gpu.createShader(fsi);

    if (!vertex_shader || !fragment_shader) {
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

        if (uniformTransferBuffer && uniformBuffer) {
            std::memcpy(gpu.mapTransferBuffer(uniformTransferBuffer, false), &u, sizeof(u));
            gpu.unmapTransferBuffer(uniformTransferBuffer);
            gpu.uploadToBuffer(cmdBuffer, uniformTransferBuffer, 0, uniformBuffer, 0, sizeof(u));
        }

        for (auto& inst : models) if (inst.model) uploadModelToGPU(inst.model);
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

    if (!models.empty() && models[0].model && models[0].model->vertexBuffer && models[0].model->indexBuffer) {
        GpuBufferBinding vb{ models[0].model->vertexBuffer, 0 };
        gpu.bindVertexBuffers(rp, 0, &vb, 1);

        GpuBufferBinding ib{ models[0].model->indexBuffer, 0 };
        gpu.bindIndexBuffer(rp, ib, /*use16BitIndices=*/false);

        GpuTextureHandle texture = 0;
        if (models[0].textureOverride.gpuTexture) {
            texture = models[0].textureOverride.gpuTexture;
        } else if (models[0].model->texture.gpuTexture) {
            texture = models[0].model->texture.gpuTexture;
        } else {
            texture = Renderer::WhitePixel().gpuTexture;
        }

        GpuSamplerHandle sampler = Renderer::GetSampler(ScaleMode::Linear);

        GpuTextureSamplerBinding tsb{ texture, sampler };
        gpu.bindFragmentSamplers(rp, 0, &tsb, 1);

        gpu.drawIndexedPrimitives(rp,
            static_cast<uint32_t>(models[0].model->indices.size()),
            static_cast<uint32_t>(models.size()),
            0, 0, 0);
    }

    gpu.endRenderPass(rp);
}
