#version 450
// Instanced unit-cube tile (#152) / autotiled gltf variant (#172). Local
// cube spans [0,1]^3; the z axis is scaled by the per-type height, then
// rotated and offset by the per-instance world position. For TILED
// tiles a per-instance Z rotation (radians) is composed on top of the
// per-type rotation so an autotile resolver can spin the canonical
// straight / corner / T / end mesh into place. Pairs with tile.frag
// (same uv + brightness varyings).

layout(set = 1, binding = 0) uniform Camera {
    mat4 view_proj;
} camera;

layout(set = 1, binding = 1) uniform CubeParams {
    vec4 p;     // p.x = z scale (cube height), p.y = xy scale; zw unused
    vec4 rot;   // rot.xyz = rotation in radians about x/y/z; w unused
    vec4 off;   // off.xyz = translation offset in tile units; w unused
} cube;

layout(location = 0) in vec3 in_pos;       // local geometry (cube [0,1]^3, or a mesh)
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_inst;      // per-instance: xyz world offset, w brightness
layout(location = 3) in vec3 in_normal;    // #171: local surface normal
layout(location = 4) in float in_inst_rot; // #172: per-instance Z rotation in radians

layout(location = 0) out vec2 out_uv;
layout(location = 1) out float out_dim;
layout(location = 2) out vec3 out_normal; // #171: world-space normal
layout(location = 3) out vec3 out_world;  // #171: world position (shadow lookup)

// Column-major rotation R = Rz * Ry * Rx (X applied first).
mat3 make_rotation(vec3 r) {
    float cx = cos(r.x), sx = sin(r.x);
    float cy = cos(r.y), sy = sin(r.y);
    float cz = cos(r.z), sz = sin(r.z);
    mat3 Rx = mat3(1.0, 0.0, 0.0,   0.0, cx, sx,   0.0, -sx, cx);
    mat3 Ry = mat3(cy, 0.0, -sy,    0.0, 1.0, 0.0, sy, 0.0, cy);
    mat3 Rz = mat3(cz, sz, 0.0,     -sz, cz, 0.0,  0.0, 0.0, 1.0);
    return Rz * Ry * Rx;
}

void main() {
    // p.y scales x/y, p.x scales z. Cube uses (height, 1); an instanced
    // mesh uses (scale, scale) for a uniform fit to the tile (#152).
    // Geometry is scaled, then rotated by the per-type R_type, then by
    // the per-instance Z rotation R_inst (applied on the LEFT so it
    // happens in world space after R_type has stood the mesh up), and
    // finally offset to its world tile position (#172).
    mat3 R_type = make_rotation(cube.rot.xyz);
    float c = cos(in_inst_rot), s = sin(in_inst_rot);
    mat3 R_inst = mat3( c,   s,   0.0,
                       -s,   c,   0.0,
                        0.0, 0.0, 1.0);
    mat3 R = R_inst * R_type;
    vec3 local = vec3(in_pos.x * cube.p.y, in_pos.y * cube.p.y, in_pos.z * cube.p.x);
    vec3 world = R * local + cube.off.xyz + in_inst.xyz;
    gl_Position = camera.view_proj * vec4(world, 1.0);
    out_uv    = in_uv;
    out_dim   = in_inst.w;
    out_world = world;
    // Normal transform under the (xy = p.y, z = p.x) non-uniform scale is
    // the inverse-transpose: divide each axis by its scale, then rotate.
    vec3 n = vec3(in_normal.x / cube.p.y, in_normal.y / cube.p.y, in_normal.z / cube.p.x);
    out_normal = normalize(R * n);
}
