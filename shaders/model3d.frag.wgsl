// Model 3D Fragment Shader (WGSL) — per-pixel Phong lighting + directional & point (cube) shadows.
// Mirror of model3d.frag.hlsl. Shadow lookups use textureSampleLevel (no derivatives, LOD 0) so
// they're valid in WGSL's non-uniform control flow (after the frustum-bounds early return).

// Fragment textures/samplers (group 2): model tex (0/1), directional shadow map (2/3),
// point-light distance cube (4/5). Even binding = sampler, odd = texture (backend pair layout).
@group(2) @binding(0) var gSampler          : sampler;
@group(2) @binding(1) var gTexture          : texture_2d<f32>;
@group(2) @binding(2) var shadowSampler      : sampler;
@group(2) @binding(3) var shadowMap          : texture_2d<f32>;
@group(2) @binding(4) var shadowCubeSampler  : sampler;
@group(2) @binding(5) var shadowCube         : texture_cube<f32>;

// Lighting inputs (fragment uniform block). Layout must match Model3DRenderPass::LightData.
struct LightData {
    shadowViewProj   : mat4x4<f32>,        // directional caster's view-projection
    cameraPos        : vec4<f32>,
    ambientLight     : vec4<f32>,          // rgb = colour, a = intensity
    lightPositions   : array<vec4<f32>, 4>,// xyz = position/direction, w = type (0=point, 1=directional)
    lightColors      : array<vec4<f32>, 4>,// rgb = colour, a = intensity
    lightParams      : array<vec4<f32>, 4>,// x=constant, y=linear, z=quadratic
    pointLightPosFar : vec4<f32>,          // xyz = point caster world pos, w = far range
    lightCount       : i32,
    shadowLight      : i32,                // directional caster index, or -1
    pointShadowLight : i32,                // point (cube) caster index, or -1
    _pad             : i32,
}
@group(1) @binding(0) var<uniform> lights : LightData;

struct FragIn {
    @location(0) worldPos : vec3<f32>,
    @location(1) normal   : vec3<f32>,
    @location(2) texCoord : vec2<f32>,
    @location(3) color    : vec4<f32>,
}

// 0 = shadowed, 1 = lit. Samples the point-light distance cube by direction and compares distances.
// Normal-offset bias: push the receiver along the surface normal instead of biasing depth.
fn PointShadowFactor(worldPos : vec3<f32>, N : vec3<f32>, normalOffset : f32) -> f32 {
    let p      = worldPos + N * normalOffset;
    let toFrag = p - lights.pointLightPosFar.xyz;
    let dist   = length(toFrag) / lights.pointLightPosFar.w;

    // Tangent basis around the sample direction for angular PCF offsets.
    let dir = normalize(toFrag);
    var up  = vec3<f32>(0.0, 1.0, 0.0);
    if (abs(dir.y) >= 0.99) { up = vec3<f32>(1.0, 0.0, 0.0); }
    let t = normalize(cross(up, dir));
    let b = cross(dir, t);
    // Spread ≈ a few cube-map texels; too large and taps cross faces and read as several shadows.
    let spread = 0.0010 * length(toFrag);

    var lit = 0.0;
    for (var x : i32 = 0; x < 4; x++) {
        for (var y : i32 = 0; y < 4; y++) {
            let o = vec2<f32>(f32(x), f32(y)) - 1.5;           // -1.5,-0.5,0.5,1.5
            let s = toFrag + (t * o.x + b * o.y) * spread;
            let stored = textureSampleLevel(shadowCube, shadowCubeSampler, s, 0.0).r;
            lit += select(1.0, 0.0, dist > stored);
        }
    }
    return lit / 16.0;
}

// 0 = fully shadowed, 1 = fully lit. Directional shadow map lookup with normal-offset bias +
// bilinear PCF over a 3x3 grid. res must match kShadowRes (8192).
fn ShadowFactor(worldPos : vec3<f32>, N : vec3<f32>, normalOffset : f32) -> f32 {
    let p   = worldPos + N * normalOffset;
    let lp  = lights.shadowViewProj * vec4<f32>(p, 1.0);
    let ndc = lp.xyz / lp.w;
    var uv  = ndc.xy * 0.5 + 0.5;
    uv.y    = 1.0 - uv.y;                          // NDC y-up -> texture y-down

    // Outside the shadow frustum: treat as lit.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }

    let current = ndc.z;   // already [0,1] (zero-to-one light ortho)

    let res    = 8192.0;   // must match kShadowRes
    let texel  = 1.0 / res;
    let bias   = 0.0005;
    let kernel = 0.9;

    var lit = 0.0;
    for (var kx : i32 = -1; kx <= 1; kx++) {
        for (var ky : i32 = -1; ky <= 1; ky++) {
            let suv   = uv + vec2<f32>(f32(kx), f32(ky)) * texel * kernel;
            let tc    = suv * res - 0.5;
            let fpart = fract(tc);
            let base  = (floor(tc) + 0.5) * texel;

            let s00 = select(1.0, 0.0, current - bias > textureSampleLevel(shadowMap, shadowSampler, base, 0.0).r);
            let s10 = select(1.0, 0.0, current - bias > textureSampleLevel(shadowMap, shadowSampler, base + vec2<f32>(texel, 0.0), 0.0).r);
            let s01 = select(1.0, 0.0, current - bias > textureSampleLevel(shadowMap, shadowSampler, base + vec2<f32>(0.0, texel), 0.0).r);
            let s11 = select(1.0, 0.0, current - bias > textureSampleLevel(shadowMap, shadowSampler, base + vec2<f32>(texel, texel), 0.0).r);

            lit += mix(mix(s00, s10, fpart.x), mix(s01, s11, fpart.x), fpart.y);
        }
    }
    return lit / 9.0;
}

@fragment
fn fs_main(in : FragIn) -> @location(0) vec4<f32> {
    let N = normalize(in.normal);
    let viewDir = normalize(lights.cameraPos.xyz - in.worldPos);

    var shadow = 1.0;
    if (lights.shadowLight >= 0) {
        shadow = ShadowFactor(in.worldPos, N, 0.06);
    }
    var pointShadow = 1.0;
    if (lights.pointShadowLight >= 0) {
        pointShadow = PointShadowFactor(in.worldPos, N, 0.08);
    }

    var lighting = lights.ambientLight.rgb * lights.ambientLight.a;

    for (var i : i32 = 0; i < lights.lightCount && i < 4; i++) {
        let lightType  = i32(lights.lightPositions[i].w);
        let lightColor = lights.lightColors[i].rgb;
        let intensity  = lights.lightColors[i].a;

        var lightDir    : vec3<f32>;
        var attenuation : f32 = 1.0;

        if (lightType == 1) {
            lightDir = normalize(lights.lightPositions[i].xyz);
        } else {
            let lightPos = lights.lightPositions[i].xyz;
            lightDir = normalize(lightPos - in.worldPos);
            let dist = length(lightPos - in.worldPos);
            let p    = lights.lightParams[i];
            attenuation = 1.0 / (p.x + p.y * dist + p.z * dist * dist);
        }

        let diff     = max(dot(N, lightDir), 0.0);
        let diffuse  = diff * lightColor * intensity;

        let halfDir  = normalize(lightDir + viewDir);
        let spec     = pow(max(dot(N, halfDir), 0.0), 32.0);
        let specular = spec * lightColor * intensity * 0.2;

        // Apply the relevant shadow to each caster light.
        var s = 1.0;
        if (i == lights.shadowLight)      { s = s * shadow; }
        if (i == lights.pointShadowLight) { s = s * pointShadow; }
        lighting += (diffuse + specular) * attenuation * s;
    }

    let texColor = textureSample(gTexture, gSampler, in.texCoord);
    return vec4<f32>(texColor.rgb * in.color.rgb * lighting, texColor.a * in.color.a);
}
