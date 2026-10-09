#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D src;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D mapTexture;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap,texel,p0,p1,p2,p3,color,mapRegion,region,size,options;
    mat4 compFromLayer;
} p;
float channel(vec4 value,int mode) {
    if(mode==4) return value.a;
    vec3 encoded=aurea_linear_to_srgb(max(unpremultiply(value).rgb,vec3(0)));
    return mode==0?dot(encoded,vec3(.2126,.7152,.0722)):encoded[mode-1];
}
vec3 field(vec2 point) {
    vec2 q=point+p.p3.zw;
    if(p.size.z>.5) {vec4 c=p.compFromLayer*vec4(q,0,1);q=c.xy/max(abs(c.w),1e-6);}
    vec2 uv=(q-p.mapRegion.xy)/max(p.mapRegion.zw,vec2(.001));
    if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1)))) return vec3(0);
    vec4 value=texture(mapTexture,uv);
    vec3 f=vec3(channel(value,int(p.p2.x)),channel(value,int(p.p2.y)),channel(value,int(p.p2.z)));
    return (f-p.p1.z)*2.0*value.a;
}
vec2 warp(vec2 point,float chromatic) {
    vec2 center=p.p3.xy*p.size.xy;
    int count=clamp(int(p.p2.w),1,16);
    float amount=p.p1.y*chromatic/float(count);
    vec2 q=point;
    for(int i=0;i<16;i++) {
        if(i>=count) break;
        vec3 f=field(q);
        q-=p.p0.xy*f.xy*amount;
        float angle=-p.p1.x*f.z*amount,c=cos(angle),s=sin(angle);
        q=mat2(c,s,-s,c)*(q-center)*exp2(clamp(-p.p0.zw*f.z*amount,vec2(-8),vec2(8)))+center;
    }
    return q;
}
vec4 sourceAt(vec2 point) {
    vec2 q=point/max(p.size.xy,vec2(1));int edge=int(p.options.x);
    if(edge==0&&(any(lessThan(q,vec2(0)))||any(greaterThan(q,vec2(1))))) return vec4(0);
    if(edge==1) q=fract(q);else if(edge==2) q=1.0-abs(mod(q,2.0)-1.0);
    else q=clamp(q,vec2(0),vec2(1));
    return texture(src,(q*p.size.xy-p.region.xy)/p.region.zw);
}
vec4 filtered(vec2 q,vec2 dx,vec2 dy) {
    if(p.options.y<.5) return sourceAt(q);
    return (sourceAt(q-dx-dy)+sourceAt(q+dx-dy)+sourceAt(q-dx+dy)+sourceAt(q+dx+dy))*.25;
}
void main() {
    vec2 point=p.region.xy+v_uv*p.region.zw;
    vec2 q=warp(point,1.0),dx=dFdx(q)*.25,dy=dFdy(q)*.25;
    vec4 value=filtered(q,dx,dy);
    if(p.p1.w>0.0) {
        vec4 red=filtered(warp(point,1.0+p.p1.w),dx,dy),blue=filtered(warp(point,1.0-p.p1.w),dx,dy);
        vec3 straight=unpremultiply(value).rgb;
        straight.r=unpremultiply(red).r;straight.b=unpremultiply(blue).b;
        value.rgb=straight*value.a;
    }
    o_color=mix(texture(src,v_uv),value,clamp(p.options.z,0.0,1.0));
}
