#version 450

// Vertex inputs
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;

// Storage buffer with scene uniforms
layout(binding = 0, std430) readonly buffer SceneUniforms {
    mat4 viewProj;
    mat4 models[16];        // Array of model matrices
    vec4 cameraPos;
    vec4 ambientLight;
    vec4 lightPositions[4];
    vec4 lightColors[4];
    vec4 lightParams[4];
    int lightCount;
    int modelCount;
} scene;

// Outputs to fragment shader
layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragColor;

void main() {
    // Use gl_InstanceIndex to get the correct model matrix
    mat4 model = scene.models[gl_InstanceIndex];
    
    // Transform position
    vec4 worldPos = model * vec4(inPosition, 1.0);
    gl_Position = scene.viewProj * worldPos;
    
    // Transform normal to world space
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vec3 normal = normalize(normalMatrix * inNormal);
    
    // Compute lighting (Gouraud shading - per vertex)
    vec3 viewDir = normalize(scene.cameraPos.xyz - worldPos.xyz);
    
    // Start with ambient light
    vec3 lighting = scene.ambientLight.rgb * scene.ambientLight.a;
    
    // Add contribution from each light
    for (int i = 0; i < scene.lightCount && i < 4; i++) {
        int lightType = int(scene.lightPositions[i].w);
        vec3 lightColor = scene.lightColors[i].rgb;
        float intensity = scene.lightColors[i].a;
        
        vec3 lightDir;
        float attenuation = 1.0;
        
        if (lightType == 1) {
            // Directional light - use direction as-is (direction toward light source)
            lightDir = normalize(scene.lightPositions[i].xyz);
            attenuation = 1.0; // No attenuation for directional lights
        } else {
            // Point light - calculate direction from position
            vec3 lightPos = scene.lightPositions[i].xyz;
            lightDir = normalize(lightPos - worldPos.xyz);
            
            // Attenuation for point lights
            float distance = length(lightPos - worldPos.xyz);
            float constant = scene.lightParams[i].x;
            float linear = scene.lightParams[i].y;
            float quadratic = scene.lightParams[i].z;
            attenuation = 1.0 / (constant + linear * distance + quadratic * (distance * distance));
        }
        
        // Diffuse lighting
        float diff = max(dot(normal, lightDir), 0.0);
        vec3 diffuse = diff * lightColor * intensity;
        
        // Specular lighting (Blinn-Phong) - reduced intensity
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
        vec3 specular = spec * lightColor * intensity * 0.2;  // Reduced from 0.5 to 0.2
        
        lighting += (diffuse + specular) * attenuation;
    }
    
    // Apply lighting to vertex color
    fragColor = vec4(inColor.rgb * lighting, inColor.a);
    
    // Pass through other attributes
    fragPosition = worldPos.xyz;
    fragNormal = normal;
    fragTexCoord = inTexCoord;
}
