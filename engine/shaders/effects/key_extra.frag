#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
vec3 encode(vec3 v) { v=max(v,vec3(0)); return mix(1.055*pow(v,vec3(1.0/2.4))-.055,12.92*v,lessThanEqual(v,vec3(.0031308))); }
vec3 decode(vec3 v) { v=max(v,vec3(0)); return mix(pow((v+.055)/1.055,vec3(2.4)),v/12.92,lessThanEqual(v,vec3(.04045))); }
void main() {
    vec2 uv=v_uv;
    if(p.p0.x>2.5) {
        vec2 point=p.p2.xy+v_uv*p.p2.zw-p.p1.xy;
        point=mod(point,max(p.p3.xy,vec2(1)));
        uv=(point-p.p2.xy)/p.p2.zw;
    }
    vec4 src=all(greaterThanEqual(uv,vec2(0)))&&all(lessThanEqual(uv,vec2(1)))?texture(u_tex0,uv):vec4(0);
    if(p.p0.x>2.5) { o_color=src; return; }
    if(p.p0.x>1.5) {
        float a=clamp(p.color.a*p.p0.y,0.0,1.0); o_color=src+vec4(p.color.rgb*a,a)*(1.0-src.a); return;
    }
    vec3 c=encode(src.a>1e-6?src.rgb/src.a:vec3(0)), k=encode(p.color.rgb), delta=c-k;
    float luma=dot(delta,vec3(.2126,.7152,.0722)); float distance;
    if(p.p0.x<.5) distance=length(delta-vec3(luma))*.5+abs(luma)*.25;
    else if(p.p1.z<.5) distance=length(delta)/sqrt(3.0);
    else if(p.p1.z<1.5) distance=abs(luma);
    else if(p.p1.z<2.5) distance=abs(delta.r);
    else if(p.p1.z<3.5) distance=abs(delta.g);
    else distance=abs(delta.b);
    float alpha=smoothstep(p.p1.x,p.p1.x+max(.00001,p.p1.y),distance);
    if(p.p1.w>.5) alpha=1.0-alpha;
    if(p.p0.x<.5 && p.p1.z>.5 && p.p1.w<.5) {
        vec3 chroma=k-vec3(dot(k,vec3(.2126,.7152,.0722)));
        c-=chroma*max(0.0,dot(c,chroma)/max(1e-6,dot(chroma,chroma)))*(1.0-alpha);
    }
    o_color=vec4(decode(c)*(src.a*alpha),src.a*alpha);
}
