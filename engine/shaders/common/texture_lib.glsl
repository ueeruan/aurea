// =============================================================================
//  Aurea / shaders / common / texture_lib.glsl
//
//  BIBLIOTECA DE TEXTURAS PROCEDURAIS (0.5.2): ouro, cromo, madeira, mármore,
//  pedra, fogo, água, galáxia, papel, tecido... calculadas no pixel, sem
//  imagem nenhuma no pacote. Servem ao PREENCHIMENTO COM TEXTURA do texto
//  (text/glyph.frag) e ao efeito Textura (effects/texture_overlay.frag) — uma
//  fonte só: a mesma textura sai igual nos dois.
//
//  COORDENADAS: `p` em "unidades de textura". No texto 1 unidade = o tamanho
//  da fonte (px da camada); no efeito, uma fração do tamanho da camada. Nada
//  aqui depende da resolução do alvo: a prévia reduzida e o export calculam o
//  mesmo valor no mesmo ponto da camada.
//
//  RUÍDO: hash inteiro (pcg3d), sem seno e sem textura de ruído — o mesmo
//  resultado em qualquer GPU (Vulkan, Metal, GLES).
//
//  NÚMEROS (contrato com TextTexture.hpp e TextureOverlayEffect.cpp):
//    1 ouro, 2 prata/cromo, 3 metal escovado, 4 madeira, 5 mármore, 6 pedra,
//    7 concreto, 8 glitter, 9 holográfico, 10 neon (degradê animado), 11 fogo,
//    12 água, 13 galáxia, 14 papel, 15 jeans/tecido, 16 couro, 17 ferrugem,
//    18 gelo, 19 papel amassado, 20 grão de filme, 21 poeira e arranhões,
//    22 lona (canvas), 23 grunge, 24 meio-tom, 25 ruído, 26 vidro fosco.
//  Só acrescentar no fim: o número fica gravado no projeto.
// =============================================================================
#ifndef AUREA_TEXTURE_LIB_GLSL
#define AUREA_TEXTURE_LIB_GLSL

/// O que a textura diz de cada ponto. `albedo` em valores de tela (sRGB,
/// 0..1); `height` = relevo próprio (0..1, o efeito de luz usa a variação);
/// `metal` = o brilho especular toma a cor da superfície; `emit` = a cor
/// vale mesmo no escuro (fogo, neon, galáxia), a luz não a apaga.
struct AtxTexel {
    vec3 albedo;
    float height;
    float metal;
    float emit;
};

// --- Ruído ---------------------------------------------------------------------
uint atx_hash_u(ivec3 c) {
    uvec3 v = uvec3(c) * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v.x ^ v.y ^ v.z;
}
/// 0..1 por célula inteira de `cell`; `salt` separa sequências.
float atx_h(vec2 cell, float salt) {
    return float(atx_hash_u(ivec3(ivec2(floor(cell)), int(floor(salt)))) & 0xFFFFFFu) / 16777216.0;
}
float atx_noise(vec2 p, float salt) {
    const vec2 i = floor(p);
    const vec2 f = fract(p);
    const vec2 u = f * f * (3.0 - 2.0 * f);
    const float a = atx_h(i, salt);
    const float b = atx_h(i + vec2(1.0, 0.0), salt);
    const float c = atx_h(i + vec2(0.0, 1.0), salt);
    const float d = atx_h(i + vec2(1.0, 1.0), salt);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
/// FBM de até 6 oitavas, cada uma girada (sem o xadrez do ruído de valor).
float atx_fbm(vec2 p, int octaves, float salt) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    const mat2 r = mat2(0.8, -0.6, 0.6, 0.8);
    for (int i = 0; i < 6; ++i) {
        if (i >= octaves) break;
        sum += amp * atx_noise(p, salt + float(i) * 11.0);
        norm += amp;
        p = r * p * 2.03 + vec2(17.13, 9.71);
        amp *= 0.5;
    }
    return sum / max(norm, 1e-4);
}
/// Cristas (|ruído| acumulado, invertido): veios e vincos.
float atx_ridged(vec2 p, int octaves, float salt) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    const mat2 r = mat2(0.8, -0.6, 0.6, 0.8);
    for (int i = 0; i < 6; ++i) {
        if (i >= octaves) break;
        sum += amp * (1.0 - abs(atx_noise(p, salt + float(i) * 13.0) * 2.0 - 1.0));
        norm += amp;
        p = r * p * 2.07 + vec2(3.7, 21.3);
        amp *= 0.5;
    }
    return sum / max(norm, 1e-4);
}
/// Células de Voronoi: x = distância ao 1º ponto, y = ao 2º, z = sorteio da célula.
vec3 atx_voronoi(vec2 p, float salt) {
    const vec2 i = floor(p);
    const vec2 f = fract(p);
    float f1 = 8.0, f2 = 8.0, id = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const vec2 g = vec2(float(x), float(y));
            const vec2 o = vec2(atx_h(i + g, salt), atx_h(i + g, salt + 7.0));
            const vec2 r = g + o - f;
            const float d = dot(r, r);
            if (d < f1) { f2 = f1; f1 = d; id = atx_h(i + g, salt + 13.0); }
            else if (d < f2) { f2 = d; }
        }
    }
    return vec3(sqrt(f1), sqrt(f2), id);
}

// --- Cor -----------------------------------------------------------------------
vec3 atx_hsv(float h, float s, float v) {
    const vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return v * mix(vec3(1.0), k, s);
}
vec3 atx_ramp3(vec3 a, vec3 b, vec3 c, float t) {
    t = clamp(t, 0.0, 1.0);
    return t < 0.5 ? mix(a, b, t * 2.0) : mix(b, c, t * 2.0 - 1.0);
}
float atx_luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

/// Reflexo de metal polido: céu claro no alto, a linha escura do horizonte e
/// o chão. `n` inclina o reflexo (o relevo das letras faz a faixa curvar).
float atx_chrome_band(vec2 p, vec3 n, float salt) {
    const float s = p.y * 0.85 - n.y * 0.55 + n.x * 0.15 + (atx_fbm(p * 1.3, 3, salt) - 0.5) * 0.16;
    const float b = fract(s * 0.72 + 0.5);
    return b < 0.5 ? mix(1.0, 0.52, smoothstep(0.0, 0.5, b))
                   : mix(0.16, 0.74, smoothstep(0.5, 1.0, b));
}

// --- Geradores -------------------------------------------------------------------
AtxTexel atx_make(vec3 albedo, float height, float metal, float emit) {
    AtxTexel t;
    t.albedo = clamp(albedo, 0.0, 1.0);
    t.height = clamp(height, 0.0, 1.0);
    t.metal = metal;
    t.emit = emit;
    return t;
}

AtxTexel atx_gold(vec2 p, vec3 n, float salt) {
    const float v = atx_chrome_band(p, n, salt);
    const float grain = atx_fbm(p * 9.0, 3, salt + 3.0);
    const vec3 c = atx_ramp3(vec3(0.33, 0.18, 0.03), vec3(0.86, 0.6, 0.17), vec3(1.0, 0.94, 0.66), v + (grain - 0.5) * 0.08);
    return atx_make(c, 0.5 + (grain - 0.5) * 0.2, 1.0, 0.0);
}
AtxTexel atx_silver(vec2 p, vec3 n, float salt) {
    const float v = atx_chrome_band(p, n, salt);
    const vec3 c = mix(vec3(0.1, 0.11, 0.13), vec3(0.97, 0.98, 1.0), v) + vec3(-0.02, 0.0, 0.03) * (1.0 - v);
    return atx_make(c, 0.5, 1.0, 0.0);
}
AtxTexel atx_brushed(vec2 p, vec3 n, float salt) {
    const float streak = atx_fbm(vec2(p.x * 0.7, p.y * 70.0), 4, salt);
    const float fine = atx_noise(vec2(p.x * 2.0, p.y * 260.0), salt + 5.0);
    const float sheen = 0.5 + 0.5 * cos((p.x * 0.6 - n.x * 0.8) * 3.14159);
    const float v = 0.46 + (streak - 0.5) * 0.36 + (fine - 0.5) * 0.08 + sheen * 0.22;
    return atx_make(vec3(v * 0.98, v, v * 1.03), streak, 1.0, 0.0);
}
AtxTexel atx_wood(vec2 p, float salt) {
    const vec2 q = p + vec2(atx_fbm(p * 0.8, 3, salt), atx_fbm(p * 0.8 + 5.2, 3, salt)) * 0.45;
    const float rings = length(vec2(q.x * 0.22, q.y * 1.0) + vec2(0.0, 3.0)) * 7.0 + atx_fbm(q * 3.0, 3, salt + 2.0) * 1.6;
    const float ring = fract(rings);
    const float late = smoothstep(0.55, 0.95, ring) * (1.0 - smoothstep(0.95, 1.0, ring));
    const float fiber = atx_fbm(vec2(p.x * 1.5, p.y * 45.0), 3, salt + 4.0);
    const vec3 light = vec3(0.78, 0.56, 0.33), dark = vec3(0.42, 0.24, 0.11);
    const vec3 c = mix(light, dark, clamp(late * 0.85 + (fiber - 0.5) * 0.35, 0.0, 1.0));
    return atx_make(c, 1.0 - late * 0.6 + (fiber - 0.5) * 0.2, 0.0, 0.0);
}
AtxTexel atx_marble(vec2 p, float salt) {
    const float t = atx_ridged(p * 1.2, 5, salt);
    const float vein = abs(sin((p.x * 0.9 + p.y * 0.55) * 2.6 + atx_fbm(p * 1.1, 5, salt + 3.0) * 7.0));
    const float lines = pow(1.0 - vein, 10.0) + pow(t, 9.0) * 0.6;
    const float cloud = atx_fbm(p * 2.0, 4, salt + 7.0);
    const vec3 base = mix(vec3(0.95, 0.94, 0.92), vec3(0.86, 0.86, 0.88), cloud);
    const vec3 c = mix(base, vec3(0.3, 0.31, 0.34), clamp(lines, 0.0, 1.0) * 0.85);
    return atx_make(c, 0.6 - lines * 0.2, 0.0, 0.0);
}
AtxTexel atx_stone(vec2 p, float salt) {
    const vec3 v = atx_voronoi(p * 2.4, salt);
    const float crack = 1.0 - smoothstep(0.02, 0.09, v.y - v.x);
    const float speck = atx_fbm(p * 14.0, 4, salt + 2.0);
    const vec3 cell = mix(vec3(0.46, 0.44, 0.41), vec3(0.62, 0.6, 0.56), v.z);
    const vec3 c = mix(cell * (0.82 + speck * 0.36), vec3(0.18, 0.17, 0.16), crack * 0.8);
    return atx_make(c, (1.0 - crack) * (0.55 + speck * 0.45), 0.0, 0.0);
}
AtxTexel atx_concrete(vec2 p, float salt) {
    const float base = atx_fbm(p * 3.0, 5, salt);
    const vec3 pits = atx_voronoi(p * 11.0, salt + 3.0);
    const float hole = (1.0 - smoothstep(0.05, 0.12, pits.x)) * step(0.72, pits.z);
    const float speck = atx_h(p * 90.0, salt + 5.0);
    const float v = 0.5 + (base - 0.5) * 0.45 + (speck - 0.5) * 0.1 - hole * 0.25;
    return atx_make(vec3(v, v * 0.99, v * 0.97), base - hole * 0.6, 0.0, 0.0);
}
AtxTexel atx_glitter(vec2 p, vec3 n, float t, float salt) {
    const float v = atx_chrome_band(p, n, salt) * 0.35 + 0.42;
    const vec2 g = p * 26.0;
    const vec2 cell = floor(g);
    const vec2 o = vec2(atx_h(cell, salt + 1.0), atx_h(cell, salt + 2.0));
    const float d = length(fract(g) - o);
    const float phase = atx_h(cell, salt + 3.0);
    const float twinkle = pow(0.5 + 0.5 * cos((t * 1.7 + phase) * 6.28318), 6.0);
    const float spark = (1.0 - smoothstep(0.0, 0.32, d)) * twinkle * step(0.35, phase);
    const float flake = atx_h(cell, salt + 4.0);
    const vec3 rainbow = atx_hsv(fract(phase * 3.0 + t * 0.05), 0.35, 1.0);
    const vec3 c = vec3(v) * mix(0.75, 1.15, flake) + rainbow * spark * 1.2;
    return atx_make(c, flake, 1.0, spark);
}
AtxTexel atx_holo(vec2 p, vec3 n, float t, float salt) {
    const float h = p.x * 0.32 + p.y * 0.18 + n.x * 0.6 + n.y * 0.4 + t * 0.12 + (atx_fbm(p * 1.4, 3, salt) - 0.5) * 0.35;
    const vec3 rain = atx_hsv(fract(h), 0.5, 1.0);
    const float band = atx_chrome_band(p * 0.7, n, salt + 2.0);
    return atx_make(mix(vec3(band), rain, 0.62) * 0.55 + rain * 0.45, 0.5, 0.85, 0.15);
}
AtxTexel atx_neon(vec2 p, float t) {
    const float h = (p.x - p.y * 0.35) * 0.22 - t * 0.18;
    const vec3 a = atx_hsv(fract(h), 0.85, 1.0);
    return atx_make(a, 0.5, 0.0, 1.0);
}
AtxTexel atx_fire(vec2 p, float t, float salt) {
    const vec2 q = vec2(p.x * 1.6, p.y * 1.3 + t * 1.6);
    const float warp = atx_fbm(q * 1.2 + vec2(0.0, t * 0.6), 3, salt + 4.0);
    const float f = atx_fbm(q * 2.0 + warp * 1.4, 5, salt);
    const float heat = clamp(f * 1.55 - 0.18, 0.0, 1.0);
    vec3 c = mix(vec3(0.42, 0.03, 0.0), vec3(1.0, 0.36, 0.02), smoothstep(0.15, 0.55, heat));
    c = mix(c, vec3(1.0, 0.86, 0.3), smoothstep(0.55, 0.85, heat));
    c = mix(c, vec3(1.0, 0.98, 0.86), smoothstep(0.88, 1.0, heat));
    return atx_make(c, heat, 0.0, 1.0);
}
AtxTexel atx_water(vec2 p, float t, float salt) {
    vec2 q = p * 2.2;
    float c = 0.0;
    float k = 1.0;
    for (int i = 0; i < 3; ++i) {
        const float s = t * (0.45 + 0.15 * float(i));
        q += vec2(sin(q.y * 1.3 + s * 2.0), cos(q.x * 1.1 - s * 1.6)) * 0.42;
        c += k * (1.0 - abs(sin(q.x + q.y + s)));
        k *= 0.6;
    }
    c = pow(clamp(c / 1.96, 0.0, 1.0), 4.0);
    const float depth = atx_fbm(p * 1.3 + vec2(t * 0.05, 0.0), 3, salt);
    const vec3 base = mix(vec3(0.0, 0.22, 0.42), vec3(0.04, 0.48, 0.7), depth);
    return atx_make(base + vec3(0.55, 0.85, 0.9) * c, 0.4 + c * 0.6, 0.2, 0.35);
}
AtxTexel atx_galaxy(vec2 p, float t, float salt) {
    const vec2 q = p * 0.9 + vec2(t * 0.02, 0.0);
    const float neb = atx_fbm(q + atx_fbm(q * 1.7, 3, salt + 1.0) * 1.2, 5, salt);
    const float neb2 = atx_fbm(q * 1.6 + 9.0, 4, salt + 5.0);
    vec3 c = vec3(0.02, 0.01, 0.06);
    c = mix(c, vec3(0.32, 0.08, 0.52), smoothstep(0.35, 0.75, neb));
    c = mix(c, vec3(0.06, 0.3, 0.68), smoothstep(0.45, 0.8, neb2) * 0.7);
    c = mix(c, vec3(1.0, 0.48, 0.78), smoothstep(0.68, 0.92, neb) * 0.6);
    const vec2 g = p * 18.0;
    const vec2 cell = floor(g);
    const float star = atx_h(cell, salt + 2.0);
    const vec2 o = vec2(atx_h(cell, salt + 3.0), atx_h(cell, salt + 4.0));
    const float tw = 0.6 + 0.4 * cos((t * 0.9 + star * 7.0) * 6.28318);
    const float s = (1.0 - smoothstep(0.0, 0.09, length(fract(g) - o))) * step(0.82, star) * tw;
    return atx_make(c + vec3(s), neb, 0.0, 1.0);
}
AtxTexel atx_paper(vec2 p, float salt) {
    const float fibers = atx_fbm(vec2(p.x * 6.0, p.y * 40.0), 3, salt) * 0.5 + atx_fbm(vec2(p.y * 7.0, p.x * 38.0), 3, salt + 3.0) * 0.5;
    const float tooth = atx_fbm(p * 30.0, 3, salt + 6.0);
    const float v = 0.93 + (fibers - 0.5) * 0.07 + (tooth - 0.5) * 0.06;
    return atx_make(vec3(v, v * 0.985, v * 0.94), tooth * 0.6 + fibers * 0.4, 0.0, 0.0);
}
AtxTexel atx_denim(vec2 p, float salt) {
    const vec2 g = p * 34.0;
    const float twill = fract((g.x + g.y) * 0.25);
    const float thread = smoothstep(0.1, 0.5, abs(twill - 0.5) * 2.0);
    const float slub = atx_fbm(vec2(p.x * 2.0, p.y * 24.0), 3, salt);
    const float fade = atx_fbm(p * 1.6, 4, salt + 4.0);
    const vec3 indigo = mix(vec3(0.07, 0.15, 0.32), vec3(0.3, 0.42, 0.62), fade * 0.8);
    const vec3 c = mix(indigo, vec3(0.78, 0.8, 0.85), (1.0 - thread) * 0.35 + (slub - 0.5) * 0.25);
    return atx_make(c, thread * 0.7 + slub * 0.3, 0.0, 0.0);
}
AtxTexel atx_leather(vec2 p, float salt) {
    const vec3 v = atx_voronoi(p * 16.0, salt);
    const float groove = 1.0 - smoothstep(0.0, 0.12, v.y - v.x);
    const float tone = atx_fbm(p * 2.0, 4, salt + 2.0);
    const vec3 c = mix(vec3(0.4, 0.21, 0.1), vec3(0.58, 0.33, 0.17), tone) * (1.0 - groove * 0.4);
    return atx_make(c, (1.0 - groove) * (0.6 + v.z * 0.4), 0.0, 0.0);
}
AtxTexel atx_rust(vec2 p, float salt) {
    const float patches = atx_fbm(p * 1.8, 5, salt);
    const float flake = atx_fbm(p * 12.0, 4, salt + 4.0);
    const float metal = smoothstep(0.62, 0.7, patches);
    vec3 c = atx_ramp3(vec3(0.24, 0.09, 0.04), vec3(0.66, 0.29, 0.08), vec3(0.82, 0.55, 0.26), flake * 0.8 + patches * 0.3);
    c = mix(c, vec3(0.45, 0.45, 0.47) * (0.8 + flake * 0.4), metal);
    return atx_make(c, flake * 0.7 + metal * 0.3, metal * 0.8, 0.0);
}
AtxTexel atx_ice(vec2 p, float salt) {
    const vec3 v = atx_voronoi(p * 2.0, salt);
    const float crack = 1.0 - smoothstep(0.0, 0.035, v.y - v.x);
    const float frost = atx_fbm(p * 7.0, 5, salt + 3.0);
    const float deep = atx_fbm(p * 1.2, 3, salt + 6.0);
    vec3 c = mix(vec3(0.42, 0.7, 0.9), vec3(0.86, 0.96, 1.0), smoothstep(0.3, 0.8, frost * 0.7 + deep * 0.5));
    c = mix(c, vec3(1.0), crack * 0.7);
    return atx_make(c, frost * 0.6 + (1.0 - crack) * 0.4, 0.25, 0.1);
}
AtxTexel atx_crumpled(vec2 p, float salt) {
    const AtxTexel paper = atx_paper(p, salt);
    const vec3 v = atx_voronoi(p * 1.6 + atx_fbm(p * 0.9, 3, salt) * 0.6, salt + 9.0);
    const float crease = 1.0 - smoothstep(0.0, 0.06, v.y - v.x);
    const float facet = v.z * 0.5 + atx_ridged(p * 1.5, 4, salt + 2.0) * 0.5;
    return atx_make(paper.albedo * (0.94 + facet * 0.08) - crease * 0.06, facet * 0.8 + crease * 0.2, 0.0, 0.0);
}
AtxTexel atx_grain(vec2 p, float t, float salt) {
    const float frame = floor(t * 24.0);
    const float g = atx_h(p * 180.0, salt + frame * 3.0) * 0.6 + atx_noise(p * 120.0, salt + frame * 3.0 + 1.0) * 0.4;
    const float v = 0.5 + (g - 0.5) * 0.8;
    return atx_make(vec3(v), g, 0.0, 0.0);
}
AtxTexel atx_dust(vec2 p, float t, float salt) {
    const float frame = floor(t * 12.0);
    const vec3 d = atx_voronoi(p * 9.0, salt + frame * 5.0);
    const float speck = (1.0 - smoothstep(0.0, 0.05 + d.z * 0.06, d.x)) * step(0.86, d.z);
    // Arranhões verticais: poucos por quadro, em x sorteado, de comprimento variável.
    const float col = floor(p.x * 3.0);
    const float x0 = atx_h(vec2(col, frame), salt + 21.0);
    const float on = step(0.66, atx_h(vec2(col, frame), salt + 22.0));
    const float scratch = (1.0 - smoothstep(0.0, 0.006, abs(fract(p.x * 3.0) - x0)))
                        * on * step(0.3, atx_noise(vec2(col * 3.1, p.y * 1.5), salt + frame));
    const float bright = atx_h(vec2(col, frame), salt + 23.0);
    float v = 0.5 - speck * 0.42 * (d.z > 0.93 ? -0.8 : 1.0);
    v += scratch * (bright > 0.5 ? 0.42 : -0.38);
    return atx_make(vec3(v), v, 0.0, 0.0);
}
AtxTexel atx_canvas(vec2 p, float salt) {
    const vec2 g = p * 30.0;
    const float wx = 0.5 + 0.5 * sin(g.x * 3.14159) * sign(sin(g.y * 1.5708 + 0.785));
    const float wy = 0.5 + 0.5 * sin(g.y * 3.14159) * sign(sin(g.x * 1.5708));
    const float weave = mix(wx, wy, 0.5);
    const float slub = atx_fbm(p * 6.0, 3, salt);
    const float v = 0.52 + (weave - 0.5) * 0.32 + (slub - 0.5) * 0.12;
    return atx_make(vec3(v * 1.02, v, v * 0.95), weave * 0.8 + slub * 0.2, 0.0, 0.0);
}
AtxTexel atx_grunge(vec2 p, float salt) {
    const float stain = atx_fbm(p * 1.4, 5, salt);
    const float dirt = atx_ridged(p * 4.0, 4, salt + 3.0);
    const float speck = atx_h(p * 70.0, salt + 6.0);
    float v = 0.55 + (stain - 0.5) * 0.7 - smoothstep(0.75, 0.95, dirt) * 0.25;
    v -= step(0.985, speck) * 0.3;
    return atx_make(vec3(v, v * 0.97, v * 0.92), stain, 0.0, 0.0);
}
AtxTexel atx_halftone(vec2 p, float salt) {
    const vec2 q = mat2(0.7071, -0.7071, 0.7071, 0.7071) * p * 16.0;
    const vec2 cell = floor(q) + 0.5;
    const float tone = atx_fbm((mat2(0.7071, 0.7071, -0.7071, 0.7071) * cell) / 16.0 * 1.3, 3, salt);
    const float r = clamp(tone * 1.1 - 0.05, 0.0, 1.0) * 0.62;
    const float d = length(q - cell);
    const float dot_ = 1.0 - smoothstep(r - 0.06, r + 0.06, d);
    const float v = 1.0 - dot_ * 0.85;
    return atx_make(vec3(v), dot_, 0.0, 0.0);
}
AtxTexel atx_value_noise(vec2 p, float t, float salt) {
    const float frame = floor(t * 24.0);
    const float v = atx_noise(p * 60.0, salt + frame * 2.0) * 0.6 + atx_noise(p * 15.0, salt + frame * 2.0 + 1.0) * 0.4;
    return atx_make(vec3(v), v, 0.0, 0.0);
}
AtxTexel atx_frost(vec2 p, float salt) {
    const float f = atx_fbm(p * 20.0, 4, salt);
    const float v = 0.8 + (f - 0.5) * 0.3;
    return atx_make(vec3(v, v, min(v * 1.04, 1.0)), f, 0.0, 0.0);
}

/// A textura `id` no ponto `p`. `t` = fase da animação (segundos × velocidade);
/// `n` = normal da superfície (z = 1 é plana; inclina os reflexos dos metais);
/// `salt` = semente (variação). Id desconhecido: cinza médio liso.
AtxTexel atx_texel(int id, vec2 p, float t, vec3 n, float salt) {
    if (id == 1) return atx_gold(p, n, salt);
    if (id == 2) return atx_silver(p, n, salt);
    if (id == 3) return atx_brushed(p, n, salt);
    if (id == 4) return atx_wood(p, salt);
    if (id == 5) return atx_marble(p, salt);
    if (id == 6) return atx_stone(p, salt);
    if (id == 7) return atx_concrete(p, salt);
    if (id == 8) return atx_glitter(p, n, t, salt);
    if (id == 9) return atx_holo(p, n, t, salt);
    if (id == 10) return atx_neon(p, t);
    if (id == 11) return atx_fire(p, t, salt);
    if (id == 12) return atx_water(p, t, salt);
    if (id == 13) return atx_galaxy(p, t, salt);
    if (id == 14) return atx_paper(p, salt);
    if (id == 15) return atx_denim(p, salt);
    if (id == 16) return atx_leather(p, salt);
    if (id == 17) return atx_rust(p, salt);
    if (id == 18) return atx_ice(p, salt);
    if (id == 19) return atx_crumpled(p, salt);
    if (id == 20) return atx_grain(p, t, salt);
    if (id == 21) return atx_dust(p, t, salt);
    if (id == 22) return atx_canvas(p, salt);
    if (id == 23) return atx_grunge(p, salt);
    if (id == 24) return atx_halftone(p, salt);
    if (id == 25) return atx_value_noise(p, t, salt);
    if (id == 26) return atx_frost(p, salt);
    return atx_make(vec3(0.5), 0.5, 0.0, 0.0);
}

/// Cor de tela (sRGB) → linear do espaço de trabalho.
vec3 atx_to_linear(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
/// Linear → cor de tela (sRGB).
vec3 atx_to_display(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

/// Contraste e brilho (frações, −1..1) sobre a cor de tela; tinta multiplicada
/// (`tint.a` = quanto da tinta entra).
vec3 atx_grade(vec3 c, float contrast, float brightness, vec4 tint) {
    c = (c - 0.5) * (1.0 + contrast) + 0.5 + brightness;
    c = clamp(c, 0.0, 1.0);
    return mix(c, c * tint.rgb, clamp(tint.a, 0.0, 1.0));
}

/// Luz de relevo: superfície com normal `n`, luz pela direção `L` (unitária,
/// z para o observador). Metal: especular da cor da superfície. Devolve cor
/// de tela (pode passar de 1 no brilho especular; quem chama corta).
vec3 atx_light(AtxTexel tx, vec3 albedo, vec3 n, vec3 L) {
    const float ndl = max(dot(n, L), 0.0);
    const vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
    const float nh = max(dot(n, H), 0.0);
    const float spec = pow(nh, mix(24.0, 60.0, tx.metal)) * mix(0.35, 0.9, tx.metal);
    const vec3 specColor = mix(vec3(1.0), max(albedo, vec3(0.25)) * 1.25, tx.metal * 0.7);
    // Plano de frente (n = z) com a luz a ~45°: perto da cor original.
    const vec3 lit = albedo * (0.38 + 0.82 * ndl) + specColor * spec;
    return mix(lit, albedo + specColor * spec * 0.5, clamp(tx.emit, 0.0, 1.0));
}

#endif
