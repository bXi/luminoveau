// Model 3D Vertex Shader (HLSL)

// Vertex input structure
struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR;
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

VertexOutput main(VertexInput input, uint instanceID : SV_InstanceID)
{
    VertexOutput output;
    
    // Get scene uniforms
    SceneUniforms scene = SceneData[0];
    
    // Use instance ID to get the correct model matrix
    float4x4 model = scene.models[instanceID];
    
    // Transform position
    float4 worldPos = mul(model, float4(input.Position, 1.0));
    output.Position = mul(scene.viewProj, worldPos);
    
    // Transform normal to world space
    // Extract 3x3 from model matrix for normal transformation
    float3x3 normalMatrix = transpose((float3x3)model);
    float3 normal = normalize(mul(normalMatrix, input.Normal));
    
    // Compute lighting (Gouraud shading - per vertex)
    float3 viewDir = normalize(scene.cameraPos.xyz - worldPos.xyz);
    
    // Start with ambient light
    float3 lighting = scene.ambientLight.rgb * scene.ambientLight.a;
    
    // Add contribution from each light
    for (int i = 0; i < scene.lightCount && i < 4; i++)
    {
        int lightType = (int)scene.lightPositions[i].w;
        float3 lightColor = scene.lightColors[i].rgb;
        float intensity = scene.lightColors[i].a;
        
        float3 lightDir;
        float attenuation = 1.0;
        
        if (lightType == 1)
        {
            // Directional light - use direction as-is (direction toward light source)
            lightDir = normalize(scene.lightPositions[i].xyz);
            attenuation = 1.0; // No attenuation for directional lights
        }
        else
        {
            // Point light - calculate direction from position
            float3 lightPos = scene.lightPositions[i].xyz;
            lightDir = normalize(lightPos - worldPos.xyz);
            
            // Attenuation for point lights
            float distance = length(lightPos - worldPos.xyz);
            float constantAtten = scene.lightParams[i].x;
            float linearAtten = scene.lightParams[i].y;
            float quadraticAtten = scene.lightParams[i].z;
            attenuation = 1.0 / (constantAtten + linearAtten * distance + quadraticAtten * (distance * distance));
        }
        
        // Diffuse lighting
        float diff = max(dot(normal, lightDir), 0.0);
        float3 diffuse = diff * lightColor * intensity;
        
        // Specular lighting (Blinn-Phong) - reduced intensity
        float3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
        float3 specular = spec * lightColor * intensity * 0.2;  // Reduced intensity
        
        lighting += (diffuse + specular) * attenuation;
    }
    
    // Apply lighting to vertex color
    output.Color = float4(input.Color.rgb * lighting, input.Color.a);
    
    // Pass through other attributes
    output.WorldPosition = worldPos.xyz;
    output.Normal = normal;
    output.TexCoord = input.TexCoord;
    
    return output;
}
