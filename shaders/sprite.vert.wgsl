struct SpriteData {
    pos_xy   : u32,
    pos_z_rot: u32,
    tex_uv   : u32,
    tex_wh   : u32,
    color_rg : u32,
    color_ba : u32,
    size_wh  : u32,
    pivot_xy : u32,
}

struct UniformBlock {
    viewProjection : mat4x4<f32>,
}

struct InstanceOffset {
    baseInstance : u32,
    _pad         : vec3<u32>,
}

@group(0) @binding(0) var<uniform>       uniforms        : UniformBlock;
@group(0) @binding(1) var<uniform>       instOffset      : InstanceOffset;
@group(3) @binding(0) var<storage, read> spriteInstances : array<SpriteData>;

struct VertOut {
    @builtin(position)               position  : vec4<f32>,
    @location(0)                     texCoord  : vec2<f32>,
    @location(1)                     color     : vec4<f32>,
    @location(2) @interpolate(flat)  isSDF     : u32,
}

fn unpackHalfLo(packed: u32) -> f32 { return unpack2x16float(packed).x; }
fn unpackHalfHi(packed: u32) -> f32 { return unpack2x16float(packed).y; }
fn unpackHalf2(packed: u32) -> vec2<f32> { return unpack2x16float(packed); }

@vertex
fn vs_main(
    @location(0) vertPosXY : u32,
    @location(1) vertUV    : u32,
    @builtin(instance_index) instanceIndex : u32,
) -> VertOut {
    let sprite = spriteInstances[instanceIndex + instOffset.baseInstance];

    let x        = unpackHalfLo(sprite.pos_xy);
    let y        = unpackHalfHi(sprite.pos_xy);
    let rotation = unpackHalfHi(sprite.pos_z_rot);
    let texUV    = unpackHalf2(sprite.tex_uv);
    let texWH    = unpackHalf2(sprite.tex_wh);
    let color    = vec4<f32>(
        unpackHalfLo(sprite.color_rg),
        unpackHalfHi(sprite.color_rg),
        unpackHalfLo(sprite.color_ba),
        unpackHalfHi(sprite.color_ba),
    );
    let scale = unpackHalf2(sprite.size_wh);

    let pivotPacked  = sprite.pivot_xy;
    let isSDF        = (pivotPacked >> 31u) & 1u;
    let pivotCleared = pivotPacked & 0x7FFFFFFFu;
    let pivot        = unpackHalf2(pivotCleared);

    let vertexPos = unpackHalf2(vertPosXY);
    let vertexUV  = unpackHalf2(vertUV);

    var coord = vertexPos;

    let texcoord = vec2<f32>(
        texUV.x + vertexUV.x * texWH.x,
        texUV.y + vertexUV.y * texWH.y,
    );

    if rotation != 0.0 {
        coord -= pivot;
    }

    coord *= scale;

    if rotation != 0.0 {
        let c = cos(rotation);
        let s = sin(rotation);
        coord = vec2<f32>(c * coord.x - s * coord.y,
                          s * coord.x + c * coord.y);
        coord += pivot * scale;
    }

    let worldPos = vec3<f32>(coord + vec2<f32>(x, y), 0.0);

    var out : VertOut;
    out.position = uniforms.viewProjection * vec4<f32>(worldPos, 1.0);
    out.texCoord = texcoord;
    out.color    = color;
    out.isSDF    = isSDF;
    return out;
}
