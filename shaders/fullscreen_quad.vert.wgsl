struct Uniforms {
    camera  : mat4x4<f32>,
    model   : mat4x4<f32>,
    flipped : vec2<f32>,
    uv0     : vec2<f32>,
    uv1     : vec2<f32>,
    uv2     : vec2<f32>,
    uv3     : vec2<f32>,
    uv4     : vec2<f32>,
    uv5     : vec2<f32>,
    tintR   : f32,
    tintG   : f32,
    tintB   : f32,
    tintA   : f32,
    _pad    : vec2<f32>,  // pad to 208 bytes (multiple of 16)
}

@group(0) @binding(0) var<uniform> u : Uniforms;

struct VertOut {
    @builtin(position) position : vec4<f32>,
    @location(0) texCoord       : vec2<f32>,
    @location(1) tintColor      : vec4<f32>,
}

var<private> quadPositions : array<vec2<f32>, 6> = array<vec2<f32>, 6>(
    vec2<f32>(1.0, 1.0),
    vec2<f32>(0.0, 1.0),
    vec2<f32>(1.0, 0.0),
    vec2<f32>(0.0, 1.0),
    vec2<f32>(0.0, 0.0),
    vec2<f32>(1.0, 0.0),
);

@vertex
fn vs_main(@builtin(vertex_index) vi : u32) -> VertOut {
    var out : VertOut;

    let pos = quadPositions[vi];
    out.position = u.camera * u.model * vec4<f32>(pos, 0.0, 1.0);

    var uv : vec2<f32>;
    switch vi {
        case 0u: { uv = u.uv0; }
        case 1u: { uv = u.uv1; }
        case 2u: { uv = u.uv2; }
        case 3u: { uv = u.uv3; }
        case 4u: { uv = u.uv4; }
        case 5u: { uv = u.uv5; }
        default: { uv = vec2<f32>(0.0, 0.0); }
    }

    out.texCoord  = u.flipped * uv;
    out.tintColor = vec4<f32>(u.tintR, u.tintG, u.tintB, u.tintA);
    return out;
}
