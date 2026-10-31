// Mirror of tile.vert.glsl for the Metal backend.
// Entry point name must be "main0" per SDL_GPU's MSL convention.
#include <metal_stdlib>
using namespace metal;

struct Camera { float4x4 view_proj; };

struct VSIn {
    float3 in_pos    [[attribute(0)]];
    float2 in_uv     [[attribute(1)]];
    float4 in_inst   [[attribute(2)]];   // xyz world offset, w brightness (#144)
    float3 in_normal [[attribute(3)]];   // #171: surface normal
};

struct VSOut {
    float4 position [[position]];
    float2 uv       [[user(locn0)]];
    float  dim      [[user(locn1)]];
    float3 normal   [[user(locn2)]];     // #171: world-space normal
    float3 world    [[user(locn3)]];     // #171: world position (shadow lookup)
};

vertex VSOut main0(VSIn in [[stage_in]],
                   constant Camera& camera [[buffer(0)]]) {
    VSOut out;
    float3 world = in.in_pos + in.in_inst.xyz;
    out.position = camera.view_proj * float4(world, 1.0);
    out.uv     = in.in_uv;
    out.dim    = in.in_inst.w;
    out.normal = in.in_normal;   // quads are axis-aligned, never rotated
    out.world  = world;
    return out;
}
