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
        
        // Calculate adaptive smoothing based on screen-space derivatives
        float smoothing = fwidth(sd) * 0.5;
        
        // Use slightly wider threshold (0.515 instead of 0.5) for better small text rendering
        // Reference: https://www.redblobgames.com/x/2403-distance-field-fonts/
        float threshold = 0.515;
        
        // Reconstruct sharp edge using smoothstep
        float alpha = smoothstep(threshold - smoothing, threshold + smoothing, sd);
        
        // Apply gamma correction (1.5) for more natural font weight
        // Compromise between gamma 1.0 (too thin) and 2.2 (too thick on non-gamma-correct displays)
        const float gamma = 1.5;
        alpha = pow(alpha, 1.0 / gamma);
        
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
