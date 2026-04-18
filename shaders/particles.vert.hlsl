// Particle Vertex Shader (HLSL)
// Instanced billboard rendering — one particle per instance, 6 verts per quad.

struct GPUParticle {
    float4 posAndLife;      // xyz = world pos, w = remaining life (<= 0 → dead)
    float4 velAndMaxLife;   // xyz = velocity,  w = maxLife
    uint   systemID;
    float  respawnTimer;
    float  startSize;       // size at birth (pixels)
    float  endSize;         // size at death (pixels)
};

struct GPUParticleSystem {
    float4 spawnPos;
    float4 spawnVel;
    float4 gravityAndDrag;
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
    uint   flags;
    uint   shapeType;
};

// Billboard quad corners — CCW, 2 triangles (6 vertices)
static const float2 QUAD[6] = {
    float2(-0.5, -0.5),
    float2( 0.5, -0.5),
    float2( 0.5,  0.5),
    float2(-0.5, -0.5),
    float2( 0.5,  0.5),
    float2(-0.5,  0.5)
};

// set=0: storage buffers
StructuredBuffer<GPUParticle>       particles : register(t0, space0);
StructuredBuffer<GPUParticleSystem> systems   : register(t1, space0);

// set=1: uniform buffer
cbuffer VertUniforms : register(b0, space1)
{
    float4x4 camera;
    float2   screenSize;
    float2   _pad;
};

struct Output
{
    float4                vColor     : TEXCOORD0;
    float2                vUV        : TEXCOORD1;
    nointerpolation uint  vShapeType : TEXCOORD2;
    float4                Position   : SV_Position;
};

// ── Multi-stop colour gradient ────────────────────────────────────────────────
// Stops are sorted by position (ascending). Position -1 marks an unused stop.
// Stop 0 is always active (position 0.0 from CPU side).

float4 SampleGradient(float4 colorArr[4], float4 positions, float t)
{
    float  prevPos   = positions[0];
    float4 lastColor = colorArr[0];

    for (int i = 1; i < 4; i++)
    {
        float stopPos = positions[i];
        if (stopPos < 0.0)
            break;  // unused — clamp to previous color

        if (t <= stopPos)
        {
            float range  = max(stopPos - prevPos, 1e-5);
            float localT = saturate((t - prevPos) / range);
            return lerp(colorArr[i - 1], colorArr[i], localT);
        }

        prevPos   = stopPos;
        lastColor = colorArr[i];
    }

    return lastColor;  // t is beyond the last active stop
}

// ─────────────────────────────────────────────────────────────────────────────

Output main(uint vertID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    uint  cornerIdx = vertID % 6u;
    GPUParticle p   = particles[instanceID];
    float life      = p.posAndLife.w;
    float maxLife   = p.velAndMaxLife.w;

    Output o;

    // Dead particle → push fully outside clip space; GPU clips all 6 verts
    if (life <= 0.0)
    {
        o.Position   = float4(2.0, 2.0, 0.0, 1.0);
        o.vColor     = float4(0.0, 0.0, 0.0, 0.0);
        o.vUV        = float2(0.0, 0.0);
        o.vShapeType = 0u;
        return o;
    }

    GPUParticleSystem sys = systems[p.systemID];

    // Normalised age: 0 at birth, 1 at death
    float t = clamp(1.0 - life / max(maxLife, 1e-5), 0.0, 1.0);

    o.vColor     = SampleGradient(sys.colors, sys.colorPositions, t);
    o.vUV        = QUAD[cornerIdx] + float2(0.5, 0.5);
    o.vShapeType = sys.shapeType;

    // Per-particle size, interpolated from birth to death
    float particleSize = lerp(p.startSize, p.endSize, t);

    // Project centre to clip space (orthographic → w == 1)
    float4 clipPos = mul(camera, float4(p.posAndLife.xyz, 1.0));

    // Expand screen-aligned billboard (particleSize is in pixels)
    float2 pixelToClip = 2.0 / screenSize;
    clipPos.xy += QUAD[cornerIdx] * particleSize * pixelToClip * clipPos.w;

    o.Position = clipPos;
    return o;
}
