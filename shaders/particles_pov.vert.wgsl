struct POVUniforms {
    gScale : f32,
    _pad   : vec3<f32>,
}

@group(0) @binding(0) var<uniform> u : POVUniforms;

struct VertOut {
    @builtin(position)               position : vec4<f32>,
    @location(0)                     vUV      : vec2<f32>,
    @location(1) @interpolate(flat)  vScale   : f32,
}

@vertex
fn vs_main(@builtin(vertex_index) vi : u32) -> VertOut {
    var pos : vec2<f32>;
    if      vi == 0u { pos = vec2<f32>(-1.0,  1.0); }
    else if vi == 1u { pos = vec2<f32>( 3.0,  1.0); }
    else             { pos = vec2<f32>(-1.0, -3.0); }

    var out : VertOut;
    out.position = vec4<f32>(pos, 0.0, 1.0);
    out.vUV      = pos * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5, 0.5);
    out.vScale   = u.gScale;
    return out;
}
