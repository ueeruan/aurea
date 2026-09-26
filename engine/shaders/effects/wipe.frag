#version 450
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
uint hash32(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
void main() {
    vec4 source = texture(u_tex0, v_uv);
    float completion = p.p0.x;
    if (completion <= 0.0) { o_color = source; return; }
    if (completion >= 1.0) { o_color = vec4(0.0); return; }
    vec2 point = p.p2.xy + v_uv * p.p2.zw;
    vec2 size = max(p.p3.xy, vec2(1.0));
    float angle = radians(p.p0.y);
    float threshold;
    int mode = int(p.p1.x + 0.5);
    if (mode == 0) {
        vec2 axis = vec2(cos(angle), sin(angle));
        float span = dot(abs(axis), size);
        threshold = dot(point - size * 0.5, axis) / max(span, 1.0) + 0.5;
    } else if (mode == 1) {
        vec2 delta = point - size * (p.p1.yz / 100.0);
        threshold = fract((atan(delta.y, delta.x) - angle) / 6.28318530718 + 1.0);
    } else {
        ivec2 cell = ivec2(floor(point / max(p.p1.yz, vec2(1.0))));
        uint h = hash32(uint(cell.x) ^ hash32(uint(cell.y)) ^ hash32(uint(p.p1.w)));
        threshold = (float(h & 0xffffffu) + 0.5) / 16777216.0;
    }
    if (p.p0.w > 0.5) threshold = 1.0 - threshold;
    float feather = p.p0.z * 0.5;
    // Move the softened edge beyond both ends; 0% and 100% are exact.
    float edge = mix(-feather, 1.0 + feather, completion);
    float keep = feather > 0.00001 ? smoothstep(edge - feather, edge + feather, threshold) : step(edge, threshold);
    o_color = source * keep; // Preserve premultiplied alpha and HDR values.
}
