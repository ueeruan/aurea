#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_original;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap;vec4 texel;vec4 p0;vec4 p1;vec4 p2;vec4 p3;vec4 color;} p;
const float PI=3.14159265359;
float phase(float x,float y){return .5*PI*x+PI*mod(y+floor(p.p2.x*29.97),2.);}
float pal(float y){return p.p0.y>.5?(mod(y,2.)<.5?1.:-1.):1.;}
void main(){
    vec2 size=p.uvMap.zw;
    if(p.p3.x<.5){
        vec2 pixel=floor(v_uv*size);float t=p.p2.x;uint salt=uint(p.p2.y)+uint(max(0.,floor(t*30.)))*40503u;
        float bend=p.texel.z*(sin(pixel.y*.021+t*2.)+.25*sin(pixel.y*.091-t*3.));
        float head=smoothstep(.92,1.,v_uv.y)*p.p1.x*(aurea_hash(uvec2(salt,uint(pixel.y)))*2.-1.)*100.;
        vec2 uv=vec2(v_uv.x+(bend+head)/size.x,fract(v_uv.y+t*p.p1.y));
        vec4 src=unpremultiply(texture(u_tex0,uv));vec3 rgb=aurea_linear_to_srgb(src.rgb);
        float y=dot(rgb,vec3(.299,.587,.114));float i=dot(rgb,vec3(.595716,-.274453,-.321263));float q=dot(rgb,vec3(.211456,-.522591,.311135));
        float a=phase(pixel.x,pixel.y);float signal=y+i*cos(a)+q*pal(pixel.y)*sin(a);
        float noise=(aurea_hash(uvec2(pixel)^uvec2(salt,salt*7u))*2.-1.)*p.texel.x;
        if(aurea_hash(uvec2(uint(pixel.y),salt))<p.p2.z*.08){signal=.8+noise; y=.8;}
        o_color=vec4(signal+noise,y+noise,0,src.a);return;
    }
    vec2 px=floor(v_uv*size);float radius=mix(12.,4.,p.texel.y);vec3 total=vec3(0);float weights=0;
    for(int j=-12;j<=12;++j){float f=float(j);float weight=max(0.,1.-abs(f)/(radius+1.));
        vec2 at=vec2(clamp(px.x+f+p.p1.w,0.,size.x-1.),px.y);vec4 s=texelFetch(u_tex0,ivec2(at),0);
        float a=phase(at.x,at.y)+p.p0.z;float carrier=s.r-mix(s.g,0.,p.p0.w);
        total+=vec3(s.r,2.*carrier*cos(a),2.*carrier*sin(a)*pal(at.y))*weight;weights+=weight;
    }
    total/=max(weights,1.);vec4 center=texelFetch(u_tex0,ivec2(px),0);float y=mix(center.g,total.x,p.p0.w);
    vec2 iq=total.yz*p.texel.w;vec3 rgb=vec3(y+.9563*iq.x+.6210*iq.y,y-.2721*iq.x-.6474*iq.y,y-1.107*iq.x+1.7046*iq.y);
    rgb*=1.-p.p1.z*(.5+.5*cos(v_uv.y*size.y*6.2831853));
    vec4 src=texture(u_original,v_uv);vec4 result=vec4(aurea_srgb_to_linear(clamp(rgb,0.,1.))*center.a,center.a);
    o_color=mix(src,result,clamp(p.p0.x,0.,1.));
}
