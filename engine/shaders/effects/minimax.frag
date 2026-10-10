#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_tex1;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap,texel;
    vec4 p0; // kind: axis/merge/resolve/mix; dilation; comparison channel; amount
    vec4 p1; // axis direction.xy, inclusive integer low/high
    vec4 p2; // source-grid offset.xy
    vec4 p3,color,otherUv;
} p;
float probe(vec4 raw) {
    vec4 s=unpremultiply(raw);
    int channel=int(p.p0.z+.5);
    if(channel==1)return s.a;
    if(channel==2)return max(max(s.r,s.g),s.b);
    if(channel==3)return max(max(s.r,s.g),max(s.b,s.a));
    return aurea_luma(s.rgb)*s.a;
}
// A total order is associative across axes/parity branches. Other than center
// preservation, equal probes previously depended on row traversal order.
bool better(vec4 a,vec4 b) {
    float ap=probe(a),bp=probe(b);
    if(ap!=bp)return p.p0.y>.5 ? ap>bp : ap<bp;
    for(int c=0;c<4;++c)if(a[c]!=b[c])return a[c]>b[c];
    return false;
}
void main() {
    int kind=int(p.p0.x+.5);
    vec2 uv=v_uv*p.uvMap.xy+p.uvMap.zw;
    if(kind==0) {
        int low=int(p.p1.z),high=int(p.p1.w);
        vec4 best=texture(u_tex0,uv+(p.p2.xy+p.p1.xy*float(low))*p.texel.xy);
        for(int i=low+1;i<=high;++i) {
            vec4 candidate=texture(u_tex0,uv+(p.p2.xy+p.p1.xy*float(i))*p.texel.xy);
            if(better(candidate,best))best=candidate;
        }
        // Preserve premultiplied winners exactly across intermediate stores.
        o_color=best;
    } else {
        vec4 a=texture(u_tex0,uv),b=texture(u_tex1,v_uv*p.otherUv.xy+p.otherUv.zw);
        if(kind==1)o_color=better(b,a)?b:a;
        else if(kind==2)o_color=probe(a)==probe(b)?a:b;
        else {
            vec4 s=mix(unpremultiply(a),unpremultiply(b),clamp(p.p0.w,0.,1.));
            o_color=premultiply(vec4(max(s.rgb,vec3(0)),s.a));
        }
    }
}
