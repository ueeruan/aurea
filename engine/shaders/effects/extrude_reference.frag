#version 450
#include "../common/bindings.glsl"
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_source;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Parameters {
    vec4 region, inputRegion, frontColor, sideControls;
    vec4 angles[2];
    vec4 colors[5];
    vec4 controls;
} p;
layout(location = 0) in vec4 v_source;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec4 v_stroke;
layout(location = 0) out vec4 o_color;

float angle_at(int index) {
    if (index == 0) return 0.0;
    if (index == 1) return p.angles[0].y;
    if (index == 2) return p.angles[0].z;
    if (index == 3) return p.angles[0].w;
    return p.angles[1].x;
}
vec4 side_color() {
    int count = clamp(int(p.sideControls.y + 0.5), 1, 5);
    if (p.sideControls.x < 0.5 || count == 1) return p.colors[0];
    float angle = mod(v_source.z - p.sideControls.z, 360.0);
    float previousDistance = 361.0, nextDistance = 361.0;
    int previous = 0, next = 0;
    for (int index = 0; index < 5; ++index) {
        if (index >= count) break;
        float stopAngle = mod(angle_at(index), 360.0);
        float left = mod(angle - stopAngle, 360.0);
        float right = mod(stopAngle - angle, 360.0);
        if (left < previousDistance) { previousDistance = left; previous = index; }
        if (right < nextDistance) { nextDistance = right; next = index; }
    }
    if (p.sideControls.w < 0.5) return p.colors[previous];
    float weight = previousDistance / max(previousDistance + nextDistance, 1e-6);
    return mix(p.colors[previous], p.colors[next], weight);
}
void main() {
    int kind = int(v_source.w + 0.5);
    vec4 color;
    if (kind < 2) {
        if (p.controls.x > 0.5) {
            vec2 uv = (v_source.xy - p.inputRegion.xy) / p.inputRegion.zw;
            vec4 source = texture(u_source, uv);
            color = vec4(source.a > 1e-6 ? source.rgb / source.a : vec3(0), source.a);
        } else {
            color = p.frontColor;
            color.a *= v_color.a;
        }
        color.a *= p.controls.y;
    } else if (kind == 2) {
        color = side_color();
        color.a *= v_color.a * p.controls.y;
    } else {
        color = v_color;
        float antialias = max(fwidth(v_stroke.x), 0.5);
        color.a *= 1.0 - smoothstep(v_stroke.y - antialias, v_stroke.y + antialias, abs(v_stroke.x));
    }
    if (color.a <= 0.0) discard;
    o_color = vec4(color.rgb * clamp(color.a, 0.0, 1.0), clamp(color.a, 0.0, 1.0));
}
