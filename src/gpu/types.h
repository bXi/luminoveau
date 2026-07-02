#pragma once

#include <cstdint>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────
// Opaque GPU resource handles.
// Each backend casts its native pointer/id to/from uintptr_t.
// Zero is always invalid (null) for every handle type.
// ─────────────────────────────────────────────────────────────────────────────

using GpuTextureHandle          = uintptr_t;
using GpuBufferHandle           = uintptr_t;
using GpuTransferBufferHandle   = uintptr_t;
using GpuSamplerHandle          = uintptr_t;
using GpuShaderHandle           = uintptr_t;
using GpuGraphicsPipelineHandle = uintptr_t;
using GpuComputePipelineHandle  = uintptr_t;
using GpuCmdBufferHandle        = uintptr_t;
using GpuRenderPassHandle       = uintptr_t;
using GpuComputePassHandle      = uintptr_t;
using GpuFenceHandle            = uintptr_t;

// ─────────────────────────────────────────────────────────────────────────────
// Texture formats
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuTextureFormat : uint32_t {
    Invalid,
    R8_Unorm,
    R8G8_Unorm,
    R8G8B8A8_Unorm,
    B8G8R8A8_Unorm,
    R8G8B8A8_Unorm_SRGB,
    B8G8R8A8_Unorm_SRGB,
    R16_Float,
    R16G16_Float,
    R16G16B16A16_Float,
    R32_Float,
    R32G32_Float,
    R32G32B32A32_Float,
    R10G10B10A2_Unorm,
    B5G6R5_Unorm,
    D16_Unorm,
    D24_Unorm,
    D32_Float,
    D24_Unorm_S8_Uint,
    D32_Float_S8_Uint,
    BC1_Unorm,
    BC2_Unorm,
    BC3_Unorm,
    BC4_Unorm,
    BC5_Unorm,
    BC7_Unorm,
    ASTC_4x4_Unorm,   // native on Apple/mobile GPUs; same 16 B / 4x4 block size as BC7
};

// ─────────────────────────────────────────────────────────────────────────────
// MSAA sample counts
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuSampleCount : uint32_t {
    x1 = 1,
    x2 = 2,
    x4 = 4,
    x8 = 8,
};

// ─────────────────────────────────────────────────────────────────────────────
// Render pass load/store operations
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuLoadOp : uint8_t {
    Load,
    Clear,
    DontCare,
};

enum class GpuStoreOp : uint8_t {
    Store,
    DontCare,
    Resolve,
    ResolveAndStore,
};

// ─────────────────────────────────────────────────────────────────────────────
// Shader stage
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuShaderStage : uint8_t {
    Vertex,
    Fragment,
    Compute,
};

// ─────────────────────────────────────────────────────────────────────────────
// Buffer usage flags
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuBufferUsage : uint32_t {
    Vertex        = 1 << 0,
    Index         = 1 << 1,
    StorageRead   = 1 << 2,
    StorageWrite  = 1 << 3,
    Indirect      = 1 << 4,
};

inline GpuBufferUsage operator|(GpuBufferUsage a, GpuBufferUsage b) {
    return static_cast<GpuBufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool operator&(GpuBufferUsage a, GpuBufferUsage b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture usage flags
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuTextureUsage : uint32_t {
    Sampler             = 1 << 0,
    ColorTarget         = 1 << 1,
    DepthStencilTarget  = 1 << 2,
    StorageRead         = 1 << 3,
    StorageWrite        = 1 << 4,
    Transfer            = 1 << 5,
};

inline GpuTextureUsage operator|(GpuTextureUsage a, GpuTextureUsage b) {
    return static_cast<GpuTextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool operator&(GpuTextureUsage a, GpuTextureUsage b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Transfer buffer direction
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuTransferUsage : uint8_t {
    Upload,
    Download,
};

// ─────────────────────────────────────────────────────────────────────────────
// Texture filter / address modes (for sampler creation)
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuFilter : uint8_t {
    Nearest,
    Linear,
};

enum class GpuSamplerAddressMode : uint8_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
};

// ─────────────────────────────────────────────────────────────────────────────
// Scale / blend modes (used by renderer and draw systems)
// ─────────────────────────────────────────────────────────────────────────────

enum class ScaleMode : uint8_t {
    Nearest,
    Linear,
};

enum class BlendMode : uint8_t {
    Default,    // standard alpha blending
    SrcAlpha,   // source-alpha blending
    Additive,   // additive blending
    None,       // no blending
};

// ─────────────────────────────────────────────────────────────────────────────
// Vertex element format
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuVertexElementFormat : uint8_t {
    Float,
    Float2,
    Float3,
    Float4,
    Byte2,
    Byte4,
    UByte2,
    UByte4,
    Byte2Norm,
    Byte4Norm,
    UByte2Norm,
    UByte4Norm,
    Short2,
    Short4,
    Short2Norm,
    Short4Norm,
    Half2,
    Half4,
    UInt,
    UInt2,
    UInt4,
    Int,
    Int2,
    Int4,
};

// ─────────────────────────────────────────────────────────────────────────────
// Fill / cull modes
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuFillMode : uint8_t { Fill, Line };
enum class GpuCullMode : uint8_t { None, Front, Back };
enum class GpuFrontFace : uint8_t { CounterClockwise, Clockwise };

// ─────────────────────────────────────────────────────────────────────────────
// Blend factors / operations
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuBlendFactor : uint8_t {
    Zero,
    One,
    SrcColor,    OneMinusSrcColor,
    DstColor,    OneMinusDstColor,
    SrcAlpha,    OneMinusSrcAlpha,
    DstAlpha,    OneMinusDstAlpha,
    ConstantColor,    OneMinusConstantColor,
    SrcAlphaSaturate,
    Src1Color,   OneMinusSrc1Color,
    Src1Alpha,   OneMinusSrc1Alpha,
};

enum class GpuBlendOp : uint8_t {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

// ─────────────────────────────────────────────────────────────────────────────
// Resource creation structs
// ─────────────────────────────────────────────────────────────────────────────

enum class GpuTextureType : uint8_t {
    Tex2D,       // standard 2D texture
    Tex2DArray,  // array of 2D layers (depthOrLayers = layer count)
    TexCube,     // cube map (depthOrLayers must be 6)
};

struct GpuTextureCreateInfo {
    uint32_t         width        = 1;
    uint32_t         height       = 1;
    uint32_t         depthOrLayers = 1;
    uint32_t         numLevels    = 1;
    GpuTextureFormat format       = GpuTextureFormat::R8G8B8A8_Unorm;
    GpuSampleCount   sampleCount  = GpuSampleCount::x1;
    GpuTextureUsage  usage        = GpuTextureUsage::Sampler;
    GpuTextureType   type         = GpuTextureType::Tex2D;
};

struct GpuBufferCreateInfo {
    uint32_t      size  = 0;
    GpuBufferUsage usage = GpuBufferUsage::Vertex;
};

struct GpuTransferBufferCreateInfo {
    uint32_t         size  = 0;
    GpuTransferUsage usage = GpuTransferUsage::Upload;
};

struct GpuSamplerCreateInfo {
    GpuFilter            minFilter   = GpuFilter::Nearest;
    GpuFilter            magFilter   = GpuFilter::Nearest;
    GpuFilter            mipFilter   = GpuFilter::Nearest;
    GpuSamplerAddressMode addressU   = GpuSamplerAddressMode::ClampToEdge;
    GpuSamplerAddressMode addressV   = GpuSamplerAddressMode::ClampToEdge;
    GpuSamplerAddressMode addressW   = GpuSamplerAddressMode::ClampToEdge;
    float                mipLodBias  = 0.0f;
    float                maxAniso    = 1.0f;
    float                minLod      = 0.0f;
    float                maxLod      = 1000.0f;
};

struct GpuShaderCreateInfo {
    const uint8_t*  code                 = nullptr;
    size_t          codeSize             = 0;
    const char*     entrypoint           = "main";
    GpuShaderStage  stage                = GpuShaderStage::Vertex;
    uint32_t        samplerCount         = 0;
    uint32_t        uniformBufferCount   = 0;
    uint32_t        storageBufferCount   = 0;
    uint32_t        storageTextureCount  = 0;
    // Bit i set = fragment sampler pair i binds a cube texture, so its WebGPU bind-group layout
    // entry needs a Cube view dimension. Default 0 = all 2D. Ignored by SDL_GPU.
    uint32_t        samplerCubeMask      = 0;
};

struct GpuVertexAttribute {
    uint32_t              location = 0;
    uint32_t              binding  = 0;
    GpuVertexElementFormat format  = GpuVertexElementFormat::Float4;
    uint32_t              offset   = 0;
};

struct GpuVertexBinding {
    uint32_t binding         = 0;
    uint32_t stride          = 0;
    bool     instanceStepping = false;
};

struct GpuColorTargetBlendState {
    bool           blendEnabled    = false;
    GpuBlendFactor srcColorFactor  = GpuBlendFactor::One;
    GpuBlendFactor dstColorFactor  = GpuBlendFactor::Zero;
    GpuBlendOp     colorOp         = GpuBlendOp::Add;
    GpuBlendFactor srcAlphaFactor  = GpuBlendFactor::One;
    GpuBlendFactor dstAlphaFactor  = GpuBlendFactor::Zero;
    GpuBlendOp     alphaOp         = GpuBlendOp::Add;
};

struct GpuGraphicsPipelineCreateInfo {
    GpuShaderHandle              vertexShader   = 0;
    GpuShaderHandle              fragmentShader = 0;

    // vertex input
    const GpuVertexAttribute*    attributes     = nullptr;
    uint32_t                     attributeCount = 0;
    const GpuVertexBinding*      bindings       = nullptr;
    uint32_t                     bindingCount   = 0;

    // rasterizer
    GpuFillMode                  fillMode       = GpuFillMode::Fill;
    GpuCullMode                  cullMode       = GpuCullMode::None;
    GpuFrontFace                 frontFace      = GpuFrontFace::CounterClockwise;

    // render target. Single target: set colorTargetFormat (+ blend). Multiple render targets
    // (MRT): set colorTargetCount > 0 and fill colorTargetFormats[]/colorTargetBlends[]; the
    // single colorTargetFormat/blend above are then ignored.
    static constexpr uint32_t    MAX_COLOR_TARGETS = 4;
    GpuTextureFormat             colorTargetFormat  = GpuTextureFormat::R8G8B8A8_Unorm;
    GpuColorTargetBlendState     blend              = {};
    uint32_t                     colorTargetCount   = 0;   // 0 = use the single colorTargetFormat
    GpuTextureFormat             colorTargetFormats[MAX_COLOR_TARGETS] = {};
    GpuColorTargetBlendState     colorTargetBlends[MAX_COLOR_TARGETS]  = {};
    bool                         hasDepthTarget     = false;
    GpuTextureFormat             depthTargetFormat  = GpuTextureFormat::D32_Float;
    GpuSampleCount               sampleCount        = GpuSampleCount::x1;
    uint32_t                     vertexStorageBufferCount = 0;  // read-only storage buffers bound at group 3, vertex stage
};

struct GpuComputePipelineCreateInfo {
    const uint8_t*  code                           = nullptr;
    size_t          codeSize                       = 0;
    const char*     entrypoint                     = "main";
    uint32_t        threadCountX                   = 1;
    uint32_t        threadCountY                   = 1;
    uint32_t        threadCountZ                   = 1;
    uint32_t        samplerCount                   = 0;
    uint32_t        readonlyStorageTextureCount    = 0;
    uint32_t        readwriteStorageTextureCount   = 0;
    uint32_t        readonlyStorageBufferCount     = 0;
    uint32_t        readwriteStorageBufferCount    = 0;
    uint32_t        uniformBufferCount             = 0;
    // WebGPU only: per-binding storage texture format for BGL. Must match WGSL
    // texture_storage_2d<FORMAT, ...> declaration. nullptr → defaults to RGBA8Unorm.
    const GpuTextureFormat* readonlyStorageTextureFormats  = nullptr;
    const GpuTextureFormat* readwriteStorageTextureFormats = nullptr;
    // WebGPU only: per-binding access mode override for the RW storage texture group.
    // true = WriteOnly (matches GLSL `writeonly image2D`), false = ReadWrite. nullptr → all RW.
    const bool* readwriteStorageTextureWriteOnly = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Render pass begin structs
// ─────────────────────────────────────────────────────────────────────────────

struct GpuColorTargetInfo {
    GpuTextureHandle texture        = 0;
    GpuTextureHandle resolveTexture = 0;
    GpuLoadOp        loadOp         = GpuLoadOp::Load;
    GpuStoreOp       storeOp        = GpuStoreOp::Store;
    float            clearR         = 0.0f;
    float            clearG         = 0.0f;
    float            clearB         = 0.0f;
    float            clearA         = 0.0f;
    uint32_t         mipLevel       = 0;
    uint32_t         layer          = 0;
};

struct GpuDepthStencilTargetInfo {
    GpuTextureHandle texture      = 0;
    GpuLoadOp        loadOp       = GpuLoadOp::Clear;
    GpuStoreOp       storeOp      = GpuStoreOp::DontCare;
    float            clearDepth   = 1.0f;
    uint8_t          clearStencil = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Texture region structs (for upload/copy)
// ─────────────────────────────────────────────────────────────────────────────

struct GpuTextureRegion {
    GpuTextureHandle texture  = 0;
    uint32_t         mipLevel = 0;
    uint32_t         layer    = 0;
    uint32_t         x        = 0;
    uint32_t         y        = 0;
    uint32_t         z        = 0;
    uint32_t         width    = 0;
    uint32_t         height   = 0;
    uint32_t         depth    = 1;
};

struct GpuTransferBufferRegion {
    GpuTransferBufferHandle transferBuffer = 0;
    uint32_t                offset         = 0;
    uint32_t                pixels_per_row = 0;
    uint32_t                rows_per_layer = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Sampler texture binding (for draw calls)
// ─────────────────────────────────────────────────────────────────────────────

struct GpuTextureSamplerBinding {
    GpuTextureHandle texture = 0;
    GpuSamplerHandle sampler = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Buffer binding
// ─────────────────────────────────────────────────────────────────────────────

struct GpuBufferBinding {
    GpuBufferHandle buffer = 0;
    uint32_t        offset = 0;
};
