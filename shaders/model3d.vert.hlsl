// Model 3D Vertex Shader (HLSL)

// Vertex input structure
struct VertexInput
{
    float3 Position : TEXCOORD0;  // Changed from POSITION
    float3 Normal : TEXCOORD1;    // Changed from NORMAL
    float2 TexCoord : TEXCOORD2;  // Changed from TEXCOORD0
    float4 Color : TEXCOORD3;     // Changed from COLOR
};

// Vertex output structure
struct VertexOutput
{
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float4 Color : TEXCOORD3;
    float4 Position : SV_Position;
};

// Scene uniforms structure
struct SceneUniforms
{
    float4x4 viewProj;
    float4x4 models[16];        // Array of model matrices
    float4 cameraPos;
    float4 ambientLight;
    float4 lightPositions[4];
    float4 lightColors[4];
    float4 lightParams[4];
    int lightCount;
    int modelCount;
    int padding[2];
};

// Storage buffer for scene uniforms
StructuredBuffer<SceneUniforms> SceneData : register(t0, space0);

// Per-draw base instance: lets one mesh-group draw index its own contiguous slice of
// scene.models[] (SDL3 expects vertex uniforms at space1).
cbuffer InstanceOffset : register(b0, space1)
{
    uint baseInstance;
};

VertexOutput main(VertexInput input, uint instanceID : SV_InstanceID)
{
    VertexOutput output;

    // Get scene uniforms
    SceneUniforms scene = SceneData[0];

    // Use instance ID (+ this draw's base) to get the correct model matrix
    float4x4 model = scene.models[instanceID + baseInstance];
    
    // Transform position
    float4 worldPos = mul(model, float4(input.Position, 1.0));
    output.Position = mul(scene.viewProj, worldPos);

    // Normal matrix = inverse-transpose of the model's upper 3x3, built from column cross products
    // (the cofactor matrix). Equals the inverse-transpose up to a positive scale that normalize()
    // removes — correct for rotation + non-uniform scale, no matrix inverse needed.
    float3 ma = model._m00_m10_m20;   // column 0
    float3 mb = model._m01_m11_m21;   // column 1
    float3 mc = model._m02_m12_m22;   // column 2
    float3x3 normalMatrix = float3x3(cross(mb, mc), cross(mc, ma), cross(ma, mb));
    float3 normal = normalize(mul(normalMatrix, input.Normal));

    // Lighting is done per-pixel in the fragment shader; forward interpolants + raw vertex colour.
    output.Color = input.Color;
    output.WorldPosition = worldPos.xyz;
    output.Normal = normal;
    output.TexCoord = input.TexCoord;

    return output;
}
