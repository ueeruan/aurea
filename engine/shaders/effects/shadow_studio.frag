#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_blur;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap;vec4 texel;vec4 p0;vec4 p1;vec4 p2;vec4 p3;vec4 color;vec4 farColor;vec4 blurMap;vec4 dimensions;} p;
void main(){
    vec2 sourceUv=v_uv*p.uvMap.xy+p.uvMap.zw;vec4 src=texture(u_tex0,sourceUv);
    vec2 pixel=p.texel.xy+v_uv*p.texel.zw;vec2 direction=vec2(cos(p.p0.z),sin(p.p0.z));
    if(p.p0.x>1.5){vec2 delta=pixel-(p.dimensions.xy+p.p1.xy*p.dimensions.zw);direction=delta/max(length(delta),1e-5);}
    direction*=p.p1.w;int count=int(p.p3.x);float best=0.;vec3 rgb=vec3(0);
    for(int j=0;j<128;++j){if(j>=count)break;float t=p.p0.x<.5?1.:float(j)/max(1.,float(count-1));
        vec2 shift=direction*(p.p0.y*t);vec2 uv=sourceUv-shift/p.dimensions.zw;
        vec4 hard=texture(u_tex0,uv);vec2 buv=v_uv*p.blurMap.xy+p.blurMap.zw-shift/(p.texel.zw/p.blurMap.xy);
        vec4 soft=texture(u_blur,buv);vec4 caster=mix(hard,soft,sqrt(t));float coverage=p.p1.z>.5?1.-caster.a:caster.a;
        float shadeT=p.p0.x<.5?0.:t;
        coverage=clamp(coverage*(1.+p.p2.z*4.),0.,1.);vec4 tint=mix(p.color,p.farColor,shadeT);
        float a=coverage*tint.a*pow(max(1.-shadeT,1e-4),p.p2.y*4.);
        if(a>best){best=a;rgb=p.p2.x>.5?unpremultiply(caster).rgb*tint.rgb:tint.rgb;}
        if(p.p0.x<.5)break;
    }
    best*=p.p0.w;
    if(p.p3.y>0.)best*=1.-p.p3.y*.7*aurea_hash3(pixel,uint(p.p3.z));
    if(p.p1.z>.5)best*=src.a;
    vec4 shadow=vec4(rgb*best,best);
    if(p.p2.w>.5){o_color=shadow;return;}
    o_color=p.p1.z>.5?vec4(shadow.rgb+src.rgb*(1.-best/max(src.a,1e-5)),src.a):src+shadow*(1.-src.a);
}
