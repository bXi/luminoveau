// Particle Compute Shader (HLSL)
// Advances all particle physics (integration, respawn) in a single GPU dispatch.

// ── Structs (must match GPUParticle / GPUParticleSystem in particlesystem.h) ──

struct GPUParticle {
    float4 posAndLife;      // xyz = world pos, w = remaining life (<= 0 → dead)
    float4 velAndMaxLife;   // xyz = velocity,  w = maxLife
    uint   systemID;
    float  respawnTimer;
    float  _pad0;
    float  _pad1;
};

struct GPUParticleSystem {
    float4 spawnPos;        // xyz = origin, w = spawn radius
    float4 spawnVel;        // xyz = base vel, w = velocity spread
    float4 gravityAndDrag;  // xyz = gravity,  w = drag coefficient
    float4 colorStart;
    float4 colorEnd;
    float  emitRate;
    float  lifetime;
    uint   flags;           // bit 0 = emitting
    float  size;
};

// set=0: read-only system data
StructuredBuffer<GPUParticleSystem>   systems   : register(t0, space0);

// set=1: read-write particle data
RWStructuredBuffer<GPUParticle>       particles : register(u0, space1);

// set=2: per-dispatch constants
cbuffer ComputeUniforms : register(b0, space2)
{
    uint  totalParticles;
    float deltaTime;
    float time;
    uint  _pad;
};

// ── Simple deterministic hash (Wang hash) ────────────────────────────────────

float hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return float(seed & 0x7FFFFFFFu) / float(0x7FFFFFFFu);
}

// ── Main ─────────────────────────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    uint idx = dispatchID.x;
    if (idx >= totalParticles) return;

    float4 posAndLife    = particles[idx].posAndLife;
    float4 velAndMaxLife = particles[idx].velAndMaxLife;
    uint   sysID         = particles[idx].systemID;
    float  respawnTimer  = particles[idx].respawnTimer;

    GPUParticleSystem sys = systems[sysID];
    bool isEmitting = (sys.flags & 1u) != 0u;

    if (posAndLife.w > 0.0)
    {
        // ── Alive: integrate physics ──────────────────────────────────────────
        float3 vel  = velAndMaxLife.xyz;
        float  drag = sys.gravityAndDrag.w;

        vel += sys.gravityAndDrag.xyz * deltaTime;
        vel *= (1.0 - clamp(drag * deltaTime, 0.0, 1.0));

        posAndLife.xyz += vel * deltaTime;
        posAndLife.w   -= deltaTime;

        particles[idx].posAndLife    = posAndLife;
        particles[idx].velAndMaxLife = float4(vel, velAndMaxLife.w);
    }
    else
    {
        // ── Dead: count down respawn timer ────────────────────────────────────
        if (isEmitting)
        {
            respawnTimer -= deltaTime;

            if (respawnTimer <= 0.0)
            {
                // Spawn
                uint seed = idx * 1973u ^ uint(time * 1000.0) * 9277u ^ 17389u;

                float angle    = hash(seed)      * 6.2831853;
                float r        = hash(seed + 1u) * sys.spawnPos.w;
                float3 spawnPos = sys.spawnPos.xyz + float3(cos(angle) * r, sin(angle) * r, 0.0);

                float spread   = sys.spawnVel.w;
                float velAngle = hash(seed + 2u) * 6.2831853;
                float velMag   = hash(seed + 3u) * spread;
                float3 randVel = float3(cos(velAngle) * velMag, sin(velAngle) * velMag, 0.0);

                float maxLife = sys.lifetime;

                particles[idx].posAndLife    = float4(spawnPos, maxLife);
                particles[idx].velAndMaxLife = float4(sys.spawnVel.xyz + randVel, maxLife);
                // Delay next respawn by 1/emitRate so consecutive slots spread out
                particles[idx].respawnTimer  = (sys.emitRate > 0.0)
                                               ? (1.0 / sys.emitRate)
                                               : 1e6;
            }
            else
            {
                particles[idx].respawnTimer = respawnTimer;
            }
        }
    }
}
