#version 450
// =============================================================================
//  Aurea / shaders / effects / enhance_temporal.frag
//
//  Aprimorar: redução de ruído TEMPORAL compensada por movimento. Um vizinho
//  por passe (do mais perto para o mais longe): o vizinho é trazido para o
//  lugar do quadro atual pelo optical flow (atual → vizinho, o mesmo Lucas-
//  Kanade em pirâmide do Flow Retime) e entra na média só onde CASA com o que
//  já foi acumulado — a semelhança é medida num pequeno remendo (1, 5 ou 9
//  amostras) e comparada com o que o próprio ruído explicaria. Onde o fluxo
//  erra (oclusão, objeto que aparece) a diferença passa do ruído e o peso cai
//  a zero: sem fantasma, a borda que anda fica no lugar dela.
//
//  O acumulador guarda (Y, Co, Cg) perceptual e o PESO somado em A; o
//  espacial seguinte usa esse peso (onde o tempo já limpou, ele age menos).
// =============================================================================
#include "../common/bindings.glsl"
#include "enhance_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_acc;    // acumulador, ou a entrada linear (flags.x)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_nbr;    // o vizinho (linear pré-multiplicado)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_flow;   // RG = fluxo atual → vizinho (px do nível base)
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_w;      // pesos (enhance_solve)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho da imagem, zw = uv por px do fluxo
    vec4 flags;   // x = acumulador é a entrada (0/1), y = amostras do remendo (1/5/9), z = peso pela distância no tempo
} p;

vec4 acc_at(vec2 uv) { return enh_load(u_acc, uv, p.flags.x > 0.5); }
vec3 nbr_at(vec2 uv) { return enh_from_linear(textureLod(u_nbr, uv, 0.0)); }

void main() {
    const vec4 a0 = acc_at(v_uv);
    const vec4 w0 = enh_w0(u_w);
    const vec4 w1 = enh_w1(u_w);
    const float sigma = w1.z;
    const float strength = smoothstep(0.0, 0.5, w0.w);
    if (strength <= 0.0 || sigma <= 0.0) { o_color = a0; return; }
    const vec2 f = textureLod(u_flow, v_uv, 0.0).rg;
    const vec2 uvn = v_uv + f * p.texel.zw;
    // O ponto de chegada fora do quadro: o vizinho não tem esse pixel.
    if (any(lessThan(uvn, vec2(0.0))) || any(greaterThan(uvn, vec2(1.0)))) { o_color = a0; return; }
    const vec3 n0 = nbr_at(uvn);
    const int taps = int(p.flags.y + 0.5);
    vec3 d = a0.xyz - n0;
    float d2 = d.x * d.x + 0.5 * (d.y * d.y + d.z * d.z);
    float count = 1.0;
    if (taps > 1) {
        for (int k = 0; k < 8; ++k) {
            // 5 = a cruz; 9 = a cruz e as diagonais.
            if (k >= 4 && taps < 9) break;
            const vec2 o = k < 4 ? vec2(k == 0 ? 1.0 : (k == 1 ? -1.0 : 0.0), k == 2 ? 1.0 : (k == 3 ? -1.0 : 0.0))
                                 : vec2((k & 1) == 0 ? 1.0 : -1.0, k < 6 ? 1.0 : -1.0);
            const vec2 step = o * p.texel.xy;
            const vec3 e = acc_at(v_uv + step).xyz - nbr_at(uvn + step);
            d2 += e.x * e.x + 0.5 * (e.y * e.y + e.z * e.z);
            count += 1.0;
        }
    }
    d2 /= count;
    // O que o ruído explica: o do vizinho (σ²) mais o que sobrou no
    // acumulador (σ²/peso), na luz e em meia cor (o ruído de Co/Cg é da
    // ordem do de Y: 1 + 0,5·(1,3 + 1) ≈ 1,6 para ruído por canal).
    const float s2 = sigma * sigma;
    const float expected = s2 * (1.0 + 1.0 / max(a0.w, 1.0)) * 1.6;
    const float tolerance = s2 * (1.5 + 4.0 * strength) + 1e-6;
    const float w = exp(-max(d2 - expected, 0.0) / tolerance) * strength * clamp(p.flags.z, 0.0, 1.0);
    o_color = vec4((a0.xyz * a0.w + n0 * w) / (a0.w + w), a0.w + w);
}
