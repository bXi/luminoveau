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
    float  angle;           // current rotation (radians)
    float  angularVelocity; // radians per second
    float  _pad0;
    float  _pad1;
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
    uint   flags;           // bit 0 = emitting, bit 1 = custom compute
    uint   shapeType;
    float  angVelMin;
    float  angVelMax;
    float  angVelBias;
    float  trailStretch;
};

struct GPUCollider {
    float4 params;       // HalfPlane:(nx,ny,d,0)  Circle:(cx,cy,r,0)
    float  restitution;
    float  friction;
    uint   type;         // 0=HalfPlane, 1=Circle
    uint   enabled;
};

// set=0: read-only buffers
StructuredBuffer<GPUParticleSystem>   systems   : register(t0, space0);
StructuredBuffer<GPUCollider>         colliders : register(t1, space0);

// set=1: read-write particle data
RWStructuredBuffer<GPUParticle>       particles : register(u0, space1);

// set=2: per-dispatch constants
cbuffer ComputeUniforms : register(b0, space2)
{
    uint  totalParticles;
    float deltaTime;
    float time;
    uint  numColliders;  // high-water slot count; entries may have enabled==0
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

// ── Collision helper — shared by alive update and first-frame spawn ───────────

void ApplyColliders(inout float3 pos, inout float3 vel)
{
    for (uint ci = 0u; ci < numColliders; ci++)
    {
        GPUCollider col = colliders[ci];
        if (!col.enabled) continue;

        float2 n   = float2(0.0, 0.0);
        float  pen = 0.0;

        if (col.type == 0u)
        {
            n = col.params.xy;
            float dist = dot(pos.xy, n) - col.params.z;
            if (dist < 0.0) pen = -dist;
        }
        else if (col.type == 1u)
        {
            float2 delta = pos.xy - col.params.xy;
            float  dist  = length(delta);
            if (dist < col.params.z && dist > 1e-5)
            {
                n   = delta / dist;
                pen = col.params.z - dist;
            }
        }

        if (pen > 0.0)
        {
            pos.xy += n * pen;
            float vn = dot(vel.xy, n);
            if (vn < 0.0)
            {
                float2 vt = vel.xy - vn * n;
                vel.xy = (-vn * col.restitution) * n + vt * (1.0 - col.friction);
            }
        }
    }
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

    // bit 1 = custom compute pipeline handles this system — skip built-in logic
    if ((sys.flags & 2u) != 0u) return;

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

        // ── Collider responses ────────────────────────────────────────────────
        ApplyColliders(posAndLife.xyz, vel);

        particles[idx].posAndLife    = posAndLife;
        particles[idx].velAndMaxLife = float4(vel, velAndMaxLife.w);
        particles[idx].angle        += particles[idx].angularVelocity * deltaTime;
    }
    else
    {
        // ── Dead: count down respawn timer ────────────────────────────────────
        if (isEmitting)
        {
            // respawnTimer is a normalised fraction [0,1] of one respawn period.
            // Decrement by (dt * storedRate) where storedRate = emitRate/N.
            // Changing emitRate immediately changes the decrement speed — no
            // burst, no scatter, smooth live updates in both directions.
            respawnTimer -= deltaTime * sys.emitRate;

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

                // Initial rotation angle — fully random regardless of angVel range
                float startAngle = hash(seed + 7u) * 6.2831853;

                // Angular velocity sampled from system range
                float angVel = lerp(sys.angVelMin, sys.angVelMax,
                                    biasedSample(seed + 8u, sys.angVelBias));

                float3 spawnVel3 = sys.spawnVel.xyz + randVel;
                ApplyColliders(spawnPos, spawnVel3);

                particles[idx].posAndLife    = float4(spawnPos, maxLife);
                particles[idx].velAndMaxLife = float4(spawnVel3, maxLife);
                particles[idx].startSize     = startSize;
                particles[idx].endSize       = endSize;
                particles[idx].angle         = startAngle;
                particles[idx].angularVelocity = angVel;
                particles[idx].respawnTimer  = 1.0; // reset to full period fraction
            }
            else
            {
                particles[idx].respawnTimer = respawnTimer;
            }
        }
    }
}
