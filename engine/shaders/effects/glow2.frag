#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_tex1;
layout(set=0,binding=AUREA_TEX2) uniform sampler2D u_tex2;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap;vec4 texel;vec4 p0;vec4 p1;vec4 p2;vec4 p3;vec4 color;} p;
vec3 tonemap(vec3 x,int mode){
    x=max(x,vec3(0));
    if(mode==1)return x/(1.+x);
    if(mode==2)return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.);
    if(mode==3){x=max(vec3(0),x-.004);return (x*(6.2*x+.5))/(x*(6.2*x+1.7)+.06);}
    if(mode==4)return 1.-exp(-x);
    if(mode==5)return min(x,vec3(1));return x;
}
void main(){
    if(p.p3.x<.5){
        vec4 c=texture(u_tex0,v_uv*p.uvMap.xy+p.uvMap.zw);float l=aurea_luma(c.rgb);
        float knee=max(1e-5,p.texel.x*p.texel.y),soft=clamp(l-p.texel.x+knee,0.,2.*knee);
        float k=clamp(max(soft*soft/(4.*knee),l-p.texel.x)/max(l,1e-5),0.,1.);
        float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b));float sat=(hi-lo)/max(hi,1e-5);
        o_color=c*k*mix(1.,sat,p.texel.z);return;
    }
    if(p.p3.x<1.5){vec4 lobe=texture(u_tex0,v_uv);vec4 old=texture(u_tex1,v_uv)*p.p0.w;vec3 light=lobe.rgb*p.p0.rgb*p.color.rgb;
        o_color=old+vec4(light,lobe.a*max(p.p0.x,max(p.p0.y,p.p0.z)));return;}
    vec4 src=texture(u_tex0,v_uv*p.uvMap.xy+p.uvMap.zw);
    vec4 light=texture(u_tex1,v_uv)*p.p0.x;
    if(p.p0.w>1.5){o_color=mix(src,texture(u_tex2,v_uv),p.p1.x);return;}
    float a=clamp(max(light.a,max(light.r,max(light.g,light.b))),0.,1.);
    vec4 glow=vec4(max(light.rgb,vec3(0)),a);
    vec4 result;
    if(p.p0.w>.5)result=glow;
    else if(p.p0.y>.5)result=vec4(src.rgb+min(glow.rgb,vec3(1))*(1.-clamp(src.rgb,0.,1.)),src.a+a*(1.-src.a));
    else result=vec4(src.rgb+glow.rgb,src.a+a*(1.-src.a));
    if(result.a>1e-6)result.rgb=tonemap(result.rgb/result.a,int(p.p0.z+.5))*result.a;
    o_color=mix(src,result,clamp(p.p1.x,0.,1.));
}
