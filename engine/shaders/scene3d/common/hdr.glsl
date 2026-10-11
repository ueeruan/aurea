// =============================================================================
//  Aurea / shaders / scene3d / common / hdr.glsl
//
//  A cena 3D em HDR: o passe da cena escreve luz LINEAR SEM TETO num alvo
//  próprio (o 2D de exibição — planos, partículas, unlit — vai no outro alvo
//  do MRT, intocado). Depois do resolve do MSAA, o pós do grupo aplica
//  exposição, bloom e o tone map, e soma o 2D por cima.
//
//  1. CODIFICAÇÃO PARA O RESOLVE (Karis). O resolve do MSAA é uma média de
//     amostras. Em HDR, uma amostra de 50 ao lado de uma de 0 dá 25 — que o
//     tone map leva a ~1: a borda de um realce fica "gorda" e serrilhada de
//     novo. A saída do passe é comprimida só ACIMA de 1 (Reinhard com teto 3),
//     a média acontece no espaço comprimido e o pós descomprime:
//
//         e(m) = m                         m ≤ 1
//         e(m) = 1 + (m-1)/(1 + (m-1)/2)   m > 1        (m = maior canal)
//
//     Por que não o 1/(1+luma) inteiro do Karis: a mistura de hardware dos
//     materiais transparentes também acontece nesse espaço. Com identidade até
//     1, tudo que é LDR (a quase totalidade de um frame) mistura EXATAMENTE
//     como antes; só o realce > 1 mistura comprimido — e é justamente nele que
//     a média ponderada importa. Precisão: fp16 guarda e(m) até m ≈ 1000
//     (1% de erro em m = 10, ~5% em m = 100 — invisível depois do tone map).
//
//  2. TONE MAP (saída do grupo, espaço de exibição linear 0..1, como o
//     compositor espera — o output.frag codifica sRGB no fim):
//       0 = Khronos PBR Neutral (padrão): preserva a cor base até ~0,76 e só
//           comprime o realce. É o visual de sempre do Aurea.
//       1 = AgX (Sobotka, o "Filmic" do Blender 4), look base: curva
//           sigmoide em log2, dessatura o realce em vez de mudar a matiz (o
//           ACES "ajustado" puxa azul para roxo e satura demais). Cinza médio
//           0,18 → 0,18.
// =============================================================================
#ifndef AUREA_HDR_GLSL
#define AUREA_HDR_GLSL

const float AUREA_HDR_ENCODE_MAX = 2.999;

vec3 aurea_hdr_encode(vec3 c) {
    c = max(c, vec3(0.0));
    float m = max(c.r, max(c.g, c.b));
    if (m <= 1.0) return c;
    float x = m - 1.0;
    float e = 1.0 + x / (1.0 + 0.5 * x);
    return c * (e / m);
}

vec3 aurea_hdr_decode(vec3 c) {
    c = max(c, vec3(0.0));
    float e = max(c.r, max(c.g, c.b));
    if (e <= 1.0) return c;
    float y = min(e, AUREA_HDR_ENCODE_MAX) - 1.0;
    float m = 1.0 + y / (1.0 - 0.5 * y);
    return c * (m / e);
}

// Khronos PBR Neutral (https://github.com/KhronosGroup/ToneMapping).
vec3 aurea_tonemap_neutral(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

// AgX, look base (aproximação polinomial de 6ª ordem da curva do Blender).
vec3 aurea_tonemap_agx(vec3 color, float look) {
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                            0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                             -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                             -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    vec3 v = inset * max(color, vec3(0.0));
    v = clamp(log2(max(v, vec3(1e-10))), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    vec3 v2 = v * v;
    vec3 v4 = v2 * v2;
    v = 15.5 * v4 * v2 - 40.14 * v4 * v + 31.96 * v4 - 6.868 * v2 * v + 0.4298 * v2 + 0.1191 * v - 0.00232;
    // CDL look parameters, specified by the AgX author's config. Apply in
    // AgX's encoded base space before the outset/linear conversion.
    if (look > 1.5) {
        vec3 slope = look > 2.5 ? vec3(1.0, 0.9, 0.5) : vec3(1.0);
        float power = look > 2.5 ? 0.8 : 1.35;
        float saturation = look > 2.5 ? 1.3 : 1.4;
        v = pow(max(v * slope, vec3(0.0)), vec3(power));
        float luma = dot(v, vec3(0.2126, 0.7152, 0.0722));
        v = mix(vec3(luma), v, saturation);
    }
    v = outset * v;
    // A curva sai codificada (≈ gama 2,2): o compositor quer linear.
    return pow(clamp(v, 0.0, 1.0), vec3(2.2));
}

vec3 aurea_tonemap(vec3 color, float op) {
    vec3 x = max(color, vec3(0.0));
    if (op > 4.5) {
        // ACES SDR rational fit, rather than a full ACES color pipeline.
        return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    }
    if (op > 3.5) {
        // Uchimura 2017: power toe, linear middle and exponential shoulder.
        const float m = 0.22;
        const float shoulder = 0.532;
        vec3 toe = m * pow(x / m, vec3(1.33));
        vec3 high = 1.0 - (1.0 - shoulder) * exp(-(x - shoulder) / (1.0 - shoulder));
        vec3 toeWeight = 1.0 - smoothstep(vec3(0.0), vec3(m), x);
        vec3 highWeight = step(vec3(shoulder), x);
        return clamp(toe * toeWeight + x * (1.0 - toeWeight - highWeight) + high * highWeight, 0.0, 1.0);
    }
    return op > 0.5 ? aurea_tonemap_agx(x, op) : aurea_tonemap_neutral(x);
}

float aurea_luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

#endif
