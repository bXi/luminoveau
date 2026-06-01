// WebGPU-backend builder for the built-in particle compute pipeline.
// Embeds the WGSL source directly so no extra asset roundtrip is needed.
// Binding layout: group0 = uniforms, group1 = RO bufs (systems, colliders),
// group2 = RW buf (particles).

#include "draw/particles_builtin.h"

#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "renderer/renderer.h"

#include <cstring>

static const char kParticlesCompWgsl[] = R"WGSL(
struct GPUParticle {
    posAndLife:      vec4<f32>,
    velAndMaxLife:   vec4<f32>,
    systemID:        u32,
    respawnTimer:    f32,
    startSize:       f32,
    endSize:         f32,
    angle:           f32,
    angularVelocity: f32,
    pad0:            f32,
    pad1:            f32,
}
struct GPUParticleSystem {
    spawnPos:       vec4<f32>,
    spawnVel:       vec4<f32>,
    gravityAndDrag: vec4<f32>,
    colors:         array<vec4<f32>, 4>,
    colorPositions: vec4<f32>,
    sizeStartMin:   f32, sizeStartMax: f32,
    sizeEndMin:     f32, sizeEndMax:   f32,
    sizeStartBias:  f32, sizeEndBias:  f32,
    lifetimeMin:    f32, lifetimeMax:  f32,
    lifetimeBias:   f32, emitRate:     f32,
    flags:          u32, shapeType:    u32,
    angVelMin:      f32, angVelMax:    f32,
    angVelBias:     f32, trailStretch: f32,
}
struct GPUCollider {
    params:      vec4<f32>,
    restitution: f32,
    friction:    f32,
    kind:        u32,
    enabled:     u32,
}
struct ComputeUniforms {
    totalParticles: u32,
    deltaTime:      f32,
    time:           f32,
    numColliders:   u32,
}
@group(0) @binding(0) var<uniform>             uniforms:  ComputeUniforms;
@group(1) @binding(0) var<storage, read>       systems:   array<GPUParticleSystem>;
@group(1) @binding(1) var<storage, read>       colliders: array<GPUCollider>;
@group(2) @binding(0) var<storage, read_write> particles: array<GPUParticle>;

fn hash(seed: u32) -> f32 {
    var s = seed;
    s = (s ^ 61u) ^ (s >> 16u);
    s = s * 9u;
    s = s ^ (s >> 4u);
    s = s * 0x27d4eb2du;
    s = s ^ (s >> 15u);
    return f32(s & 0x7FFFFFFFu) / f32(0x7FFFFFFFu);
}
fn biasedSample(seed: u32, bias: f32) -> f32 {
    return pow(hash(seed), max(bias, 1e-5));
}
fn applyColliders(pos: ptr<function, vec3<f32>>, vel: ptr<function, vec3<f32>>) {
    for (var ci = 0u; ci < uniforms.numColliders; ci++) {
        let col = colliders[ci];
        if (col.enabled == 0u) { continue; }
        var n = vec2<f32>(0.0, 0.0);
        var pen = 0.0f;
        if (col.kind == 0u) {
            n = col.params.xy;
            let dist = dot((*pos).xy, n) - col.params.z;
            if (dist < 0.0) { pen = -dist; }
        } else if (col.kind == 1u) {
            let delta = (*pos).xy - col.params.xy;
            let dist  = length(delta);
            if (dist < col.params.z && dist > 1e-5) { n = delta / dist; pen = col.params.z - dist; }
        }
        if (pen > 0.0) {
            (*pos) = vec3<f32>((*pos).xy + n * pen, (*pos).z);
            let vn = dot((*vel).xy, n);
            if (vn < 0.0) {
                let vt = (*vel).xy - vn * n;
                (*vel) = vec3<f32>((-vn * col.restitution) * n + vt * (1.0 - col.friction), (*vel).z);
            }
        }
    }
}
@compute @workgroup_size(64, 1, 1)
fn main(@builtin(global_invocation_id) dispatchID: vec3<u32>) {
    let idx = dispatchID.x;
    if (idx >= uniforms.totalParticles) { return; }
    var posAndLife    = particles[idx].posAndLife;
    var velAndMaxLife = particles[idx].velAndMaxLife;
    let sysID         = particles[idx].systemID;
    var respawnTimer  = particles[idx].respawnTimer;
    let sys = systems[sysID];
    if ((sys.flags & 2u) != 0u) { return; }
    let isEmitting = (sys.flags & 1u) != 0u;
    if (posAndLife.w > 0.0) {
        var vel  = velAndMaxLife.xyz;
        let drag = sys.gravityAndDrag.w;
        vel += sys.gravityAndDrag.xyz * uniforms.deltaTime;
        vel *= (1.0 - clamp(drag * uniforms.deltaTime, 0.0, 1.0));
        var pos = posAndLife.xyz + vel * uniforms.deltaTime;
        posAndLife.w -= uniforms.deltaTime;
        applyColliders(&pos, &vel);
        particles[idx].posAndLife    = vec4<f32>(pos, posAndLife.w);
        particles[idx].velAndMaxLife = vec4<f32>(vel, velAndMaxLife.w);
        particles[idx].angle        += particles[idx].angularVelocity * uniforms.deltaTime;
    } else if (isEmitting) {
        respawnTimer -= uniforms.deltaTime * sys.emitRate;
        if (respawnTimer <= 0.0) {
            let seed = (idx * 1973u) ^ (u32(uniforms.time * 1000.0) * 9277u) ^ 17389u;
            let spawnAngle = hash(seed)      * 6.2831853;
            let spawnR     = hash(seed + 1u) * sys.spawnPos.w;
            var spawnPos   = sys.spawnPos.xyz + vec3<f32>(cos(spawnAngle)*spawnR, sin(spawnAngle)*spawnR, 0.0);
            let velAngle   = hash(seed + 2u) * 6.2831853;
            let velMag     = hash(seed + 3u) * sys.spawnVel.w;
            let randVel    = vec3<f32>(cos(velAngle)*velMag, sin(velAngle)*velMag, 0.0);
            let maxLife    = mix(sys.lifetimeMin, sys.lifetimeMax,   biasedSample(seed+4u, sys.lifetimeBias));
            let startSize  = mix(sys.sizeStartMin, sys.sizeStartMax, biasedSample(seed+5u, sys.sizeStartBias));
            let endSize    = mix(sys.sizeEndMin,   sys.sizeEndMax,   biasedSample(seed+6u, sys.sizeEndBias));
            let startAngle = hash(seed + 7u) * 6.2831853;
            let angVel     = mix(sys.angVelMin, sys.angVelMax,       biasedSample(seed+8u, sys.angVelBias));
            var spawnVel3  = sys.spawnVel.xyz + randVel;
            applyColliders(&spawnPos, &spawnVel3);
            particles[idx].posAndLife      = vec4<f32>(spawnPos, maxLife);
            particles[idx].velAndMaxLife   = vec4<f32>(spawnVel3, maxLife);
            particles[idx].startSize       = startSize;
            particles[idx].endSize         = endSize;
            particles[idx].angle           = startAngle;
            particles[idx].angularVelocity = angVel;
            particles[idx].respawnTimer    = 1.0;
        } else {
            particles[idx].respawnTimer = respawnTimer;
        }
    }
}
)WGSL";

GpuComputePipelineHandle ParticlesBuiltin::CreateComputePipeline() {
    GpuComputePipelineCreateInfo info;
    info.code                        = reinterpret_cast<const uint8_t*>(kParticlesCompWgsl);
    info.codeSize                    = sizeof(kParticlesCompWgsl) - 1;
    info.entrypoint                  = "main";
    info.threadCountX                = 64;
    info.threadCountY                = 1;
    info.threadCountZ                = 1;
    info.readonlyStorageBufferCount  = 2;  // group1: systems(b0), colliders(b1)
    info.readwriteStorageBufferCount = 1;  // group2: particles(b0)
    info.uniformBufferCount          = 1;  // group0: ComputeUniforms(b0)

    GpuComputePipelineHandle ph = Renderer::GetGpu().createComputePipeline(info);
    if (!ph) {
        LOG_WARNING("Particles: built-in WebGPU compute pipeline creation FAILED (check Dawn validation error above)");
    } else {
        LOG_INFO("Particles: built-in WebGPU compute pipeline created");
    }
    return ph;
}
