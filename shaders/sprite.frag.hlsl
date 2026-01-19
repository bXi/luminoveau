// Sprite Fragment Shader (HLSL)
// Mixed sprite and SDF text rendering

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

Output main(Input input)
{
    Output output;
    
    if (input.IsSDF != 0)
    {
        // SDF text rendering: distance field stored in alpha channel
        float distance = SpriteTexture.Sample(SpriteSampler, input.Texcoord).a;
        
        // Calculate adaptive smoothing based on screen-space derivatives
        // This makes the edge antialiasing adapt to the current scale/zoom level
        float smoothing = fwidth(distance) * 0.5;
        
        // Use slightly wider threshold (0.515 instead of 0.5) for better small text rendering
        // Reference: https://www.redblobgames.com/x/2403-distance-field-fonts/
        float threshold = 0.515;
        
        // Reconstruct sharp edge using smoothstep
        // Values > threshold = inside glyph, < threshold = outside
        float alpha = smoothstep(threshold - smoothing, threshold + smoothing, distance);
        
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
