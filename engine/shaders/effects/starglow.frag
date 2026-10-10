#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D tex1;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap,texel,p0,p1,p2,p3,color;
    vec4 rays[8],colors[15],mapTypes,region,size,circle;
} p;
vec3 mapColor(int index,float t) {
    int count=int(p.mapTypes[index]);
    if(count==0) return p.colors[index*5].rgb;
    float position=clamp(t,0.0,1.0)*float(count==1?2:4);
    int a=int(floor(position)),b=min(a+1,count==1?2:4);
    if(count==1) {a*=2;b*=2;}
    return mix(p.colors[index*5+a].rgb,p.colors[index*5+b].rgb,fract(position));
}
float extractBright(vec2 uv) {
    vec4 value=texture(tex0,uv);
    vec3 encoded=aurea_linear_to_srgb(max(unpremultiply(value).rgb,vec3(0)));
    int channel=int(p.p2.w);
    float tone=dot(encoded,vec3(.2126,.7152,.0722));
    if(channel==0) tone=(max(encoded.r,max(encoded.g,encoded.b))+min(encoded.r,min(encoded.g,encoded.b)))*.5;
    if(channel>=2&&channel<=4) tone=encoded[channel-2];
    if(channel==5) tone=value.a;
    float soft=max(.0001,p.p0.w);
    return max(0.0,tone-p.p0.z)*smoothstep(p.p0.z-soft,p.p0.z+soft,tone)*value.a;
}
void main() {
    int mode=int(p.p3.x);
    vec2 point=p.region.xy+v_uv*p.region.zw;
    if(mode==0) {
        vec2 uv=v_uv*p.uvMap.xy+p.uvMap.zw;
        vec2 offset=p.texel.xy*.25;
        float light=.25*(extractBright(uv+vec2(-offset.x,-offset.y))+
                         extractBright(uv+vec2( offset.x,-offset.y))+
                         extractBright(uv+vec2(-offset.x, offset.y))+
                         extractBright(uv+vec2( offset.x, offset.y)));
        if(p.p3.z>.5) light*=1.0-smoothstep(max(0.0,p.circle.z-p.circle.w),p.circle.z+max(.0001,p.circle.w),length(point-p.circle.xy));
        o_color=vec4(vec3(light),light);return;
    }
    if(mode==1) {
        int ray=int(p.p3.y);vec4 settings=p.rays[ray];
        vec3 sum=vec3(0);
        if(settings.z>0.001) {
            // Gauss-like concentration near the origin, long one-sided tail.
            // Quadrature and attenuation are independently authored estimates.
            for(int i=0;i<48;i++) {
                float t=(float(i)+.5)/48.0;
                float distance=t*t*settings.z;
                vec2 uv=v_uv-settings.xy*distance/p.region.zw;
                float intensity=texture(tex0,uv).r;
                float shimmer=1.0+p.p1.x*sin(6.2831853*(p.p1.z+t*p.p1.y+float(ray)*.173)+dot(point/p.size.xy,vec2(3.1,4.7)));
                sum+=mapColor(clamp(int(settings.w),0,2),t)*intensity*exp(-4.0*t)*max(0.0,shimmer)*2.0*t;
            }
            sum*=p.p0.y*settings.z/48.0;
        }
        vec4 previous=ray==0?vec4(0):texture(tex1,v_uv);
        sum+=previous.rgb;
        o_color=vec4(sum,clamp(max(sum.r,max(sum.g,sum.b)),0.0,1.0));return;
    }
    vec4 source=texture(tex0,v_uv*p.uvMap.xy+p.uvMap.zw)*p.p2.x;
    vec4 glow=texture(tex1,v_uv)*p.p2.y;
    vec3 result;
    int blend=int(p.p2.z);
    if(blend==1) result=source.rgb+glow.rgb;
    else if(blend==2) result=glow.rgb+source.rgb*(1.0-glow.a);
    else if(blend==3) result=max(source.rgb,glow.rgb);
    else result=source.rgb+glow.rgb-source.rgb*clamp(glow.rgb,0.0,1.0);
    o_color=vec4(result,clamp(source.a+glow.a-source.a*glow.a,0.0,1.0));
}
