struct SceneUniforms {
    viewProj       : mat4x4<f32>,
    models         : array<mat4x4<f32>, 16>,
    cameraPos      : vec4<f32>,
    ambientLight   : vec4<f32>,
    lightPositions : array<vec4<f32>, 4>,
    lightColors    : array<vec4<f32>, 4>,
    lightParams    : array<vec4<f32>, 4>,
    lightCount     : i32,
    modelCount     : i32,
    _pad           : vec2<i32>,
}

@group(3) @binding(0) var<storage, read> sceneData : array<SceneUniforms>;

struct VertIn {
    @location(0) position : vec3<f32>,
    @location(1) normal   : vec3<f32>,
    @location(2) texCoord : vec2<f32>,
    @location(3) color    : vec4<f32>,
}

struct VertOut {
    @builtin(position) position     : vec4<f32>,
    @location(0)       worldPos     : vec3<f32>,
    @location(1)       normal       : vec3<f32>,
    @location(2)       texCoord     : vec2<f32>,
    @location(3)       color        : vec4<f32>,
}

@vertex
fn vs_main(in : VertIn, @builtin(instance_index) instanceIndex : u32) -> VertOut {
    let scene = sceneData[0];
    let model = scene.models[instanceIndex];

    let worldPos = model * vec4<f32>(in.position, 1.0);
    let clipPos  = scene.viewProj * worldPos;

    let m3 = mat3x3<f32>(model[0].xyz, model[1].xyz, model[2].xyz);
    let normalMatrix = transpose(m3);
    let worldNormal  = normalize(normalMatrix * in.normal);

    let viewDir = normalize(scene.cameraPos.xyz - worldPos.xyz);
    var lighting = scene.ambientLight.rgb * scene.ambientLight.a;

    for (var i : i32 = 0; i < scene.lightCount && i < 4; i++) {
        let lightType  = i32(scene.lightPositions[i].w);
        let lightColor = scene.lightColors[i].rgb;
        let intensity  = scene.lightColors[i].a;

        var lightDir    : vec3<f32>;
        var attenuation : f32 = 1.0;

        if lightType == 1 {
            lightDir = normalize(scene.lightPositions[i].xyz);
        } else {
            let lightPos = scene.lightPositions[i].xyz;
            lightDir = normalize(lightPos - worldPos.xyz);
            let dist    = length(lightPos - worldPos.xyz);
            let cA      = scene.lightParams[i].x;
            let lA      = scene.lightParams[i].y;
            let qA      = scene.lightParams[i].z;
            attenuation = 1.0 / (cA + lA * dist + qA * dist * dist);
        }

        let diff     = max(dot(worldNormal, lightDir), 0.0);
        let diffuse  = diff * lightColor * intensity;

        let halfDir  = normalize(lightDir + viewDir);
        let spec     = pow(max(dot(worldNormal, halfDir), 0.0), 32.0);
        let specular = spec * lightColor * intensity * 0.2;

        lighting += (diffuse + specular) * attenuation;
    }

    var out : VertOut;
    out.position = clipPos;
    out.worldPos = worldPos.xyz;
    out.normal   = worldNormal;
    out.texCoord = in.texCoord;
    out.color    = vec4<f32>(in.color.rgb * lighting, in.color.a);
    return out;
}
