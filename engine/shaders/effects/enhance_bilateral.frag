#version 450
// =============================================================================
//  Aurea / shaders / effects / enhance_bilateral.frag
//
//  Aprimorar, "Reduzir ruído" (espacial): filtro bilateral em MEIA resolução
//  (¼ dos pixels; o raio 2 cobre 10 px da imagem cheia). Peso de cada vizinho
//  = distância (gaussiana) × semelhança, com a luz e a cor julgadas à parte:
//  a luz só se mistura com luz parecida (a borda fica), a cor aceita mais
//  (o ruído de cor é grosso e o olho perdoa menos a mancha que o borrão).
//  O limiar acompanha o σ do ruído nesta escala (σ/2 na média 2×2) e o peso
//  temporal de cada pixel (onde o tempo já limpou, σ/√peso).
// =============================================================================
#include "../common/bindings.glsl"
#include "enhance_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;   // meia resolução, espaço do efeito
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_w;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho da meia resolução
    vec4 flags;   // x = raio (1, 2 ou 3)
} p;

void main() {
    const vec4 c = textureLod(u_src, v_uv, 0.0);
    const float rn = enh_w0(u_w).w;
    const float sigma = enh_w1(u_w).z * 0.5 / sqrt(max(c.w, 1.0));
    if (rn <= 0.0 || sigma <= 0.0) { o_color = c; return; }
    const int radius = int(clamp(p.flags.x, 1.0, 3.0) + 0.5);
    const float sy = 2.2 * sigma + 1e-4;
    const float sc = 3.0 * sigma + 1e-4;
    const float iy = 0.5 / (sy * sy), ic = 0.5 / (sc * sc);
    const float ss = 0.45 + 0.55 * float(radius);
    const float is = 0.5 / (ss * ss);
    float wy = 0.0, wc = 0.0;
    float accY = 0.0;
    vec2 accC = vec2(0.0);
    for (int j = -3; j <= 3; ++j) {
        if (abs(j) > radius) continue;
        for (int i = -3; i <= 3; ++i) {
            if (abs(i) > radius) continue;
            const vec4 n = textureLod(u_src, v_uv + vec2(float(i), float(j)) * p.texel.xy, 0.0);
            const vec3 d = n.xyz - c.xyz;
            const float sp = float(i * i + j * j) * is;
            const float dy = d.x * d.x, dc = d.y * d.y + d.z * d.z;
            const float ly = exp(-sp - dy * iy - 0.25 * dc * ic);
            const float lc = exp(-sp - 0.25 * dy * iy - dc * ic);
            accY += n.x * ly;
            accC += n.yz * lc;
            wy += ly;
            wc += lc;
        }
    }
    const vec3 f = vec3(accY / wy, accC / wc);
    // Força: até a metade do slider a mistura cresce; depois só o σ cresce.
    o_color = vec4(mix(c.xyz, f, smoothstep(0.0, 0.5, rn)), c.w);
}
