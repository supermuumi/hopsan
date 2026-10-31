// Mirror of mesh.vert.glsl for the Metal backend.
#include <metal_stdlib>
using namespace metal;

struct Camera { float4x4 view_proj; };
struct Model  { float4x4 model;     };

struct VSIn {
    float3 in_pos    [[attribute(0)]];
    float2 in_uv     [[attribute(1)]];
    float3 in_normal [[attribute(2)]];   // #171: model-space normal
};

struct VSOut {
    float4 position [[position]];
    float2 uv       [[user(locn0)]];
    float3 normal   [[user(locn1)]];     // #171: world-space normal
    float3 world    [[user(locn2)]];     // #171: world position (shadow lookup)
};

vertex VSOut main0(VSIn in [[stage_in]],
                   constant Camera& camera [[buffer(0)]],
                   constant Model&  model  [[buffer(1)]]) {
    VSOut out;
    float4 world = model.model * float4(in.in_pos, 1.0);
    out.position = camera.view_proj * world;
    out.uv = in.in_uv;
    // Assumes uniform scale in the model matrix.
    float3x3 m = float3x3(model.model[0].xyz, model.model[1].xyz, model.model[2].xyz);
    out.normal = normalize(m * in.in_normal);
    out.world  = world.xyz;
    return out;
}
