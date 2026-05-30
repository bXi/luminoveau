@group(2) @binding(0) var gSampler : sampler;
@group(2) @binding(1) var gTexture : texture_2d<f32>;

struct FragIn {
    @location(0) worldPos : vec3<f32>,
    @location(1) normal   : vec3<f32>,
    @location(2) texCoord : vec2<f32>,
    @location(3) color    : vec4<f32>,
}

@fragment
fn fs_main(in : FragIn) -> @location(0) vec4<f32> {
    let texColor = textureSample(gTexture, gSampler, in.texCoord);
    return texColor * in.color;
}
