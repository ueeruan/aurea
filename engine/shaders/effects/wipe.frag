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
float linear_position(vec2 point, vec2 size, float angle) {
    vec2 axis = vec2(cos(angle), sin(angle));
    float span = dot(abs(axis), size);
    return dot(point - size * 0.5, axis) / max(span, 1.0) + 0.5;
}
float feathered_edge(float edge, float halfWidth, float position) {
    return halfWidth > 0.00001
        ? smoothstep(edge - halfWidth, edge + halfWidth, position)
        : step(edge, position);
}
void main() {
    vec4 source = texture(u_tex0, v_uv);
    vec2 point = p.p2.xy + v_uv * p.p2.zw;
    vec2 size = max(p.p3.xy, vec2(1.0));
    float angle = radians(p.p0.y);
    int mode = int(p.p1.x + 0.5);
    float feather = p.p0.z * 0.5;
    if (mode == 3) {
        float start = p.p0.x, end = p.p0.w;
        if (start >= end || start >= 1.0 || end <= 0.0) { o_color = vec4(0.0); return; }
        float position = linear_position(point, size, angle);
        // Carry soft edges beyond the frame at both ends of each animation.
        float lowerEdge = mix(-feather, 1.0 + feather, start);
        float upperEdge = mix(-feather, 1.0 + feather, end);
        float lower = start <= 0.0 ? 1.0 : feathered_edge(lowerEdge, feather, position);
        float upper = end >= 1.0 ? 0.0 : feathered_edge(upperEdge, feather, position);
        o_color = source * max(0.0, lower - upper);
        return;
    }
    float completion = p.p0.x;
    if (completion <= 0.0) { o_color = source; return; }
    if (completion >= 1.0) { o_color = vec4(0.0); return; }
    float threshold;
    if (mode == 0) {
        threshold = linear_position(point, size, angle);
    } else if (mode == 1) {
        vec2 delta = point - size * (p.p1.yz / 100.0);
        threshold = fract((atan(delta.y, delta.x) - angle) / 6.28318530718 + 1.0);
    } else {
        ivec2 cell = ivec2(floor(point / max(p.p1.yz, vec2(1.0))));
        uint h = hash32(uint(cell.x) ^ hash32(uint(cell.y)) ^ hash32(uint(p.p1.w)));
        threshold = (float(h & 0xffffffu) + 0.5) / 16777216.0;
    }
    if (p.p0.w > 0.5) threshold = 1.0 - threshold;
    // Move the softened edge beyond both ends; 0% and 100% are exact.
    float edge = mix(-feather, 1.0 + feather, completion);
    float keep = feathered_edge(edge, feather, threshold);
    o_color = source * keep; // Preserve premultiplied alpha and HDR values.
}
