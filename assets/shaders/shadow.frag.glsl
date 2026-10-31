#version 450
// Depth-only fragment stage for the shadow pass (#171). No colour target;
// depth is written automatically from gl_FragCoord. The shadow-caster
// pipelines reuse the cube / mesh vertex shaders (fed the light-space
// matrix as their camera), so their extra varyings go unused here.
void main() {}
