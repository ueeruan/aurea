#version 450
// Bounded interval sorting. Unlike max-filter streaks, eligible colors are
// permuted inside each threshold-delimited run; 64 samples maximum per tile.
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = limiar baixo, y = limiar alto, z = comprimento máximo (0..1), w = aleatoriedade (0..1)
    vec4 p1;   // x = direção (0 horiz, 1 vert, 2 diag, 3 radial), y = 1 inverter o sentido, z = modo (0 luz, 1 tom, 2 R, 3 G, 4 B), w = semente
    vec4 p2;   // x = 1 ordenar por faixa de tom (em vez de por limiar)
    vec4 p3;   // x = passo em texels
    vec4 color;
} p;

/// A chave de ordenação: o que decide quem é "maior".
float key_of(vec3 c) {
    const int mode = int(p.p1.z + 0.5);
    if (mode == 1) {
        float hi=max(c.r,max(c.g,c.b)), lo=min(c.r,min(c.g,c.b)), d=hi-lo;
        if(d<1e-6) return 0.0;
        float h=hi==c.r ? (c.g-c.b)/d : hi==c.g ? 2.0+(c.b-c.r)/d : 4.0+(c.r-c.g)/d;
        return fract(h/6.0);
    }
    if (mode == 2) return c.r;
    if (mode == 3) return c.g;
    if (mode == 4) return c.b;
    return aurea_luma(aurea_linear_to_srgb(max(c, vec3(0.0))));
}

void main() {
    vec2 uv=v_uv*p.uvMap.xy+p.uvMap.zw;
    vec4 original=texture(u_tex0,uv);
    if(p.p0.z<=0.0 || original.a<=1e-6) { o_color=original; return; }
    int direction=int(p.p1.x+.5);
    vec2 axis=direction==0 ? vec2(1,0) : direction==1 ? vec2(0,1) : direction==2 ? normalize(vec2(1)) : normalize(uv-.5+vec2(1e-6));
    vec2 pixel=uv/p.texel.xy;
    float coordinate=dot(pixel,axis)/max(1.0,p.p3.x);
    int span=clamp(int(2.0+62.0*p.p0.z),2,64);
    int position=int(mod(floor(coordinate),float(span)));
    vec2 base=uv-axis*float(position)*p.texel.xy*max(1.0,p.p3.x);
    vec4 values[64]; float keys[64]; bool eligible[64];
    for(int i=0;i<64;++i) {
        vec2 q=base+axis*float(i)*p.texel.xy*max(1.0,p.p3.x);
        vec4 c=texture(u_tex0,q); float k=key_of(unpremultiply(c).rgb);
        bool inside=all(greaterThanEqual(q,vec2(0))) && all(lessThan(q,vec2(1)));
        float threshold=p.p2.x>.5 ? key_of(unpremultiply(c).rgb) : aurea_luma(aurea_linear_to_srgb(max(unpremultiply(c).rgb,vec3(0))));
        eligible[i]=i<span && inside && c.a>1e-6 && threshold>=p.p0.x && threshold<=p.p0.y;
        // Random barriers split runs, never duplicate or discard their colors.
        if(i!=0 && p.p0.w>0.0 && aurea_hash(uvec2(ivec2(floor(q/p.texel.xy))) ^ uvec2(uint(p.p1.w),0))<p.p0.w*.15) eligible[i]=false;
        values[i]=c; keys[i]=k;
    }
    if(!eligible[position]) { o_color=original; return; }
    int first=position,last=position;
    for(int n=1;n<64;++n) { int i=position-n; if(i<0 || !eligible[i]) break; first=i; }
    for(int n=1;n<64;++n) { int i=position+n; if(i>=span || !eligible[i]) break; last=i; }
    for(int i=first+1;i<=last;++i) {
        vec4 c=values[i]; float k=keys[i]; int j=i;
        for(int n=0;n<64;++n) {
            if(j<=first) break;
            bool move=p.p1.y>.5 ? keys[j-1]<k : keys[j-1]>k;
            if(!move) break;
            values[j]=values[j-1]; keys[j]=keys[j-1]; --j;
        }
        values[j]=c; keys[j]=k;
    }
    o_color=values[position];
}
