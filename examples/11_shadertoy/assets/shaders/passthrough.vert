#version 450

// Fullscreen-quad vertex shader that generates its positions from the vertex index instead of
// reading a vertex buffer. With the effect's index buffer (0,1,2,2,1,3) this yields the four
// corners (0,0),(1,0),(0,1),(1,1) as two triangles — robust against any vertex-input mismatch.

layout(location = 0) in vec2 in_position;   // unused, kept so the pipeline's vertex layout matches
layout(location = 1) in vec2 in_texcoord;   // unused

layout(location = 0) out vec2 out_texcoord;

void main() {
    vec2 pos = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    gl_Position  = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    // Flip Y so tex coords are 0 at the top (screen space), matching normalized mouse coords and
    // the framebuffer's row order.
    out_texcoord = vec2(pos.x, 1.0 - pos.y);
}
