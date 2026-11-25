#include "../../common/bindings.glsl"

layout(location = 0) in vec2 v_uv0;
layout(location = 1) in vec2 v_uv1;
layout(location = 2) in float v_alpha;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D t_base;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Cutout {
    vec4 alpha;   // material alpha, cutoff, UV set, rotation
    vec4 uv;      // offset.xy, scale.zw
} u;

void apply_cutout() {
    vec2 uv = (u.alpha.z > 0.5 ? v_uv1 : v_uv0) * u.uv.zw;
    float c = cos(u.alpha.w), s = sin(u.alpha.w);
    uv = vec2(c * uv.x + s * uv.y, -s * uv.x + c * uv.y) + u.uv.xy;
    if (texture(t_base, uv).a * u.alpha.x * v_alpha < u.alpha.y) discard;
}
