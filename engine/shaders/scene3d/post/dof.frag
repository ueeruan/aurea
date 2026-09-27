#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / dof.frag
//
//  Profundidade de campo da câmera 3D, passo 2: o desfoque, ANTES do bloom e
//  do tone map — a luz ainda é HDR linear, então o realce forte vira bokeh.
//  O raio de cada pixel vem do passo 1 (dof_coc.frag: lente fina, com a
//  profundidade mais perto do 3×3).
//
//  GATHER num disco (espiral de Vogel, ângulo de ouro) com separação perto/
//  longe no estilo "bokeh em um passe" (Gustafsson 2018):
//    • amostra MAIS LONGE que o centro não espalha sobre ele além de 2× o raio
//      do próprio centro — o fundo desfocado não "vaza" no sujeito em foco;
//    • amostra só contribui se o raio DELA alcança a distância do tap (sem
//      isso a borda nítida do sujeito faria halo no fundo desfocado);
//    • o primeiro plano desfocado espalha POR CIMA do que está em foco: um
//      anel de sondas acha o maior raio de um vizinho mais perto que alcança
//      este pixel e aumenta o disco até ele.
//  O que não contribui entra como a média corrente (não escurece nem clareia).
//  Amostragem PONTUAL: cor e raio do mesmo texel (o bilinear misturaria a cor
//  do sujeito com o raio do fundo — halo).
//
//  A cena (HDR codificado para o resolve) e o 2D de exibição (pré-multiplicado,
//  com o alfa que o tone map usa) recebem os MESMOS pesos: a borda desfocada
//  de um objeto sobre o vídeo abaixo fica macia no alfa também.
//
//  Preview: poucas amostras; export: muitas (texel.w). O ruído de rotação por
//  pixel (gradiente intercalado) troca faixas por grão fino.
// =============================================================================
#include "../../common/bindings.glsl"
#include "../common/hdr.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_display;
layout(location = 1) out vec4 o_scene;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_scene;    // HDR codificado (resolvido)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_display;  // 2D de exibição (resolvido)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_coc;      // r = raio com sinal, g = profundidade

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho, z = raio máximo (px), w = amostras do disco
} p;

const float GOLDEN_ANGLE = 2.39996323;
const float TAU = 6.28318531;

void main() {
    const vec2 uv = v_uv;
    const vec2 c0s = textureLod(u_coc, uv, 0.0).rg;
    const float a0 = abs(c0s.r);
    const float d0 = c0s.g;
    const float maxR = p.texel.z;
    const float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

    // Primeiro plano desfocado que alcança este pixel: 12 sondas em 3 anéis.
    float nearR = 0.0;
    for (int i = 0; i < 12; ++i) {
        const float rr = maxR * float(i / 4 + 1) / 3.0;
        const float ang = (float(i) + noise) * (TAU / 4.0) + float(i / 4) * 0.785398;
        const vec2 ci = textureLod(u_coc, uv + vec2(cos(ang), sin(ang)) * rr * p.texel.xy, 0.0).rg;
        if (ci.r < 0.0 && ci.g > d0 && -ci.r >= rr * 0.75) nearR = max(nearR, -ci.r);
    }

    const vec4 sc0 = textureLod(u_scene, uv, 0.0);
    const vec4 dc0 = textureLod(u_display, uv, 0.0);
    const float R = min(maxR, max(a0, nearR));
    if (R < 0.5) {
        o_scene = sc0;
        o_display = dc0;
        return;
    }

    vec4 accS = vec4(aurea_hdr_decode(sc0.rgb), sc0.a);
    vec4 accD = dc0;
    float tot = 1.0;
    const int taps = max(int(p.texel.w + 0.5), 1);
    // Meia distância entre amostras: a transição "alcança / não alcança" é
    // suave na escala da própria amostragem.
    const float band = max(0.5, 0.5 * R * inversesqrt(float(taps)));
    const float spin = noise * TAU;
    for (int i = 0; i < taps; ++i) {
        const float r = R * sqrt((float(i) + 0.5) / float(taps));
        const float ang = float(i) * GOLDEN_ANGLE + spin;
        const vec2 tap = uv + vec2(cos(ang), sin(ang)) * r * p.texel.xy;
        const vec2 ci = textureLod(u_coc, tap, 0.0).rg;
        float size = abs(ci.r);
        if (ci.g < d0) size = min(size, a0 * 2.0);   // mais longe que o centro (Z reverso: d menor)
        const float m = smoothstep(r - band, r + band, size);
        const vec4 ss = textureLod(u_scene, tap, 0.0);
        const vec4 si = vec4(aurea_hdr_decode(ss.rgb), ss.a);
        const vec4 ds = textureLod(u_display, tap, 0.0);
        accS += mix(accS / tot, si, m);
        accD += mix(accD / tot, ds, m);
        tot += 1.0;
    }
    accS /= tot;
    accD /= tot;
    vec3 hdr = accS.rgb;
    if (any(isnan(hdr)) || any(isinf(hdr))) hdr = aurea_hdr_decode(sc0.rgb);
    o_scene = vec4(aurea_hdr_encode(hdr), clamp(accS.a, 0.0, 1.0));
    o_display = vec4(max(accD.rgb, vec3(0.0)), clamp(accD.a, 0.0, 1.0));
}
