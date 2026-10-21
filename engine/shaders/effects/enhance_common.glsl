// =============================================================================
//  Aurea / shaders / effects / enhance_common.glsl
//
//  Peças do Aprimorar (aurea.enhance.video). O efeito entra e sai no espaço de
//  trabalho do motor (linear, pré-multiplicado); por dentro trabalha num
//  espaço PERCEPTUAL, que é onde nascem os defeitos que ele conserta (o codec
//  quantiza o sinal codificado, não a luz):
//
//      cor reta (sem o alfa) → raiz quadrada da luz → YCoCg
//
//  A raiz é a curva de gama 2 (inversa exata: o quadrado) — barata e sem
//  `pow` no aparelho. YCoCg separa luz e cor com somas e meios (inversa exata).
//
//  Imagens intermediárias: RGB = (Y, Co, Cg) perceptual; A = PESO TEMPORAL
//  acumulado (1 = só o quadro atual; cresce com cada vizinho que casou). O alfa
//  de verdade não passa por elas: o passe final o lê da entrada.
//
//  Pesos finais (enhance_solve.frag), textura 2×1:
//      texel 0 = (reverter compressão, recuperar detalhes, nitidez, reduzir ruído)
//      texel 1 = (remover halos, antisserrilhado/desborrar, σ do ruído, livre)
//  todos 0..1, exceto σ (desvio do ruído na escala perceptual 0..1).
// =============================================================================
#ifndef AUREA_ENHANCE_COMMON_GLSL
#define AUREA_ENHANCE_COMMON_GLSL

vec3 enh_to_ycocg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * (c.r - c.b), -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 enh_from_ycocg(vec3 y) {
    const float t = y.x - y.z;
    return vec3(t + y.y, y.x + y.z, t - y.y);
}

/// Linear pré-multiplicado → (Y, Co, Cg) perceptual. Alfa zero = preto.
vec3 enh_from_linear(vec4 premul) {
    const vec3 c = premul.a > 1e-5 ? premul.rgb / premul.a : vec3(0.0);
    return enh_to_ycocg(sqrt(max(c, vec3(0.0))));
}

/// (Y, Co, Cg) perceptual → cor reta linear.
vec3 enh_to_linear(vec3 ycc) {
    const vec3 e = max(enh_from_ycocg(ycc), vec3(0.0));
    return e * e;
}

/// Lê uma imagem do efeito: a entrada linear (convertida, peso 1) ou uma
/// intermediária (já no espaço do efeito, com o peso temporal em A).
vec4 enh_load(sampler2D t, vec2 uv, bool linearInput) {
    const vec4 c = textureLod(t, uv, 0.0);
    return linearInput ? vec4(enh_from_linear(c), 1.0) : c;
}

vec4 enh_w0(sampler2D w) { return texelFetch(w, ivec2(0, 0), 0); }
vec4 enh_w1(sampler2D w) { return texelFetch(w, ivec2(1, 0), 0); }

#endif
