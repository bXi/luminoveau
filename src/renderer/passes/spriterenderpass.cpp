#include "renderer/passes/spriterenderpass.h"

#include <utility>
#include <cstring>
#include "core/log/log.h"

#ifndef LUMINOVEAU_WEBGPU_BACKEND
#include "gpu/backends/sdl/sdlgpu.h"
#include "SDL3/SDL_gpu.h"
#include "platform/window/window.h"
#include "assets/shaders_generated.h"
#include "draw/draw.h"
#include "math/constants.h"
#else
#include "platform/window/window.h"
#include "draw/draw.h"

static constexpr const char* kSpriteVertWGSL = R"(
struct SpriteData {
    pos_xy   : u32,
    pos_z_rot: u32,
    tex_uv   : u32,
    tex_wh   : u32,
    color_rg : u32,
    color_ba : u32,
    size_wh  : u32,
    pivot_xy : u32,
}

struct UniformBlock {
    viewProjection : mat4x4<f32>,
}

struct InstanceOffset {
    baseInstance : u32,
    _pad         : vec3<u32>,
}

@group(0) @binding(0) var<uniform>       uniforms        : UniformBlock;
@group(0) @binding(1) var<uniform>       instOffset      : InstanceOffset;
@group(3) @binding(0) var<storage, read> spriteInstances : array<SpriteData>;

struct VertOut {
    @builtin(position)               position  : vec4<f32>,
    @location(0)                     texCoord  : vec2<f32>,
    @location(1)                     color     : vec4<f32>,
    @location(2) @interpolate(flat)  isSDF     : u32,
}

fn unpackHalfLo(packed: u32) -> f32 { return unpack2x16float(packed).x; }
fn unpackHalfHi(packed: u32) -> f32 { return unpack2x16float(packed).y; }
fn unpackHalf2(packed: u32) -> vec2<f32> { return unpack2x16float(packed); }

@vertex
fn vs_main(
    @location(0) vertPosXY : u32,
    @location(1) vertUV    : u32,
    @builtin(instance_index) instanceIndex : u32,
) -> VertOut {
    let sprite = spriteInstances[instanceIndex + instOffset.baseInstance];

    let x        = unpackHalfLo(sprite.pos_xy);
    let y        = unpackHalfHi(sprite.pos_xy);
    let rotation = unpackHalfHi(sprite.pos_z_rot);
    let texUV    = unpackHalf2(sprite.tex_uv);
    let texWH    = unpackHalf2(sprite.tex_wh);
    let color    = vec4<f32>(
        unpackHalfLo(sprite.color_rg),
        unpackHalfHi(sprite.color_rg),
        unpackHalfLo(sprite.color_ba),
        unpackHalfHi(sprite.color_ba),
    );
    let scale = unpackHalf2(sprite.size_wh);

    let pivotPacked  = sprite.pivot_xy;
    let isSDF        = (pivotPacked >> 31u) & 1u;
    let pivotCleared = pivotPacked & 0x7FFFFFFFu;
    let pivot        = unpackHalf2(pivotCleared);

    let vertexPos = unpackHalf2(vertPosXY);
    let vertexUV  = unpackHalf2(vertUV);

    var coord = vertexPos;

    let texcoord = vec2<f32>(
        texUV.x + vertexUV.x * texWH.x,
        texUV.y + vertexUV.y * texWH.y,
    );

    if rotation != 0.0 {
        coord -= pivot;
    }

    coord *= scale;

    if rotation != 0.0 {
        let c = cos(rotation);
        let s = sin(rotation);
        coord = vec2<f32>(c * coord.x - s * coord.y,
                          s * coord.x + c * coord.y);
        coord += pivot * scale;
    }

    let worldPos = vec3<f32>(coord + vec2<f32>(x, y), 0.0);

    var out : VertOut;
    out.position = uniforms.viewProjection * vec4<f32>(worldPos, 1.0);
    out.texCoord = texcoord;
    out.color    = color;
    out.isSDF    = isSDF;
    return out;
}
)";

static constexpr const char* kSpriteFragWGSL = R"(
@group(2) @binding(0) var gSampler : sampler;
@group(2) @binding(1) var gTexture : texture_2d<f32>;

struct FragIn {
    @location(0)                     texCoord : vec2<f32>,
    @location(1)                     color    : vec4<f32>,
    @location(2) @interpolate(flat)  isSDF    : u32,
}

fn median(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

@fragment
fn fs_main(in : FragIn) -> @location(0) vec4<f32> {
    // Sample and compute derivatives in uniform control flow (required by WebGPU).
    let texColor = textureSample(gTexture, gSampler, in.texCoord);
    let dim      = textureDimensions(gTexture, 0);
    let msdfUnit = 4.0 / vec2<f32>(f32(dim.x), f32(dim.y));
    let texDeriv = fwidth(in.texCoord);

    // SDF path
    let sd               = median(texColor.r, texColor.g, texColor.b);
    let screenPxRange    = max(dot(msdfUnit, 0.5 / texDeriv), 1.0);
    let screenPxDistance = screenPxRange * (sd - 0.5);
    let sdfAlpha         = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    let sdfColor         = vec4<f32>(in.color.rgb, sdfAlpha * in.color.a);

    // Regular path
    let regularColor = texColor * in.color;

    let outColor = select(regularColor, sdfColor, in.isSDF != 0u);

    if outColor.a == 0.0 { discard; }
    return outColor;
}
)";
#endif

#ifndef LUMINOVEAU_WEBGPU_BACKEND
void SpriteRenderPass::release(bool logRelease) {
    // Release effect resources
    releaseEffectResources();
    
    if (m_msaa_color_texture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), m_msaa_color_texture);
        m_msaa_color_texture = nullptr;
    }
    if (m_msaa_depth_texture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), m_msaa_depth_texture);
        m_msaa_depth_texture = nullptr;
    }

    if (m_depth_texture.gpuTexture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), reinterpret_cast<SDL_GPUTexture*>(m_depth_texture.gpuTexture));
        m_depth_texture.gpuTexture = 0;
    }

    // Release buffers
    if (SpriteDataTransferBuffer) {
        SDL_ReleaseGPUTransferBuffer(Renderer::GetDevice(), SpriteDataTransferBuffer);
        SpriteDataTransferBuffer = nullptr;
    }

    if (SpriteDataBuffer) {
        SDL_ReleaseGPUBuffer(Renderer::GetDevice(), SpriteDataBuffer);
        SpriteDataBuffer = nullptr;
    }

    // Release shaders
    if (vertex_shader) {
        SDL_ReleaseGPUShader(Renderer::GetDevice(), vertex_shader);
        vertex_shader = nullptr;
    }

    if (fragment_shader) {
        SDL_ReleaseGPUShader(Renderer::GetDevice(), fragment_shader);
        fragment_shader = nullptr;
    }

    // Release pipeline
    if (m_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), m_pipeline);
        m_pipeline = nullptr;
    }

    if (logRelease) {
        LOG_INFO("Released graphics pipeline: {}", passname.c_str());
    }
}
#else // LUMINOVEAU_WEBGPU_BACKEND
void SpriteRenderPass::release(bool logRelease) {
    auto& gpu = Renderer::GetGpu();

    // Release effect pipelines
    for (auto& [key, pipeline] : m_effect_pipelines) {
        if (pipeline) gpu.releaseGraphicsPipeline(pipeline);
    }
    m_effect_pipelines.clear();

    if (m_effect_tex_a)   { gpu.releaseTexture(m_effect_tex_a);   m_effect_tex_a = 0; }
    if (m_effect_tex_b)   { gpu.releaseTexture(m_effect_tex_b);   m_effect_tex_b = 0; }
    if (m_effect_sampler) { gpu.releaseSampler(m_effect_sampler); m_effect_sampler = 0; }
    if (m_effect_vbuf)    { gpu.releaseBuffer(m_effect_vbuf);     m_effect_vbuf = 0; }
    if (m_effect_ibuf)    { gpu.releaseBuffer(m_effect_ibuf);     m_effect_ibuf = 0; }

    if (m_wgpu_pipeline)   { gpu.releaseGraphicsPipeline(m_wgpu_pipeline);   m_wgpu_pipeline = 0; }
    if (m_sprite_gpu_buf)  { gpu.releaseBuffer(m_sprite_gpu_buf);             m_sprite_gpu_buf = 0; }
    if (m_sprite_xfer_buf) { gpu.releaseTransferBuffer(m_sprite_xfer_buf);    m_sprite_xfer_buf = 0; }
    if (m_quad_vertex_buf) { gpu.releaseBuffer(m_quad_vertex_buf);            m_quad_vertex_buf = 0; }
    if (m_quad_index_buf)  { gpu.releaseBuffer(m_quad_index_buf);             m_quad_index_buf = 0; }
    if (m_quad_xfer_vert)  { gpu.releaseTransferBuffer(m_quad_xfer_vert);     m_quad_xfer_vert = 0; }
    if (m_quad_xfer_idx)   { gpu.releaseTransferBuffer(m_quad_xfer_idx);      m_quad_xfer_idx = 0; }
    renderQueue = nullptr;
    if (logRelease) {
        LOG_INFO("Released graphics pipeline: {}", passname.c_str());
    }
}
#endif // LUMINOVEAU_WEBGPU_BACKEND

#ifndef LUMINOVEAU_WEBGPU_BACKEND
bool SpriteRenderPass::init(
    GpuTextureFormat swapchain_texture_format, uint32_t surface_width, uint32_t surface_height, std::string name, bool logInit,
    size_t capacity, bool forceNoMSAA) {
    m_noMSAA = forceNoMSAA;
    passname = std::move(name);
    
    // Store surface dimensions and format for effect textures
    m_surface_width = surface_width;
    m_surface_height = surface_height;
    m_swapchain_format = toSDL(swapchain_texture_format);

    SDL_GPUSampleCount sampleCount = m_noMSAA ? SDL_GPU_SAMPLECOUNT_1 : toSDL(Renderer::GetSampleCount());

    // Don't create MSAA textures - use shared framebuffer MSAA texture
    // Create local depth texture with D32_FLOAT to match pipeline
    SDL_GPUTextureCreateInfo depth_create_info{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,  // Must match pipeline format
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = surface_width,
        .height = surface_height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };
    m_depth_texture.gpuTexture = reinterpret_cast<GpuTextureHandle>(SDL_CreateGPUTexture(Renderer::GetDevice(), &depth_create_info));

    renderQueue = BufferManager::Create<Renderable>(passname + "_renderQueue", capacity > 0 ? capacity : MAX_SPRITES);

    createShaders();
    createEffectResources();

    SDL_GPUColorTargetDescription color_target_description{
        .format = toSDL(swapchain_texture_format),
        .blend_state = renderPassBlendState,
    };

    // Define vertex buffer layout for Vertex2D (CompactVertex2D)
    SDL_GPUVertexAttribute vertexAttributes[] = {
        {
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT,  // pos_xy (uint32 with 2 packed half-floats)
            .offset = 0
        },
        {
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT,  // uv (uint32 with 2 packed half-floats)
            .offset = 4
        }
    };
    
    SDL_GPUVertexBufferDescription vertexBufferDesc = {
        .slot = 0,
        .pitch = 8,  // sizeof(CompactVertex2D)
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0
    };
    
    SDL_GPUGraphicsPipelineCreateInfo pipeline_create_info{
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state =
            {
                .vertex_buffer_descriptions = &vertexBufferDesc,
                .num_vertex_buffers = 1,
                .vertex_attributes = vertexAttributes,
                .num_vertex_attributes = 2,
            },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = SDL_DefaultRasterizerState,
        .multisample_state = {
            .sample_count = sampleCount,
            .sample_mask  = 0,
            .enable_mask  = false,// Must be false when using multisampling
        },
        .depth_stencil_state = {
            .compare_op          = SDL_GPU_COMPAREOP_LESS,
            .back_stencil_state  = {},
            .front_stencil_state = {},
            .compare_mask        = 0,
            .write_mask          = 0,
            .enable_depth_test   = false,  // Disabled for 2D sprites!
            .enable_depth_write  = false,
            .enable_stencil_test = false,
        },
        .target_info = {
            .color_target_descriptions = &color_target_description,
            .num_color_targets         = 1,
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_INVALID,  // No depth needed
            .has_depth_stencil_target  = false,
        },
        .props = 0,
    };
    m_pipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &pipeline_create_info);

    SDL_GPUTransferBufferCreateInfo transferBufferCreateInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = MAX_SPRITES * sizeof(CompactSpriteInstance)  // Use compact size
    };

    SpriteDataTransferBuffer = SDL_CreateGPUTransferBuffer(
        m_gpu_device,
        &transferBufferCreateInfo
    );

    SDL_GPUBufferCreateInfo bufferCreateInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = MAX_SPRITES * sizeof(CompactSpriteInstance)  // Use compact size
    };

    SpriteDataBuffer = SDL_CreateGPUBuffer(
        m_gpu_device,
        &bufferCreateInfo
    );

    if (!m_pipeline) {
        LOG_CRITICAL("failed to create graphics pipeline: {}", SDL_GetError());
    }

    if (logInit) {
        LOG_INFO("Render pass initialized: {}", passname.c_str());
    }

    return true;
}
#else // LUMINOVEAU_WEBGPU_BACKEND
bool SpriteRenderPass::init(
    GpuTextureFormat swapchain_texture_format, uint32_t surface_width,
    uint32_t surface_height, std::string name, bool logInit,
    size_t capacity, bool /*forceNoMSAA*/) {

    passname         = std::move(name);
    m_surface_width  = surface_width;
    m_surface_height = surface_height;

    renderQueue = BufferManager::Create<Renderable>(
        passname + "_renderQueue", capacity > 0 ? capacity : MAX_SPRITES);

    auto& gpu = Renderer::GetGpu();

    GpuShaderHandle vertShader = gpu.createShader({
        reinterpret_cast<const uint8_t*>(kSpriteVertWGSL),
        strlen(kSpriteVertWGSL),
        "vs_main",
        GpuShaderStage::Vertex,
        /*samplerCount*/   0,
        /*uniformBuffers*/ 2,
        /*storageBufs*/    1,
        /*storageTex*/     0,
    });

    GpuShaderHandle fragShader = gpu.createShader({
        reinterpret_cast<const uint8_t*>(kSpriteFragWGSL),
        strlen(kSpriteFragWGSL),
        "fs_main",
        GpuShaderStage::Fragment,
        /*samplerCount*/   1,
        /*uniformBuffers*/ 0,
        /*storageBufs*/    0,
        /*storageTex*/     0,
    });

    if (!vertShader || !fragShader) {
        LOG_CRITICAL("SpriteRenderPass: failed to load WGSL shaders for {}", passname);
        return false;
    }

    // Vertex attributes for CompactVertex2D: 2× uint32 at locations 0 and 1
    GpuVertexAttribute attrs[2] = {
        { .location = 0, .binding = 0, .format = GpuVertexElementFormat::UInt, .offset = 0 },
        { .location = 1, .binding = 0, .format = GpuVertexElementFormat::UInt, .offset = 4 },
    };
    GpuVertexBinding vbind = { .binding = 0, .stride = 8, .instanceStepping = false };

    GpuGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertexShader            = vertShader;
    pipelineInfo.fragmentShader          = fragShader;
    pipelineInfo.attributes              = attrs;
    pipelineInfo.attributeCount          = 2;
    pipelineInfo.bindings                = &vbind;
    pipelineInfo.bindingCount            = 1;
    pipelineInfo.fillMode                = GpuFillMode::Fill;
    pipelineInfo.cullMode                = GpuCullMode::None;
    pipelineInfo.frontFace               = GpuFrontFace::CounterClockwise;
    pipelineInfo.colorTargetFormat       = swapchain_texture_format;
    pipelineInfo.blend                   = renderPassBlendStateGpu;
    pipelineInfo.hasDepthTarget          = false;
    pipelineInfo.sampleCount             = GpuSampleCount::x1;
    pipelineInfo.vertexStorageBufferCount = 1;

    m_wgpu_pipeline = gpu.createGraphicsPipeline(pipelineInfo);

    // Shaders are consumed by the pipeline; release them
    gpu.releaseShader(vertShader);
    gpu.releaseShader(fragShader);

    if (!m_wgpu_pipeline) {
        LOG_CRITICAL("SpriteRenderPass: failed to create WebGPU pipeline for {}", passname);
        return false;
    }

    // Sprite GPU storage buffer
    m_sprite_gpu_buf = gpu.createBuffer({
        static_cast<uint32_t>(MAX_SPRITES * sizeof(CompactSpriteInstance)),
        GpuBufferUsage::StorageRead,
    });

    // Transfer buffer for upload
    m_sprite_xfer_buf = gpu.createTransferBuffer({
        static_cast<uint32_t>(MAX_SPRITES * sizeof(CompactSpriteInstance)),
        GpuTransferUsage::Upload,
    });

    // Create unit quad geometry (CompactVertex2D — pos_xy and uv packed as uint32 half-floats)
    // Quad corners in [-0.5, 0.5] x [-0.5, 0.5] so the shader scales by sprite size
    struct QuadVertex { uint32_t pos_xy; uint32_t uv; };
    auto packHalf = [](float a, float b) -> uint32_t {
        // Simple float-to-half: works for values in [0,1] range
        auto toHalf = [](float f) -> uint16_t {
            union { float f; uint32_t i; } u = {f};
            uint32_t bits = u.i;
            uint32_t sign = (bits >> 16) & 0x8000;
            int32_t  exp  = ((bits >> 23) & 0xFF) - 127 + 15;
            uint32_t mant = (bits >> 13) & 0x3FF;
            if (exp <= 0) return static_cast<uint16_t>(sign);
            if (exp >= 31) return static_cast<uint16_t>(sign | 0x7C00);
            return static_cast<uint16_t>(sign | (exp << 10) | mant);
        };
        return static_cast<uint32_t>(toHalf(a)) | (static_cast<uint32_t>(toHalf(b)) << 16);
    };
    QuadVertex quadVerts[4] = {
        { packHalf(0.0f, 0.0f), packHalf(0.0f, 0.0f) },  // top-left
        { packHalf(1.0f, 0.0f), packHalf(1.0f, 0.0f) },  // top-right
        { packHalf(0.0f, 1.0f), packHalf(0.0f, 1.0f) },  // bottom-left
        { packHalf(1.0f, 1.0f), packHalf(1.0f, 1.0f) },  // bottom-right
    };
    uint16_t quadIdx[6] = { 0, 1, 2, 2, 1, 3 };

    m_quad_xfer_vert = gpu.createTransferBuffer({ sizeof(quadVerts), GpuTransferUsage::Upload });
    m_quad_xfer_idx  = gpu.createTransferBuffer({ sizeof(quadIdx),   GpuTransferUsage::Upload });
    m_quad_vertex_buf = gpu.createBuffer({ sizeof(quadVerts), GpuBufferUsage::Vertex });
    m_quad_index_buf  = gpu.createBuffer({ sizeof(quadIdx),   GpuBufferUsage::Index  });

    {
        void* ptr = gpu.mapTransferBuffer(m_quad_xfer_vert, false);
        memcpy(ptr, quadVerts, sizeof(quadVerts));
        gpu.unmapTransferBuffer(m_quad_xfer_vert);
    }
    {
        void* ptr = gpu.mapTransferBuffer(m_quad_xfer_idx, false);
        memcpy(ptr, quadIdx, sizeof(quadIdx));
        gpu.unmapTransferBuffer(m_quad_xfer_idx);
    }

    // We need a command buffer to upload. Acquire one and submit immediately.
    GpuCmdBufferHandle uploadCmd = gpu.acquireCommandBuffer();
    gpu.uploadToBuffer(uploadCmd, m_quad_xfer_vert, 0, m_quad_vertex_buf, 0, sizeof(quadVerts));
    gpu.uploadToBuffer(uploadCmd, m_quad_xfer_idx,  0, m_quad_index_buf,  0, sizeof(quadIdx));
    gpu.submitCommandBuffer(uploadCmd);

    // Effect temp textures (ping-pong, match swapchain format for pipeline compatibility)
    m_swapchain_fmt = swapchain_texture_format;
    {
        GpuTextureCreateInfo texInfo{};
        texInfo.width       = surface_width;
        texInfo.height      = surface_height;
        texInfo.format      = swapchain_texture_format;
        texInfo.usage       = GpuTextureUsage::Sampler | GpuTextureUsage::ColorTarget;
        texInfo.sampleCount = GpuSampleCount::x1;
        m_effect_tex_a = gpu.createTexture(texInfo);
        m_effect_tex_b = gpu.createTexture(texInfo);
    }

    // Nearest-neighbor sampler for effect pass
    {
        GpuSamplerCreateInfo si{};
        si.minFilter = GpuFilter::Nearest;
        si.magFilter = GpuFilter::Nearest;
        m_effect_sampler = gpu.createSampler(si);
    }

    // Effect fullscreen quad — position (0..1) + texcoord (0..1), 16 bytes per vertex
    // Vert shader transforms position to NDC: pos * 2.0 - 1.0
    struct EffectVertex { float px, py, ux, uy; };
    EffectVertex effectVerts[4] = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
    };
    uint16_t effectIdx[6] = { 0, 1, 2, 2, 1, 3 };

    m_effect_vbuf = gpu.createBuffer({ sizeof(effectVerts), GpuBufferUsage::Vertex });
    m_effect_ibuf = gpu.createBuffer({ sizeof(effectIdx),   GpuBufferUsage::Index  });
    {
        GpuTransferBufferHandle tvb = gpu.createTransferBuffer({ sizeof(effectVerts), GpuTransferUsage::Upload });
        GpuTransferBufferHandle tib = gpu.createTransferBuffer({ sizeof(effectIdx),   GpuTransferUsage::Upload });
        memcpy(gpu.mapTransferBuffer(tvb, false), effectVerts, sizeof(effectVerts));
        gpu.unmapTransferBuffer(tvb);
        memcpy(gpu.mapTransferBuffer(tib, false), effectIdx, sizeof(effectIdx));
        gpu.unmapTransferBuffer(tib);
        GpuCmdBufferHandle effectUploadCmd = gpu.acquireCommandBuffer();
        gpu.uploadToBuffer(effectUploadCmd, tvb, 0, m_effect_vbuf, 0, sizeof(effectVerts));
        gpu.uploadToBuffer(effectUploadCmd, tib, 0, m_effect_ibuf, 0, sizeof(effectIdx));
        gpu.submitCommandBuffer(effectUploadCmd);
        gpu.releaseTransferBuffer(tvb);
        gpu.releaseTransferBuffer(tib);
    }

    if (logInit) {
        LOG_INFO("Render pass initialized (WebGPU): {}", passname.c_str());
    }
    return true;
}
#endif // LUMINOVEAU_WEBGPU_BACKEND

#ifndef LUMINOVEAU_WEBGPU_BACKEND
void SpriteRenderPass::render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4 &camera
) {
    auto* cmd_buffer = reinterpret_cast<SDL_GPUCommandBuffer*>(cmdBuffer);
    auto* target_texture = reinterpret_cast<SDL_GPUTexture*>(targetTexture);
    #ifdef LUMIDEBUG
    SDL_PushGPUDebugGroup(cmd_buffer, CURRENT_METHOD());
    #endif
    
    // Check if ANY sprite in the queue has effects
    bool hasAnyEffects = false;
    for (uint32_t i = 0; i < renderQueue->Count(); i++) {
        if ((*renderQueue)[i].effectIndex >= 0) {
            hasAnyEffects = true;
            break;
        }
    }

    //sets up transfer - map the transfer buffer directly
    auto *dataPtr = static_cast<CompactSpriteInstance *>(SDL_MapGPUTransferBuffer(
        m_gpu_device,
        SpriteDataTransferBuffer,
        false
    ));

    // Copy and compress from renderQueue to transfer buffer
    // Convert float32 to float16 and pack pairs into uint32
    
    size_t spriteCount = renderQueue->Count();
    size_t thread_count = thread_pool.get_thread_count();
    size_t chunk_size = spriteCount / thread_count + 1;
    
    for (size_t start = 0; start < spriteCount; start += chunk_size) {
        size_t end = std::min(start + chunk_size, spriteCount);
        thread_pool.enqueue([this, dataPtr, start, end]() {
            for (size_t i = start; i < end; ++i) {
                const auto& sprite = (*renderQueue)[i];
                // Validate and sanitize input values
                float x = sprite.x;
                float y = sprite.y;
                float z = sprite.z;
                float rotation = sprite.rotation;
                float tex_u = fast_clamp(sprite.tex_u, 0.0f, 1.0f);
                float tex_v = fast_clamp(sprite.tex_v, 0.0f, 1.0f);
                float tex_w = fast_clamp(sprite.tex_w, -1.0f, 1.0f);
                float tex_h = fast_clamp(sprite.tex_h, -1.0f, 1.0f);
                float r = fast_clamp(sprite.r, 0.0f, 1.0f);
                float g = fast_clamp(sprite.g, 0.0f, 1.0f);
                float b = fast_clamp(sprite.b, 0.0f, 1.0f);
                float a = fast_clamp(sprite.a, 0.0f, 1.0f);
                float w = fast_max(sprite.w, 0.001f);  // Prevent zero size
                float h = fast_max(sprite.h, 0.001f);
                float pivot_x = sprite.pivot_x;
                float pivot_y = sprite.pivot_y;
                bool isSDF = sprite.isSDF;
                
                // Pack each pair of floats into a single uint32
                dataPtr[i].pos_xy = pack_half2(x, y);
                dataPtr[i].pos_z_rot = pack_half2(z, rotation);
                dataPtr[i].tex_uv = pack_half2(tex_u, tex_v);
                dataPtr[i].tex_wh = pack_half2(tex_w, tex_h);
                dataPtr[i].color_rg = pack_half2(r, g);
                dataPtr[i].color_ba = pack_half2(b, a);
                dataPtr[i].size_wh = pack_half2(w, h);
                
                // Pack pivot_xy with SDF flag in highest bit
                uint32_t pivot_packed = pack_half2(pivot_x, pivot_y);
                if (isSDF) {
                    pivot_packed |= 0x80000000u;  // Set highest bit
                }
                dataPtr[i].pivot_xy = pivot_packed;
            }
        });
    }
    thread_pool.wait_all();

    // Build batches respecting z-order, geometry, and texture changes
    // Also track which batches have effects
    std::vector<Batch> batches;
    batches.reserve(64);
    std::vector<bool> batchHasEffects;  // Track if each batch has effects
    batchHasEffects.reserve(64);

    size_t      currentOffset = 0;
    for (size_t i             = 0; i < spriteCount; ++i) {
        const auto& cur = (*renderQueue)[i];
        bool geometryChanged = (i > 0 && cur.geometry != (*renderQueue)[i - 1].geometry);
        bool textureChanged = (i > 0 && cur.texture.gpuTexture != (*renderQueue)[i - 1].texture.gpuTexture);
        bool effectsChanged = (i > 0 && cur.effectIndex != (*renderQueue)[i - 1].effectIndex);
        
        if (i == 0 || geometryChanged || textureChanged || effectsChanged) {
            // Start a new batch when geometry, texture, or effects change
            Batch batch;
            batch.offset  = currentOffset;
            batch.count   = 1;
            batch.geometry = cur.geometry;
            batch.texture = reinterpret_cast<SDL_GPUTexture*>(cur.texture.gpuTexture);
            batch.sampler = reinterpret_cast<SDL_GPUSampler*>(cur.texture.gpuSampler);
            batches.push_back(batch);
            batchHasEffects.push_back(cur.effectIndex >= 0);
        } else {
            // Continue the current batch
            batches.back().count++;
        }
        currentOffset++;
    }
    // Data already copied directly to transfer buffer by thread pool
    SDL_UnmapGPUTransferBuffer(m_gpu_device, SpriteDataTransferBuffer);

    if (spriteCount > 0) {
        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmd_buffer);

        SDL_GPUTransferBufferLocation transferBufferLocation = {
            .transfer_buffer = SpriteDataTransferBuffer,
            .offset = 0
        };

        SDL_GPUBufferRegion bufferRegion = {
            .buffer = SpriteDataBuffer,
            .offset = 0,
            .size = static_cast<Uint32>(spriteCount * sizeof(CompactSpriteInstance))
        };

        SDL_UploadToGPUBuffer(
            copyPass,
            &transferBufferLocation,
            &bufferRegion,
            false
        );
        SDL_EndGPUCopyPass(copyPass);
    }

    // Check if we should resolve (if renderTargetResolve is set)
    bool shouldResolve = (renderTargetResolve != 0);

    // Render batches in Z-order, handling effects appropriately
    if (!hasAnyEffects) {
        // No effects - simple single render pass
        SDL_GPUTexture* renderTarget = target_texture;
        
        SDL_GPUColorTargetInfo color_target_info{
            .texture              = renderTarget,
            .mip_level            = 0,
            .layer_or_depth_plane = 0,
            .clear_color          = SDL_FColor{color_target_clear_r, color_target_clear_g, color_target_clear_b, color_target_clear_a},
            .load_op              = toSDL(color_target_info_loadop),
            .store_op             = shouldResolve ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE,
            .resolve_texture      = reinterpret_cast<SDL_GPUTexture*>(renderTargetResolve),
            .resolve_mip_level    = 0,
            .resolve_layer        = 0,
            .cycle                = false};

        SDL_GPURenderPass* sdl_rp = SDL_BeginGPURenderPass(cmd_buffer, &color_target_info, 1, nullptr);
        render_pass = reinterpret_cast<GpuRenderPassHandle>(sdl_rp);
        assert(sdl_rp);

        SDL_GPUViewport viewport = {
            .x = 0, .y = 0,
            .w = (float)Window::GetPhysicalWidth(),
            .h = (float)Window::GetPhysicalHeight(),
            .min_depth = 0.0f, .max_depth = 1.0f
        };
        SDL_SetGPUViewport(sdl_rp, &viewport);

        if (_scissorEnabled) {
            SDL_Rect scissorRect = { _scissorX, _scissorY, static_cast<int>(_scissorW), static_cast<int>(_scissorH) };
            SDL_SetGPUScissor(sdl_rp, &scissorRect);
            _scissorEnabled = false;
        }

        SDL_BindGPUGraphicsPipeline(sdl_rp, m_pipeline);
        SDL_BindGPUVertexStorageBuffers(sdl_rp, 0, &SpriteDataBuffer, 1);

        for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
            const auto &batch = batches[batchIdx];
            if (batch.texture == nullptr || batch.sampler == nullptr || batch.geometry == nullptr) continue;

            SDL_GPUBufferBinding vertexBinding = {.buffer = reinterpret_cast<SDL_GPUBuffer*>(batch.geometry->vertexBuffer), .offset = 0};
            SDL_BindGPUVertexBuffers(sdl_rp, 0, &vertexBinding, 1);

            SDL_GPUBufferBinding indexBinding = {.buffer = reinterpret_cast<SDL_GPUBuffer*>(batch.geometry->indexBuffer), .offset = 0};
            SDL_BindGPUIndexBuffer(sdl_rp, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

            SDL_GPUTextureSamplerBinding samplerBinding = {.texture = batch.texture, .sampler = batch.sampler};
            SDL_BindGPUFragmentSamplers(sdl_rp, 0, &samplerBinding, 1);
            SDL_PushGPUVertexUniformData(cmd_buffer, 0, &camera, sizeof(glm::mat4));

            uint32_t batchOffset = static_cast<uint32_t>(batch.offset);
            SDL_PushGPUVertexUniformData(cmd_buffer, 1, &batchOffset, sizeof(uint32_t));

            SDL_DrawGPUIndexedPrimitives(sdl_rp,
                static_cast<uint32_t>(batch.geometry->GetIndexCount()),
                batch.count, 0, 0, 0);
        }

        SDL_EndGPURenderPass(sdl_rp);
    } else {
        // Has effects - need to process batches in order to maintain z-ordering
        // Strategy: iterate batches in order, accumulating non-effect batches in a render pass,
        // then when we hit an effect batch, end pass, render effect batch, restart pass
        
        SDL_GPURenderPass* currentPass = nullptr;
        
        for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
            const auto& batch = batches[batchIdx];
            if (batch.texture == nullptr || batch.sampler == nullptr || batch.geometry == nullptr) continue;
            
            if (!batchHasEffects[batchIdx]) {
                // Non-effect batch - render normally
                // Start a render pass if needed
                if (!currentPass) {
                    SDL_GPUColorTargetInfo colorTarget = {
                        .texture = target_texture,
                        .mip_level = 0,
                        .layer_or_depth_plane = 0,
                        .clear_color = SDL_FColor{color_target_clear_r, color_target_clear_g, color_target_clear_b, color_target_clear_a},
                        .load_op = (batchIdx == 0) ? toSDL(color_target_info_loadop) : SDL_GPU_LOADOP_LOAD,
                        .store_op = SDL_GPU_STOREOP_STORE,
                        .resolve_texture = nullptr,
                        .resolve_mip_level = 0,
                        .resolve_layer = 0,
                        .cycle = false
                    };
                    currentPass = SDL_BeginGPURenderPass(cmd_buffer, &colorTarget, 1, nullptr);
                    
                    SDL_GPUViewport viewport = {.x = 0, .y = 0,
                        .w = (float)Window::GetPhysicalWidth(), .h = (float)Window::GetPhysicalHeight(),
                        .min_depth = 0.0f, .max_depth = 1.0f};
                    SDL_SetGPUViewport(currentPass, &viewport);
                    if (_scissorEnabled) {
                        SDL_Rect scissorRect = { _scissorX, _scissorY, static_cast<int>(_scissorW), static_cast<int>(_scissorH) };
                        SDL_SetGPUScissor(currentPass, &scissorRect);
                        _scissorEnabled = false;
                    }
                    SDL_BindGPUGraphicsPipeline(currentPass, m_pipeline);
                    SDL_BindGPUVertexStorageBuffers(currentPass, 0, &SpriteDataBuffer, 1);
                }
                
                // Render this batch
                SDL_GPUBufferBinding vertexBinding = {.buffer = reinterpret_cast<SDL_GPUBuffer*>(batch.geometry->vertexBuffer), .offset = 0};
                SDL_BindGPUVertexBuffers(currentPass, 0, &vertexBinding, 1);

                SDL_GPUBufferBinding indexBinding = {.buffer = reinterpret_cast<SDL_GPUBuffer*>(batch.geometry->indexBuffer), .offset = 0};
                SDL_BindGPUIndexBuffer(currentPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                
                SDL_GPUTextureSamplerBinding samplerBinding = {.texture = batch.texture, .sampler = batch.sampler};
                SDL_BindGPUFragmentSamplers(currentPass, 0, &samplerBinding, 1);
                SDL_PushGPUVertexUniformData(cmd_buffer, 0, &camera, sizeof(glm::mat4));
                
                uint32_t batchOffset = static_cast<uint32_t>(batch.offset);
                SDL_PushGPUVertexUniformData(cmd_buffer, 1, &batchOffset, sizeof(uint32_t));
                
                SDL_DrawGPUIndexedPrimitives(currentPass,
                    static_cast<uint32_t>(batch.geometry->GetIndexCount()),
                    batch.count, 0, 0, 0);
            } else {
                // Effect batch - need to end current pass, render through effect pipeline
                if (currentPass) {
                    SDL_EndGPURenderPass(currentPass);
                    currentPass = nullptr;
                }
                
                // Get effects from first sprite in batch
                size_t spriteIdx = batch.offset;
                int32_t effectIdx = (*renderQueue)[spriteIdx].effectIndex;
                if (spriteIdx >= spriteCount || effectIdx < 0) continue;
                const auto& effectStore = Draw::GetEffectStore();
                if (effectIdx >= (int32_t)effectStore.size()) continue;
                const auto& effects = effectStore[effectIdx];
                
                // Step 1: Render this batch to temp texture
                SDL_GPUColorTargetInfo tempTarget = {
                    .texture = reinterpret_cast<SDL_GPUTexture*>(effectTempA.gpuTexture),
                    .mip_level = 0,
                    .layer_or_depth_plane = 0,
                    .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
                    .load_op = SDL_GPU_LOADOP_CLEAR,
                    .store_op = SDL_GPU_STOREOP_STORE,
                    .resolve_texture = nullptr,
                    .resolve_mip_level = 0,
                    .resolve_layer = 0,
                    .cycle = false  // Don't cycle - we're using it in the same command buffer
                };
                
                SDL_GPURenderPass* tempPass = SDL_BeginGPURenderPass(cmd_buffer, &tempTarget, 1, nullptr);
                
                // Viewport matches physical pixel size within desktop-sized texture
                SDL_GPUViewport viewport = {.x = 0, .y = 0,
                    .w = (float)Window::GetPhysicalWidth(), .h = (float)Window::GetPhysicalHeight(),
                    .min_depth = 0.0f, .max_depth = 1.0f};
                SDL_SetGPUViewport(tempPass, &viewport);
                SDL_BindGPUGraphicsPipeline(tempPass, effectSpritePipeline);  // Use no-blend pipeline!
                SDL_BindGPUVertexStorageBuffers(tempPass, 0, &SpriteDataBuffer, 1);
                
                SDL_GPUBufferBinding vertexBinding = {.buffer = reinterpret_cast<SDL_GPUBuffer*>(batch.geometry->vertexBuffer), .offset = 0};
                SDL_BindGPUVertexBuffers(tempPass, 0, &vertexBinding, 1);

                SDL_GPUBufferBinding indexBinding = {.buffer = reinterpret_cast<SDL_GPUBuffer*>(batch.geometry->indexBuffer), .offset = 0};
                SDL_BindGPUIndexBuffer(tempPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                
                SDL_GPUTextureSamplerBinding samplerBinding = {.texture = batch.texture, .sampler = batch.sampler};
                SDL_BindGPUFragmentSamplers(tempPass, 0, &samplerBinding, 1);
                SDL_PushGPUVertexUniformData(cmd_buffer, 0, &camera, sizeof(glm::mat4));
                
                uint32_t batchOffset = static_cast<uint32_t>(batch.offset);
                SDL_PushGPUVertexUniformData(cmd_buffer, 1, &batchOffset, sizeof(uint32_t));
                
                SDL_DrawGPUIndexedPrimitives(tempPass,
                    static_cast<uint32_t>(batch.geometry->GetIndexCount()),
                    batch.count, 0, 0, 0);
                
                SDL_EndGPURenderPass(tempPass);
                
                const auto& effectTextureStore = Draw::GetEffectTextureStore();
                const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>> emptyTextures;
                const auto& storedTextures = (effectIdx < (int32_t)effectTextureStore.size()) ? effectTextureStore[effectIdx] : emptyTextures;
                applyEffects(cmd_buffer, effects, reinterpret_cast<SDL_GPUTexture*>(effectTempA.gpuTexture), target_texture, camera, m_swapchain_format, batchIdx == 0, storedTextures);
            }
        }
        
        // End any remaining render pass
        if (currentPass) {
            SDL_EndGPURenderPass(currentPass);
        }

        // The effects path opens/closes its own render passes with STOREOP_STORE,
        // so the normal renderTargetResolve mechanism is never triggered.
        // Manually resolve fbContentMSAA → fbContent here if needed.
        if (shouldResolve) {
            SDL_GPUColorTargetInfo resolveInfo = {
                .texture               = target_texture,
                .mip_level             = 0,
                .layer_or_depth_plane  = 0,
                .clear_color           = {0.0f, 0.0f, 0.0f, 0.0f},
                .load_op               = SDL_GPU_LOADOP_LOAD,
                .store_op              = SDL_GPU_STOREOP_RESOLVE,
                .resolve_texture       = reinterpret_cast<SDL_GPUTexture*>(renderTargetResolve),
                .resolve_mip_level     = 0,
                .resolve_layer         = 0,
                .cycle                 = false,
                .cycle_resolve_texture = false,
            };
            SDL_GPURenderPass* resolvePass = SDL_BeginGPURenderPass(cmd_buffer, &resolveInfo, 1, nullptr);
            SDL_EndGPURenderPass(resolvePass);
        }
    }

#ifdef LUMIDEBUG
    SDL_PopGPUDebugGroup(cmd_buffer);
#endif
}
#else // LUMINOVEAU_WEBGPU_BACKEND
void SpriteRenderPass::render(
    GpuCmdBufferHandle cmdBuffer, GpuTextureHandle targetTexture, const glm::mat4& camera
) {
    auto& gpu = Renderer::GetGpu();

    if (!renderQueue || renderQueue->Count() == 0) {
        // Empty queue — just clear
        GpuColorTargetInfo ct{};
        ct.texture  = targetTexture;
        ct.loadOp   = color_target_info_loadop;
        ct.storeOp  = GpuStoreOp::Store;
        ct.clearR   = color_target_clear_r;
        ct.clearG   = color_target_clear_g;
        ct.clearB   = color_target_clear_b;
        ct.clearA   = color_target_clear_a;
        auto rp = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
        gpu.endRenderPass(rp);
        return;
    }

    size_t spriteCount = renderQueue->Count();

    // Pack sprite data into transfer buffer
    auto* dataPtr = static_cast<CompactSpriteInstance*>(gpu.mapTransferBuffer(m_sprite_xfer_buf, false));
    for (size_t i = 0; i < spriteCount; ++i) {
        const auto& sprite = (*renderQueue)[i];
        float x       = sprite.x;
        float y       = sprite.y;
        float z       = sprite.z;
        float rotation = sprite.rotation;
        float tex_u   = fast_clamp(sprite.tex_u, 0.0f, 1.0f);
        float tex_v   = fast_clamp(sprite.tex_v, 0.0f, 1.0f);
        float tex_w   = fast_clamp(sprite.tex_w, -1.0f, 1.0f);
        float tex_h   = fast_clamp(sprite.tex_h, -1.0f, 1.0f);
        float r       = fast_clamp(sprite.r, 0.0f, 1.0f);
        float g       = fast_clamp(sprite.g, 0.0f, 1.0f);
        float b       = fast_clamp(sprite.b, 0.0f, 1.0f);
        float a       = fast_clamp(sprite.a, 0.0f, 1.0f);
        float w       = fast_max(sprite.w, 0.001f);
        float h       = fast_max(sprite.h, 0.001f);
        float pivot_x = sprite.pivot_x;
        float pivot_y = sprite.pivot_y;
        bool  isSDF   = sprite.isSDF;

        dataPtr[i].pos_xy    = pack_half2(x, y);
        dataPtr[i].pos_z_rot = pack_half2(z, rotation);
        dataPtr[i].tex_uv    = pack_half2(tex_u, tex_v);
        dataPtr[i].tex_wh    = pack_half2(tex_w, tex_h);
        dataPtr[i].color_rg  = pack_half2(r, g);
        dataPtr[i].color_ba  = pack_half2(b, a);
        dataPtr[i].size_wh   = pack_half2(w, h);
        uint32_t pivot_packed = pack_half2(pivot_x, pivot_y);
        if (isSDF) pivot_packed |= 0x80000000u;
        dataPtr[i].pivot_xy  = pivot_packed;
    }
    gpu.unmapTransferBuffer(m_sprite_xfer_buf);

    // Upload to GPU buffer
    gpu.uploadToBuffer(cmdBuffer, m_sprite_xfer_buf, 0, m_sprite_gpu_buf, 0,
                       static_cast<uint32_t>(spriteCount * sizeof(CompactSpriteInstance)));

    // Build batches
    std::vector<Batch> batches;
    batches.reserve(64);
    size_t currentOffset = 0;
    for (size_t i = 0; i < spriteCount; ++i) {
        const auto& cur = (*renderQueue)[i];
        bool geomChanged    = (i > 0 && cur.geometry != (*renderQueue)[i - 1].geometry);
        bool textureChanged = (i > 0 && cur.texture.gpuTexture != (*renderQueue)[i - 1].texture.gpuTexture);
        bool effectChanged  = (i > 0 && cur.effectIndex != (*renderQueue)[i - 1].effectIndex);

        if (i == 0 || geomChanged || textureChanged || effectChanged) {
            Batch batch;
            batch.offset  = currentOffset;
            batch.count   = 1;
            // Use per-renderable geometry when provided (Mode7, custom meshes); otherwise unit quad.
            if (cur.geometry && cur.geometry->vertexBuffer && cur.geometry->indexBuffer) {
                batch.vertexBuffer = cur.geometry->vertexBuffer;
                batch.indexBuffer  = cur.geometry->indexBuffer;
                batch.indexCount   = static_cast<uint32_t>(cur.geometry->GetIndexCount());
            } else {
                batch.vertexBuffer = m_quad_vertex_buf;
                batch.indexBuffer  = m_quad_index_buf;
                batch.indexCount   = 6;
            }
            batch.texture = cur.texture.gpuTexture;
            batch.sampler = cur.texture.gpuSampler;
            batches.push_back(batch);
        } else {
            batches.back().count++;
        }
        currentOffset++;
    }

    // Determine if any batch uses effects
    bool hasAnyEffects = false;
    const auto& effectStore = Draw::GetEffectStore();
    for (size_t i = 0; i < batches.size(); ++i) {
        size_t spriteIdx = batches[i].offset;
        int32_t effectIdx = (*renderQueue)[spriteIdx].effectIndex;
        if (effectIdx >= 0 && effectIdx < (int32_t)effectStore.size() && !effectStore[effectIdx].empty()) {
            hasAnyEffects = true;
            break;
        }
    }

    if (!hasAnyEffects) {
        // Simple path: one render pass, all batches go directly to target
        GpuColorTargetInfo ct{};
        ct.texture  = targetTexture;
        ct.loadOp   = color_target_info_loadop;
        ct.storeOp  = GpuStoreOp::Store;
        ct.clearR   = color_target_clear_r;
        ct.clearG   = color_target_clear_g;
        ct.clearB   = color_target_clear_b;
        ct.clearA   = color_target_clear_a;
        auto rp = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
        gpu.bindGraphicsPipeline(rp, m_wgpu_pipeline);
        gpu.bindVertexStorageBuffers(rp, 0, &m_sprite_gpu_buf, 1);

        for (const auto& batch : batches) {
            if (!batch.texture || !batch.sampler || !batch.vertexBuffer || !batch.indexBuffer) continue;
            GpuBufferBinding vb{ batch.vertexBuffer, 0 };
            gpu.bindVertexBuffers(rp, 0, &vb, 1);
            GpuBufferBinding ib{ batch.indexBuffer, 0 };
            gpu.bindIndexBuffer(rp, ib, true);
            GpuTextureSamplerBinding tsb{ batch.texture, batch.sampler };
            gpu.bindFragmentSamplers(rp, 0, &tsb, 1);
            gpu.pushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));
            uint32_t instOff[8] = {};
            instOff[0] = static_cast<uint32_t>(batch.offset);
            gpu.pushVertexUniformData(cmdBuffer, 1, instOff, 32);
            gpu.drawIndexedPrimitives(rp, batch.indexCount, static_cast<uint32_t>(batch.count), 0, 0, 0);
        }
        gpu.endRenderPass(rp);
    } else {
        // Effect path: iterate batches, switching between direct and effect rendering
        GpuRenderPassHandle currentPass = 0;
        bool passIsOpen = false;

        auto openSpritePass = [&](bool isFirst) {
            if (passIsOpen) return;
            GpuColorTargetInfo ct{};
            ct.texture  = targetTexture;
            ct.loadOp   = isFirst ? color_target_info_loadop : GpuLoadOp::Load;
            ct.storeOp  = GpuStoreOp::Store;
            ct.clearR   = color_target_clear_r;
            ct.clearG   = color_target_clear_g;
            ct.clearB   = color_target_clear_b;
            ct.clearA   = color_target_clear_a;
            currentPass = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
            gpu.bindGraphicsPipeline(currentPass, m_wgpu_pipeline);
            gpu.bindVertexStorageBuffers(currentPass, 0, &m_sprite_gpu_buf, 1);
            passIsOpen = true;
        };
        auto closeSpritePass = [&]() {
            if (!passIsOpen) return;
            gpu.endRenderPass(currentPass);
            currentPass = 0;
            passIsOpen = false;
        };

        bool firstBatch = true;
        for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
            const auto& batch = batches[batchIdx];
            if (!batch.texture || !batch.sampler || !batch.vertexBuffer || !batch.indexBuffer) continue;

            size_t spriteIdx = batch.offset;
            int32_t effectIdx = (*renderQueue)[spriteIdx].effectIndex;
            bool batchHasEffect = (effectIdx >= 0 && effectIdx < (int32_t)effectStore.size()
                                   && !effectStore[effectIdx].empty());

            if (!batchHasEffect) {
                openSpritePass(firstBatch);
                firstBatch = false;

                GpuBufferBinding vb{ batch.vertexBuffer, 0 };
                gpu.bindVertexBuffers(currentPass, 0, &vb, 1);
                GpuBufferBinding ib{ batch.indexBuffer, 0 };
                gpu.bindIndexBuffer(currentPass, ib, true);
                GpuTextureSamplerBinding tsb{ batch.texture, batch.sampler };
                gpu.bindFragmentSamplers(currentPass, 0, &tsb, 1);
                gpu.pushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));
                uint32_t instOff[8] = {};
                instOff[0] = static_cast<uint32_t>(batch.offset);
                gpu.pushVertexUniformData(cmdBuffer, 1, instOff, 32);
                gpu.drawIndexedPrimitives(currentPass, batch.indexCount, static_cast<uint32_t>(batch.count), 0, 0, 0);
            } else {
                closeSpritePass();
                firstBatch = false;

                const auto& effects = effectStore[effectIdx];

                // Render sprite batch to effectTempA
                {
                    GpuColorTargetInfo ct{};
                    ct.texture  = m_effect_tex_a;
                    ct.loadOp   = GpuLoadOp::Clear;
                    ct.storeOp  = GpuStoreOp::Store;
                    ct.clearR = ct.clearG = ct.clearB = ct.clearA = 0.0f;
                    auto tmpRp = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);
                    gpu.bindGraphicsPipeline(tmpRp, m_wgpu_pipeline);
                    gpu.bindVertexStorageBuffers(tmpRp, 0, &m_sprite_gpu_buf, 1);
                    GpuBufferBinding vb{ batch.vertexBuffer, 0 };
                    gpu.bindVertexBuffers(tmpRp, 0, &vb, 1);
                    GpuBufferBinding ib{ batch.indexBuffer, 0 };
                    gpu.bindIndexBuffer(tmpRp, ib, true);
                    GpuTextureSamplerBinding tsb{ batch.texture, batch.sampler };
                    gpu.bindFragmentSamplers(tmpRp, 0, &tsb, 1);
                    gpu.pushVertexUniformData(cmdBuffer, 0, &camera, sizeof(glm::mat4));
                    uint32_t instOff[8] = {};
                    instOff[0] = static_cast<uint32_t>(batch.offset);
                    gpu.pushVertexUniformData(cmdBuffer, 1, instOff, 32);
                    gpu.drawIndexedPrimitives(tmpRp, batch.indexCount, static_cast<uint32_t>(batch.count), 0, 0, 0);
                    gpu.endRenderPass(tmpRp);
                }

                // Apply effects: effectTempA → targetTexture
                // firstBatch tracks whether target was already written (to choose load vs clear)
                const auto& effectTextureStore = Draw::GetEffectTextureStore();
                const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>> emptyExtraTex;
                const auto& storedTextures = (effectIdx < (int32_t)effectTextureStore.size())
                                              ? effectTextureStore[effectIdx] : emptyExtraTex;
                _applyEffectsWGPU(cmdBuffer, effects, m_effect_tex_a, targetTexture, storedTextures,
                                  batchIdx == 0 ? color_target_info_loadop : GpuLoadOp::Load,
                                  color_target_clear_r, color_target_clear_g,
                                  color_target_clear_b, color_target_clear_a);
            }
        }
        closeSpritePass();
    }
}
#endif // LUMINOVEAU_WEBGPU_BACKEND

#ifdef LUMINOVEAU_WEBGPU_BACKEND
GpuGraphicsPipelineHandle SpriteRenderPass::_getOrCreateEffectPipeline(
    const ShaderAsset& vertShader, const ShaderAsset& fragShader)
{
    auto it = m_effect_pipelines.find(fragShader.gpuShader);
    if (it != m_effect_pipelines.end()) return it->second;

    auto& gpu = Renderer::GetGpu();

    // Effect pipeline: 2 vertex attributes (position + texcoord, both vec2<f32>)
    GpuVertexAttribute attrs[2] = {
        { .location = 0, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 0 },
        { .location = 1, .binding = 0, .format = GpuVertexElementFormat::Float2, .offset = 8 },
    };
    GpuVertexBinding vbind{ .binding = 0, .stride = 16, .instanceStepping = false };

    // Standard alpha blend (over)
    GpuColorTargetBlendState blend{};
    blend.blendEnabled   = true;
    blend.srcColorFactor = GpuBlendFactor::SrcAlpha;
    blend.dstColorFactor = GpuBlendFactor::OneMinusSrcAlpha;
    blend.colorOp        = GpuBlendOp::Add;
    blend.srcAlphaFactor = GpuBlendFactor::One;
    blend.dstAlphaFactor = GpuBlendFactor::OneMinusSrcAlpha;
    blend.alphaOp        = GpuBlendOp::Add;

    GpuGraphicsPipelineCreateInfo pci{};
    pci.vertexShader      = vertShader.gpuShader;
    pci.fragmentShader    = fragShader.gpuShader;
    pci.attributes        = attrs;
    pci.attributeCount    = 2;
    pci.bindings          = &vbind;
    pci.bindingCount      = 1;
    pci.fillMode          = GpuFillMode::Fill;
    pci.cullMode          = GpuCullMode::None;
    pci.frontFace         = GpuFrontFace::CounterClockwise;
    pci.colorTargetFormat = m_swapchain_fmt;
    pci.blend             = blend;
    pci.hasDepthTarget    = false;
    pci.sampleCount       = GpuSampleCount::x1;
    pci.vertexStorageBufferCount = 0;

    GpuGraphicsPipelineHandle pipeline = gpu.createGraphicsPipeline(pci);
    m_effect_pipelines[fragShader.gpuShader] = pipeline;
    return pipeline;
}

void SpriteRenderPass::_applyEffectsWGPU(
    GpuCmdBufferHandle cmdBuffer,
    const std::vector<EffectAsset>& effects,
    GpuTextureHandle sourceTexture,
    GpuTextureHandle targetTexture,
    const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>>& extraTextures,
    GpuLoadOp targetLoadOp,
    float clearR, float clearG, float clearB, float clearA)
{
    auto& gpu = Renderer::GetGpu();

    GpuTextureHandle readTex  = sourceTexture;
    // Intermediate results ping-pong through tempA/tempB; last effect writes to targetTexture.
    GpuTextureHandle writeTex = m_effect_tex_b;

    for (size_t i = 0; i < effects.size(); ++i) {
        const auto& effect = effects[i];
        bool isLast = (i == effects.size() - 1);
        if (isLast) writeTex = targetTexture;

        GpuGraphicsPipelineHandle pipeline = _getOrCreateEffectPipeline(effect.vertShader, effect.fragShader);
        if (!pipeline) {
            LOG_ERROR("_applyEffectsWGPU: failed to get/create effect pipeline");
            continue;
        }

        // Render readTex → writeTex through effect shader
        GpuColorTargetInfo ct{};
        ct.texture  = writeTex;
        // Use caller-supplied load op for the final target; intermediate temps always use Clear
        ct.loadOp   = isLast ? targetLoadOp : GpuLoadOp::Clear;
        ct.clearR   = clearR; ct.clearG = clearG; ct.clearB = clearB; ct.clearA = clearA;
        ct.storeOp  = GpuStoreOp::Store;
        auto rp = gpu.beginRenderPass(cmdBuffer, &ct, 1, nullptr);

        gpu.bindGraphicsPipeline(rp, pipeline);

        GpuBufferBinding vb{ m_effect_vbuf, 0 };
        gpu.bindVertexBuffers(rp, 0, &vb, 1);
        GpuBufferBinding ib{ m_effect_ibuf, 0 };
        gpu.bindIndexBuffer(rp, ib, true);

        // group 2: sampler+texture pairs. Slot 0 = ping-pong source (or user override at binding 0).
        // Additional slots come from Draw::SetEffectTexture, keyed by sampler index.
        const uint32_t pairCount = effect.fragShader.samplerCount > 0 ? effect.fragShader.samplerCount : 1;
        std::vector<GpuTextureSamplerBinding> tsbs(pairCount);
        for (uint32_t s = 0; s < pairCount; ++s) {
            auto it = extraTextures.find(s);
            if (it != extraTextures.end()) {
                tsbs[s].texture = it->second.first;
                tsbs[s].sampler = Renderer::GetSampler(it->second.second);
            } else {
                tsbs[s].texture = readTex;
                tsbs[s].sampler = m_effect_sampler;
            }
        }
        gpu.bindFragmentSamplers(rp, 0, tsbs.data(), pairCount);

        // Push fragment uniform data if effect has parameters (group 1, slot 0)
        if (effect.uniforms && effect.uniforms->getBufferSize() > 0) {
            gpu.pushFragmentUniformData(cmdBuffer, 0,
                effect.uniforms->getBufferPointer(),
                static_cast<uint32_t>(effect.uniforms->getBufferSize()));
        }

        gpu.drawIndexedPrimitives(rp, 6, 1, 0, 0, 0);
        gpu.endRenderPass(rp);

        // Ping-pong: next read comes from what we just wrote; alternate between tempA and tempB
        if (!isLast) {
            readTex  = writeTex;
            writeTex = (writeTex == m_effect_tex_b) ? m_effect_tex_a : m_effect_tex_b;
        }
    }
}
#endif // LUMINOVEAU_WEBGPU_BACKEND

#ifndef LUMINOVEAU_WEBGPU_BACKEND
void SpriteRenderPass::createEffectResources() {
    // Create temporary textures for effect ping-pong rendering
    // Use surface (desktop) size to match framebuffer
    uint32_t width = m_surface_width;
    uint32_t height = m_surface_height;
    
    SDL_GPUTextureCreateInfo tempTexInfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    
    effectTempA.gpuTexture = reinterpret_cast<GpuTextureHandle>(SDL_CreateGPUTexture(Renderer::GetDevice(), &tempTexInfo));
    effectTempA.gpuSampler = Renderer::GetSampler(ScaleMode::Nearest);
    effectTempA.width = width;
    effectTempA.height = height;
    effectTempA.filename = "[Lumi]EffectTempA";

    effectTempB.gpuTexture = reinterpret_cast<GpuTextureHandle>(SDL_CreateGPUTexture(Renderer::GetDevice(), &tempTexInfo));
    effectTempB.gpuSampler = Renderer::GetSampler(ScaleMode::Nearest);
    effectTempB.width = width;
    effectTempB.height = height;
    effectTempB.filename = "[Lumi]EffectTempB";
    
    if (!effectTempA.gpuTexture || !effectTempB.gpuTexture) {
        LOG_ERROR("Failed to create effect temp textures: {}", SDL_GetError());
        return;
    }
    
    // Create pipeline for rendering sprites to temp texture (no blending - direct copy)
    SDL_GPUColorTargetBlendState noBlendState = {
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
        .color_blend_op = SDL_GPU_BLENDOP_ADD,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
        .color_write_mask = 0xF,
        .enable_blend = true,  // Enable but with ONE/ZERO = direct copy
        .enable_color_write_mask = false,
    };
    
    SDL_GPUColorTargetDescription colorTargetDesc = {
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_state = noBlendState,
    };
    
    SDL_GPUVertexAttribute vertexAttributes[] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT, .offset = 0},
        {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT, .offset = 4}
    };
    
    SDL_GPUVertexBufferDescription vertexBufferDesc = {
        .slot = 0,
        .pitch = 8,  // sizeof(CompactVertex2D)
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0
    };
    
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state = {
            .vertex_buffer_descriptions = &vertexBufferDesc,
            .num_vertex_buffers = 1,
            .vertex_attributes = vertexAttributes,
            .num_vertex_attributes = 2,
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = SDL_DefaultRasterizerState,
        .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1, .sample_mask = 0, .enable_mask = false},
        .depth_stencil_state = {.enable_depth_test = false, .enable_depth_write = false, .enable_stencil_test = false},
        .target_info = {
            .color_target_descriptions = &colorTargetDesc,
            .num_color_targets = 1,
            .has_depth_stencil_target = false,
        },
        .props = 0,
    };
    
    effectSpritePipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &pipelineInfo);
    if (!effectSpritePipeline) {
        LOG_ERROR("Failed to create effect sprite pipeline: {}", SDL_GetError());
    }
}

void SpriteRenderPass::releaseEffectResources() {
    if (effectTempA.gpuTexture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), reinterpret_cast<SDL_GPUTexture*>(effectTempA.gpuTexture));
        effectTempA.gpuTexture = 0;
    }
    if (effectTempB.gpuTexture) {
        SDL_ReleaseGPUTexture(Renderer::GetDevice(), reinterpret_cast<SDL_GPUTexture*>(effectTempB.gpuTexture));
        effectTempB.gpuTexture = 0;
    }
    if (effectPipeline) {
        SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), effectPipeline);
        effectPipeline = nullptr;
    }
    if (effectSpritePipeline) {
        SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), effectSpritePipeline);
        effectSpritePipeline = nullptr;
    }
    if (effectVertShader) {
        SDL_ReleaseGPUShader(Renderer::GetDevice(), effectVertShader);
        effectVertShader = nullptr;
    }
}

void SpriteRenderPass::applyEffects(SDL_GPUCommandBuffer* cmd_buffer, const std::vector<EffectAsset>& effects,
                                   SDL_GPUTexture* sourceTexture, SDL_GPUTexture* targetTexture, const glm::mat4& camera,
                                   SDL_GPUTextureFormat targetFormat, bool isFirstBatch,
                                   const std::unordered_map<uint32_t, std::pair<GpuTextureHandle, ScaleMode>>& effectTextures) {


    if (effects.empty()) {
        return;
    }
    
    // Create fullscreen quad vertex data (position + texcoord)
    struct Vertex {
        float x, y;    // Position (0-1 range, shader converts to NDC)
        float u, v;    // Texcoord
    };
    
    // Calculate UV scale - temp textures are desktop-sized but only the physical pixel portion is rendered
    float uvScaleX = (float)Window::GetPhysicalWidth() / (float)m_surface_width;
    float uvScaleY = (float)Window::GetPhysicalHeight() / (float)m_surface_height;
    
    // Note: V coordinate flipped (0 at bottom, uvScaleY at top) to account for texture orientation
    Vertex quadVertices[] = {
        {0.0f, 0.0f, 0.0f, uvScaleY},          // Top-left (V flipped)
        {1.0f, 0.0f, uvScaleX, uvScaleY},      // Top-right (V flipped)
        {0.0f, 1.0f, 0.0f, 0.0f},              // Bottom-left (V flipped)
        {1.0f, 1.0f, uvScaleX, 0.0f},          // Bottom-right (V flipped)
    };
    
    uint16_t quadIndices[] = {0, 1, 2, 2, 1, 3};
    
    // Create temporary buffers for the quad
    SDL_GPUTransferBufferCreateInfo transferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(quadVertices),
        .props = 0
    };
    SDL_GPUTransferBuffer* vertexTransfer = SDL_CreateGPUTransferBuffer(Renderer::GetDevice(), &transferInfo);
    
    transferInfo.size = sizeof(quadIndices);
    SDL_GPUTransferBuffer* indexTransfer = SDL_CreateGPUTransferBuffer(Renderer::GetDevice(), &transferInfo);
    
    SDL_GPUBufferCreateInfo bufferInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(quadVertices),
        .props = 0
    };
    SDL_GPUBuffer* vertexBuffer = SDL_CreateGPUBuffer(Renderer::GetDevice(), &bufferInfo);
    
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    bufferInfo.size = sizeof(quadIndices);
    SDL_GPUBuffer* indexBuffer = SDL_CreateGPUBuffer(Renderer::GetDevice(), &bufferInfo);
    
    // Upload vertex data
    void* vertexData = SDL_MapGPUTransferBuffer(Renderer::GetDevice(), vertexTransfer, false);
    memcpy(vertexData, quadVertices, sizeof(quadVertices));
    SDL_UnmapGPUTransferBuffer(Renderer::GetDevice(), vertexTransfer);
    
    void* indexData = SDL_MapGPUTransferBuffer(Renderer::GetDevice(), indexTransfer, false);
    memcpy(indexData, quadIndices, sizeof(quadIndices));
    SDL_UnmapGPUTransferBuffer(Renderer::GetDevice(), indexTransfer);
    
    // Copy to GPU buffers
    SDL_GPUTransferBufferLocation vertexLocation = {
        .transfer_buffer = vertexTransfer,
        .offset = 0
    };
    SDL_GPUBufferRegion vertexRegion = {
        .buffer = vertexBuffer,
        .offset = 0,
        .size = sizeof(quadVertices)
    };
    
    SDL_GPUTransferBufferLocation indexLocation = {
        .transfer_buffer = indexTransfer,
        .offset = 0
    };
    SDL_GPUBufferRegion indexRegion = {
        .buffer = indexBuffer,
        .offset = 0,
        .size = sizeof(quadIndices)
    };
    
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd_buffer);
    SDL_UploadToGPUBuffer(copyPass, &vertexLocation, &vertexRegion, false);
    SDL_UploadToGPUBuffer(copyPass, &indexLocation, &indexRegion, false);
    SDL_EndGPUCopyPass(copyPass);
    
    // Ping-pong between temp textures for multi-effect chains
    SDL_GPUTexture* readTex = sourceTexture;
    SDL_GPUTexture* writeTex = (effects.size() == 1) ? targetTexture : reinterpret_cast<SDL_GPUTexture*>(effectTempB.gpuTexture);
    
    for (size_t i = 0; i < effects.size(); ++i) {
        const auto& effect = effects[i];
        bool isLastEffect = (i == effects.size() - 1);
        
        // On last effect, write to final target instead of temp
        if (isLastEffect) {
            writeTex = targetTexture;
        }
        
        // Use the effect's shaders
        SDL_GPUShader* vertShader = reinterpret_cast<SDL_GPUShader*>(effect.vertShader.gpuShader);
        SDL_GPUShader* fragShader = reinterpret_cast<SDL_GPUShader*>(effect.fragShader.gpuShader);
        
        if (!vertShader || !fragShader) {
            LOG_ERROR("Effect shaders are NULL: vert={}, frag={}", (void*)vertShader, (void*)fragShader);
            continue;
        }
        
        // Begin render pass to write texture
        SDL_GPUColorTargetInfo colorTarget = {
            .texture = writeTex,
            .mip_level = 0,
            .layer_or_depth_plane = 0,
            .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
            .load_op = isLastEffect ? (isFirstBatch ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD) : SDL_GPU_LOADOP_CLEAR,  // Clear intermediate, preserve/clear final
            .store_op = SDL_GPU_STOREOP_STORE,
            .resolve_texture = nullptr,
            .resolve_mip_level = 0,
            .resolve_layer = 0,
            .cycle = false  // Don't cycle - we're explicitly ping-ponging between A/B
        };
        
        SDL_GPURenderPass* effectPass = SDL_BeginGPURenderPass(cmd_buffer, &colorTarget, 1, nullptr);
        
        // CRITICAL: Set viewport for the effect pass (physical pixels)
        SDL_GPUViewport viewport = {
            .x = 0,
            .y = 0,
            .w = (float)Window::GetPhysicalWidth(),
            .h = (float)Window::GetPhysicalHeight(),
            .min_depth = 0.0f,
            .max_depth = 1.0f
        };
        SDL_SetGPUViewport(effectPass, &viewport);
        
        // Create pipeline for this effect's shaders
        // TODO: Cache pipelines per effect to avoid recreation every frame
        
        // Use alpha blending when compositing to final target.
        // Custom render targets (m_noMSAA=true) use direct write (ONE/ZERO) so that
        // effect shaders which store non-opacity data in the alpha channel (e.g. the
        // HRC extend/merge passes store transmittance) are not silently premultiplied
        // by their own alpha before being written to the buffer.
        SDL_GPUColorTargetBlendState blendState;
        if (isLastEffect) {
            if (m_noMSAA) {
                // Custom render target — write the fragment output verbatim.
                blendState = {
                    .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                    .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                    .color_blend_op = SDL_GPU_BLENDOP_ADD,
                    .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                    .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                    .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                    .color_write_mask = 0xF,
                    .enable_blend = false,
                    .enable_color_write_mask = false,
                };
            } else {
                // Primary framebuffer — blend effect output over existing scene content.
                blendState = {
                    .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                    .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                    .color_blend_op = SDL_GPU_BLENDOP_ADD,
                    .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                    .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                    .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                    .color_write_mask = 0xF,
                    .enable_blend = true,
                    .enable_color_write_mask = false,
                };
            }
        } else {
            // Intermediate pass - no blending, direct write (ONE/ZERO)
            blendState = {
                .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                .color_blend_op = SDL_GPU_BLENDOP_ADD,
                .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
                .color_write_mask = 0xF,
                .enable_blend = true,
                .enable_color_write_mask = false,
            };
        }
        
        SDL_GPUColorTargetDescription colorTargetDesc = {
            .format = isLastEffect ? targetFormat : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,  // Use target format for final composite
            .blend_state = blendState,
        };
        
        SDL_GPUVertexAttribute vertexAttribs[] = {
            {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0},
            {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 8}
        };
        
        SDL_GPUVertexBufferDescription vertexBufferDesc = {
            .slot = 0,
            .pitch = 16,  // sizeof(Vertex2D)
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0
        };
        
        // Final composite pass targets fbContentMSAA when MSAA is on — pipeline must match.
        // Intermediate passes always target 1x temp textures.
        SDL_GPUSampleCount pipelineSampleCount = (isLastEffect && !m_noMSAA)
            ? toSDL(Renderer::GetSampleCount())
            : SDL_GPU_SAMPLECOUNT_1;

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
            .vertex_shader = vertShader,
            .fragment_shader = fragShader,
            .vertex_input_state = {
                .vertex_buffer_descriptions = &vertexBufferDesc,
                .num_vertex_buffers = 1,
                .vertex_attributes = vertexAttribs,
                .num_vertex_attributes = 2,
            },
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = SDL_DefaultRasterizerState,
            .multisample_state = {.sample_count = pipelineSampleCount, .sample_mask = 0, .enable_mask = false},
            .depth_stencil_state = {.enable_depth_test = false, .enable_depth_write = false, .enable_stencil_test = false},
            .target_info = {
                .color_target_descriptions = &colorTargetDesc,
                .num_color_targets = 1,
                .has_depth_stencil_target = false,
            },
            .props = 0,
        };
        
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(Renderer::GetDevice(), &pipelineInfo);
        if (!pipeline) {
            LOG_ERROR("Failed to create effect pipeline: {}", SDL_GetError());
            SDL_EndGPURenderPass(effectPass);
            continue;
        }

        SDL_BindGPUGraphicsPipeline(effectPass, pipeline);
        
        // Bind textures - source texture plus any additional effect textures
        std::vector<SDL_GPUTextureSamplerBinding> textureBindings;
        
        // Always bind source texture at binding 0
        textureBindings.push_back(SDL_GPUTextureSamplerBinding{
            .texture = readTex,
            .sampler = reinterpret_cast<SDL_GPUSampler*>(Renderer::GetSampler(ScaleMode::Nearest))
        });
        
        // Bind additional textures at their specified bindings
        // Note: bindings must be sequential starting from 0
        for (const auto& [binding, texture] : effectTextures) {
            // Resize vector if needed to accommodate the binding index
            while (textureBindings.size() <= binding) {
                // Fill gaps with the first texture as a placeholder
                textureBindings.push_back(textureBindings[0]);
            }
            
            // Set the texture at the specified binding using its per-binding sampler mode.
            textureBindings[binding] = SDL_GPUTextureSamplerBinding{
                .texture = reinterpret_cast<SDL_GPUTexture*>(texture.first),
                .sampler = reinterpret_cast<SDL_GPUSampler*>(Renderer::GetSampler(texture.second))
            };
        }
        
        SDL_BindGPUFragmentSamplers(effectPass, 0, textureBindings.data(), (uint32_t)textureBindings.size());
        
        // Always bind effect's uniform buffer - shader expects it even if empty
        // Push dummy data if no uniforms exist
        if (effect.uniforms && effect.uniforms->getBufferSize() > 0) {
            SDL_PushGPUFragmentUniformData(cmd_buffer, 0,
                effect.uniforms->getBufferPointer(),
                effect.uniforms->getBufferSize());
        } else {
            // Push empty/dummy uniform data to satisfy shader requirements
            float dummyData[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            SDL_PushGPUFragmentUniformData(cmd_buffer, 0, &dummyData, sizeof(dummyData));
        }
        
        // Bind quad geometry
        SDL_GPUBufferBinding vertexBinding = {.buffer = vertexBuffer, .offset = 0};
        SDL_BindGPUVertexBuffers(effectPass, 0, &vertexBinding, 1);
        
        SDL_GPUBufferBinding indexBinding = {.buffer = indexBuffer, .offset = 0};
        SDL_BindGPUIndexBuffer(effectPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

        // Draw fullscreen quad
        SDL_DrawGPUIndexedPrimitives(effectPass, 6, 1, 0, 0, 0);
        
        SDL_EndGPURenderPass(effectPass);
        
        // Clean up pipeline (TODO: cache these)
        SDL_ReleaseGPUGraphicsPipeline(Renderer::GetDevice(), pipeline);

        // Ping-pong: read from where we just wrote, write to the other temp
        if (!isLastEffect) {
            readTex = writeTex;
            writeTex = (readTex == reinterpret_cast<SDL_GPUTexture*>(effectTempA.gpuTexture)) ? reinterpret_cast<SDL_GPUTexture*>(effectTempB.gpuTexture) : reinterpret_cast<SDL_GPUTexture*>(effectTempA.gpuTexture);
        }
    }
    
    // Clean up temporary buffers
    SDL_ReleaseGPUBuffer(Renderer::GetDevice(), vertexBuffer);
    SDL_ReleaseGPUBuffer(Renderer::GetDevice(), indexBuffer);
    SDL_ReleaseGPUTransferBuffer(Renderer::GetDevice(), vertexTransfer);
    SDL_ReleaseGPUTransferBuffer(Renderer::GetDevice(), indexTransfer);

}

void SpriteRenderPass::createShaders() {
    // Select shader format based on build configuration
    SDL_GPUShaderFormat shaderFormat;
    #if defined(LUMINOVEAU_SHADER_BACKEND_DXIL)
        shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
    #elif defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        shaderFormat = SDL_GPU_SHADERFORMAT_METALLIB;
    #else
        shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;  // Default: Vulkan
    #endif

    // spirv-cross renames "main" to "main0" in MSL (reserved keyword)
    #if defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
        const char* entryPoint = "main0";
    #else
        const char* entryPoint = "main";
    #endif

    SDL_GPUShaderCreateInfo vertexShaderInfo = {
        .code_size = Luminoveau::Shaders::Sprite_Vert_Size,
        .code = Luminoveau::Shaders::Sprite_Vert,
        .entrypoint = entryPoint,
        .format = shaderFormat,  // Use selected format
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = 1,
        .num_uniform_buffers = 2,  // Now we have 2: ViewProjection and InstanceOffset
    };

    vertex_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &vertexShaderInfo);

    if (!vertex_shader) {
        LOG_CRITICAL("failed to create vertex shader for: {} ({})", passname.c_str(), SDL_GetError());
    }

    SDL_GPUShaderCreateInfo fragmentShaderInfo = {
        .code_size = Luminoveau::Shaders::Sprite_Frag_Size,
        .code = Luminoveau::Shaders::Sprite_Frag,
        .entrypoint = entryPoint,
        .format = shaderFormat,  // Use selected format
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 0,
    };

    fragment_shader = SDL_CreateGPUShader(Renderer::GetDevice(), &fragmentShaderInfo);

    if (!fragment_shader) {
        LOG_CRITICAL("failed to create fragment shader for: {} ({})", passname.c_str(), SDL_GetError());
    }
}
#endif // LUMINOVEAU_WEBGPU_BACKEND
