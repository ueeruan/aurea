#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_tex1;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap;vec4 texel;vec4 p0;vec4 p1;vec4 p2;vec4 p3;vec4 color;} p;
int width(){return int(p.texel.x);}
int height(){return int(p.texel.y);}
int label(vec4 v){return v.a>.5?int(v.g-1.)*width()+int(v.r-1.):2147483647;}
vec4 fetch(ivec2 xy){if(any(lessThan(xy,ivec2(0)))||xy.x>=width()||xy.y>=height())return vec4(0);return texelFetch(u_tex0,xy,0);}
int countAt(int index){vec4 v=texelFetch(u_tex0,ivec2(index%width(),index/width()),0);return int(v.r+.5)+128*int(v.g+.5);}
vec4 countValue(int count){return vec4(float(count%128),float(count/128),0,1);}
void main(){
    ivec2 xy=ivec2(gl_FragCoord.xy);int stage=int(p.p3.x+.5);
    if(stage==0){
        vec2 d=vec2(p.p0.y)/p.texel.xy;vec4 c=texture(u_tex0,v_uv);
        if(p.p0.y>0.)c=(c*4.+texture(u_tex0,v_uv+vec2(d.x,0))+texture(u_tex0,v_uv-vec2(d.x,0))+texture(u_tex0,v_uv+vec2(0,d.y))+texture(u_tex0,v_uv-vec2(0,d.y)))/8.;
        vec3 rgb=aurea_linear_to_srgb(unpremultiply(c).rgb);vec3 key=aurea_linear_to_srgb(p.color.rgb);
        bool selected=c.a>.01&&length(rgb-key)/1.7320508<=p.p0.x;
        o_color=selected?vec4(vec2(xy)+1.,0,1):vec4(0);return;
    }
    if(stage==1){
        vec4 v=fetch(xy);if(v.a<.5){o_color=vec4(0);return;}
        const ivec2 offsets[4]=ivec2[4](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1));
        for(int j=0;j<4;++j){vec4 n=fetch(xy+offsets[j]);if(label(n)<label(v))v=n;}
        vec4 parent=fetch(ivec2(v.rg)-1);if(label(parent)<label(v))v=parent;o_color=v;return;
    }
    if(stage==2){vec4 v=fetch(xy);o_color=countValue(v.a>.5&&all(equal(ivec2(v.rg),xy+1))?1:0);return;}
    if(stage==3){int index=xy.y*width()+xy.x;int step=int(p.p3.y);int count=countAt(index);if(index>=step)count+=countAt(index-step);o_color=countValue(count);return;}
    if(stage==4){
        int rank=xy.x+1,total=countAt(width()*height()-1);if(rank>total){o_color=vec4(0);return;}
        int lo=0,hi=width()*height()-1;
        for(int j=0;j<15;++j){if(lo>=hi)break;int mid=(lo+hi)/2;if(countAt(mid)<rank)lo=mid+1;else hi=mid;}
        o_color=vec4(float(lo%width()+1),float(lo/width()+1),0,1);return;
    }
    if(stage==5){
        vec4 root=texelFetch(u_tex1,ivec2(xy.x,0),0);if(root.a<.5){o_color=vec4(0);return;}
        int left=width(),right=-1,count=0;
        for(int x=0;x<128;++x){if(x>=width())break;vec4 v=fetch(ivec2(x,xy.y));if(v.a>.5&&all(equal(v.rg,root.rg))){left=min(left,x);right=max(right,x);++count;}}
        o_color=count>0?vec4(float(left)/p.texel.x,float(right+1)/p.texel.x,float(count),1):vec4(0);return;
    }
    if(stage==6){
        vec4 box=vec4(1,1,0,0);float area=0.;vec2 sum=vec2(0);
        for(int row=0;row<128;++row){if(row>=height())break;vec4 bounds=texelFetch(u_tex0,ivec2(xy.x,row),0);if(bounds.a<.5)continue;
            box.xy=min(box.xy,vec2(bounds.r,float(row)/p.texel.y));box.zw=max(box.zw,vec2(bounds.g,float(row+1)/p.texel.y));area+=bounds.b;
            sum+=vec2((bounds.r+bounds.g)*.5,(float(row)+.5)/p.texel.y)*bounds.b;
        }
        bool valid=area>0.&&area>=p.p0.z*p.texel.x*p.texel.y;
        o_color=!valid?vec4(0):(xy.y==0?box:vec4(sum/area,area/(p.texel.x*p.texel.y),1));return;
    }
    int limit=int(p.p0.w),mode=int(p.p1.x),idx=xy.x;
    vec2 centers[64];bool valid[64];bool visited[64];float best[64];int parent[64];
    int first=-1;
    for(int j=0;j<64;++j){valid[j]=false;visited[j]=false;best[j]=1e30;parent[j]=-1;if(j<limit){vec4 v=texelFetch(u_tex0,ivec2(j,1),0);centers[j]=v.xy*p.texel.zw;valid[j]=v.a>.5;if(valid[j]&&first<0)first=j;}}
    if(first<0||!valid[idx]){o_color=vec4(0);return;}
    if(mode==4){
        best[first]=0.;
        for(int step=0;step<64;++step){if(step>=limit)break;int nearest=-1;float d=1e30;
            for(int j=0;j<64;++j){if(j>=limit)break;if(valid[j]&&!visited[j]&&best[j]<d){d=best[j];nearest=j;}}
            if(nearest<0)break;visited[nearest]=true;
            for(int j=0;j<64;++j){if(j>=limit)break;if(!valid[j]||visited[j])continue;float distance2=dot(centers[j]-centers[nearest],centers[j]-centers[nearest]);if(distance2<best[j]){best[j]=distance2;parent[j]=nearest;}}
        }
    }else if(mode==1){for(int j=0;j<64;++j){if(j>=idx)break;if(valid[j])parent[idx]=j;}}
    else if(mode==2&&idx!=first)parent[idx]=first;
    o_color=vec4(float(parent[idx]+1),0,0,1);
}
