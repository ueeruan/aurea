#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
vec4 samplePoint(vec2 point) {
    vec2 uv=(point-p.p2.xy)/p.p2.zw;
    if(p.p1.w<.5 && (any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))))return vec4(0);
    return texture(u_tex0,clamp(uv,vec2(0),vec2(1)));
}
void main() {
    vec2 point=p.uvMap.xy+v_uv*p.uvMap.zw;
    vec2 r=p.p0.xy,g=p.p0.zw,b=p.p1.xy;
    if(p.p3.x>.5) {
        vec2 delta=point-p.p0.yz;
        // Bounded radial displacement avoids singularities and unbounded textures.
        float radius=length(delta);
        r=radius>.0001 ? delta/radius*p.p0.x*min(radius/p.p0.w,1.0) : vec2(0);
        g=vec2(0);b=-r;
    }
    vec4 red=samplePoint(point-r),green=samplePoint(point-g),blue=samplePoint(point-b);
    vec4 split=vec4(red.r,green.g,blue.b,max(red.a,max(green.a,blue.a)));
    o_color=mix(samplePoint(point),split,clamp(p.p1.z,0,1));
}
