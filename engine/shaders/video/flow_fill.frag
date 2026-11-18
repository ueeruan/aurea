#version 450
// =============================================================================
//  Aurea / shaders / video / flow_fill.frag
//
//  Oclusão no nível base (0.1.8): um pixel do quadro de origem que SOME no
//  outro quadro (o fundo que vai ser coberto) não tem par — o Lucas-Kanade
//  devolve um fluxo qualquer ali, e o quadro intermediário arrastava o objeto
//  por cima do fundo (o rastro na borda do Movimento suave). Detecta pela ida
//  e volta (o fluxo de ida mais o de volta no ponto de chegada ≠ 0) e troca o
//  fluxo desses pixels pela média dos vizinhos COERENTES de luminância
//  parecida — o fundo herda o movimento do fundo.
//
//  1.0.3: "parecida" é na COR (luminância + crominância da guia): o fundo
//  verde-água atrás de um braço de pele não herda o movimento do braço só
//  porque os dois têm a mesma luminância.
//
//  Entrada: o fluxo deste sentido (RG + confiança), o do outro sentido (RG),
//  o nível de luminância (R = atual, G = seguinte) e a guia de cor (RG = CbCr
//  do atual, BA = do seguinte). Saída: RG + confiança.
// =============================================================================
#include "../common/bindings.glsl"
#include "flow_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_flow;    // este sentido
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_other;   // o sentido contrário
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_level;   // R = atual, G = seguinte
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_guide;   // RG = CbCr do atual, BA = do seguinte

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;    // xy = 1/tamanho do nível base
    vec4 flags;    // x = origem é o G (sentido de volta), y = tolerância da ida e volta (px), z = peso da crominância
} p;

float source_luma(vec2 uv) {
    const vec2 c = textureLod(u_level, uv, 0.0).rg;
    return p.flags.x > 0.5 ? c.g : c.r;
}
vec2 source_chroma(vec2 uv) {
    const vec4 c = textureLod(u_guide, uv, 0.0);
    return p.flags.x > 0.5 ? c.ba : c.rg;
}

// Erro da ida e volta em px: F(x) + G(x + F(x)).
float roundtrip(vec2 uv, vec2 f) {
    const vec2 back = textureLod(u_other, uv + f * p.texel.xy, 0.0).rg;
    return length(f + back);
}

void main() {
    const vec3 own = textureLod(u_flow, v_uv, 0.0).rgb;
    const float tol = max(p.flags.y, 0.25);
    if (roundtrip(v_uv, own.rg) < tol) {
        o_color = vec4(own, 1.0);
        return;
    }
    // Sem par: média dos vizinhos coerentes (13×13 em passos de 2 px).
    const float lc = source_luma(v_uv);
    const vec2 cc = source_chroma(v_uv);
    vec2 acc = vec2(0.0);
    float wsum = 0.0;
    for (int j = -6; j <= 6; j += 2) {
        for (int i = -6; i <= 6; i += 2) {
            const vec2 o = vec2(float(i), float(j));
            const vec2 uv = v_uv + o * p.texel.xy;
            const vec2 f = textureLod(u_flow, uv, 0.0).rg;
            if (roundtrip(uv, f) >= tol) continue;
            const float dl = source_luma(uv) - lc;
            const float w = exp(-dot(o, o) * 0.03) * exp(-flow_color_dist2(dl, source_chroma(uv) - cc, p.flags.z) * 120.0);
            acc += f * w;
            wsum += w;
        }
    }
    o_color = wsum > 1e-4 ? vec4(acc / wsum, own.b, 1.0) : vec4(own, 1.0);
}
