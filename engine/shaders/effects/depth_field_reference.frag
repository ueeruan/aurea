#version 450
// =============================================================================
//  Aurea / shaders / effects / depth_field_reference.frag
//
//  Depth of Field reference (aurea.ai.depth_field_reference; behaviour of AE
//  3D Channel › Depth of Field, REA batch-c/rea/Depth_Field). Per pixel:
//    d = |Z − focal plane|; T = focal plane thickness
//    d < T            → radius 0
//    d − T ≥ T        → radius = maximum radius
//    otherwise u = (d − T)/T, with bias b in (0,1): u /= (1/b − 2)(1 − u) + 1,
//                       radius = maximum radius · u
//    b = 1 − (Focal Bias · 0.006 + 0.2)
//  level = radius < 1 ? radius : log2(radius) (texels of level 0); the
//  colour is the linear mix of the two pyramid levels around it.
//  Z comes from Aurea's AI depth (the native 3D channel is a render pass):
//  Z = mix(far Z, near Z, nearness).
//
//  u.p[0]: x = focal plane, y = max radius (texels), z = thickness, w = bias b
//  u.p[1]: x = near Z, y = far Z, z = levels available (1..10)
//  u.p[8]/p[9]/p[10]: the depth-map contract of ae_depth.frag.
// =============================================================================
#include "ae_common.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_depth;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_l1;
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_l2;
layout(set = 0, binding = AUREA_TEX4) uniform sampler2D u_l3;
layout(set = 0, binding = AUREA_TEX5) uniform sampler2D u_l4;
layout(set = 0, binding = AUREA_TEX6) uniform sampler2D u_l5;
layout(set = 0, binding = AUREA_TEX7) uniform sampler2D u_l6;
layout(set = 0, binding = AUREA_TEX8) uniform sampler2D u_l7;
layout(set = 0, binding = AUREA_TEX9) uniform sampler2D u_l8;
layout(set = 0, binding = AUREA_TEX10) uniform sampler2D u_l9;
layout(set = 0, binding = AUREA_TEX11) uniform sampler2D u_l10;

vec4 level_at(int k, vec2 uv) {
    if (k <= 0) return texture(u_tex0, uv);
    if (k == 1) return texture(u_l1, uv);
    if (k == 2) return texture(u_l2, uv);
    if (k == 3) return texture(u_l3, uv);
    if (k == 4) return texture(u_l4, uv);
    if (k == 5) return texture(u_l5, uv);
    if (k == 6) return texture(u_l6, uv);
    if (k == 7) return texture(u_l7, uv);
    if (k == 8) return texture(u_l8, uv);
    if (k == 9) return texture(u_l9, uv);
    return texture(u_l10, uv);
}

float nearness() {
    vec2 point = ae_px();
    vec2 size = ae_layer_size();
    if (u.p[8].y > 0.5) {
        point = vec2(dot(u.p[9].xy, point) + u.p[10].z, dot(u.p[9].zw, point) + u.p[10].w);
        size = max(u.p[10].xy, vec2(1.0));
    }
    const float raw = texture(u_depth, clamp(point / size, vec2(0.0), vec2(1.0))).r;
    return clamp((raw - u.p[8].z) / max(u.p[8].w - u.p[8].z, 1e-5), 0.0, 1.0);
}

void main() {
    const vec2 uv = ae_in_uv();
    if (u.p[8].x < 0.5) { o_color = texture(u_tex0, uv); return; }
    const float z = mix(u.p[1].y, u.p[1].x, nearness());
    const float d = abs(z - u.p[0].x);
    const float t = u.p[0].z;
    float radius = 0.0;
    if (d >= t) {
        radius = u.p[0].y;
        float f = d - t;
        if (f < t) {
            f /= t;
            const float b = u.p[0].w;
            if (b > 0.0 && b < 1.0) f = f / ((1.0 / b - 2.0) * (1.0 - f) + 1.0);
            radius *= f;
        }
    }
    float level = radius < 1.0 ? radius : log2(radius);   // the native float path
    level = min(level, u.p[1].z);
    const int k = int(floor(level));
    const float w = level - float(k);
    vec4 c = level_at(k, uv);
    if (w > 0.0 && float(k) < u.p[1].z) c = mix(c, level_at(k + 1, uv), w);
    o_color = c;
}
