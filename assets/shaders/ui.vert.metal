// Mirror of ui.vert.glsl for the Metal backend.
#include <metal_stdlib>
using namespace metal;

struct VSIn {
    float2 pos    [[attribute(0)]];
    float2 uv     [[attribute(1)]];
    float4 color  [[attribute(2)]];
};

struct VSOut {
    float4 position [[position]];
    float2 uv       [[user(locn0)]];
    float4 color    [[user(locn1)]];
};

struct Screen {
    float2 inv_size;
};

vertex VSOut main0(VSIn in                  [[stage_in]],
                   constant Screen& u_screen [[buffer(0)]]) {
    float2 ndc = in.pos * u_screen.inv_size * 2.0 - 1.0;
    ndc.y = -ndc.y;
    VSOut o;
    o.position = float4(ndc, 0.0, 1.0);
    o.uv = in.uv;
    o.color = in.color;
    return o;
}
