#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_scatter.frag
//
//  Espalhar (o comportamento do CC Scatterize): cada pixel lê um vizinho
//  sorteado até "Dispersão" px (um spray de grãos). As torções giram o ponto
//  sorteado em volta do centro da camada por uma fração sorteada do ângulo —
//  "Torção à esquerda" vale na borda esquerda, "à direita" na direita, e entre
//  elas mistura — e o spray vira redemoinho.
//
//  O sorteio é por px da camada e pela semente: o mesmo grão em todo quadro.
//  p0 = dispersão (px), torção esquerda (rad), torção direita (rad), modo;
//  p1.x = semente.
//  Modos: 0 substituir, 1 sobre o original, 2 atrás do original, 3 somar.
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const uint seed = uint(p.p1.x) * 2654435761u;
    const uvec2 q = uvec2(ivec2(floor(px)) + ivec2(1 << 20));
    const float r1 = aurea_hash(q ^ uvec2(seed, seed >> 7));
    const float r2 = aurea_hash(q.yx ^ uvec2(seed + 977u, seed >> 3));
    const float r3 = aurea_hash(q + uvec2(seed + 4111u, 17u));
    const float ang = r1 * AUREA_TAU;
    vec2 src = px + vec2(cos(ang), sin(ang)) * (sqrt(r2) * p.p0.x);
    const float fx = clamp((px.x - p.box.x) / max(p.box.z, 1.0), 0.0, 1.0);
    const float twist = mix(p.p0.y, p.p0.z, fx) * r3;
    const vec2 c = cc_box_center();
    src = c + cc_rot(src - c, twist);
    const vec4 sc = cc_at(src);
    const int mode = int(p.p0.w + 0.5);
    if (mode == 0) { o_color = sc; return; }
    const vec4 orig = cc_at(px);
    if (mode == 1) o_color = sc + orig * (1.0 - sc.a);
    else if (mode == 2) o_color = orig + sc * (1.0 - orig.a);
    else o_color = vec4(orig.rgb + sc.rgb, clamp(orig.a + sc.a, 0.0, 1.0));
}
