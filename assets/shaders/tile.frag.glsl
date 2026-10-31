#version 450

layout(set = 2, binding = 0) uniform sampler2D u_texture;
layout(set = 2, binding = 1) uniform sampler2D u_shadow;   // #171: light depth map

layout(set = 3, binding = 0) uniform Frag {
    vec4 tint;          // rgb = day/night tint, a unused
    vec4 sun_dir;       // #171: xyz = unit vector toward the sun (world); w unused
    vec4 sun_color;     // #171: rgb = directional sun colour * intensity; w unused
    vec4 ambient;       // #171: rgb = ambient light colour; w unused
    mat4 light_vp;      // #171: world -> light clip space
    vec4 shadow_params; // #171: x = depth bias, y = texel size, z = enable (>=0.5), w unused
} u_frag;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in float in_dim;   // per-instance brightness (lower floors dimmer, #144)
layout(location = 2) in vec3 in_normal; // #171: world-space normal
layout(location = 3) in vec3 in_world;  // #171: world position
layout(location = 0) out vec4 out_color;

// #171: fraction (0..1) of the fragment that is in shadow. 3x3 PCF with a
// slope-scaled bias. SDL_GPU clip space is y-up / z in [0,1], but textures
// sample y-down, so the lookup flips v.
float shadow_frac(vec3 world, float ndl) {
    if (u_frag.shadow_params.z < 0.5) return 0.0;
    vec4 lp = u_frag.light_vp * vec4(world, 1.0);
    vec3 p = lp.xyz / lp.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    float cur = p.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || cur > 1.0) return 0.0;
    float bias = max(u_frag.shadow_params.x * (1.0 - ndl), u_frag.shadow_params.x * 0.15);
    float texel = u_frag.shadow_params.y;
    float sh = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            float d = texture(u_shadow, uv + vec2(dx, dy) * texel).r;
            sh += (cur - bias > d) ? 1.0 : 0.0;
        }
    return sh / 9.0;
}

void main() {
    vec4 c = texture(u_texture, in_uv);
    vec3 n = normalize(in_normal);
    float ndl = max(dot(n, normalize(u_frag.sun_dir.xyz)), 0.0);
    float lit = ndl * (1.0 - shadow_frac(in_world, ndl));
    vec3 light = u_frag.ambient.rgb + u_frag.sun_color.rgb * lit;
    out_color = vec4(c.rgb * u_frag.tint.rgb * light * in_dim, c.a);
}
