// Point-light cube shadow — depth vertex shader (WGSL). Mirror of shadowcube.vert.hlsl.
// Renders the scene from a point light for one cube face; passes world position to the fragment
// shader, which stores linear distance from the light (distance shadow map).

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

struct CubeShadowParams {
    faceViewProj : mat4x4<f32>,
    baseInstance : u32,
    // Three separate u32 (not vec3<u32>): vec3's 16-byte alignment would pad to 96 and mismatch
    // the 80-byte C++ CubeShadowParams the CPU uploads.
    _pad0        : u32,
    _pad1        : u32,
    _pad2        : u32,
}
@group(0) @binding(0) var<uniform> params : CubeShadowParams;

struct VertIn {
    @location(0) position : vec3<f32>,
    @location(1) normal   : vec3<f32>,
    @location(2) texCoord : vec2<f32>,
    @location(3) color    : vec4<f32>,
}

struct VertOut {
    @builtin(position) position : vec4<f32>,
    @location(0)       worldPos : vec3<f32>,
}

@vertex
fn vs_main(in : VertIn, @builtin(instance_index) instanceIndex : u32) -> VertOut {
    // Index the storage reference directly; copy-then-dynamic-index miscompiles under naga (Firefox).
    // See model3d.vert.wgsl for the full explanation.
    let model = sceneData[0].models[instanceIndex + params.baseInstance];

    let world = model * vec4<f32>(in.position, 1.0);

    var out : VertOut;
    out.worldPos = world.xyz;
    out.position = params.faceViewProj * world;
    return out;
}
