// Mirror of sprite.frag.glsl for the Metal backend.
#include <metal_stdlib>
using namespace metal;

struct FSIn {
    float4 position [[position]];
    float2 uv       [[user(locn0)]];
    float4 color    [[user(locn1)]];
};

fragment float4 main0(FSIn in [[stage_in]],
                      texture2d<float> tex [[texture(0)]],
                      sampler smp          [[sampler(0)]]) {
    return tex.sample(smp, in.uv) * in.color;
}
