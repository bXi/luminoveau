#version 450

// Vertex stage for the RmlUi render interface (`RenderInterface_Lumi`).
//
// RmlUi hands over its own vertex buffers in `Rml::Vertex` layout — a 2D position, a packed
// RGBA8 colour and a texture coordinate, twenty bytes — so this is a plain transform-and-pass.
// The interesting work is all in the interface's command replay; see rmluirenderinterface.h.
//
// **Layout follows the engine's SPIR-V set convention**: the vertex stage takes its uniforms in
// set 1. Getting this wrong does not fail to compile, it binds nothing and draws the whole
// document at the origin.

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec4 a_color;    // UByte4Norm — already 0..1 by the time it arrives
layout(location = 2) in vec2 a_texCoord;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_texCoord;

layout(set = 1, binding = 0) uniform VertexParams {
    // Projection times RmlUi's own transform, combined on the CPU.
    //
    // **One matrix rather than two.** RmlUi sets a transform through `SetTransform` and expects
    // it applied *under* the projection; multiplying them per draw on the CPU costs nothing at
    // this rate and keeps the shader from having to know which order they compose in.
    mat4 transform;

    // Where this geometry goes, in RmlUi's coordinate space. It arrives per draw call rather
    // than baked into the vertices because RmlUi compiles a mesh once and then renders it at
    // many positions — a scrollbar, a repeated background — and rewriting the buffer each time
    // would defeat the point of compiling it.
    vec2 translation;
    vec2 pad;
} params;

void main() {
    v_color    = a_color;
    v_texCoord = a_texCoord;

    gl_Position = params.transform * vec4(a_position + params.translation, 0.0, 1.0);
}
