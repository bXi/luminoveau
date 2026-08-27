#version 450

// Fragment stage for the RmlUi render interface (`RenderInterface_Lumi`).
//
// Vertex colour times the bound texture, and nothing else. RmlUi does its own blending decisions
// through the pipeline's blend state, and everything it draws — panels, borders, text glyphs from
// its font atlas — is that same multiply.
//
// **Untextured geometry binds a 1x1 white texel rather than taking a second pipeline.** A
// document alternates between textured and untextured elements constantly (text over a panel),
// and a colour-only pipeline would mean a pipeline switch at every alternation. Multiplying by
// white is free by comparison. See the note on `_whiteTexture`.
//
// Engine SPIR-V set convention: the fragment stage takes samplers in set 2.

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_texCoord;

layout(set = 2, binding = 0) uniform sampler2D u_texture;

layout(location = 0) out vec4 o_color;

void main() {
    o_color = v_color * texture(u_texture, v_texCoord);
}
