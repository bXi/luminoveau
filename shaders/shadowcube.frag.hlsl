// Point-light cube shadow — depth fragment shader (HLSL).
// Stores the linear distance from the light to this fragment, normalized by the light's far range,
// into R32F. Sampling the cube by direction and comparing distances gives omnidirectional shadows.

// Fragment uniforms live in space3 for SDL_shadercross.
cbuffer CubeShadowFragParams : register(b0, space3)
{
    float4 lightPosFar;   // xyz = light world position, w = far range
};

struct PixelInput
{
    float3 WorldPos : TEXCOORD0;
};

struct PixelOutput
{
    float4 Color : SV_Target0;
};

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    float dist = length(input.WorldPos - lightPosFar.xyz) / lightPosFar.w;
    output.Color = float4(dist, 0.0, 0.0, 1.0);
    return output;
}
