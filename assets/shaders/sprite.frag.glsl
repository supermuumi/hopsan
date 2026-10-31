#version 450
// Textured screen-space quads (sprite atlases). Shares ui.vert. Samples an
// RGBA texture and multiplies by the per-vertex colour (tint + alpha).

layout(set = 2, binding = 0) uniform sampler2D u_tex;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(u_tex, in_uv) * in_color;
}
