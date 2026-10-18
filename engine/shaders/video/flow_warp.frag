#version 450
// =============================================================================
//  Aurea / shaders / video / flow_warp.frag
//
//  Quadro intermediário por movimento de pixels (0.1.8: nos DOIS sentidos).
//  No instante t entre o quadro atual A e o seguinte B:
//    * de A: o pixel que chega em p saiu de xa = p − t·F(xa)  (F = A → B);
//    * de B: o pixel que chega em p saiu de xb = p − (1−t)·G(xb)  (G = B → A).
//  Os dois pontos fixos são resolvidos por iteração. Se a iteração NÃO
//  converge (o fluxo no ponto achado não é o que levou até lá), aquele lado
//  não tem o pixel no instante t — ele está coberto (some) ou ainda não
//  apareceu. É assim que a borda de um objeto que passa por cima do fundo sai
//  limpa: o fundo que vai ser coberto vem de A, o que aparece vem de B. Onde
//  os dois lados valem, a mistura pesa pela proximidade no tempo.
//  Leituras do fluxo reaproveitadas: 6 por lado (o passe mais repetido).
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_a;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_b;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_flow;   // RG = A→B, BA = B→A (px do nível base)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 blend;   // x = t, yz = uv por pixel do nível base (converte o fluxo em uv)
} p;

void main() {
    const float t = p.blend.x;
    const vec2 scale = p.blend.yz;
    // Lado A: ponto fixo, resíduo (o fluxo no ponto achado) e ida e volta.
    vec2 fa = textureLod(u_flow, v_uv, 0.0).rg;
    for (int i = 0; i < 3; ++i) fa = textureLod(u_flow, v_uv - t * fa * scale, 0.0).rg;
    const vec2 xa = v_uv - t * fa * scale;
    const vec2 fxa = textureLod(u_flow, xa, 0.0).rg;
    const float ra = t * length(fxa - fa);
    const float ca = length(fxa + textureLod(u_flow, xa + fxa * scale, 0.0).ba);
    // Lado B.
    vec2 fb = textureLod(u_flow, v_uv, 0.0).ba;
    for (int i = 0; i < 3; ++i) fb = textureLod(u_flow, v_uv - (1.0 - t) * fb * scale, 0.0).ba;
    const vec2 xb = v_uv - (1.0 - t) * fb * scale;
    const vec2 gxb = textureLod(u_flow, xb, 0.0).ba;
    const float rb = (1.0 - t) * length(gxb - fb);
    const float cb = length(gxb + textureLod(u_flow, xb + gxb * scale, 0.0).rg);
    const vec4 a = texture(u_a, xa);
    const vec4 b = texture(u_b, xb);
    // Validade pelo resíduo (σ ≈ 0,6 px do nível) e pela ida e volta; peso no
    // tempo: o lado mais perto conta mais.
    const float va = exp(-ra * ra * 1.4) * mix(1.0, exp(-ca * ca * 0.25), 0.5);
    const float vb = exp(-rb * rb * 1.4) * mix(1.0, exp(-cb * cb * 0.25), 0.5);
    const float wa = (1.0 - t) * va + 1e-4;
    const float wb = t * vb + 1e-4;
    const vec4 o = (a * wa + b * wb) / (wa + wb);
    // Os dois lados sem o pixel (fluxo errado demais): o lado mais confiável,
    // com transição suave para não piscar.
    const float lost = 1.0 - smoothstep(0.02, 0.15, max(va, vb));
    o_color = mix(o, va >= vb ? a : b, lost);
}
