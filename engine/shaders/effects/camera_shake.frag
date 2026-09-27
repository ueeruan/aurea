#version 450
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 rows[16]; // Up to eight inverse affine camera samples.
    vec4 options; // sample count, mix, edge mode (mirror/clamp/tile/transparent)
    vec4 bounds; // half texel xy
} p;
vec4 sampleSource(vec2 uv) {
    if (p.options.z < .5) uv = 1.0 - abs(mod(uv, 2.0) - 1.0);
    else if (p.options.z > 1.5 && p.options.z < 2.5) uv = fract(uv);
    else if (p.options.z > 2.5 && (any(lessThan(uv, vec2(0))) || any(greaterThan(uv, vec2(1))))) return vec4(0);
    return texture(u_tex0, clamp(uv, p.bounds.xy, 1.0-p.bounds.xy));
}
void main() {
    vec3 h = vec3(v_uv, 1);
    vec4 shaken = vec4(0);
    int count = clamp(int(p.options.x + .5), 1, 8);
    for (int i = 0; i < 8; ++i) {
        if (i >= count) break;
        shaken += sampleSource(vec2(dot(p.rows[i*2].xyz, h), dot(p.rows[i*2+1].xyz, h)));
    }
    o_color = mix(texture(u_tex0, v_uv), shaken/float(count), p.options.y);
}
