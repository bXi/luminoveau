@group(2) @binding(0) var gSampler : sampler;
@group(2) @binding(1) var gTexture : texture_2d<f32>;

// Lighting inputs (fragment uniform block). Layout must match Model3DRenderPass::LightData.
struct LightData {
    cameraPos      : vec4<f32>,
    ambientLight   : vec4<f32>,        // rgb = colour, a = intensity
    lightPositions : array<vec4<f32>, 4>,  // xyz = position/direction, w = type (0=point, 1=directional)
    lightColors    : array<vec4<f32>, 4>,  // rgb = colour, a = intensity
    lightParams    : array<vec4<f32>, 4>,  // x=constant, y=linear, z=quadratic
    lightCount     : i32,
    _pad           : vec3<i32>,
}
@group(3) @binding(0) var<uniform> lights : LightData;

struct FragIn {
    @location(0) worldPos : vec3<f32>,
    @location(1) normal   : vec3<f32>,
    @location(2) texCoord : vec2<f32>,
    @location(3) color    : vec4<f32>,
}

@fragment
fn fs_main(in : FragIn) -> @location(0) vec4<f32> {
    let N = normalize(in.normal);
    let viewDir = normalize(lights.cameraPos.xyz - in.worldPos);

    var lighting = lights.ambientLight.rgb * lights.ambientLight.a;

    for (var i : i32 = 0; i < lights.lightCount && i < 4; i++) {
        let lightType  = i32(lights.lightPositions[i].w);
        let lightColor = lights.lightColors[i].rgb;
        let intensity  = lights.lightColors[i].a;

        var lightDir    : vec3<f32>;
        var attenuation : f32 = 1.0;

        if lightType == 1 {
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

        lighting += (diffuse + specular) * attenuation;
    }

    let texColor = textureSample(gTexture, gSampler, in.texCoord);
    return vec4<f32>(texColor.rgb * in.color.rgb * lighting, texColor.a * in.color.a);
}
