#version 450

// Every RmlUi layer operation in one shader, picked by `mode`. Colours are premultiplied throughout.

#define MODE_COPY 0         // times `factor`, which is how the opacity filter is done
#define MODE_COLOR_MATRIX 1
#define MODE_MASK 2         // times the mask texture's alpha
#define MODE_BLUR 3         // one direction of a separable gaussian
#define MODE_SHADOW 4       // source alpha as `color`, zero outside `region`

layout(location = 0) in vec2 v_texCoord;

layout(set = 2, binding = 0) uniform sampler2D u_source;
layout(set = 2, binding = 1) uniform sampler2D u_mask;

layout(set = 3, binding = 0) uniform PostParams {
    mat4  colorMatrix;
    vec4  color;
    vec4  region;    // uv min.xy, max.xy; blur clamps to it, shadow cuts off at it
    vec2  texelStep; // uv distance between blur taps
    float factor;
    float sigma;     // in taps
    int   mode;
    int   taps;      // each side of the centre
    vec2  pad;
} params;

layout(location = 0) out vec4 o_color;

void main() {
    int mode = params.mode;

    if (mode == MODE_COLOR_MATRIX) {
        vec4 c  = texture(u_source, v_texCoord);
        o_color = vec4(vec3(params.colorMatrix * c), c.a);
    } else if (mode == MODE_MASK) {
        o_color = texture(u_source, v_texCoord) * texture(u_mask, v_texCoord).a;
    } else if (mode == MODE_BLUR) {
        vec4  sum    = vec4(0.0);
        float weight = 0.0;
        float k      = -0.5 / (params.sigma * params.sigma);
        for (int i = -params.taps; i <= params.taps; i++) {
            float w  = exp(k * float(i * i));
            vec2  uv = clamp(v_texCoord + params.texelStep * float(i), params.region.xy, params.region.zw);
            sum     += texture(u_source, uv) * w;
            weight  += w;
        }
        o_color = sum / weight;
    } else if (mode == MODE_SHADOW) {
        vec2 inside = step(params.region.xy, v_texCoord) * step(v_texCoord, params.region.zw);
        o_color     = texture(u_source, v_texCoord).a * inside.x * inside.y * params.color;
    } else {
        o_color = texture(u_source, v_texCoord) * params.factor;
    }
}
