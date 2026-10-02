#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_lut;
layout(set=0,binding=AUREA_PARAMS) uniform Params { vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color; } p;
vec3 entry(int i) {
    int width=textureSize(u_lut,0).x;
    return texelFetch(u_lut,ivec2(i%width,i/width),0).rgb;
}
vec3 table3(ivec3 i, int n) { return entry(i.x+n*(i.y+n*i.z)); }
float encode(float v) { return v <= .0031308 ? v * 12.92 : 1.055 * pow(v, 1.0/2.4) - .055; }
float decode(float v) { return v <= .04045 ? v / 12.92 : pow((v + .055)/1.055, 2.4); }
void main() {
    vec4 source=texture(u_tex0,v_uv);
    if(source.a<=1e-7) { o_color=vec4(0); return; }
    vec3 straight=source.rgb/source.a;
    vec3 rgb=vec3(encode(straight.r),encode(straight.g),encode(straight.b));
    int n=int(p.p0.y+.5);
    vec3 q=clamp((rgb-p.p1.xyz)/max(p.p2.xyz-p.p1.xyz,vec3(1e-7)),0,1)*float(n-1);
    ivec3 a=ivec3(floor(q)), b=min(a+1,ivec3(n-1)); vec3 f=fract(q), mapped;
    if(p.p0.z<2) {
        mapped=vec3(mix(entry(a.x).r,entry(b.x).r,f.x),mix(entry(a.y).g,entry(b.y).g,f.y),mix(entry(a.z).b,entry(b.z).b,f.z));
    } else {
        vec3 z0=mix(mix(table3(a,n),table3(ivec3(b.x,a.y,a.z),n),f.x),
                    mix(table3(ivec3(a.x,b.y,a.z),n),table3(ivec3(b.x,b.y,a.z),n),f.x),f.y);
        vec3 z1=mix(mix(table3(ivec3(a.x,a.y,b.z),n),table3(ivec3(b.x,a.y,b.z),n),f.x),
                    mix(table3(ivec3(a.x,b.y,b.z),n),table3(b,n),f.x),f.y);
        mapped=mix(z0,z1,f.z);
    }
    vec3 linear=clamp(vec3(decode(mapped.r),decode(mapped.g),decode(mapped.b)),vec3(-65504),vec3(65504));
    o_color=vec4(mix(source.rgb/source.a,linear,clamp(p.p0.x,0,1))*source.a,source.a);
}
