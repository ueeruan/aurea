#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 region;vec4 shape;vec4 optics;vec4 finish;vec4 light;vec4 spare;vec4 tint;
} p;
vec4 source(vec2 q){return textureLod(u_tex0,(q-p.region.xy)/p.region.zw,0.0);}
vec4 frosted(vec2 q,float radius) {
    if(radius<.01)return source(q);
    vec4 sum=vec4(0);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)sum+=source(q+vec2(x,y)*radius)*float((x==0?2:1)*(y==0?2:1));
    return sum/16.0;
}
void main() {
    vec2 point=p.region.xy+v_uv*p.region.zw;
    vec4 original=source(point);
    vec2 local=point-p.shape.xy,halfSize=max(p.shape.zw,vec2(.5));
    float radius=min(p.optics.y,min(halfSize.x,halfSize.y));
    vec2 q=abs(local)-halfSize+radius;
    float distance=length(max(q,vec2(0)))+min(max(q.x,q.y),0.0)-radius;
    float coverage=1.0-smoothstep(-.5,.5,distance);
    if(coverage<=0.0||p.optics.x<=0.0||p.finish.w<=0.0){o_color=original;return;}
    vec2 positive=max(q,vec2(0));
    vec2 normal=dot(positive,positive)>1e-6?normalize(positive):q.x>q.y?vec2(1,0):vec2(0,1);
    normal*=sign(local);
    float edge=1.0-smoothstep(0.0,max(p.optics.w,1.0),max(-distance,0.0));
    vec2 offset=(-normal*edge-local/max(halfSize.x,halfSize.y)*.25)*p.optics.z*p.optics.x;
    float blur=p.finish.y*p.optics.x,dispersion=p.finish.x*edge*p.optics.x;
    vec4 glass=frosted(point+offset,blur);
    if(dispersion>.001) {
        vec4 red=frosted(point+offset+normal*dispersion,blur),blue=frosted(point+offset-normal*dispersion,blur);
        // Recombine straight colors using common coverage; dispersion cannot
        // create opaque pixels out of transparent source material.
        glass.r=red.a>1e-6?red.r/red.a*glass.a:0.0;
        glass.b=blue.a>1e-6?blue.b/blue.a*glass.a:0.0;
    }
    glass.rgb=mix(glass.rgb,p.tint.rgb*glass.a,clamp(p.tint.a*p.optics.x,0.0,1.0));
    float specular=pow(max(dot(normal,-p.light.xy),0.0),6.0)*edge*p.finish.z*p.optics.x;
    glass.rgb+=vec3(specular)*glass.a;
    o_color=mix(original,glass,coverage*p.finish.w);
}
