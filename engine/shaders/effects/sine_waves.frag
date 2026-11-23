#version 450
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D source;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D matte;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap, texel, axes, wave, edge, layer, matteRegion, inputOrigin;
    mat4 compFromLayer;
} p;

float wrap(float x, int mode) {
    if (mode == 1) return fract(x);
    if (mode == 2) return 1.0 - abs(mod(x, 2.0) - 1.0);
    return x;
}

vec4 sampleSource(vec2 uv) {
    vec2 xy = vec2(wrap(uv.x, int(p.edge.x)), wrap(uv.y, int(p.edge.y)));
    if (any(lessThan(xy, vec2(0.0))) || any(greaterThan(xy, vec2(1.0)))) return vec4(0.0);
    return texture(source, xy);
}

void main() {
    vec2 size = max(p.layer.xy, vec2(1.0));
    vec2 centered = v_uv - 0.5;
    float weight = 1.0;
    if (p.edge.w > 0.5) {
        vec4 point = p.compFromLayer * vec4(p.inputOrigin.xy + v_uv * size, 0.0, 1.0);
        vec2 matteUv = (point.xy / max(point.w, 1e-6) - p.matteRegion.xy) / max(p.matteRegion.zw, vec2(0.001));
        bool inside = all(greaterThanEqual(matteUv, vec2(0.0))) && all(lessThanEqual(matteUv, vec2(1.0)));
        vec4 m = inside ? texture(matte, matteUv) : vec4(0.0);
        weight = clamp(p.layer.z > 0.5 ? m.a : aurea_luma(m.rgb), 0.0, 1.0);
        if (p.layer.w > 0.5) weight = 1.0 - weight;
    }
    float coordinate = dot(centered * size, p.axes.xy) / size.x;
    float phase = (coordinate * p.wave.y + p.wave.z) * AUREA_TAU;
    vec2 shift = -p.axes.zw * (p.wave.x * sin(phase) * weight) * size.x / size;
    vec2 uv = centered * p.wave.w + 0.5 + shift;
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    vec2 pixels = vec2(textureSize(source, 0));
    float footprint = max(length(dx * pixels), length(dy * pixels));
    if (p.edge.z > 0.5 && footprint > 1.35) {
        vec2 a = dx * 0.25, b = dy * 0.25;
        o_color = (sampleSource(uv + a + b) + sampleSource(uv - a + b) +
                   sampleSource(uv + a - b) + sampleSource(uv - a - b)) * 0.25;
    } else {
        o_color = sampleSource(uv);
    }
}
