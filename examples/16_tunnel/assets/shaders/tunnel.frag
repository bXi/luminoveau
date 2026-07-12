#version 450

// GPU tunnel: each pixel is converted to (angle, depth) polar coordinates, a periodic-sine
// checkerboard is sampled there, and depth shading darkens the far throat — the classic demoscene
// tunnel, running per-pixel on the GPU so it costs nothing to run at full resolution.
//
// Effect ABI (see 11_shadertoy): sampled textures live in descriptor set 2, uniform buffers in
// set 3. Using set 0 leaves the block unbound and the effect renders nothing.

layout(set = 2, binding = 0) uniform sampler2D input_texture;   // unused, part of the effect ABI

layout(location = 0) in vec2 tex_coords;
layout(location = 0) out vec4 out_color;

layout(set = 3, binding = 0) uniform EffectParams {
    float time;
    vec2  resolution;   // framebuffer size in pixels (for aspect correction)
    float steer;        // -1 (mouse full left) .. +1 (full right); curves the tunnel toward it
} params;

const float PI = 3.14159265;

vec3 hsv(float h, float v) {
    h = fract(h);
    vec3 c = clamp(vec3(abs(h * 6.0 - 3.0) - 1.0,
                        2.0 - abs(h * 6.0 - 2.0),
                        2.0 - abs(h * 6.0 - 4.0)), 0.0, 1.0);
    return c * v;
}

void main() {
    float t = params.time;

    // Centre + aspect-correct so the tunnel is round, not oval.
    vec2 uv = tex_coords - 0.5;
    uv.x *= params.resolution.x / params.resolution.y;

    // Steering — model an ACTUAL curved tube and RAY-MARCH it. We are inside a hollow cylinder of
    // radius rt whose centreline curves sideways: cx(Z) = m*(sqrt(Z^2 + b^2) - b). That bends
    // quadratically up close then STRAIGHTENS to a constant slope m far away, so the tube heads off at
    // a fixed angle (never coils, never runs to infinity). A ray through pixel s=uv starts inside
    // (radial ~0) and we walk Z forward until it crosses the wall (radial = rt) — the FIRST crossing
    // is the visible wall. Because the tube encloses the camera, every ray hits wall: no black voids,
    // no throat silhouette artifacts, and it draws as far down the bend as needed. (All the earlier
    // uv-warp / Newton attempts distorted rings or left holes; a march is robust by construction.)
    const float rt   = 0.15;                   // tube radius
    const float bpar = 3.0;                    // how gradually the bend straightens out
    float m = params.steer * 0.55;             // final slope of the centreline (steer -> curve amount)
    vec2  s = uv;

    #define CX(Z)     (m * (sqrt((Z)*(Z) + bpar*bpar) - bpar))
    #define RADIAL(Z) length(vec2(s.x*(Z) - CX(Z), s.y*(Z)))

    float Zfar = 40.0;
    float Zhit = -1.0, Zprev = 0.0, gPrev = rt;   // rt - radial(0) = rt > 0 (inside)
    for (int i = 1; i <= 96; i++) {
        float Z = Zfar * float(i) / 96.0;
        float g = rt - RADIAL(Z);              // >0 inside the tube, <0 past the wall
        if (g < 0.0 && gPrev >= 0.0) {         // crossed the wall between Zprev..Z -> bisect to refine
            float a = Zprev, bb = Z;
            for (int j = 0; j < 8; j++) {
                float mid = 0.5 * (a + bb);
                if (rt - RADIAL(mid) < 0.0) bb = mid; else a = mid;
            }
            Zhit = 0.5 * (a + bb);
            break;
        }
        Zprev = Z; gPrev = g;
    }

    if (Zhit < 0.0) { out_color = vec4(0.0, 0.0, 0.0, 1.0); return; }   // ray never reached a wall

    // Angle around the tube at the hit, and effective screen radius for depth shading.
    float px    = s.x * Zhit - CX(Zhit);
    float py    = s.y * Zhit;
    float angle = atan(py, px);
    float dist  = rt / Zhit;                    // bright near, dark far

    float u = angle / PI;              // around the tunnel (-1..1)
    float v = Zhit + t * 1.5;          // depth, scrolling toward the viewer

    // Checkerboard from periodic sines so the angular bands are seamless across the atan seam. The
    // angular term spins with time; the depth term is tighter now so the checkers are shorter.
    bool chk    = (sin(angle * 8.0 + t * 6.0) > 0.0) != (sin(v * 9.0) > 0.0);
    float tone  = chk ? 1.0 : 0.55;
    float shade = clamp(dist / 0.6, 0.0, 1.0);   // throat dark, walls bright

    float hue = v * 0.05 + u * 0.5 + t * 0.08;
    out_color = vec4(hsv(hue, tone * shade), 1.0);
}
