#version 450
// =============================================================================
//  Aurea / shaders / effects / warp.frag
//
//  Distorção de lente (Fase 7.3 §28): a imagem se curva em volta de um ponto,
//  com raio e intensidade próprios. Cinco modos, porque "warp" é uma família,
//  não uma coisa só:
//
//    Empurrar — o centro incha para fora
//    Puxar    — o centro encolhe para dentro
//    Torcer   — gira em espiral, mais forte perto do centro
//    Esfera   — lente de aumento, com a luz que a calota pegaria
//    Canto    — os quatro cantos puxam para o centro
//
//  Deslocamento em pixels da LAYER: o mesmo número vale no preview e no export.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = modo, y = intensidade (px), z = raio (px da layer), w = brilho da esfera
    vec4 p1;   // x = centro X, y = centro Y, z = borda (0 repetir, 1 recortar, 2 esticar), w = mistura
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

vec4 read_edge(vec2 uv) {
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        const int mode = int(p.p1.z + 0.5);
        if (mode == 1) return vec4(0.0);
        if (mode == 2) return unpremultiply(texture(u_tex0, clamp(uv, 0.0, 1.0)));
        return unpremultiply(texture(u_tex0, fract(uv)));
    }
    return unpremultiply(texture(u_tex0, uv));
}

void main() {
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    // Tudo em pixels da layer, medidos do centro do efeito.
    const vec2 layerPx = inUv / uvPerLayer;
    const vec2 rel = layerPx - p.p1.xy / uvPerLayer;
    const float r = length(rel);
    const float radius = max(p.p0.z, 1.0);
    const int mode = int(p.p0.x + 0.5);

    vec2 uv = inUv;
    float shade = 1.0;

    if (mode == 0 || mode == 1) {
        // Empurrar / puxar: cada pixel anda ao longo do próprio raio, com
        // queda suave até o raio do efeito.
        const float fall = 1.0 - smoothstep(0.0, radius, r);
        uv = inUv + (rel / max(r, 1e-6)) * (p.p0.y * fall) * uvPerLayer;
    } else if (mode == 2) {
        // Torcer: rotação que decai com a distância — espiral.
        const float fall = 1.0 - smoothstep(0.0, radius, r);
        uv = inUv + (aurea_rot2(radians(p.p0.y) * fall) - mat2(1.0)) * rel;
    } else if (mode == 3) {
        // Esfera: o raio é remapeado como uma calota. O centro amplia, a borda
        // encolhe, e `shade` devolve a luz que a calota pegaria.
        const float rr = r / max(radius, 1e-4);
        if (rr < 1.0) {
            const float k = sqrt(max(0.0, 1.0 - rr * rr));
            uv = inUv + rel * (mix(1.0, 1.0 / max(k, 0.35), clamp(p.p0.y / 100.0, 0.0, 1.0)) - 1.0);
            shade = mix(1.0, 0.55 + 0.75 * k, clamp(p.p0.w, 0.0, 1.0));
        }
    } else {
        // Canto: os quatro cantos puxam para o centro do quadro, mais forte
        // quanto mais longe do centro.
        const vec2 sgn = vec2(rel.x < 0.0 ? -1.0 : 1.0, rel.y < 0.0 ? -1.0 : 1.0);
        const float fall = pow(clamp(r / max(radius, 1e-4), 0.0, 1.0), 2.0);
        uv = inUv - sgn * rel * (p.p0.y / 100.0) * fall * uvPerLayer;
    }

    const vec4 outc = read_edge(uv);
    const bool srcInside = all(greaterThanEqual(inUv, vec2(0.0))) && all(lessThanEqual(inUv, vec2(1.0)));
    const vec4 src = srcInside ? unpremultiply(texture(u_tex0, inUv)) : vec4(0.0);
    const float k = clamp(p.p1.w, 0.0, 1.0);
    const vec3 c = mix(src.rgb, outc.rgb, k) * shade;
    o_color = premultiply(vec4(max(c, vec3(0.0)), mix(src.a, outc.a, k)));
}
