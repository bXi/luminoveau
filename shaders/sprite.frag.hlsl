// Sprite Fragment Shader (HLSL)
// Instanced sprite rendering

struct Input
{
    float2 Texcoord : TEXCOORD0;
    float4 Color : TEXCOORD1;
};

struct Output
{
    float4 Color : SV_Target0;
};

Texture2D SpriteTexture : register(t0, space2);
SamplerState SpriteSampler : register(s0, space2);

Output main(Input input)
{
    Output output;
    
    // Sample the texture
    float4 texColor = SpriteTexture.Sample(SpriteSampler, input.Texcoord);
    
    // Apply tint color
    output.Color = texColor * input.Color;
    
    // Discard fully transparent pixels
    if (output.Color.a == 0.0)
    {
        discard;
    }
    
    return output;
}
