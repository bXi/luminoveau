// Fullscreen Quad Fragment Shader (HLSL)
// Used for RTT and shader render passes

struct PixelInput
{
    float2 TexCoord : TEXCOORD0;
    float4 TintColor : TEXCOORD1;
};

struct PixelOutput
{
    float4 Color : SV_Target0;
};

Texture2D QuadTexture : register(t0, space2);
SamplerState QuadSampler : register(s0, space2);

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    
    // Sample the texture
    float4 texColor = QuadTexture.Sample(QuadSampler, input.TexCoord);
    
    // Discard fully transparent pixels
    if (texColor.a == 0.0)
    {
        discard;
    }
    
    // Apply tint color
    output.Color = texColor * input.TintColor;
    
    return output;
}
