// Model 3D Fragment Shader (HLSL)

// Input from vertex shader (interpolated, already lit)
struct PixelInput
{
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float4 Color : TEXCOORD3;
};

// Output structure
struct PixelOutput
{
    float4 Color : SV_Target0;
};

// Texture and sampler (set 2, binding 0 for fragment shaders in SDL_GPU)
Texture2D ModelTexture : register(t0, space2);
SamplerState ModelSampler : register(s0, space2);

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    // Sample texture
    float4 texColor = ModelTexture.Sample(ModelSampler, input.TexCoord);
    
    // Combine texture with pre-computed lighting from vertex shader
    output.Color = texColor * input.Color;
    
    return output;
}
