// Particle Vertex Shader (HLSL)
// Instanced billboard rendering — one particle per instance, 6 verts per quad.
// No vertex buffers: quad corners are generated from SV_VertexID.

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

// Billboard quad corners — CCW, 2 triangles (6 vertices)
static const float2 QUAD[6] = {
    float2(-0.5, -0.5),
    float2( 0.5, -0.5),
    float2( 0.5,  0.5),
    float2(-0.5, -0.5),
    float2( 0.5,  0.5),
    float2(-0.5,  0.5)
};

// set=0: storage buffers (vertex shader)
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
    float4 vColor   : TEXCOORD0;
    float2 vUV      : TEXCOORD1;
    float4 Position : SV_Position;
};

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
        o.Position = float4(2.0, 2.0, 0.0, 1.0);
        o.vColor   = float4(0.0, 0.0, 0.0, 0.0);
        o.vUV      = float2(0.0, 0.0);
        return o;
    }

    GPUParticleSystem sys = systems[p.systemID];

    // Colour lerp from birth (t=0) to death (t=1)
    float t  = clamp(1.0 - life / max(maxLife, 0.0001), 0.0, 1.0);
    o.vColor = lerp(sys.colorStart, sys.colorEnd, t);

    // UV for fragment shader (0..1 over the billboard face)
    o.vUV = QUAD[cornerIdx] + float2(0.5, 0.5);

    // Project particle centre to clip space (orthographic → w == 1)
    float4 clipPos = mul(camera, float4(p.posAndLife.xyz, 1.0));

    // Expand to screen-aligned billboard.
    // sys.size is in pixels; 2/screenSize converts pixels to clip-space units.
    float2 pixelToClip = 2.0 / screenSize;
    clipPos.xy += QUAD[cornerIdx] * sys.size * pixelToClip * clipPos.w;

    o.Position = clipPos;
    return o;
}
