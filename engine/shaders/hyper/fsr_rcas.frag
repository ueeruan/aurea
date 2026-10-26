#version 450
// =============================================================================
//  Aurea / shaders / hyper / fsr_rcas.frag
//
//  Hyper Upscale (Turbo): RCAS do AMD FidelityFX Super Resolution 1
//  ("Robust Contrast Adaptive Sharpening"). Algoritmo portado de ffx_fsr1.h
//  (Copyright (c) 2021 Advanced Micro Devices, Inc., licença MIT; aviso em
//  engine/assets/licenses/THIRD_PARTY_HYPER.md), em GLSL 450 próprio.
//
//  Cruz 3x3 (b / d e f / h): o "lobo" negativo que dá a nitidez é o maior
//  possível SEM levar nenhum canal para fora do intervalo do anel (não cria
//  halo nem estoura), limitado a 1/4 - 1/16 e multiplicado pela nitidez
//  (2^-paradas). Com "ruído" ligado o lobo cai onde o centro destoa do anel.
//
//  Entrada perceptual; saída perceptual ou, no último passe, linear
//  pré-multiplicada (o espaço de trabalho do motor).
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;    // xy = tamanho (texels)
    vec4 flags;   // x = nitidez (2^-paradas, 0..1), y = 1 saída linear, z = 1 tira ruído
} p;

void main() {
    const ivec2 sz = ivec2(p.size.xy);
    const ivec2 sp = ivec2(gl_FragCoord.xy);
    const vec4 E = texelFetch(u_src, sp, 0);
    const vec3 b = texelFetch(u_src, hy_clampi(sp + ivec2(0, -1), sz), 0).rgb;
    const vec3 d = texelFetch(u_src, hy_clampi(sp + ivec2(-1, 0), sz), 0).rgb;
    const vec3 e = E.rgb;
    const vec3 f = texelFetch(u_src, hy_clampi(sp + ivec2(1, 0), sz), 0).rgb;
    const vec3 h = texelFetch(u_src, hy_clampi(sp + ivec2(0, 1), sz), 0).rgb;
    // Luma x2 (como no EASU).
    const float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    const float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    const float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    const float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    const float hL = h.b * 0.5 + (h.r * 0.5 + h.g);
    float nz = 0.25 * (bL + dL + fL + hL) - eL;
    const float range = max(max(max(bL, dL), max(eL, fL)), hL) - min(min(min(bL, dL), min(eL, fL)), hL);
    nz = clamp(abs(nz) / max(range, 1.0 / 4096.0), 0.0, 1.0);
    nz = -0.5 * nz + 1.0;
    const vec3 mn4 = min(min(b, d), min(f, h));
    const vec3 mx4 = max(max(b, d), max(f, h));
    // Limites do lobo por canal.
    const vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(1.0 / 65536.0));
    const vec3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, vec3(-1.0 / 65536.0));
    const vec3 lobeRGB = max(-hitMin, hitMax);
    const float limit = 0.25 - (1.0 / 16.0);
    float lobe = max(-limit, min(max(max(lobeRGB.r, lobeRGB.g), lobeRGB.b), 0.0)) * p.flags.x;
    if (p.flags.z > 0.5) lobe *= nz;
    const float rcpL = 1.0 / (4.0 * lobe + 1.0);
    const vec3 pix = clamp((lobe * (b + d + h + f) + e) * rcpL, 0.0, 1.0);
    const vec4 outp = vec4(pix, E.a);
    o_color = p.flags.y > 0.5 ? hy_from_perc(outp) : outp;
}
