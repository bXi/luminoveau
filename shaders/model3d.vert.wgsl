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

// Per-draw base instance: lets one mesh-group draw index its own contiguous slice of models[].
struct InstanceOffset {
    baseInstance : u32,
    _pad         : vec3<u32>,
}
@group(0) @binding(0) var<uniform> instOffset : InstanceOffset;

struct VertIn {
    @location(0) position : vec3<f32>,
    @location(1) normal   : vec3<f32>,
    @location(2) texCoord : vec2<f32>,
    @location(3) color    : vec4<f32>,
}

struct VertOut {
    @builtin(position) position     : vec4<f32>,
    @location(0)       worldPos     : vec3<f32>,
    @location(1)       normal       : vec3<f32>,
    @location(2)       texCoord     : vec2<f32>,
    @location(3)       color        : vec4<f32>,
}

@vertex
fn vs_main(in : VertIn, @builtin(instance_index) instanceIndex : u32) -> VertOut {
    // Index through the storage reference directly. Copying the whole struct to a value first
    // (`let scene = sceneData[0]`) and then dynamic-indexing scene.models[..] makes naga (wgpu's
    // WGSL translator, used by Firefox) emit a bad access chain -> garbage matrix -> w~=0 ->
    // vertices explode to infinity. Dawn (Chrome) tolerates it; naga does not.
    let model = sceneData[0].models[instanceIndex + instOffset.baseInstance];

    let worldPos = model * vec4<f32>(in.position, 1.0);

    // Normal matrix = inverse-transpose of the model's upper 3x3. Built from column cross products
    // (the cofactor matrix), which equals the inverse-transpose up to a positive scale that
    // normalize() removes — correct for rotation + non-uniform scale, no matrix inverse needed.
    let a = model[0].xyz;
    let b = model[1].xyz;
    let c = model[2].xyz;
    let normalMatrix = mat3x3<f32>(cross(b, c), cross(c, a), cross(a, b));
    let worldNormal  = normalize(normalMatrix * in.normal);

    // Lighting is done per-pixel in the fragment shader; the vertex shader just forwards the
    // interpolated world position, normal, uv and raw vertex colour.
    var out : VertOut;
    out.position = sceneData[0].viewProj * worldPos;
    out.worldPos = worldPos.xyz;
    out.normal   = worldNormal;
    out.texCoord = in.texCoord;
    out.color    = in.color;
    return out;
}
