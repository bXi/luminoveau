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
