// Point-light cube shadow — depth fragment shader (WGSL). Mirror of shadowcube.frag.hlsl.
// Stores linear distance from the light to this fragment, normalized by the light's far range,
// into R32F. Sampling the cube by direction and comparing distances gives omnidirectional shadows.

struct CubeShadowFragParams {
    lightPosFar : vec4<f32>,   // xyz = light world position, w = far range
}
@group(1) @binding(0) var<uniform> params : CubeShadowFragParams;

struct FragIn {
    @location(0) worldPos : vec3<f32>,
}

@fragment
fn fs_main(in : FragIn) -> @location(0) vec4<f32> {
    let dist = length(in.worldPos - params.lightPosFar.xyz) / params.lightPosFar.w;
    return vec4<f32>(dist, 0.0, 0.0, 1.0);
}
