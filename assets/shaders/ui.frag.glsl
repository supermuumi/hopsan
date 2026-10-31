#version 450
// Samples a single-channel R8 atlas (font glyphs + a white sentinel pixel
// for untextured quads). The sampled value is the per-pixel alpha; the
// per-vertex color provides RGB + base alpha.

layout(set = 2, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 out_color;

void main() {
    float a = texture(u_atlas, in_uv).r;
    out_color = vec4(in_color.rgb, in_color.a * a);
}
