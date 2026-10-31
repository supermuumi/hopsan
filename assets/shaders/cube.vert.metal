// Mirror of cube.vert.glsl for the Metal backend (#152, #172).
#include <metal_stdlib>
using namespace metal;

struct Camera     { float4x4 view_proj; };
// p.x = z scale (height), p.y = xy scale; rot.xyz = rotation radians x/y/z;
// off.xyz = translation offset in tile units.
struct CubeParams { float4 p; float4 rot; float4 off; };

struct VSIn {
    float3 in_pos    [[attribute(0)]];
    float2 in_uv     [[attribute(1)]];
    float4 in_inst   [[attribute(2)]];
    float3 in_normal [[attribute(3)]];   // #171: local surface normal
    float  in_inst_rot [[attribute(4)]]; // #172: per-instance Z rotation (radians)
};

struct VSOut {
    float4 position [[position]];
    float2 uv       [[user(locn0)]];
    float  dim      [[user(locn1)]];
    float3 normal   [[user(locn2)]];     // #171: world-space normal
    float3 world    [[user(locn3)]];     // #171: world position (shadow lookup)
};

// Column-major rotation R = Rz * Ry * Rx (X applied first).
static float3x3 make_rotation(float3 r) {
    float cx = cos(r.x), sx = sin(r.x);
    float cy = cos(r.y), sy = sin(r.y);
    float cz = cos(r.z), sz = sin(r.z);
    float3x3 Rx = float3x3(float3(1.0, 0.0, 0.0), float3(0.0, cx, sx),   float3(0.0, -sx, cx));
    float3x3 Ry = float3x3(float3(cy, 0.0, -sy),  float3(0.0, 1.0, 0.0), float3(sy, 0.0, cy));
    float3x3 Rz = float3x3(float3(cz, sz, 0.0),   float3(-sz, cz, 0.0),  float3(0.0, 0.0, 1.0));
    return Rz * Ry * Rx;
}

vertex VSOut main0(VSIn in [[stage_in]],
                   constant Camera&     camera [[buffer(0)]],
                   constant CubeParams& cube   [[buffer(1)]]) {
    VSOut out;
    // Per-type R_type stands the mesh up; per-instance R_inst (autotile,
    // about world Z) is applied on the LEFT so it happens in world space
    // after R_type. #172.
    float3x3 R_type = make_rotation(cube.rot.xyz);
    float c = cos(in.in_inst_rot), s = sin(in.in_inst_rot);
    float3x3 R_inst = float3x3(float3( c,   s,   0.0),
                               float3(-s,   c,   0.0),
                               float3( 0.0, 0.0, 1.0));
    float3x3 R = R_inst * R_type;
    float3 local = float3(in.in_pos.x * cube.p.y, in.in_pos.y * cube.p.y, in.in_pos.z * cube.p.x);
    float3 world = R * local + cube.off.xyz + in.in_inst.xyz;
    out.position = camera.view_proj * float4(world, 1.0);
    out.uv    = in.in_uv;
    out.dim   = in.in_inst.w;
    out.world = world;
    // Inverse-transpose of the (xy = p.y, z = p.x) scale, then rotate.
    float3 n = float3(in.in_normal.x / cube.p.y, in.in_normal.y / cube.p.y, in.in_normal.z / cube.p.x);
    out.normal = normalize(R * n);
    return out;
}
