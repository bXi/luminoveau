// Particle Fragment Shader (HLSL)
// Renders a soft circular billboard by discarding corners and fading at the edge.

struct Input
{
    float4 vColor : TEXCOORD0;
    float2 vUV    : TEXCOORD1;
};

float4 main(Input i) : SV_Target
{
    // Map UV 0..1 → -1..1, compute squared distance from billboard centre
    float2 uv   = i.vUV * 2.0 - 1.0;
    float  dist = dot(uv, uv);    // 0 at centre, 1 at corner

    if (dist > 1.0) discard;      // clip corners → circular particle

    // Soft fall-off toward the edge
    float alpha = 1.0 - smoothstep(0.0, 1.0, dist);

    return i.vColor * float4(1.0, 1.0, 1.0, alpha);
}
