#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap; vec4 texel;
    vec4 p0; // center in layer pixels, width, sweep intensity
    vec4 p1; // direction, edge intensity, edge thickness, shape
    vec4 p2; // input region in layer pixels
    vec4 p3; // reception, legacy multiply/follow
    vec4 color;
} p;
float alphaAt(vec2 uv) {
    if (any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) return 0;
    return texture(u_tex0,uv).a;
}
void main() {
    vec2 uv = v_uv*p.uvMap.xy+p.uvMap.zw;
    vec4 raw = texture(u_tex0,uv), src = unpremultiply(raw);
    vec2 axis = vec2(cos(p.p1.x),sin(p.p1.x));
    vec2 point = p.p2.xy + uv*p.p2.zw;
    float distance = abs(dot(point-p.p0.xy,axis));
    float t = p.p0.z > 0 ? clamp(1.0-distance/max(.001,p.p0.z*.5),0.0,1.0) : 0;
    float beam = p.p1.w < .5 ? t : p.p1.w < 1.5 ? t*t*(3.0-2.0*t) : t*t*t;
    float edge = 0;
    if (p.p1.y > 0 && p.p1.z > 0 && src.a > 0) {
        // Alpha bevel: source texture must not create false embossed edges.
        vec2 stepUV = p.p1.z / max(p.p2.zw,vec2(.001));
        vec2 gradient = vec2(alphaAt(uv+vec2(stepUV.x,0))-alphaAt(uv-vec2(stepUV.x,0)),
                             alphaAt(uv+vec2(0,stepUV.y))-alphaAt(uv-vec2(0,stepUV.y)));
        edge = max(0.0,-dot(gradient,axis))*p.p1.y;
    }
    float intensity = beam*(p.p0.w+edge);
    if (p.p3.z > .5) intensity *= smoothstep(.02,.35,aurea_luma(aurea_linear_to_srgb(max(src.rgb,vec3(0)))));
    vec3 light = max(p.color.rgb,vec3(0))*p.color.a*intensity;
    if (p.p3.x > 1.5) {
        float alpha = src.a*clamp(intensity,0,1);
        o_color = premultiply(vec4(src.rgb+light,alpha));
    } else if (p.p3.x > .5) {
        o_color = premultiply(vec4(mix(src.rgb,max(p.color.rgb,vec3(0)),clamp(intensity,0,1)),src.a));
    } else {
        vec3 rgb = p.p3.y > .5 ? src.rgb*(1.0+light) : src.rgb+light;
        o_color = premultiply(vec4(rgb,src.a));
    }
}
