#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
// Threshold: duas cores pelo brilho percebido (sRGB, Rec. 709).
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D src;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap,texel,p0,p1,p2,p3,color,second;} p;
const vec3 channelLuma = vec3(0.3125, 0.5, 0.1875);

void main() {
    vec4 value=texture(src,v_uv);
    if(value.a<=1e-6) {o_color=vec4(0);return;}
    vec3 straight=aurea_linear_to_srgb(max(value.rgb/value.a,vec3(0)));
    if (p.p0.z > 2.5) {
        vec3 rgb;
        if (p.p0.z < 3.5) {
            float luma = dot(straight, channelLuma);
            float hue = fract(luma * p.p0.x + p.p0.y) * 6.0;
            rgb = clamp(abs(mod(hue + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
            rgb *= mix(1.0, pow(max(luma, 0.0), p.p1.w), p.p1.z);
        } else {
            vec3 encoded = straight * value.a;
            float density = dot(encoded, channelLuma);
            rgb = mix(straight, straight * density, p.p0.x) + p.p0.y;
        }
        rgb = mix(vec3(dot(rgb, channelLuma)), rgb, p.p0.w) * p.p1.x;
        rgb = max(mix(rgb, straight, p.p1.y), vec3(0.0));
        o_color = vec4(aurea_srgb_to_linear(rgb) * value.a, value.a);
        return;
    }
    if (p.p0.z > 1.5) {
        vec3 folded = 1.0 - p.p0.x - abs(straight - p.p0.x);
        if (p.p0.w > 0.5) folded = 1.0 - folded;
        float grey = dot(folded, channelLuma);
        vec3 rgb = mix(vec3(grey), folded, p.p0.y);
        rgb = max(mix(vec3(p.p1.x), vec3(p.p1.y), rgb), vec3(0.0));
        o_color = vec4(aurea_srgb_to_linear(rgb) * value.a, value.a);
        return;
    }
    if (p.p0.z > 0.5) {
        vec3 threshold = vec3(p.p0.x) + aurea_linear_to_srgb(max(p.color.rgb, vec3(0.0)));
        vec3 width = p.p0.y * p.p1.rgb;
        // At full softness the ramp spans [0, 1]. Zero softness uses a step.
        vec3 low = threshold * (vec3(1.0) - width);
        vec3 ramp = clamp((straight - low) / max(width, vec3(1e-5)), 0.0, 1.0);
        vec3 rgb = mix(step(threshold, straight), ramp, greaterThan(width, vec3(1e-5)));
        float grey = dot(rgb, channelLuma);
        rgb = clamp(mix(vec3(grey), rgb, p.p0.w), 0.0, 1.0);
        o_color = vec4(aurea_srgb_to_linear(rgb) * value.a, value.a);
        return;
    }
    float luma=dot(straight,vec3(.2126,.7152,.0722));
    float half_width=p.p0.y*.5;
    float t=half_width>1e-4?smoothstep(p.p0.x-half_width,p.p0.x+half_width,luma):step(p.p0.x,luma);
    vec3 rgb=mix(p.color.rgb,p.second.rgb,t);
    o_color=vec4(rgb*value.a,value.a);
}
