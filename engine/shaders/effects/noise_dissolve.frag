#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 region;vec4 size;vec4 noiseControl;vec4 travel;vec4 edge;vec4 spare;vec4 color;
} p;
uint hash(uvec2 q,uint seed){uint n=q.x*1597334677u+q.y*3812015801u+seed*958282891u;n^=n>>16;n*=2246822519u;n^=n>>13;return n;}
float random(ivec2 q,uint seed){return float(hash(uvec2(q),seed)&0x00ffffffu)/16777215.0;}
float noise(vec2 q,uint seed) {
    ivec2 cell=ivec2(floor(q));vec2 f=fract(q);f=f*f*(3.0-2.0*f);
    return mix(mix(random(cell,seed),random(cell+ivec2(1,0),seed),f.x),mix(random(cell+ivec2(0,1),seed),random(cell+ivec2(1,1),seed),f.x),f.y);
}
void main() {
    vec4 original=textureLod(u_tex0,v_uv,0.0);
    float progress=p.noiseControl.x;
    if(progress<=0.0||p.edge.z<=0.0){o_color=original;return;}
    if(progress>=1.0){o_color=original*(1.0-p.edge.z);return;}
    vec2 point=p.region.xy+v_uv*p.region.zw;
    vec2 q=point/max(p.noiseControl.y,1.0)+p.travel.w*vec2(17.31,29.73);
    float field=0.0,weight=1.0,total=0.0;
    for(int i=0;i<4;++i){if(float(i)>=p.noiseControl.z)break;field+=noise(q,uint(p.travel.z)+uint(i)*73u)*weight;total+=weight;weight*=.5;q=q*2.03+vec2(11.7,5.3);}
    field/=max(total,.001);
    vec2 axis=vec2(cos(p.travel.x),sin(p.travel.x));
    float position=.5+dot(point/max(p.size.xy,vec2(1))-.5,axis)/max(abs(axis.x)+abs(axis.y),.001);
    if(p.edge.w>.5)position=1.0-position;
    field=mix(field,clamp(position,0.0,1.0)*.75+field*.25,p.travel.y);
    if(p.travel.y<.001&&p.edge.w>.5)field=1.0-field;
    float softness=max(p.noiseControl.w,.00001);
    float coverage=smoothstep(progress-softness,progress+softness,field);
    float rim=p.edge.x>0.0?1.0-smoothstep(0.0,p.edge.x,max(field-progress,0.0)):0.0;
    vec4 result=original*coverage;
    result.rgb+=p.color.rgb*p.color.a*p.edge.y*rim*result.a;
    o_color=mix(original,result,p.edge.z);
}
