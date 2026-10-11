#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D src;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D previous;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap,texel,p0,p1,p2,p3,color,q0,q1,q2,q3,region,curves[64];} p;
uint hash32(uint x) {x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
float noise(ivec2 cell,uint salt) {return float(hash32(uint(cell.x)^hash32(uint(cell.y))^hash32(uint(p.p0.y))^salt)&0xffffffu)/16777215.0;}
vec2 wrapped(vec2 point) {
    vec2 q=point/max(p.q2.xy,vec2(1));
    int mode=int(p.q1.x);
    if(mode==0) q=fract(q);
    else if(mode==1) q=1.0-abs(mod(q,2.0)-1.0);
    else if(mode==2) q=clamp(q,vec2(0),vec2(1));
    return (q*p.q2.xy-p.region.xy)/p.region.zw;
}
vec4 readSource(vec2 point,bool held) {
    if(int(p.q1.x)==3 && (any(lessThan(point,vec2(0)))||any(greaterThan(point,p.q2.xy)))) return vec4(0);
    vec2 uv=wrapped(point);
    return held?texture(previous,uv*p.q3.xy+p.q3.zw):texture(src,uv);
}
void main() {
    vec2 point=p.region.xy+v_uv*p.region.zw;
    vec4 original=texture(src,v_uv);
    if(dot(abs(p.p1),vec4(1))<1e-6 && p.q1.z<.5) {o_color=original;return;}
    int tick=int(floor(p.p0.x));
    int epoch=int(floor(p.p0.x/max(1.0,p.q2.z)));
    float nx=noise(ivec2(tick,0),1u)*2.0-1.0,ny=noise(ivec2(tick,1),2u)*2.0-1.0;
    vec2 q=point-p.q2.xy*.5;
    float angle=p.p3.z*nx*p.p1.z,c=cos(angle),s=sin(angle);
    q=mat2(c,s,-s,c)*q+p.q2.xy*.5-p.q0.xy*vec2(nx,ny)*p.p1.z;
    int band=int(floor(point.y/max(1.0,p.q2.y)*max(1.0,p.p2.w)));
    float shift=(noise(ivec2(band,epoch),11u)*2.0-1.0)*p.p1.y;
    q-=vec2(p.p2.y,p.p2.z)*shift;
    q.x-=tan(clamp(p.p3.y,-1.4,1.4))*(point.y-p.q2.y*.5)*shift;
    if(noise(ivec2(band,epoch),13u)<p.p3.x*p.p1.y) q.y=floor(q.y/(p.q2.y/max(1.0,p.p2.w)))*(p.q2.y/max(1.0,p.p2.w));
    vec2 blockSize=vec2(max(2.0,p.p0.w));
    if(int(p.p2.x)==1) blockSize.x=max(1.0,p.q2.x);
    if(int(p.p2.x)==2) blockSize.y=max(1.0,p.q2.y);
    ivec2 cell=ivec2(floor(q/blockSize));
    float damage=step(noise(cell+ivec2(0,epoch),19u),clamp(p.p1.x*.5,0.0,1.0));
    bool held=damage>.5 && p.p0.z>.5;
    if(damage>.5) {
        vec2 randomOffset=vec2(noise(cell+ivec2(epoch),23u),noise(cell+ivec2(epoch),29u))-.5;
        q+=randomOffset*blockSize*8.0*p.p1.x;
        if(int(p.p2.x)==0) q=(floor(q/blockSize)+.5)*blockSize;
    }
    vec4 value=readSource(q,held);
    float split=p.p3.w*p.p1.z;
    if(split>.001) {
        vec4 left=readSource(q-vec2(split,0),held),right=readSource(q+vec2(split,0),held);
        vec3 straight=unpremultiply(value).rgb;
        straight.r=unpremultiply(left).r;straight.b=unpremultiply(right).b;
        value.rgb=straight*value.a;
    }
    float flicker=(noise(ivec2(tick,2),31u)*2.0-1.0)*p.p1.w;
    vec3 straight=unpremultiply(value).rgb;
    float luma=dot(straight,vec3(.2126,.7152,.0722));
    straight=mix(vec3(luma),straight,max(0.0,1.0+flicker*p.q0.w))*exp2(flicker*p.q0.z*3.0);
    value.rgb=straight*value.a;
    o_color=mix(original,value,clamp(p.q1.y,0.0,1.0));
    if(p.q1.z>.5) {
        if(p.q1.z<1.5) o_color=vec4(0,0,0,1);
        float position=clamp(point.x/max(1.0,p.q2.x),0.0,1.0)*63.0;
        int a=int(floor(position)),b=min(a+1,63);
        vec4 values=mix(p.curves[a],p.curves[b],fract(position));
        float y=1.0-point.y/max(1.0,p.q2.y);
        for(int group=0;group<4;group++) {
            float line=1.0-smoothstep(.004,.012,abs(y-clamp(values[group],0.0,1.0)*.8));
            vec3 color=group==0?vec3(1,0,0):(group==1?vec3(0,1,0):(group==2?vec3(0,.3,1):vec3(1)));
            o_color.rgb=mix(o_color.rgb,color,line);
            o_color.a=max(o_color.a,line);
        }
    }
}
