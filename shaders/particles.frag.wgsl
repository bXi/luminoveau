@group(2) @binding(0) var gSampler : sampler;
@group(2) @binding(1) var gTexture : texture_2d<f32>;

const SHAPE_SOFT_CIRCLE : u32 = 0u;
const SHAPE_HARD_CIRCLE : u32 = 1u;
const SHAPE_SQUARE      : u32 = 2u;
const SHAPE_SOFT_SQUARE : u32 = 3u;
const SHAPE_RING        : u32 = 4u;
const SHAPE_TEXTURED    : u32 = 5u;

struct FragIn {
    @location(0)                     vColor     : vec4<f32>,
    @location(1)                     vUV        : vec2<f32>,
    @location(2) @interpolate(flat)  vShapeType : u32,
}

@fragment
fn fs_main(in : FragIn) -> @location(0) vec4<f32> {
    let uv = in.vUV * 2.0 - 1.0;

    // textureSample must be in uniform control flow — sample unconditionally.
    let texColor = textureSample(gTexture, gSampler, vec2<f32>(in.vUV.x, 1.0 - in.vUV.y));

    if in.vShapeType == SHAPE_TEXTURED {
        if texColor.a < 0.01 { discard; }
        return in.vColor * texColor;
    }

    var alpha : f32 = 1.0;

    if in.vShapeType == SHAPE_SOFT_CIRCLE {
        let dist = dot(uv, uv);
        if dist > 1.0 { discard; }
        alpha = 1.0 - smoothstep(0.0, 1.0, dist);
    } else if in.vShapeType == SHAPE_HARD_CIRCLE {
        if dot(uv, uv) > 1.0 { discard; }
        alpha = 1.0;
    } else if in.vShapeType == SHAPE_SQUARE {
        alpha = 1.0;
    } else if in.vShapeType == SHAPE_SOFT_SQUARE {
        let edge    = abs(uv);
        let maxEdge = max(edge.x, edge.y);
        alpha = 1.0 - smoothstep(0.7, 1.0, maxEdge);
        if alpha < 0.01 { discard; }
    } else if in.vShapeType == SHAPE_RING {
        let dist = length(uv);
        alpha = smoothstep(0.55, 0.65, dist) * (1.0 - smoothstep(0.85, 1.0, dist));
        if alpha < 0.01 { discard; }
    }

    return in.vColor * vec4<f32>(1.0, 1.0, 1.0, alpha);
}
