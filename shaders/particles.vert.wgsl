struct GPUParticle {
    posAndLife      : vec4<f32>,
    velAndMaxLife   : vec4<f32>,
    systemID        : u32,
    respawnTimer    : f32,
    startSize       : f32,
    endSize         : f32,
    angle           : f32,
    angularVelocity : f32,
    _pad0           : f32,
    _pad1           : f32,
}

struct GPUParticleSystem {
    spawnPos       : vec4<f32>,
    spawnVel       : vec4<f32>,
    gravityAndDrag : vec4<f32>,
    colors         : array<vec4<f32>, 4>,
    colorPositions : vec4<f32>,
    sizeStartMin   : f32,
    sizeStartMax   : f32,
    sizeEndMin     : f32,
    sizeEndMax     : f32,
    sizeStartBias  : f32,
    sizeEndBias    : f32,
    lifetimeMin    : f32,
    lifetimeMax    : f32,
    lifetimeBias   : f32,
    emitRate       : f32,
    flags          : u32,
    shapeType      : u32,
    angVelMin      : f32,
    angVelMax      : f32,
    angVelBias     : f32,
    trailStretch   : f32,
}

struct VertUniforms {
    camera     : mat4x4<f32>,
    screenSize : vec2<f32>,
    _pad       : vec2<f32>,
}

@group(3) @binding(0) var<storage, read> particles : array<GPUParticle>;
@group(3) @binding(1) var<storage, read> systems   : array<GPUParticleSystem>;
@group(0) @binding(0) var<uniform>       vertUni   : VertUniforms;

struct VertOut {
    @builtin(position)               position   : vec4<f32>,
    @location(0)                     vColor     : vec4<f32>,
    @location(1)                     vUV        : vec2<f32>,
    @location(2) @interpolate(flat)  vShapeType : u32,
}

var<private> QUAD : array<vec2<f32>, 6> = array<vec2<f32>, 6>(
    vec2<f32>(-0.5, -0.5),
    vec2<f32>( 0.5, -0.5),
    vec2<f32>( 0.5,  0.5),
    vec2<f32>(-0.5, -0.5),
    vec2<f32>( 0.5,  0.5),
    vec2<f32>(-0.5,  0.5),
);

fn sampleGradient(colors: array<vec4<f32>, 4>, positions: vec4<f32>, t: f32) -> vec4<f32> {
    var prevPos   = positions[0];
    var lastColor = colors[0];

    for (var i : i32 = 1; i < 4; i++) {
        let stopPos = positions[i];
        if stopPos < 0.0 { break; }

        if t <= stopPos {
            let range  = max(stopPos - prevPos, 1e-5);
            let localT = clamp((t - prevPos) / range, 0.0, 1.0);
            return mix(colors[i - 1], colors[i], localT);
        }

        prevPos   = stopPos;
        lastColor = colors[i];
    }

    return lastColor;
}

@vertex
fn vs_main(
    @builtin(vertex_index)   vertID       : u32,
    @builtin(instance_index) instanceIndex : u32,
) -> VertOut {
    let cornerIdx = vertID % 6u;
    let p         = particles[instanceIndex];
    let life      = p.posAndLife.w;
    let maxLife   = p.velAndMaxLife.w;

    var out : VertOut;

    if life <= 0.0 {
        out.position   = vec4<f32>(2.0, 2.0, 0.0, 1.0);
        out.vColor     = vec4<f32>(0.0);
        out.vUV        = vec2<f32>(0.0);
        out.vShapeType = 0u;
        return out;
    }

    let sys = systems[p.systemID];
    let t   = clamp(1.0 - life / max(maxLife, 1e-5), 0.0, 1.0);

    out.vColor     = sampleGradient(sys.colors, sys.colorPositions, t);
    out.vShapeType = sys.shapeType;

    let corner        = QUAD[cornerIdx];
    let particleSize  = mix(p.startSize, p.endSize, t);
    let pixelToClip   = 2.0 / vertUni.screenSize;

    var clipPos = vertUni.camera * vec4<f32>(p.posAndLife.xyz, 1.0);

    let vel2D = p.velAndMaxLife.xy;
    let speed = length(vel2D);

    if sys.trailStretch > 0.0 && speed > 0.5 {
        let velDir   = vec2<f32>(vel2D.x, -vel2D.y) / speed;
        let perpDir  = vec2<f32>(-velDir.y, velDir.x);
        let stretchLen = particleSize + sys.trailStretch * speed;
        let offset     = corner.x * perpDir * particleSize
                       + corner.y * velDir  * stretchLen;
        clipPos = vec4<f32>(clipPos.xy + offset * pixelToClip * clipPos.w,
                            clipPos.z, clipPos.w);
        out.vUV = corner + vec2<f32>(0.5);
    } else {
        let sinA    = sin(p.angle);
        let cosA    = cos(p.angle);
        let rotated = vec2<f32>(cosA * corner.x - sinA * corner.y,
                                sinA * corner.x + cosA * corner.y);
        clipPos = vec4<f32>(clipPos.xy + rotated * particleSize * pixelToClip * clipPos.w,
                            clipPos.z, clipPos.w);
        out.vUV = rotated + vec2<f32>(0.5);
    }

    out.position = clipPos;
    return out;
}
