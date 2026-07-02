// Shadow depth fragment shader (WGSL). Mirror of shadow.frag.hlsl.
// Writes the interpolated clip-space depth into an R32F shadow texture; the bound depth buffer
// resolves the nearest occluder so the R channel holds the closest surface's depth from the light.

struct FragIn {
    @location(0) depth : f32,
}

@fragment
fn fs_main(in : FragIn) -> @location(0) vec4<f32> {
    return vec4<f32>(in.depth, 0.0, 0.0, 1.0);
}
