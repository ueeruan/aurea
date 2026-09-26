#version 450
// =============================================================================
//  Aurea / shaders / effects / turbulence_displace.frag
//
//  Fractal displacement with explicit evolution phase and edge pinning.
//  Animate evolution with keyframes; playback alone does not change the field.
//
//  O deslocamento é medido em pixels da LAYER; `texel.zw` traz texels por
//  pixel de layer, então o mesmo número vale no preview reduzido e no 4K.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = intensidade (px da layer), y = tamanho do ruído (px), z = complexidade (oitavas), w = evolução (graus)
    vec4 p1;   // x = deslocamento X (px), y = deslocamento Y (px), z = semente, w = 1 só na horizontal
    vec4 p2;   // x = borda (0 repetir, 1 recortar, 2 esticar), y = girar o vetor (graus)
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    // Unidades de uv por PIXEL DA LAYER: com isto, um deslocamento em pixels
    // da layer vira o mesmo número de uv no preview e no export.
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));

    const float sizePx = max(p.p0.y, 1.0);
    // Evolution is a phase, not an implicit playback speed. Animate it with keys.
    const float phase = radians(p.p0.w);
    const vec2 layerPx = inUv / uvPerLayer + p.p1.xy;
    const vec2 seed = vec2(p.p1.z*13.7,p.p1.z*7.3);
    vec2 q=(layerPx+seed)/sizePx;
    vec2 evolution=vec2(cos(phase),sin(phase))*1.7;
    float complexity=clamp(p.p0.z,1.0,6.0);
    vec2 noise=vec2(0); float weight=1.0, total=0.0;
    for(int i=0;i<6;++i) {
        float contribution=clamp(complexity-float(i),0.0,1.0)*weight;
        noise+=vec2(aurea_turbulence(q+evolution,1,.5),aurea_turbulence(q+evolution+vec2(31.7,17.3),1,.5))*contribution;
        total+=contribution; weight*=.5; q*=2.0;
    }
    vec2 d=(noise/max(total,1e-6)-.5)*(2.0*p.p0.x)*uvPerLayer;
    int pin=int(p.p2.w+.5); float edge=1.0;
    if(pin==1) edge=min(min(inUv.x,1.0-inUv.x),min(inUv.y,1.0-inUv.y));
    if(pin==2) edge=inUv.x; if(pin==3) edge=1.0-inUv.x;
    if(pin==4) edge=inUv.y; if(pin==5) edge=1.0-inUv.y;
    if(pin!=0) d*=smoothstep(0.0,.12,edge);
    if (p.p1.w > 0.5) d.y = 0.0;
    if (abs(p.p2.y) > 1e-4) d = aurea_rot2(radians(p.p2.y)) * d;

    const vec2 uv = inUv + d;
    const int mode = int(p.p2.x + 0.5);
    // O tratamento de borda é sobre a COORDENADA DA ENTRADA: fora do [0,1] da
    // textura é que não há pixel para ler.
    vec2 sampleUv = uv;
    vec4 outc;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        if (mode == 1) {
            outc = vec4(0.0);
        } else if (mode == 2) {
            sampleUv = clamp(uv, 0.0, 1.0);
            outc = unpremultiply(texture(u_tex0, sampleUv));
        } else {
            outc = unpremultiply(texture(u_tex0, fract(uv)));
        }
    } else {
        outc = unpremultiply(texture(u_tex0, uv));
    }

    // A borda transparente do modo recortar vale para o alfa também: a camada
    // não pode "crescer" só na cor.
    o_color = mix(texture(u_tex0,inUv),premultiply(vec4(max(outc.rgb, vec3(0.0)), outc.a)),clamp(p.p2.z,0.0,1.0));
}
