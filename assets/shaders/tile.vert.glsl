#version 450
// Instanced unit-quad tile. World position = quad + per-instance offset.
// The per-instance vec4 is (x, y, z, brightness): xyz is the tile's world
// position (z lifts upper floors, #144), w is a brightness multiplier the
// fragment shader applies so floors below the active one read as dimmer.

layout(set = 1, binding = 0) uniform Camera {
    mat4 view_proj;
} camera;

layout(location = 0) in vec3 in_pos;        // unit quad, z=0
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_inst;       // per-instance: xyz world offset, w brightness
layout(location = 3) in vec3 in_normal;     // #171: surface normal (floor quad faces +Z)

layout(location = 0) out vec2 out_uv;
layout(location = 1) out float out_dim;
layout(location = 2) out vec3 out_normal;   // #171: world-space normal
layout(location = 3) out vec3 out_world;    // #171: world position (shadow lookup)

void main() {
    vec3 world = in_pos + in_inst.xyz;
    gl_Position = camera.view_proj * vec4(world, 1.0);
    out_uv     = in_uv;
    out_dim    = in_inst.w;
    out_normal = in_normal;   // quads are axis-aligned, never rotated
    out_world  = world;
}
