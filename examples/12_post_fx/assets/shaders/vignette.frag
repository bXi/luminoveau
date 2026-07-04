#version 450

// Darkens the corners for a lens-vignette look.

layout(set = 2, binding = 0) uniform sampler2D input_texture;   // SDL_shadercross: frag samplers in set 2

layout(location = 0) in vec2 tex_coords;
layout(location = 0) out vec4 out_color;

layout(set = 3, binding = 0) uniform EffectParams {   // SDL_shadercross: frag uniform buffers in set 3
    float strength;
} params;

void main() {
    vec4 c = texture(input_texture, tex_coords);
    vec2 d = tex_coords - 0.5;
    float v = smoothstep(0.85, 0.25, length(d) * params.strength);
    out_color = vec4(c.rgb * v, c.a);
}
