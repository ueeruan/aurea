#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 region;vec4 options;vec4 dither;vec4 colorA;vec4 colorB;vec4 colorC;vec4 colorD;vec4 spare;
} p;
uint hash(uvec2 q,uint seed){uint n=q.x*1597334677u+q.y*3812015801u+seed*958282891u;n^=n>>16;n*=2246822519u;n^=n>>13;return n;}
float threshold(ivec2 cell) {
    int mode=int(p.options.w+.5);if(mode==0)return 0.0;
    if(mode==2)return (float(hash(uvec2(cell),uint(p.dither.y))&65535u)/65536.0-.5)*p.dither.x;
    uint x=uint(cell.x)&3u,y=uint(cell.y)&3u,rank=0u;
    for(uint bit=0u;bit<2u;++bit)rank=(rank<<2u)|((((x^y)>>bit)&1u)<<1u)|((y>>bit)&1u);
    return ((float(rank)+.5)/16.0-.5)*p.dither.x;
}
void main() {
    vec4 original=textureLod(u_tex0,v_uv,0.0);
    if(p.dither.z<=0.0){o_color=original;return;}
    float pixel=max(p.options.x,1.0);vec2 point=p.region.xy+v_uv*p.region.zw;
    ivec2 cell=ivec2(floor(point/pixel));vec2 samplePoint=(vec2(cell)+.5)*pixel;
    vec4 sampleColor=textureLod(u_tex0,(samplePoint-p.region.xy)/p.region.zw,0.0);
    if(sampleColor.a<=1e-6){o_color=mix(original,vec4(0),p.dither.z);return;}
    vec3 encoded=clamp(aurea_linear_to_srgb(sampleColor.rgb/sampleColor.a),0.0,1.0);
    float d=threshold(cell);int palette=int(p.options.y+.5);vec4 mapped=vec4(0,0,0,1);
    if(palette==3) {
        vec4 colors[4]=vec4[4](p.colorA,p.colorB,p.colorC,p.colorD);
        vec3 target=clamp(encoded+d*.3,0.0,1.0);float best=1e20;
        for(int i=0;i<4;++i){vec3 delta=target-aurea_linear_to_srgb(colors[i].rgb);float distance=dot(delta*delta,vec3(.2126,.7152,.0722));if(distance<best){best=distance;mapped=colors[i];}}
    } else {
        vec3 intervals=palette==0?vec3(7,7,3):vec3(max(p.options.z-1.0,1.0));
        if(palette==2)encoded=vec3(dot(encoded,vec3(.2126,.7152,.0722)));
        mapped.rgb=aurea_srgb_to_linear(floor(clamp(encoded+d/intervals,0.0,1.0)*intervals+.5)/intervals);
    }
    float alpha=sampleColor.a*mapped.a;
    o_color=mix(original,vec4(mapped.rgb*alpha,alpha),p.dither.z);
}
