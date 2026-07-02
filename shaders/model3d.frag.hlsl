// Model 3D Fragment Shader (HLSL) — per-pixel Phong lighting + directional shadow map.

struct PixelInput
{
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float4 Color : TEXCOORD3;
};

struct PixelOutput
{
    float4 Color : SV_Target0;
};

// Fragment textures/samplers (set 2 in SDL_GPU): model tex (0), directional shadow map (1),
// point-light cube shadow (2).
Texture2D ModelTexture : register(t0, space2);
SamplerState ModelSampler : register(s0, space2);
Texture2D ShadowMap : register(t1, space2);
SamplerState ShadowSampler : register(s1, space2);
TextureCube ShadowCube : register(t2, space2);
SamplerState ShadowCubeSampler : register(s2, space2);

// Lighting inputs (fragment uniform buffers in space3 for SDL_shadercross). Layout must match
// Model3DRenderPass::LightData on the CPU side.
cbuffer LightData : register(b0, space3)
{
    float4x4 shadowViewProj;         // directional caster's view-projection
    float4 cameraPos;
    float4 ambientLight;             // rgb = colour, a = intensity
    float4 lightPositions[4];        // xyz = position/direction, w = type (0=point, 1=directional)
    float4 lightColors[4];           // rgb = colour, a = intensity
    float4 lightParams[4];           // x=constant, y=linear, z=quadratic (point-light attenuation)
    float4 pointLightPosFar;         // xyz = point caster world pos, w = far range
    int    lightCount;
    int    shadowLight;              // directional caster index, or -1
    int    pointShadowLight;         // point (cube) caster index, or -1
    int    _pad;
};

// 0 = shadowed, 1 = lit. Samples the point-light distance cube by direction and compares distances.
// Uses normal-offset bias: the receiver point is pushed along the surface normal instead of biasing
// depth, so surfaces don't self-shadow (no acne) yet shadows stay attached (no peter-panning).
float PointShadowFactor(float3 worldPos, float3 N, float normalOffset)
{
    float3 p = worldPos + N * normalOffset;
    float3 toFrag = p - pointLightPosFar.xyz;
    float dist = length(toFrag) / pointLightPosFar.w;

    // 3x3 PCF: offset the sample direction within the plane perpendicular to it (tangent basis),
    // scaled by distance so softness stays roughly constant. Softens the cube-face stair-stepping.
    float3 dir = normalize(toFrag);
    float3 up  = (abs(dir.y) < 0.99) ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 t   = normalize(cross(up, dir));
    float3 b   = cross(dir, t);
    // Spread ≈ a few cube-map texels in angular terms (90° face over kCubeShadowRes texels). Too
    // large and the taps cross into different directions/faces and read as several separate shadows.
    float spread = 0.0010 * length(toFrag);

    // 4x4 PCF for a smooth penumbra (16 taps).
    float lit = 0.0;
    [unroll] for (int x = 0; x < 4; x++)
    [unroll] for (int y = 0; y < 4; y++)
    {
        float2 o = float2(x, y) - 1.5;               // -1.5,-0.5,0.5,1.5
        float3 s = toFrag + (t * o.x + b * o.y) * spread;
        float stored = ShadowCube.Sample(ShadowCubeSampler, s).r;
        lit += (dist > stored) ? 0.0 : 1.0;
    }
    return lit / 16.0;
}

// 0 = fully shadowed, 1 = fully lit. Directional shadow map lookup with normal-offset bias: the
// receiver point is pushed along the surface normal so surfaces don't self-shadow (no acne) and the
// shadow stays attached to the occluder (no peter-panning). Near-zero depth bias.
float ShadowFactor(float3 worldPos, float3 N, float normalOffset)
{
    float3 p = worldPos + N * normalOffset;
    float4 lp = mul(shadowViewProj, float4(p, 1.0));
    float3 ndc = lp.xyz / lp.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;                            // NDC y-up -> texture y-down

    // Outside the shadow frustum: treat as lit.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;

    float current = ndc.z;   // already [0,1] (zero-to-one light ortho)

    // Bilinear PCF: compare 2x2 texels then interpolate the RESULTS by the fractional texel
    // position — anti-aliases within a texel (removes the stair-stepped corners). Repeated over a
    // 3x3 grid for a soft penumbra. res must match kShadowRes (4096).
    const float res   = 8192.0;   // must match kShadowRes
    const float texel = 1.0 / res;
    const float bias  = 0.0005;

    const float kernel = 0.9;   // penumbra tightness (texels between taps; lower = tighter/sharper)
    float lit = 0.0;
    [unroll] for (int kx = -1; kx <= 1; kx++)
    [unroll] for (int ky = -1; ky <= 1; ky++)
    {
        float2 suv  = uv + float2(kx, ky) * texel * kernel;
        float2 tc   = suv * res - 0.5;
        float2 fpart = frac(tc);
        float2 base = (floor(tc) + 0.5) * texel;

        float s00 = (current - bias > ShadowMap.Sample(ShadowSampler, base).r) ? 0.0 : 1.0;
        float s10 = (current - bias > ShadowMap.Sample(ShadowSampler, base + float2(texel, 0)).r) ? 0.0 : 1.0;
        float s01 = (current - bias > ShadowMap.Sample(ShadowSampler, base + float2(0, texel)).r) ? 0.0 : 1.0;
        float s11 = (current - bias > ShadowMap.Sample(ShadowSampler, base + float2(texel, texel)).r) ? 0.0 : 1.0;

        lit += lerp(lerp(s00, s10, fpart.x), lerp(s01, s11, fpart.x), fpart.y);
    }
    return lit / 9.0;
}

PixelOutput main(PixelInput input)
{
    PixelOutput output;

    float3 N = normalize(input.Normal);
    float3 viewDir = normalize(cameraPos.xyz - input.WorldPosition);

    // Directional shadow with normal-offset bias (world units along the surface normal).
    float shadow = 1.0;
    if (shadowLight >= 0)
        shadow = ShadowFactor(input.WorldPosition, N, 0.06);

    // Point-light (cube) shadow with normal-offset bias (world units — pushes the receiver ~a
    // couple shadow-map texels off the surface). Kills self-shadow acne without detaching shadows.
    float pointShadow = 1.0;
    if (pointShadowLight >= 0)
        pointShadow = PointShadowFactor(input.WorldPosition, N, 0.08);

    float3 lighting = ambientLight.rgb * ambientLight.a;

    for (int i = 0; i < lightCount && i < 4; i++)
    {
        int lightType = (int)lightPositions[i].w;
        float3 lightColor = lightColors[i].rgb;
        float intensity = lightColors[i].a;

        float3 lightDir;
        float attenuation = 1.0;

        if (lightType == 1)
        {
            lightDir = normalize(lightPositions[i].xyz);
        }
        else
        {
            float3 lightPos = lightPositions[i].xyz;
            lightDir = normalize(lightPos - input.WorldPosition);
            float dist = length(lightPos - input.WorldPosition);
            attenuation = 1.0 / (lightParams[i].x + lightParams[i].y * dist + lightParams[i].z * dist * dist);
        }

        float diff = max(dot(N, lightDir), 0.0);
        float3 diffuse = diff * lightColor * intensity;

        float3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(N, halfwayDir), 0.0), 32.0);
        float3 specular = spec * lightColor * intensity * 0.2;

        // Apply the relevant shadow to each caster light.
        float s = 1.0;
        if (i == shadowLight)      s *= shadow;
        if (i == pointShadowLight) s *= pointShadow;
        lighting += (diffuse + specular) * attenuation * s;
    }

    float4 texColor = ModelTexture.Sample(ModelSampler, input.TexCoord);
    output.Color = float4(texColor.rgb * input.Color.rgb * lighting, texColor.a * input.Color.a);
    return output;
}
