#version 450
// =============================================================================
//  Aurea / shaders / hyper / fsr_easu.frag
//
//  Hyper Upscale (General, Turbo): EASU do AMD FidelityFX Super Resolution 1
//  ("Edge Adaptive Spatial Upsampling"). Algoritmo portado de ffx_fsr1.h
//  (Copyright (c) 2021 Advanced Micro Devices, Inc., licença MIT; o aviso
//  está em engine/assets/licenses/THIRD_PARTY_HYPER.md), reescrito em GLSL
//  450 para o pipeline de shaders do motor (Vulkan, GLES e Metal saem deste
//  arquivo): leituras por texelFetch em vez de textureGather e recíprocas
//  exatas protegidas em vez das aproximações por bits.
//
//  Para cada pixel de saída: o quadrado 2x2 da entrada em volta dele (f g /
//  j k) e o anel de 12 amostras (b c / e f g h / i j k l / n o). A direção e
//  o "comprimento" da borda saem dos gradientes em cruz de cada canto,
//  pesados pela posição bilinear; um núcleo tipo Lanczos-2 aproximado é
//  girado nessa direção e esticado ao longo da borda; o resultado é preso ao
//  mínimo/máximo dos 4 vizinhos (sem anéis).
//
//  Entrada e saída PERCEPTUAIS (hyper_common.glsl), alfa reto bilinear.
//  Qualquer escala (até 2 recomendado; 4x = duas passadas).
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 inSize;    // xy = tamanho da entrada (texels), zw = 1/xy
    vec4 outSize;   // xy = tamanho da saída, zw = 1/xy
} p;

vec4 tapAt(ivec2 base, ivec2 size, int x, int y) {
    return texelFetch(u_src, hy_clampi(base + ivec2(x, y), size), 0);
}
float fsr_luma(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }

void easu_set(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
    // Direção = a diferença em cruz; comprimento = |dir| sobre a maior
    // diferença de cada lado (0 na inversão do gradiente, 1 na borda limpa).
    const float dc = lD - lC;
    const float cb = lC - lB;
    float lenX = 1.0 / max(max(abs(dc), abs(cb)), 1.0 / 65536.0);
    const float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    len += lenX * lenX * w;
    const float ec = lE - lC;
    const float ca = lC - lA;
    float lenY = 1.0 / max(max(abs(ec), abs(ca)), 1.0 / 65536.0);
    const float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    len += lenY * lenY * w;
}

void easu_tap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
    vec2 v;
    v.x = off.x * dir.x + off.y * dir.y;
    v.y = off.x * -dir.y + off.y * dir.x;
    v *= len;
    const float d2 = min(dot(v, v), clp);
    // Lanczos-2 aproximado sem sin/rcp/sqrt:
    //   (25/16 (2/5 x^2 - 1)^2 - (25/16 - 1)) (lob x^2 - 1)^2
    float wB = 0.4 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 1.5625 * wB - 0.5625;
    const float w = wB * wA;
    aC += c * w;
    aW += w;
}

void main() {
    const ivec2 size = ivec2(p.inSize.xy);
    const vec2 ip = (floor(gl_FragCoord.xy) + 0.5) * p.inSize.xy * p.outSize.zw - 0.5;
    const vec2 fp = floor(ip);
    const vec2 pp = ip - fp;
    const ivec2 base = ivec2(fp);
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    const vec4 b = tapAt(base, size, 0, -1), c = tapAt(base, size, 1, -1);
    const vec4 e = tapAt(base, size, -1, 0), f = tapAt(base, size, 0, 0);
    const vec4 g = tapAt(base, size, 1, 0), h = tapAt(base, size, 2, 0);
    const vec4 i = tapAt(base, size, -1, 1), j = tapAt(base, size, 0, 1);
    const vec4 k = tapAt(base, size, 1, 1), l = tapAt(base, size, 2, 1);
    const vec4 n = tapAt(base, size, 0, 2), o = tapAt(base, size, 1, 2);
    const float bL = fsr_luma(b.rgb), cL = fsr_luma(c.rgb), eL = fsr_luma(e.rgb), fL = fsr_luma(f.rgb);
    const float gL = fsr_luma(g.rgb), hL = fsr_luma(h.rgb), iL = fsr_luma(i.rgb), jL = fsr_luma(j.rgb);
    const float kL = fsr_luma(k.rgb), lL = fsr_luma(l.rgb), nL = fsr_luma(n.rgb), oL = fsr_luma(o.rgb);
    vec2 dir = vec2(0.0);
    float len = 0.0;
    easu_set(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    easu_set(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    easu_set(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    easu_set(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
    // Normaliza a direção (perto de zero = horizontal).
    const vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    const bool zro = dirR < (1.0 / 32768.0);
    dirR = zro ? 1.0 : inversesqrt(dirR);
    dir.x = zro ? 1.0 : dir.x;
    dir *= dirR;
    // {0..2} -> {0..1}, moldado pelo quadrado.
    len = len * 0.5;
    len *= len;
    // Estica: 1 na horizontal/vertical, raiz de 2 na diagonal.
    const float stretch = dot(dir, dir) / max(max(abs(dir.x), abs(dir.y)), 1e-6);
    const vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    // Janela de +-raiz(2) até pouco além de 2 conforme a quantidade de borda.
    const float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    const float clp = 1.0 / lob;
    const vec3 min4 = min(min(f.rgb, g.rgb), min(j.rgb, k.rgb));
    const vec3 max4 = max(max(f.rgb, g.rgb), max(j.rgb, k.rgb));
    vec3 aC = vec3(0.0);
    float aW = 0.0;
    easu_tap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b.rgb);
    easu_tap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c.rgb);
    easu_tap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i.rgb);
    easu_tap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j.rgb);
    easu_tap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f.rgb);
    easu_tap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e.rgb);
    easu_tap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k.rgb);
    easu_tap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l.rgb);
    easu_tap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h.rgb);
    easu_tap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g.rgb);
    easu_tap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o.rgb);
    easu_tap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n.rgb);
    const vec3 pix = min(max4, max(min4, aC / (abs(aW) > 1e-6 ? aW : 1e-6)));
    const float a = mix(mix(f.a, g.a, pp.x), mix(j.a, k.a, pp.x), pp.y);
    o_color = vec4(pix, a);
}
