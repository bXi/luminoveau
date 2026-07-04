#version 450

// Desaturates the framebuffer toward luminance.

layout(set = 2, binding = 0) uniform sampler2D input_texture;   // SDL_shadercross: frag samplers in set 2

layout(location = 0) in vec2 tex_coords;
layout(location = 0) out vec4 out_color;

layout(set = 3, binding = 0) uniform EffectParams {   // SDL_shadercross: frag uniform buffers in set 3
    float amount;   // 0 = original, 1 = fully grey
} params;

void main() {
    vec4 c = texture(input_texture, tex_coords);
    float g = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    out_color = vec4(mix(c.rgb, vec3(g), params.amount), c.a);
}
