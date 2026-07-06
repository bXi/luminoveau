// Shadow depth vertex shader (WGSL). Mirror of shadow.vert.hlsl.
// Renders scene geometry from the light's viewpoint, transforming by lightViewProj and outputting
// clip-space depth (z/w) that the fragment shader writes to an R32F shadow texture.

struct SceneUniforms {
    viewProj       : mat4x4<f32>,
    models         : array<mat4x4<f32>, 16>,
    cameraPos      : vec4<f32>,
    ambientLight   : vec4<f32>,
    lightPositions : array<vec4<f32>, 4>,
    lightColors    : array<vec4<f32>, 4>,
    lightParams    : array<vec4<f32>, 4>,
    lightCount     : i32,
    modelCount     : i32,
    _pad           : vec2<i32>,
}
@group(3) @binding(0) var<storage, read> sceneData : array<SceneUniforms>;

struct ShadowParams {
    lightViewProj : mat4x4<f32>,
    baseInstance  : u32,
    // Three separate u32 (not vec3<u32>): vec3 has 16-byte alignment, which would pad the struct
    // to 96 bytes and mismatch the 80-byte C++ ShadowParams the CPU uploads.
    _pad0         : u32,
    _pad1         : u32,
    _pad2         : u32,
}
@group(0) @binding(0) var<uniform> params : ShadowParams;

struct VertIn {
    @location(0) position : vec3<f32>,
    @location(1) normal   : vec3<f32>,
    @location(2) texCoord : vec2<f32>,
    @location(3) color    : vec4<f32>,
}

struct VertOut {
    @builtin(position) position : vec4<f32>,
    @location(0)       depth    : f32,     // clip-space z/w, matched in model3d.frag
}

@vertex
fn vs_main(in : VertIn, @builtin(instance_index) instanceIndex : u32) -> VertOut {
    // Index the storage reference directly; copy-then-dynamic-index miscompiles under naga (Firefox).
    // See model3d.vert.wgsl for the full explanation.
    let model = sceneData[0].models[instanceIndex + params.baseInstance];

    let world = model * vec4<f32>(in.position, 1.0);
    let clip  = params.lightViewProj * world;

    var out : VertOut;
    out.position = clip;
    // clip.z is already [0,1] (zero-to-one light ortho), matching the depth buffer + the frag compare.
    out.depth = clip.z / clip.w;
    return out;
}
