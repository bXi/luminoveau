// Shadow depth fragment shader (HLSL).
// Writes the interpolated clip-space depth into an R32F shadow texture. A depth buffer bound to
// this pass resolves the nearest occluder, so the R channel ends up holding the closest surface's
// depth as seen from the light.

struct PixelInput
{
    float Depth : TEXCOORD0;
};

struct PixelOutput
{
    float4 Color : SV_Target0;
};

PixelOutput main(PixelInput input)
{
    PixelOutput output;
    output.Color = float4(input.Depth, 0.0, 0.0, 1.0);
    return output;
}
