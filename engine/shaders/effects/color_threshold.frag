#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
// Threshold: duas cores pelo brilho percebido (sRGB, Rec. 709).
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D src;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap,texel,p0,p1,p2,p3,color,second;} p;
void main() {
    vec4 value=texture(src,v_uv);
    if(value.a<=1e-6) {o_color=vec4(0);return;}
    vec3 straight=aurea_linear_to_srgb(max(value.rgb/value.a,vec3(0)));
    float luma=dot(straight,vec3(.2126,.7152,.0722));
    float half_width=p.p0.y*.5;
    float t=half_width>1e-4?smoothstep(p.p0.x-half_width,p.p0.x+half_width,luma):step(p.p0.x,luma);
    vec3 rgb=mix(p.color.rgb,p.second.rgb,t);
    o_color=vec4(rgb*value.a,value.a);
}
