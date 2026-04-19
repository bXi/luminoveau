// Particle Fragment Shader (HLSL)
// Renders a particle billboard with the shape selected per-system.

struct Input
{
    float4               vColor     : TEXCOORD0;
    float2               vUV        : TEXCOORD1;
    nointerpolation uint vShapeType : TEXCOORD2;
};

// Shape type constants (must match ParticleShape enum in particlesystem.h)
#define SHAPE_SOFT_CIRCLE  0u
#define SHAPE_HARD_CIRCLE  1u
#define SHAPE_SQUARE       2u
#define SHAPE_SOFT_SQUARE  3u
#define SHAPE_RING         4u
#define SHAPE_TEXTURED     5u

// Always bound — non-textured draws receive a 1×1 white pixel
Texture2D    gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);

float4 main(Input i) : SV_Target
{
    // UV is 0..1 over the billboard; map to -1..1 for distance math
    float2 uv = i.vUV * 2.0 - 1.0;

    if (i.vShapeType == SHAPE_TEXTURED)
    {
        // Sample the image; tint via the colour gradient.
        // V is flipped because the quad's -Y corner (screen bottom) maps to UV.y=0,
        // but Vulkan/SPIRV convention places the texture origin at the top.
        float4 texColor = gTexture.Sample(gSampler, float2(i.vUV.x, 1.0 - i.vUV.y));
        if (texColor.a < 0.01) discard;
        return i.vColor * texColor;
    }

    float alpha = 1.0;

    if (i.vShapeType == SHAPE_SOFT_CIRCLE)
    {
        float dist = dot(uv, uv);           // squared distance from centre
        if (dist > 1.0) discard;
        alpha = 1.0 - smoothstep(0.0, 1.0, dist);
    }
    else if (i.vShapeType == SHAPE_HARD_CIRCLE)
    {
        if (dot(uv, uv) > 1.0) discard;
        alpha = 1.0;
    }
    else if (i.vShapeType == SHAPE_SQUARE)
    {
        alpha = 1.0;                        // full billboard, no clipping
    }
    else if (i.vShapeType == SHAPE_SOFT_SQUARE)
    {
        float2 edge = abs(uv);
        float  maxEdge = max(edge.x, edge.y);
        alpha = 1.0 - smoothstep(0.7, 1.0, maxEdge);
        if (alpha < 0.01) discard;
    }
    else if (i.vShapeType == SHAPE_RING)
    {
        float dist = length(uv);
        alpha = smoothstep(0.55, 0.65, dist) * (1.0 - smoothstep(0.85, 1.0, dist));
        if (alpha < 0.01) discard;
    }

    return i.vColor * float4(1.0, 1.0, 1.0, alpha);
}
