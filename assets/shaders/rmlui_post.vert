#version 450

// One triangle covering the target, for RmlUi's layer copies and filters. No vertex buffer.

layout(location = 0) out vec2 v_texCoord;

layout(set = 1, binding = 0) uniform PostVertexParams {
    vec4 uvTransform; // xy scale, zw offset, applied to the 0..1 screen UV
} params;

void main() {
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    vec2 uv       = vec2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);

    v_texCoord  = uv * params.uvTransform.xy + params.uvTransform.zw;
    gl_Position = vec4(position, 0.0, 1.0);
}
