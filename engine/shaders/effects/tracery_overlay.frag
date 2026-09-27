#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_boxes;
layout(set=0,binding=AUREA_TEX2) uniform sampler2D u_edges;
layout(set=0,binding=AUREA_TEX3) uniform sampler2D u_labels;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap;vec4 texel;vec4 p0;vec4 p1;vec4 p2;vec4 p3;vec4 color;vec4 extra;vec4 display;} p;
float line(vec2 pt,vec2 a,vec2 b){vec2 ab=b-a;return length(pt-a-ab*clamp(dot(pt-a,ab)/max(dot(ab,ab),1e-5),0.,1.));}
float stroke(float d,float size){float aa=max(.5,.5/max(p.extra.y,.1));return 1.-smoothstep(max(0.,size*.5-aa),size*.5+aa,d);}
float digit(vec2 pt,int number){
    const int codes[10]=int[10](119,36,93,109,46,107,123,37,127,111);if(number<0||number>9)return 0.;
    const vec2 a[7]=vec2[7](vec2(0,0),vec2(0,0),vec2(4,0),vec2(0,4),vec2(0,4),vec2(4,4),vec2(0,8));
    const vec2 b[7]=vec2[7](vec2(4,0),vec2(0,4),vec2(4,4),vec2(4,4),vec2(0,8),vec2(4,8),vec2(4,8));
    float d=1000.;for(int j=0;j<7;++j)if((codes[number]&(1<<j))!=0)d=min(d,line(pt,a[j],b[j]));return stroke(d,1.);
}
float number3(vec2 pt,int n){n=clamp(n,0,999);return max(digit(pt,n/100),max(digit(pt-vec2(6,0),(n/10)%10),digit(pt-vec2(12,0),n%10)));}
float number5(vec2 pt,int n){n=clamp(n,0,99999);return max(max(digit(pt,n/10000),digit(pt-vec2(6,0),(n/1000)%10)),number3(pt-vec2(12,0),n%1000));}
float connector(vec2 pt,vec2 from,vec2 to){
    float a=stroke(line(pt,from,to),max(p.p0.y,1.));
    if(p.p1.w>.5){vec2 direction=(to-from)/max(length(to-from),1e-5),normal=vec2(-direction.y,direction.x);float len=max(4.,p.p0.z*2.);a=max(a,stroke(line(pt,to,to-direction*len+normal*len*.45),max(p.p0.y,1.)));a=max(a,stroke(line(pt,to,to-direction*len-normal*len*.45),max(p.p0.y,1.)));}return a;
}
void main(){
    vec4 src=p.p2.y>.5?vec4(0):texture(u_tex0,v_uv);
    if(p.p2.z>.5){float a=texture(u_labels,v_uv).a;o_color=vec4(vec3(a),1);return;}
    vec2 pt=v_uv*p.texel.xy;float coverage=0.;int limit=int(p.p0.x);
    for(int j=0;j<64;++j){if(j>=limit)break;vec4 center=texelFetch(u_boxes,ivec2(j,1),0);if(center.a<.5)continue;vec4 box=texelFetch(u_boxes,ivec2(j,0),0);
        vec2 lo=box.xy*p.texel.xy-p.p2.w,hi=box.zw*p.texel.xy+p.p2.w,middle=center.xy*p.texel.xy;
        vec2 q=abs(pt-(lo+hi)*.5)-(hi-lo)*.5;float signedDistance=length(max(q,0.))+min(max(q.x,q.y),0.);
        float border=stroke(abs(signedDistance),p.p0.y);
        if(p.p1.y<.999){vec2 cornerDistance=min(abs(pt-lo),abs(pt-hi));float fraction=clamp(p.p1.y,0.,1.);if(cornerDistance.x>(hi.x-lo.x)*fraction*.5&&cornerDistance.y>(hi.y-lo.y)*fraction*.5)border=0.;}
        coverage=max(coverage,max(p.p0.y>0.?border:0.,signedDistance<0.?p.p1.x:0.));
        vec2 d=pt-middle;float size=p.p0.z;int marker=int(p.p1.z);
        if(size>0.&&marker<3){float shape=marker==0?length(d)-size:(marker==1?min(line(d,vec2(-size,0),vec2(size,0)),line(d,vec2(0,-size),vec2(0,size))):min(line(d,vec2(-size),vec2(size)),line(d,vec2(-size,size),vec2(size,-size))));coverage=max(coverage,marker==0?1.-smoothstep(-.5,.5,shape):stroke(shape,max(p.p0.y,1.)));}
        int parent=int(texelFetch(u_edges,ivec2(j,0),0).r+.5)-1;
        if(parent>=0){vec2 to=texelFetch(u_boxes,ivec2(parent,1),0).xy*p.texel.xy;coverage=max(coverage,connector(pt,middle,to));}
        if(int(p.p0.w)==3)for(int k=0;k<16;++k){if(k>=j)break;vec4 other=texelFetch(u_boxes,ivec2(k,1),0);if(other.a>.5)coverage=max(coverage,connector(pt,middle,other.xy*p.texel.xy));}
        if(p.p2.x>.5){vec2 at=pt-(lo+vec2(0,-12));if(at.x>=-2.&&at.x<66.&&at.y>=-2.&&at.y<10.){float label=p.p2.x<1.5?number3(at,j+1):max(number5(at,int(middle.x)),number5(at-vec2(36,0),int(middle.y)));coverage=max(coverage,label);}}
    }
    float alpha=clamp(coverage*p.color.a*p.extra.x,0.,1.);o_color=vec4(p.color.rgb*alpha,alpha)+src*(1.-alpha);
}
