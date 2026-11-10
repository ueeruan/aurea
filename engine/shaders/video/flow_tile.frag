#version 450
// =============================================================================
//  Aurea / shaders / video / flow_tile.frag
//
//  Maior vetor de movimento de cada bloco (e, na segunda passada, dos blocos
//  vizinhos). O desfoque vetorial usa esse vetor para saber se o rastro de um
//  objeto rápido alcança um pixel que está parado.
//
//  Entrada: RG = fluxo. Saída: RG = o vetor mais longo, em px do nível base.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_flow;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 grid;    // xy = extensão em uv percorrida, z = amostras por lado
    vec4 scale;   // x = multiplicador do vetor, y = comprimento máximo
} p;

void main() {
    const int n = clamp(int(p.grid.z), 1, 8);
    vec2 longest = vec2(0.0);
    float best = -1.0;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const vec2 o = (vec2(float(i), float(j)) + 0.5) / float(n) - 0.5;
            vec2 v = textureLod(u_flow, v_uv + o * p.grid.xy, 0.0).rg;
            if (v.x > 5000.0) continue;   // corte de cena (flow_pack.frag)
            v *= p.scale.x;
            const float len = length(v);
            if (len > p.scale.y) v *= p.scale.y / len;
            if (len > best) { best = len; longest = v; }
        }
    }
    o_color = vec4(longest, 0.0, 1.0);
}
