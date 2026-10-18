#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_flow_pinch.frag
//
//  Fluxo e pinça (o comportamento do CC Flo Motion): dois nós puxam a imagem.
//  Força > 0 suga o conteúdo para dentro do nó (o pixel perto dele lê mais
//  longe); força < 0 empurra para fora (lente de aumento). O peso cai como
//  uma gaussiana de alcance "Alcance"; os dois nós se compõem em sequência.
//  "Repetir bordas" faz o que sai da camada voltar pelo outro lado.
//
//  p0 = nó 1 (px), força 1, —; p1 = nó 2 (px), força 2, —;
//  p2 = alcance (px), repetir bordas (0/1), amostras por eixo (1..3).
// =============================================================================
#include "cc_common.glsl"

vec2 pinch(vec2 q, vec2 knot, float amount, float reach) {
    const vec2 d = q - knot;
    const float w = exp(-dot(d, d) / (reach * reach));
    return knot + d * (1.0 + amount * w);
}

vec4 flow(vec2 q) {
    const float reach = max(p.p2.x, 1e-3);
    q = pinch(q, p.p0.xy, p.p0.z, reach);
    q = pinch(q, p.p1.xy, p.p1.z, reach);
    if (p.p2.y > 0.5) q = p.box.xy + mod(q - p.box.xy, max(p.box.zw, vec2(1.0)));
    return cc_at(q);
}

void main() {
    const vec2 px = cc_px();
    const int n = clamp(int(p.p2.z + 0.5), 1, 3);
    if (n == 1) { o_color = flow(px); return; }
    // Superamostragem n×n dentro do texel de saída (o antisserrilhado).
    vec4 sum = vec4(0.0);
    const float stp = cc_texel() / float(n);
    for (int j = 0; j < 3; ++j) {
        if (j >= n) break;
        for (int i = 0; i < 3; ++i) {
            if (i >= n) break;
            sum += flow(px + (vec2(float(i), float(j)) + 0.5 - 0.5 * float(n)) * stp);
        }
    }
    o_color = sum / float(n * n);
}
