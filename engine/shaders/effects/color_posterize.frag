#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
#include "color_oklab.glsl"
// Posterize: p0.x níveis, p0.y gamma, p0.z = 1 só no brilho (L do Oklab).
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D src;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D original;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap,texel,p0,p1,p2,p3,color;} p;
float steps(float x,float levels,float gamma) {
    float y=pow(clamp(x,0.0,1.0),1.0/gamma);
    return pow(floor(y*(levels-1.0)+.5)/(levels-1.0),gamma);
}
vec3 quantize_ramp(vec3 x, float stepSize, float softness, float phase) {
    if (stepSize < 1e-5) return x;
    vec3 levels = (x + phase) / stepSize;
    float edge = clamp(softness, 1e-5, 1.0);
    vec3 rounded = levels - 0.5 + 0.5 * edge;
    vec3 lower = floor(rounded);
    return (lower + clamp(fract(rounded) / edge, 0.0, 1.0)) * stepSize - phase;
}
void main() {
    vec4 value=texture(src,v_uv);
    if (p.p0.w > 1.5) {
        const vec3 weights = vec3(0.3125, 0.5, 0.1875);
        vec3 encoded = unpremultiply(value).rgb;
        float gray = dot(encoded, weights);
        vec3 levels = quantize_ramp(encoded, p.p0.x, p.p0.y, p.p1.x);
        float tonalLevel = quantize_ramp(vec3(gray), p.p0.x, p.p0.y, p.p1.x).x;
        vec3 rgb = mix(levels, encoded + (tonalLevel - gray), p.p0.z);
        rgb = mix(vec3(dot(rgb, weights)), rgb, p.p1.w);
        rgb = max(rgb * p.p1.y + p.p1.z, vec3(0.0));
        o_color = premultiply(vec4(srgb_to_linear(rgb), texture(original, v_uv).a));
        return;
    }
    if (p.p0.w > 0.5) {
        o_color = premultiply(vec4(linear_to_srgb(unpremultiply(value).rgb), value.a));
        return;
    }
    if(value.a<=1e-6) {o_color=vec4(0);return;}
    vec3 linear=max(value.rgb/value.a,vec3(0));
    float levels=max(p.p0.x,2.0),gamma=max(p.p0.y,.1);
    vec3 rgb;
    if(p.p0.z>.5) {
        vec3 lab=aurea_linear_to_oklab(linear);
        lab.x=steps(lab.x,levels,gamma);
        rgb=max(aurea_oklab_to_linear(lab),vec3(0));
    } else {
        vec3 s=aurea_linear_to_srgb(linear);
        rgb=aurea_srgb_to_linear(vec3(steps(s.r,levels,gamma),steps(s.g,levels,gamma),steps(s.b,levels,gamma)));
    }
    o_color=vec4(rgb*value.a,value.a);
}
