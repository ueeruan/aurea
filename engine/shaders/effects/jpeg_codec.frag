#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_original;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap;vec4 texel;vec4 p0;vec4 p1;vec4 p2;vec4 p3;vec4 color;} p;
const float PI=3.14159265359;
const int QT[64]=int[64](16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99);
float basis(int f,int x){return .5*(f==0?.70710678118:1.)*cos(PI*(float(x)+.5)*float(f)/8.);}
vec3 rgbYcc(vec3 c){return vec3(dot(c,vec3(.299,.587,.114))-.5,dot(c,vec3(-.168736,-.331264,.5)),dot(c,vec3(.5,-.418688,-.081312)));}
vec3 yccRgb(vec3 c){float y=c.x+.5;return vec3(y+1.402*c.z,y-.344136*c.y-.714136*c.z,y+1.772*c.y);}
vec3 source(ivec2 xy){
    vec2 uv=(vec2(clamp(xy,ivec2(0),ivec2(p.texel.xy)-1))+.5)/p.texel.xy;
    vec3 c=rgbYcc(aurea_linear_to_srgb(unpremultiply(texture(u_original,uv)).rgb));
    if(p.p2.z>.5){
        ivec2 step=ivec2(2,p.p2.z>1.5?2:1);ivec2 origin=(xy/step)*step;vec2 chroma=vec2(0);
        for(int j=0;j<2;++j)for(int i=0;i<2;++i){ivec2 at=origin+ivec2(i,min(j,step.y-1));vec2 v=(vec2(clamp(at,ivec2(0),ivec2(p.texel.xy)-1))+.5)/p.texel.xy;chroma+=rgbYcc(aurea_linear_to_srgb(unpremultiply(texture(u_original,v)).rgb)).yz;}
        c.yz=chroma*.25;
    }return c;
}
void main(){
    int stage=int(p.p3.x+.5);ivec2 xy=ivec2(gl_FragCoord.xy);ivec2 block=(xy/8)*8;ivec2 f=xy-block;vec3 c=vec3(0);
    if(stage==0){for(int x=0;x<8;++x)c+=basis(f.x,x)*source(ivec2(block.x+x,xy.y));}
    else if(stage==1){
        for(int y=0;y<8;++y)c+=basis(f.y,y)*texelFetch(u_tex0,ivec2(xy.x,block.y+y),0).rgb;
        int index=f.y*8+f.x;float quality=clamp(p.p0.x,1.,100.);float scale=quality<50.?5000./quality:200.-quality*2.;
        float q=clamp(floor((float(QT[index])*scale+50.)/100.),1.,255.);
        if(p.p1.y>.5&&index==int(p.p1.z))q=p.p1.w;
        uint seed=uint(p.p0.w)+uint(max(0.,p.p1.x))*40503u;
        if(aurea_hash(uvec2(uint(index),seed))<p.p2.x/64.)q=1.+floor(aurea_hash(uvec2(uint(index)+777u,seed))*p.p2.y);
        q/=255.;c=round(c/q)*q;
        float r=aurea_hash(uvec2(block)^uvec2(seed+uint(index)*31337u,seed));
        if(float(index)>=p.color.y&&float(index)<=p.color.z&&r<p.p0.y){
            vec3 damage=vec3(aurea_hash(uvec2(block)^uvec2(seed+1u,uint(index))),aurea_hash(uvec2(block)^uvec2(seed+2u,uint(index))),aurea_hash(uvec2(block)^uvec2(seed+3u,uint(index))))*2.-1.;
            c+=damage*p.p0.z*(index==0?4.:.8);
        }
        if(index==0)c+=vec3(aurea_hash(uvec2(block)^uvec2(seed,99u))-.5)*p.p2.w*4.;
    }else if(stage==2){for(int y=0;y<8;++y)c+=basis(y,f.y)*texelFetch(u_tex0,ivec2(xy.x,block.y+y),0).rgb;}
    else{
        for(int x=0;x<8;++x)c+=basis(x,f.x)*texelFetch(u_tex0,ivec2(block.x+x,xy.y),0).rgb;
        vec4 src=texture(u_original,v_uv);vec3 decoded=aurea_srgb_to_linear(clamp(yccRgb(c),0.,1.));
        o_color=mix(src,vec4(decoded*src.a,src.a),clamp(p.color.x,0.,1.));return;
    }
    o_color=vec4(c,1.);
}
