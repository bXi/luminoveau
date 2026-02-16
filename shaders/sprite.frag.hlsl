// Sprite Fragment Shader (HLSL)
// Mixed sprite and MSDF text rendering

struct Input
{
    float2 Texcoord : TEXCOORD0;
    float4 Color : TEXCOORD1;
    uint IsSDF : TEXCOORD2;
};

struct Output
{
    float4 Color : SV_Target0;
};

Texture2D SpriteTexture : register(t0, space2);
SamplerState SpriteSampler : register(s0, space2);

// Median function for MSDF
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

Output main(Input input)
{
    Output output;
    
    if (input.IsSDF != 0)
    {
        // MSDF text rendering: multi-channel signed distance field
        float3 msd = SpriteTexture.Sample(SpriteSampler, input.Texcoord).rgb;
        
        // Median of 3 channels gives the signed distance
        float sd = median(msd.r, msd.g, msd.b);
        
        // Compute screen pixel range using Chlumsky's method
        // pxRange (4.0) / atlas texture dimensions gives the distance field range in UV space.
        // Dividing by fwidth(uv) converts that to screen pixels.
        uint texW, texH;
        SpriteTexture.GetDimensions(texW, texH);
        float2 msdfUnit = 4.0 / float2(texW, texH);
        float sigDist = sd - 0.5;
        float screenPxRange = max(dot(msdfUnit, 0.5 / fwidth(input.Texcoord)), 1.0);
        float screenPxDistance = screenPxRange * sigDist;
        float alpha = saturate(screenPxDistance + 0.5);
        
        // Apply tint color with computed alpha
        output.Color = float4(input.Color.rgb, alpha * input.Color.a);
    }
    else
    {
        // Regular sprite rendering: sample texture normally
        float4 texColor = SpriteTexture.Sample(SpriteSampler, input.Texcoord);
        
        // Apply tint color
        output.Color = texColor * input.Color;
    }
    
    // Discard fully transparent pixels
    if (output.Color.a == 0.0)
    {
        discard;
    }
    
    return output;
}
