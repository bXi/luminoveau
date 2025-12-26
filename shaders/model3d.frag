#version 450

// Input from vertex shader (interpolated, already lit)
layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragColor;

// Texture sampler (set 2 for fragment shaders in SDL_GPU SPIR-V)
layout(set = 2, binding = 0) uniform sampler2D texSampler;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    // Sample texture
    vec4 texColor = texture(texSampler, fragTexCoord);
    
    // Combine texture with pre-computed lighting from vertex shader
    outColor = texColor * fragColor;
}
