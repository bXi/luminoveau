#version 450

// Input from vertex shader (interpolated, already lit)
layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragColor;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    // Use pre-computed lighting from vertex shader (Gouraud shading)
    outColor = fragColor;
}
