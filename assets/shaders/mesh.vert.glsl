#version 450

layout(set = 1, binding = 0) uniform Camera {
    mat4 view_proj;
} camera;

layout(set = 1, binding = 1) uniform Model {
    mat4 model;
} model_uniform;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;   // #171: model-space normal

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec3 out_normal;  // #171: world-space normal
layout(location = 2) out vec3 out_world;   // #171: world position (shadow lookup)

void main() {
    vec4 world = model_uniform.model * vec4(in_pos, 1.0);
    gl_Position = camera.view_proj * world;
    out_uv = in_uv;
    // Assumes uniform scale in the model matrix (props are placed with
    // uniform scale + translation); good enough without inverse-transpose.
    out_normal = normalize(mat3(model_uniform.model) * in_normal);
    out_world  = world.xyz;
}
