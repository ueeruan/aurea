#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D sourceTex;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D backgroundTex;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap,texel,p0,p1,p2,p3,color;
    vec4 q0,q1,q2,q3,q4,q5,q6;
    mat4 compFromLayer;
} p;

// Original deterministic gradient field; no vendor shader or pattern asset.
uint hash32(uint x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
float hash(ivec3 c) {
    uint h=hash32(uint(c.x)^hash32(uint(c.y))^hash32(uint(c.z))^floatBitsToUint(p.p1.z));
    return float(h&0xffffffu)/16777215.0;
}
float noise(vec3 v) {
    ivec3 i=ivec3(floor(v)); vec3 f=fract(v); f=f*f*(3.0-2.0*f);
    float a=mix(mix(hash(i),hash(i+ivec3(1,0,0)),f.x),mix(hash(i+ivec3(0,1,0)),hash(i+ivec3(1,1,0)),f.x),f.y);
    float b=mix(mix(hash(i+ivec3(0,0,1)),hash(i+ivec3(1,0,1)),f.x),mix(hash(i+ivec3(0,1,1)),hash(i+ivec3(1)),f.x),f.y);
    return mix(a,b,f.z);
}
float fbm(vec3 v) {
    float s=0.0,w=0.5,n=0.0;
    for(int i=0;i<10;i++) { if(i>=int(p.p1.w)) break; s+=w*noise(v);n+=w;v=v*2.03+vec3(11.7,5.3,3.1);w*=0.5; }
    return s/max(n,0.0001);
}
mat2 rotation(float a) { float c=cos(a),s=sin(a);return mat2(c,s,-s,c); }
float field(vec2 point) {
    vec2 uv=point/p.q5.xy;
    vec2 v=(uv-0.5-p.p2.xy)*vec2(p.q5.x/p.q5.y*p.p1.y,1.0)*p.p1.x;
    float f;
    if(p.p0.w<0.5) {
        // Two coupled fields deform the cells; signed amount changes curvature.
        vec2 warp=vec2(fbm(vec3(v+vec2(7,0),p.p2.z)),fbm(vec3(v+vec2(0,13),p.p2.z)))-0.5;
        v+=rotation(radians(p.p3.z))*warp*p.p3.x*2.0;
        f=fbm(vec3(v,p.p2.z));
        float soft=max(0.008,p.p3.y);
        f=0.5+atan((f-0.5)/soft)*0.318309886;
    } else {
        // Interfering filaments, accumulated over independently phased layers.
        float sum=0.0; int layers=clamp(int(p.p3.z),1,16);
        vec2 axis=vec2(cos(radians(p.p3.y)),sin(radians(p.p3.y)));
        for(int i=0;i<16;i++) {
            if(i>=layers) break;
            float phase=float(i)*2.39996323;
            vec2 d=vec2(cos(phase),sin(phase));
            float carrier=dot(v,d)+4.0*fbm(vec3(v+float(i)*9.1,p.p2.z*0.15));
            sum+=sin(carrier+phase+p.p2.z+p.p3.x*dot(v,axis));
        }
        f=0.5+0.5*sin(sum*1.7/sqrt(float(layers)));
    }
    vec2 axis=vec2(cos(p.q0.x),sin(p.q0.x));
    float grad=dot(uv-0.5,axis);
    // Normalize gradient range, retaining exact endpoints independently.
    return clamp((f+p.p3.w*grad+abs(p.p3.w)*0.5)/(1.0+abs(p.p3.w)),0.0,1.0);
}
void main() {
    vec2 point=p.q4.xy+v_uv*p.q4.zw;
    vec4 src=texture(sourceTex,v_uv);
    if(p.q5.z>0.5 && p.q5.z<1.5) { src.rgb=src.a>1e-6?src.rgb/src.a:vec3(0);src.a=1.0; }
    vec4 bg=vec4(0);
    if(p.q5.w>0.5) { vec4 cp=p.compFromLayer*vec4(point,0,1);bg=texture(backgroundTex,cp.xy/max(abs(cp.w),1e-6)/p.q6.xy); }
    float progress=p.p0.x;
    float completion=p.p0.z<0.5?progress:1.0-progress;
    if(completion<=0.0) { o_color=src;return; }
    if(completion>=1.0) { o_color=bg;return; }
    float f=field(point);
    float aa=max(fwidth(f),0.00001);
    float feather=max(aa,p.p0.y*0.5);
    float edge=mix(-feather,1.0+feather,completion);
    float keep=smoothstep(edge-feather,edge+feather,f);
    vec4 result=mix(bg,src,keep);
    float distance=f-edge-p.q1.x;
    float border=1.0-smoothstep(max(0.0,p.q0.y*0.5-p.q0.w-aa),p.q0.y*0.5+p.q0.w+aa,abs(distance));
    border*=step(0.000001,p.q0.y)*clamp(p.q0.z,0.0,1.0)*p.color.a;
    result=vec4(p.color.rgb*border+result.rgb*(1.0-border),border+result.a*(1.0-border));
    vec3 widths=max(vec3(0.0001),p.q1.z*p.q2.xyz);
    vec3 glow=exp(-distance*distance/(widths*widths))*p.q1.y;
    float modulation=max(0.0,1.0+p.q1.w*(noise(vec3(point/p.q5.xy*p.q2.w,p.p2.w*p.q6.z))*2.0-1.0));
    glow*=modulation;
    result.rgb+=p.q3.rgb*glow;
    result.a=clamp(result.a+max(glow.r,max(glow.g,glow.b))*p.q3.a,0.0,1.0);
    o_color=result;
}
