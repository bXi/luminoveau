// Point-light cube shadow — depth vertex shader (HLSL).
// Renders the scene from a point light for one cube face. Passes world position to the fragment
// shader, which stores the linear distance from the light (distance shadow map).

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

// Per-face: the cube face's view-projection + this draw's base instance (vertex uniforms space1).
cbuffer CubeShadowParams : register(b0, space1)
{
    float4x4 faceViewProj;
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
    float3 WorldPos : TEXCOORD0;
    float4 Position : SV_Position;
};

VertexOutput main(VertexInput input, uint instanceID : SV_InstanceID)
{
    SceneUniforms scene = SceneData[0];
    float4x4 model = scene.models[instanceID + baseInstance];

    float4 world = mul(model, float4(input.Position, 1.0));

    VertexOutput output;
    output.WorldPos = world.xyz;
    output.Position = mul(faceViewProj, world);
    return output;
}
