#version 450

// Horizontal wobble — offsets each row's sample by an animated sine.

layout(set = 2, binding = 0) uniform sampler2D input_texture;   // SDL_shadercross: frag samplers in set 2

layout(location = 0) in vec2 tex_coords;
layout(location = 0) out vec4 out_color;

layout(set = 3, binding = 0) uniform EffectParams {   // SDL_shadercross: frag uniform buffers in set 3
    float time;
    float strength;
} params;

void main() {
    vec2 uv = tex_coords;
    uv.x += sin(uv.y * 20.0 + params.time * 3.0) * params.strength;
    out_color = texture(input_texture, uv);
}
