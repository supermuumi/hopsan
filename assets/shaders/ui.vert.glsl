#version 450
// Screen-space UI vertex shader. One pipeline for textured (font glyphs)
// and untextured (panel / bar) quads alike -- untextured quads sample a
// known-white pixel of the font atlas, so the shader treats both the
// same way.

layout(set = 1, binding = 0) uniform Screen {
    vec2 inv_size;   // (1 / window_w, 1 / window_h)
} u_screen;

layout(location = 0) in vec2 in_pos;     // screen-space pixels
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;   // 0..1 RGBA

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;

void main() {
    vec2 ndc = in_pos * u_screen.inv_size * 2.0 - 1.0;
    ndc.y = -ndc.y;   // pixel Y goes down; NDC Y goes up
    gl_Position = vec4(ndc, 0.0, 1.0);
    out_uv = in_uv;
    out_color = in_color;
}
