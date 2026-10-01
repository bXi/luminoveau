#version 450

// Linear, radial and conic gradients for RmlUi's gradient decorators, ported from RmlUi's GL3 renderer.

#define LINEAR 0
#define RADIAL 1
#define CONIC 2
#define REPEATING_LINEAR 3
#define REPEATING_RADIAL 4
#define REPEATING_CONIC 5
#define PI 3.14159265

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_texCoord;

layout(set = 3, binding = 0) uniform GradientParams {
    vec4  stopColors[16];    // premultiplied
    vec4  stopPositions[4];  // sixteen floats, packed four to a vec4 for std140
    vec2  p;                 // linear: start, radial/conic: centre
    vec2  v;                 // linear: start to end, radial: inverse radius, conic: angle as a unit vector
    int   func;
    int   numStops;
    vec2  pad;
} params;

layout(location = 0) out vec4 o_color;

float stopPosition(int i) { return params.stopPositions[i / 4][i % 4]; }

vec4 mixStops(float t) {
    vec4 color = params.stopColors[0];
    for (int i = 1; i < params.numStops; i++) {
        color = mix(color, params.stopColors[i], smoothstep(stopPosition(i - 1), stopPosition(i), t));
    }
    return color;
}

void main() {
    int   func = params.func;
    float t    = 0.0;

    if (func == LINEAR || func == REPEATING_LINEAR) {
        vec2 d = v_texCoord - params.p;
        t      = dot(params.v, d) / dot(params.v, params.v);
    } else if (func == RADIAL || func == REPEATING_RADIAL) {
        t = length(params.v * (v_texCoord - params.p));
    } else {
        mat2 r = mat2(params.v.x, -params.v.y, params.v.y, params.v.x);
        vec2 d = r * (v_texCoord - params.p);
        t      = 0.5 + atan(-d.x, d.y) / (2.0 * PI);
    }

    if (func >= REPEATING_LINEAR) {
        float t0 = stopPosition(0);
        float t1 = stopPosition(params.numStops - 1);
        t        = t0 + mod(t - t0, t1 - t0);
    }

    o_color = v_color * mixStops(t);
}
