#ifndef AUREA_PALETTIZE_COMMON
#define AUREA_PALETTIZE_COMMON
#include "color_oklab.glsl"
// Tabela da paleta (Palette.hpp): N cores sRGB e depois as mesmas N em Oklab.
int paletteCount() { return max(textureSize(tbl,0).x/2,1); }
vec3 nearestColor(vec3 srgb) {
    int count=paletteCount();
    vec3 lab=aurea_linear_to_oklab(aurea_srgb_to_linear(clamp(srgb,0.0,1.0)));
    int best=0;float distance=1e30;
    for(int i=0;i<256;i++) {
        if(i>=count) break;
        vec3 d=lab-texelFetch(tbl,ivec2(count+i,0),0).rgb;
        float e=dot(d,d);
        if(e<distance) {distance=e;best=i;}
    }
    return texelFetch(tbl,ivec2(best,0),0).rgb;
}
// Passo do pontilhado ordenado: o tamanho típico do degrau entre cores.
float paletteSpread() {
    int count=paletteCount();
    return count<=2?1.0:.6/pow(float(count),1.0/3.0);
}
#endif
