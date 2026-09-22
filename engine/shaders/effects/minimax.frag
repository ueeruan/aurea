#version 450
// =============================================================================
//  Aurea / shaders / effects / minimax.frag
//
//  Filtro morfológico (Fase 7.3 §39): DILATA (pega o maior da vizinhança) ou
//  ERODE (o menor). Serve para engrossar ou afinar um matte, tirar um pixel de
//  borda de um recorte, e para estilizar.
//
//  O passe devolve a COR do pixel que venceu a comparação, não o valor
//  comparado: num matte isso não muda nada (o alfa é o que interessa), mas numa
//  imagem colorida é a diferença entre "achatou as cores" e "engrossou".
//
//  Um passe só, com núcleo até ±6 px. Raio maior: o C++ reduz a imagem antes
//  (um morfológico de raio r na escala 1/k é um de raio r/k em 1/k), e o custo
//  por pixel fica preso.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = raio em texels, y = 1 dilata / 0 erode, z = forma (0 cruz, 1 quadrado, 2 losango), w = mistura
    vec4 p1;   // x = o que se compara (0 luma×alfa, 1 alfa, 2 RGB, 3 RGBA)
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

/// O valor que decide quem vence. Num matte é o alfa; numa cor, a magnitude.
float probe(vec4 s) {
    const int mode = int(p.p1.x + 0.5);
    if (mode == 1) return s.a;
    if (mode == 2) return max(max(s.r, s.g), s.b);
    if (mode == 3) return max(max(s.r, s.g), max(s.b, s.a));
    return aurea_luma(s.rgb) * s.a;
}

void main() {
    const vec2 st = p.texel.xy * max(p.p0.x, 0.0);
    const int shape = int(p.p0.z + 0.5);
    const bool dilate = p.p0.y > 0.5;

    const vec2 uv0 = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec4 best = unpremultiply(texture(u_tex0, uv0));
    float bestProbe = probe(best);

    for (int dy = -6; dy <= 6; ++dy) {
        for (int dx = -6; dx <= 6; ++dx) {
            if (dx == 0 && dy == 0) continue;
            if (shape == 0 && dx != 0 && dy != 0) continue;                 // cruz
            if (shape == 2 && (abs(dx) + abs(dy)) > 6) continue;            // losango
            const float d = max(abs(float(dx)), abs(float(dy)));
            if (d > p.p0.x + 0.5) continue;                                 // fora do raio

            const vec4 s = unpremultiply(texture(u_tex0, uv0 + vec2(float(dx), float(dy)) * st));
            const float sp = probe(s);
            if (dilate ? (sp > bestProbe) : (sp < bestProbe)) {
                best = s;
                bestProbe = sp;
            }
        }
    }

    const vec4 src = unpremultiply(texture(u_tex0, uv0));
    const vec4 outc = mix(src, best, clamp(p.p0.w, 0.0, 1.0));
    o_color = premultiply(vec4(max(outc.rgb, vec3(0.0)), outc.a));
}
