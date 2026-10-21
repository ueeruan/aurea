#version 450
// =============================================================================
//  Aurea / shaders / effects / enhance_finish.frag
//
//  Aprimorar: o passe final, na resolução cheia, sobre a imagem já limpa (D):
//
//   Antisserrilhado/desborrar — alisa AO LONGO da borda (a tangente do
//       gradiente: o degrau da serra some, a borda não amolece) e reforça a
//       banda de ~4 px (o borrado leve volta a ter contorno).
//   Recuperar detalhes — devolve a textura média (meia resolução menos ¼)
//       onde HÁ textura acima do ruído; no liso a máscara é zero (o céu não
//       ganha granulado) e nas bordas fortes também (sem halo).
//   Nitidez — adaptativa ao contraste (a ideia publicada do CAS, conta
//       própria): o laplaciano em cruz com a amplitude que encolhe onde a
//       vizinhança já é contrastada; e o resultado PRESO à faixa do 3×3 com
//       uma folga pequena — mais acutância, sem auréola.
//   Remover halos — perto de uma borda forte (amostras a 5 e 7 px dos dois
//       lados, na direção do gradiente, na meia resolução), a luz é presa
//       entre os dois platôs: a auréola clara/escura de nitidez exagerada
//       some. Linha fina isolada não tem gradiente no meio nem platôs
//       diferentes dos lados, e fica.
//   Recuperar detalhe original — devolve a alta frequência da ENTRADA no
//       lugar da do resultado, na fração pedida.
//   Adicionar ruído — grão de filme pesado pela luz (mais nos meios-tons),
//       com tamanho em px da camada e semente por quadro (anda no tempo e é
//       o mesmo no preview e no export).
//   Mistura — com a entrada, no espaço de trabalho.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"
#include "enhance_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_d;      // imagem limpa (do efeito, ou a entrada linear)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_half;   // meia resolução (do efeito)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_in;     // a entrada (linear pré-multiplicada)
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_w;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;    // xy = 1/tamanho da cheia, zw = 1/tamanho da meia
    vec4 flags;    // x = D é a entrada linear (0/1), y = mistura (0..1), z = detalhe original (0..1)
    vec4 grain;    // x = amplitude do grão, y = tamanho (px da camada), z = semente (quadro), w = texels por px
    vec4 region;   // xy = origem da região (px da camada)
} p;

vec3 d_at(vec2 uv) { return enh_load(u_d, uv, p.flags.x > 0.5).xyz; }
vec3 h_at(vec2 uv) { return textureLod(u_half, uv, 0.0).xyz; }

float grain_noise(vec2 q, uint seed) {
    const vec2 i = floor(q);
    const vec2 f = fract(q);
    const vec2 u = f * f * (3.0 - 2.0 * f);
    const ivec2 b = ivec2(i);
    const uvec2 s = uvec2(seed, seed * 2654435761u);
    const float a = aurea_hash(uvec2(b) ^ s);
    const float c = aurea_hash(uvec2(b + ivec2(1, 0)) ^ s);
    const float d = aurea_hash(uvec2(b + ivec2(0, 1)) ^ s);
    const float e = aurea_hash(uvec2(b + ivec2(1, 1)) ^ s);
    return mix(mix(a, c, u.x), mix(d, e, u.x), u.y) * 2.0 - 1.0;
}

void main() {
    const vec4 dc = enh_load(u_d, v_uv, p.flags.x > 0.5);
    const vec4 w0 = enh_w0(u_w);
    const vec4 w1 = enh_w1(u_w);
    const float rd = w0.y, sh = w0.z, dh = w1.x, aa = w1.y;
    const float sigma = w1.z / sqrt(max(dc.w, 1.0));
    const vec2 t = p.texel.xy;
    const vec2 th = p.texel.zw;

    vec3 n[9];
    for (int k = 0; k < 9; ++k) n[k] = k == 4 ? dc.xyz : d_at(v_uv + vec2(float(k % 3 - 1), float(k / 3 - 1)) * t);
    float mn = n[0].x, mx = n[0].x;
    for (int k = 1; k < 9; ++k) { mn = min(mn, n[k].x); mx = max(mx, n[k].x); }
    const vec3 cross4 = 0.25 * (n[1] + n[3] + n[5] + n[7]);
    const vec3 blur3 = (4.0 * n[4] + 2.0 * (n[1] + n[3] + n[5] + n[7]) + (n[0] + n[2] + n[6] + n[8])) / 16.0;
    const float gx = ((n[2].x + 2.0 * n[5].x + n[8].x) - (n[0].x + 2.0 * n[3].x + n[6].x)) * 0.25;
    const float gy = ((n[6].x + 2.0 * n[7].x + n[8].x) - (n[0].x + 2.0 * n[1].x + n[2].x)) * 0.25;
    const float mag = length(vec2(gx, gy));
    const vec3 base2 = h_at(v_uv);
    const vec3 bq = 0.25 * (h_at(v_uv + vec2(-th.x, -th.y)) + h_at(v_uv + vec2(th.x, -th.y))
                          + h_at(v_uv + vec2(-th.x, th.y)) + h_at(v_uv + vec2(th.x, th.y)));

    vec3 x = dc.xyz;
    float boost = 0.0;   // o que a nitidez, o desborrar e o detalhe somam à luz
    // --- Antisserrilhado e desborrar ------------------------------------------
    if (aa > 0.0) {
        if (mag > 1e-4) {
            const vec2 tg = vec2(-gy, gx) / mag;
            const vec3 along = (2.0 * x + d_at(v_uv + tg * t) + d_at(v_uv - tg * t)
                                + 0.5 * (d_at(v_uv + 2.0 * tg * t) + d_at(v_uv - 2.0 * tg * t))) / 5.0;
            x = mix(x, along, aa * 0.85 * smoothstep(0.015, 0.08, mag));
        }
        boost += aa * 0.7 * (dc.x - bq.x);
    }
    // --- Recuperar detalhes ---------------------------------------------------
    if (rd > 0.0) {
        const float mid = base2.x - bq.x;
        const float fine = n[4].x - blur3.x;
        const float floorNoise = 2.5 * sigma + 0.004;
        const float energy = abs(n[4].x - cross4.x) + abs(mid);
        const float mask = smoothstep(floorNoise, 3.0 * floorNoise + 0.01, energy) * (1.0 - smoothstep(0.12, 0.35, mag));
        boost += rd * mask * (1.4 * mid + 0.7 * fine);
    }
    // --- Nitidez adaptativa ao contraste ----------------------------------------
    if (sh > 0.0) {
        // Ganho de até 4× sobre (centro − média da cruz), como o núcleo do CAS
        // no máximo; o laplaciano do tamanho do ruído que sobrou não é
        // reforçado (o liso não ganha granulado).
        const float amp = 0.25 + 0.75 * sqrt(clamp(min(mn, max(1.0 - mx, 0.0)) / max(mx, 1e-4), 0.0, 1.0));
        const float lap = n[4].x - cross4.x;
        const float core = 0.8 * sigma;
        const float kept = core > 0.0 ? lap * smoothstep(core, 2.0 * core, abs(lap)) : lap;
        boost += sh * 4.0 * amp * kept;
    }
    if (boost != 0.0) {
        // Preso à faixa do 3×3 com folga pequena: acutância sem auréola.
        const float lim = 0.12 * (mx - mn) * (1.0 - dh) + 0.003;
        x.x = clamp(x.x + boost, mn - lim, mx + lim);
    }
    // --- Remover halos ----------------------------------------------------------
    if (dh > 0.0) {
        // Os platôs ATRAVÉS da borda (na direção do gradiente), a 5 e 7 px dos
        // dois lados, na meia resolução: fora da auréola, que fica entre eles.
        const vec2 nrm = mag > 1e-4 ? vec2(gx, gy) / mag : vec2(1.0, 0.0);
        float lo = 1e9, hi = -1e9;
        for (int k = 0; k < 4; ++k) {
            const float r = (k < 2 ? 2.5 : 3.5) * ((k & 1) == 0 ? 1.0 : -1.0);
            const float h = h_at(v_uv + nrm * (r * th)).x;
            lo = min(lo, h);
            hi = max(hi, h);
        }
        const float zone = smoothstep(0.05, 0.16, hi - lo) * smoothstep(0.004, 0.02, mag);
        x.x = mix(x.x, clamp(x.x, lo, hi), dh * zone);
    }
    // --- Recuperar detalhe original --------------------------------------------
    if (p.flags.z > 0.0) {
        const vec3 oc = enh_from_linear(textureLod(u_in, v_uv, 0.0));
        const vec3 om = 0.25 * (enh_from_linear(textureLod(u_in, v_uv + vec2(t.x, 0.0), 0.0))
                              + enh_from_linear(textureLod(u_in, v_uv - vec2(t.x, 0.0), 0.0))
                              + enh_from_linear(textureLod(u_in, v_uv + vec2(0.0, t.y), 0.0))
                              + enh_from_linear(textureLod(u_in, v_uv - vec2(0.0, t.y), 0.0)));
        x += p.flags.z * ((oc - om) - (n[4] - cross4));
    }
    // --- Adicionar ruído (grão) ---------------------------------------------------
    if (p.grain.x > 0.0) {
        const vec2 lp = p.region.xy + (v_uv / max(t, vec2(1e-7))) / max(p.grain.w, 1e-3);
        const vec2 q = lp / max(p.grain.y, 0.25);
        const uint seed = uint(max(p.grain.z, 0.0)) * 747796405u + 2891336453u;
        const float g = 0.7 * grain_noise(q, seed) + 0.3 * grain_noise(q * 2.03 + 17.0, seed ^ 0x9E3779B9u);
        const float l = clamp(x.x, 0.0, 1.0);
        const float weight = 0.3 + 0.7 * clamp(4.0 * l * (1.0 - l), 0.0, 1.0);
        x.x += p.grain.x * g * weight;
    }
    const vec4 src = textureLod(u_in, v_uv, 0.0);
    const vec4 res = vec4(enh_to_linear(x) * src.a, src.a);
    o_color = mix(src, res, clamp(p.flags.y, 0.0, 1.0));
}
