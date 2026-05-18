// Persistence-of-Vision: full-screen triangle vertex shader.
// Generates 3 vertices that cover the entire render target — no vertex buffer needed.
// gScale is forwarded to the fragment shader as a flat varying to avoid
// SDL_PushGPUFragmentUniformData (unproven path); vertex uniforms are safe.

// SDL3 expects vertex shader uniforms at space1.
cbuffer POVUniforms : register(b0, space1) {
    float gScale;
    float3 _pad;
};

struct Output {
    float4                   Position : SV_Position;
    float2                   vUV      : TEXCOORD0;
    nointerpolation float    vScale   : TEXCOORD1;
};

Output main(uint vertID : SV_VertexID)
{
    // Three-vertex triangle that covers [-1,1] NDC clip space
    float2 pos;
    if      (vertID == 0u) pos = float2(-1.0,  1.0);
    else if (vertID == 1u) pos = float2( 3.0,  1.0);
    else                   pos = float2(-1.0, -3.0);

    Output o;
    o.Position = float4(pos, 0.0, 1.0);
    // NDC: (-1, 1) = top-left → UV (0,0); (1,-1) = bottom-right → UV (1,1)
    o.vUV    = pos * float2(0.5, -0.5) + float2(0.5, 0.5);
    o.vScale = gScale;
    return o;
}
