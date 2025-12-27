// Fullscreen Quad Vertex Shader (HLSL)
// Used for RTT and shader render passes

struct VertexOutput
{
    float2 TexCoord : TEXCOORD0;
    float4 TintColor : TEXCOORD1;
    float4 Position : SV_Position;
};

// SDL3 expects vertex shader uniforms at space1!
cbuffer CameraUniforms : register(b0, space1)
{
    float4x4 camera;
    float4x4 model;
    float2 flipped;
    float2 uv0;
    float2 uv1;
    float2 uv2;
    float2 uv3;
    float2 uv4;
    float2 uv5;
    float tintColorR;
    float tintColorG;
    float tintColorB;
    float tintColorA;
};

// Quad vertices (2 triangles forming full-screen quad)
static const float2 quadPositions[6] = {
    float2(1.0, 1.0),  // 0: top-right
    float2(0.0, 1.0),  // 1: top-left
    float2(1.0, 0.0),  // 2: bottom-right
    float2(0.0, 1.0),  // 3: top-left
    float2(0.0, 0.0),  // 4: bottom-left
    float2(1.0, 0.0)   // 5: bottom-right
};

VertexOutput main(uint vertexID : SV_VertexID)
{
    VertexOutput output;
    
    // Get position for this vertex
    float2 pos = quadPositions[vertexID];
    output.Position = mul(camera, mul(model, float4(pos, 0.0, 1.0)));
    
    // Select appropriate UV based on vertex ID
    float2 uv;
    switch (vertexID)
    {
        case 0: uv = uv0; break;
        case 1: uv = uv1; break;
        case 2: uv = uv2; break;
        case 3: uv = uv3; break;
        case 4: uv = uv4; break;
        case 5: uv = uv5; break;
        default: uv = float2(0.0, 0.0); break;
    }
    
    output.TexCoord = flipped * uv;
    output.TintColor = float4(tintColorR, tintColorG, tintColorB, tintColorA);
    
    return output;
}
