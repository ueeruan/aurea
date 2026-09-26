#version 450
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
vec4 atPoint(vec2 point) {
    vec2 uv = (point - p.p2.xy) / p.p2.zw;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
    return texture(u_tex0, uv);
}
void main() {
    vec2 point = p.p2.xy + v_uv * p.p2.zw;
    vec2 delta = point - p.p0.xy;
    float radius = length(delta);
    int mode = int(p.p1.x + 0.5);
    if (mode == 0) {
        // Analytic light and lens ghosts: fixed work per pixel, no history buffer.
        float unit = max(min(p.p3.x, p.p3.y) * p.p0.w / 100.0, 1.0);
        vec2 q = delta / unit;
        float r = length(q);
        float core = .004 / (r*r + .0008);
        float halo = exp(-pow((r - .18) / .016, 2.0)) * .1;
        float streak = exp(-abs(q.y)*350.0) * exp(-abs(q.x)*5.0) * .5;
        vec3 light = p.color.rgb * (core + halo + streak);
        vec2 axis = p.p3.xy * .5 - p.p0.xy;
        for (int i = 0; i < 6; ++i) {
            float t = .4 + float(i)*.32;
            float gr = length((point - (p.p0.xy + axis*t)) / unit);
            float sz = .022 + float(i)*.006;
            float ghost = (1.0 - smoothstep(sz*.7, sz, gr)) * .07;
            vec3 tint = i % 2 == 0 ? vec3(.3,.65,1.0) : vec3(1.0,.4,.2);
            light += tint * ghost * p.p1.y / 100.0;
        }
        light *= p.p0.z / 100.0;
        vec4 base = texture(u_tex0, v_uv);
        float coverage = clamp(max(light.r, max(light.g, light.b)), 0.0, 1.0);
        o_color = vec4(base.rgb + light, base.a + coverage*(1.0-base.a));
    } else if (mode == 1) {
        float phase = radius / max(p.p0.w, 2.0) * 6.28318530718 - radians(p.p1.y);
        float decay = exp(-p.p1.z * radius / max(min(p.p3.x,p.p3.y),1.0));
        vec2 shift = radius > .001 ? delta/radius * sin(phase)*p.p0.z*decay : vec2(0.0);
        o_color = atPoint(point + shift);
    } else {
        // Radial equidistant/rectilinear mapping. Reverse applies its inverse.
        float halfWidth = max(p.p3.x*.5, 1.0);
        float r = radius/halfWidth;
        float theta = radians(clamp(p.p0.z, .001, 160.0))*.5;
        float mapped;
        if (p.p0.w > .5) {
            // Outside the finite rectilinear projection is transparent.
            if (r*theta >= 1.5707) { o_color = vec4(0.0); return; }
            mapped = tan(r*theta)/tan(theta);
        } else mapped = atan(r*tan(theta))/theta;
        o_color = atPoint(p.p0.xy + delta*(radius > .001 ? mapped/max(r,.00001) : 1.0));
    }
}
