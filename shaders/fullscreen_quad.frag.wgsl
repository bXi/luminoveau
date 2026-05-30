@group(2) @binding(0) var gSampler : sampler;
@group(2) @binding(1) var gTexture : texture_2d<f32>;

struct FragIn {
    @location(0) texCoord  : vec2<f32>,
    @location(1) tintColor : vec4<f32>,
}

@fragment
fn fs_main(in : FragIn) -> @location(0) vec4<f32> {
    return textureSample(gTexture, gSampler, in.texCoord) * in.tintColor;
}
