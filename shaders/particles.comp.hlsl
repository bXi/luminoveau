// Particle Compute Shader (HLSL)
// Advances all particle physics (integration, respawn) in a single GPU dispatch.

// ── Structs (must match GPUParticle / GPUParticleSystem in particlesystem.h) ──

struct GPUParticle {
    float4 posAndLife;      // xyz = world pos, w = remaining life (<= 0 → dead)
    float4 velAndMaxLife;   // xyz = velocity,  w = maxLife
    uint   systemID;
    float  respawnTimer;
    float  startSize;       // size at birth (pixels)
    float  endSize;         // size at death (pixels)
};

struct GPUParticleSystem {
    float4 spawnPos;        // xyz = origin, w = spawn radius
    float4 spawnVel;        // xyz = base vel, w = velocity spread
    float4 gravityAndDrag;  // xyz = gravity,  w = drag coefficient
    float4 colors[4];
    float4 colorPositions;  // t-position per stop; -1 = unused
    float  sizeStartMin;
    float  sizeStartMax;
    float  sizeEndMin;
    float  sizeEndMax;
    float  sizeStartBias;
    float  sizeEndBias;
    float  lifetimeMin;
    float  lifetimeMax;
    float  lifetimeBias;
    float  emitRate;
    uint   flags;           // bit 0 = emitting
    uint   shapeType;
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

// ── Deterministic hash (Wang hash) → float in [0, 1) ─────────────────────────

float hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return float(seed & 0x7FFFFFFFu) / float(0x7FFFFFFFu);
}

// Biased sample: bias=1 is uniform, bias>1 skews toward min (t→0), bias<1 toward max
float biasedSample(uint seed, float bias)
{
    return pow(hash(seed), max(bias, 1e-5));
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
                uint seed = idx * 1973u ^ uint(time * 1000.0) * 9277u ^ 17389u;

                // Spawn position (circular disc)
                float spawnAngle = hash(seed)      * 6.2831853;
                float spawnR     = hash(seed + 1u) * sys.spawnPos.w;
                float3 spawnPos  = sys.spawnPos.xyz
                                 + float3(cos(spawnAngle) * spawnR,
                                          sin(spawnAngle) * spawnR,
                                          0.0);

                // Initial velocity (circular spread)
                float velAngle = hash(seed + 2u) * 6.2831853;
                float velMag   = hash(seed + 3u) * sys.spawnVel.w;
                float3 randVel = float3(cos(velAngle) * velMag,
                                        sin(velAngle) * velMag,
                                        0.0);

                // Variable lifetime with bias
                float maxLife = lerp(sys.lifetimeMin, sys.lifetimeMax,
                                     biasedSample(seed + 4u, sys.lifetimeBias));

                // Variable start size with bias
                float startSize = lerp(sys.sizeStartMin, sys.sizeStartMax,
                                       biasedSample(seed + 5u, sys.sizeStartBias));

                // Variable end size with bias
                float endSize = lerp(sys.sizeEndMin, sys.sizeEndMax,
                                     biasedSample(seed + 6u, sys.sizeEndBias));

                particles[idx].posAndLife    = float4(spawnPos, maxLife);
                particles[idx].velAndMaxLife = float4(sys.spawnVel.xyz + randVel, maxLife);
                particles[idx].startSize     = startSize;
                particles[idx].endSize       = endSize;
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
