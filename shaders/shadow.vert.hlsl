// Shadow depth vertex shader (HLSL).
// Renders scene geometry from a light's viewpoint. Reuses the same instanced model path as
// model3d.vert (models[] in the SceneData storage buffer) but transforms by the light's viewProj
// instead of the camera's, and outputs a linear-ish depth the fragment shader writes to an R32F
// shadow texture.

struct SceneUniforms
{
    float4x4 viewProj;
    float4x4 models[16];
    float4 cameraPos;
    float4 ambientLight;
    float4 lightPositions[4];
    float4 lightColors[4];
    float4 lightParams[4];
    int lightCount;
    int modelCount;
    int padding[2];
};

StructuredBuffer<SceneUniforms> SceneData : register(t0, space0);

// Per-draw: the light's view-projection and this draw's base instance (SDL3 vertex uniforms in space1).
cbuffer ShadowParams : register(b0, space1)
{
    float4x4 lightViewProj;
    uint baseInstance;
    uint3 _pad;
};

struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float4 Color    : TEXCOORD3;
};

struct VertexOutput
{
    float  Depth    : TEXCOORD0;   // clip-space depth (z/w), matched in model3d.frag
    float4 Position : SV_Position;
};

VertexOutput main(VertexInput input, uint instanceID : SV_InstanceID)
{
    SceneUniforms scene = SceneData[0];
    float4x4 model = scene.models[instanceID + baseInstance];

    float4 world = mul(model, float4(input.Position, 1.0));
    float4 clip  = mul(lightViewProj, world);

    VertexOutput output;
    output.Position = clip;
    // clip.z is already [0,1] (light matrix uses a zero-to-one ortho), matching the depth buffer
    // and the compare in model3d.frag.
    output.Depth    = clip.z / clip.w;
    return output;
}
