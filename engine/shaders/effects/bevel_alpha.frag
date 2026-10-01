#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
float alphaAt(vec2 uv) {
    if (any(lessThan(uv, vec2(0))) || any(greaterThan(uv, vec2(1)))) return 0.0;
    return texture(u_tex0, uv).a;
}
void main() {
    vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec4 src = texture(u_tex0, uv);
    if (src.a <= 0.000001) { o_color = vec4(0); return; }
    vec2 gradient = vec2(0);
    for (int i = 1; i <= 4; ++i) {
        vec2 d = p.p0.xy * float(i) * 0.25;
        gradient += vec2(alphaAt(uv - vec2(d.x, 0)) - alphaAt(uv + vec2(d.x, 0)),
                         alphaAt(uv - vec2(0, d.y)) - alphaAt(uv + vec2(0, d.y))) * 0.25;
    }
    float light = clamp(dot(gradient, vec2(cos(p.p0.z), sin(p.p0.z))) * p.p0.w, -1.0, 1.0);
    vec4 tint = light > 0.0 ? p.p1 : p.p2;
    vec3 rgb = mix(src.rgb / src.a, aurea_srgb_to_linear(tint.rgb), abs(light) * tint.a);
    o_color = vec4(rgb * src.a, src.a);
}
