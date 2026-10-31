// Mirror of tile.frag.glsl for the Metal backend.
#include <metal_stdlib>
using namespace metal;

struct FSIn {
    float4 position [[position]];
    float2 uv       [[user(locn0)]];
    float  dim      [[user(locn1)]];
    float3 normal   [[user(locn2)]];   // #171: world-space normal
    float3 world    [[user(locn3)]];   // #171: world position
};

struct FragUniform {
    float4   tint;          // rgb day/night tint
    float4   sun_dir;       // #171: xyz toward sun (world)
    float4   sun_color;     // #171: rgb directional intensity
    float4   ambient;       // #171: rgb ambient
    float4x4 light_vp;      // #171: world -> light clip space
    float4   shadow_params; // #171: x bias, y texel, z enable, w unused
};

static float shadow_frac(float3 world, float ndl,
                         constant FragUniform& u,
                         texture2d<float> shadow, sampler smp) {
    if (u.shadow_params.z < 0.5) return 0.0;
    float4 lp = u.light_vp * float4(world, 1.0);
    float3 p = lp.xyz / lp.w;
    float2 uv = p.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    float cur = p.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || cur > 1.0) return 0.0;
    float bias = max(u.shadow_params.x * (1.0 - ndl), u.shadow_params.x * 0.15);
    float texel = u.shadow_params.y;
    float sh = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            float d = shadow.sample(smp, uv + float2(dx, dy) * texel).r;
            sh += (cur - bias > d) ? 1.0 : 0.0;
        }
    return sh / 9.0;
}

fragment float4 main0(FSIn in [[stage_in]],
                      texture2d<float> tex     [[texture(0)]],
                      sampler smp              [[sampler(0)]],
                      texture2d<float> shadow  [[texture(1)]],
                      sampler shadow_smp       [[sampler(1)]],
                      constant FragUniform& u  [[buffer(0)]]) {
    float4 c = tex.sample(smp, in.uv);
    float3 n = normalize(in.normal);
    float ndl = max(dot(n, normalize(u.sun_dir.xyz)), 0.0);
    float lit = ndl * (1.0 - shadow_frac(in.world, ndl, u, shadow, shadow_smp));
    float3 light = u.ambient.rgb + u.sun_color.rgb * lit;
    return float4(c.rgb * u.tint.rgb * light * in.dim, c.a);
}
