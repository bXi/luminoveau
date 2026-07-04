#version 450

// A generative plasma. Ignores the source image and computes colour from the fullscreen
// tex coordinates plus animated time + mouse uniforms — a "ShaderToy" in miniature.
//
// NOTE: SDL_shadercross's SPIRV binding convention for fragment shaders puts sampled textures in
// descriptor set 2 and uniform buffers in set 3. Using set 0 leaves the uniform block unbound
// (reads garbage), so the effect shows nothing. Always use set=2 / set=3 for effect fragments.

layout(set = 2, binding = 0) uniform sampler2D input_texture;   // unused here, part of the effect ABI

layout(location = 0) in vec2 tex_coords;
layout(location = 0) out vec4 out_color;

layout(set = 3, binding = 0) uniform EffectParams {
    float time;
    vec2  mouse;   // normalised cursor position (0..1)
} params;

void main() {
    vec2 uv = tex_coords;
    float t = params.time;

    float v = sin(uv.x * 10.0 + t)
            + sin(uv.y * 10.0 + t * 1.3)
            + sin((uv.x + uv.y) * 10.0 + t * 0.7)
            + sin(length(uv - params.mouse) * 18.0 - t * 2.0);
    v *= 0.25;

    vec3 col = 0.5 + 0.5 * vec3(sin(v * 3.14159 + 0.0),
                                sin(v * 3.14159 + 2.094),
                                sin(v * 3.14159 + 4.188));
    out_color = vec4(col, 1.0);
}
