@group(2) @binding(0) var gSampler : sampler;
@group(2) @binding(1) var gPovTex  : texture_2d<f32>;

struct FragIn {
    @location(0)                     vUV    : vec2<f32>,
    @location(1) @interpolate(flat)  vScale : f32,
}

@fragment
fn fs_main(in : FragIn) -> @location(0) vec4<f32> {
    return textureSample(gPovTex, gSampler, in.vUV) * in.vScale;
}
