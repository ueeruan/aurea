#version 450
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_foreground;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_background;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;
    vec4 foreground;
    vec4 background;
    vec4 compX;
    vec4 compY;
    vec4 compW;
    vec4 blend;
} p;

void main() {
    float progress = p.blend.x;
    vec4 foreground = progress < 1.0
        ? texture(u_foreground, v_uv * p.foreground.xy + p.foreground.zw)
        : vec4(0.0);
    vec4 background = vec4(0.0);
    if (p.blend.y > 0.5) {
        vec3 pixel = vec3(p.region.xy + v_uv * p.region.zw, 1.0);
        float w = dot(p.compW.xyz, pixel);
        if (w > 1e-6) {
            vec2 position = vec2(dot(p.compX.xyz, pixel), dot(p.compY.xyz, pixel)) / w;
            background = texture(u_background, (position - p.background.xy) / p.background.zw);
        }
    }
    o_color = mix(foreground, background, progress);
}
