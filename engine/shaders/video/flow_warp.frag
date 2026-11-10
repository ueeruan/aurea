#version 450
// =============================================================================
//  Aurea / shaders / video / flow_warp.frag
//
//  Quadro intermediário por movimento de pixels (Twixtor), nos DOIS sentidos.
//  No instante t entre o quadro atual A e o seguinte B:
//    * de A: o pixel que chega em p saiu de xa = p − t·F(xa)  (F = A → B);
//    * de B: o pixel que chega em p saiu de xb = p − (1−t)·G(xb)  (G = B → A).
//  Os dois pontos fixos são resolvidos por iteração. Um lado só vale se a
//  iteração converge, a ida e volta fecha e a origem está dentro do quadro:
//  sem isso o pixel está coberto ou ainda não apareceu naquele lado. Assim o
//  fundo que vai ser coberto vem de A e o que aparece vem de B.
//
//  Vetor errado é pior que fantasma: deforma o objeto. Quando nenhum lado vale,
//  ou quando os dois valem mas trazem imagens diferentes, o pixel cai na
//  mistura simples dos quadros no lugar, que não entorta nada.
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

float inside(vec2 uv) {
    return (uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0) ? 1.0 : 0.0;
}

float tone(vec3 c) { return sqrt(max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 0.0)); }

void main() {
    const float t = p.blend.x;
    const vec2 scale = p.blend.yz;
    // Corte de cena (flow_pack.frag): não existe quadro intermediário.
    if (textureLod(u_flow, v_uv, 0.0).r > 5000.0) {
        o_color = t < 0.5 ? texture(u_a, v_uv) : texture(u_b, v_uv);
        return;
    }
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
    const float va = inside(xa) * exp(-ra * ra * 1.4) * exp(-ca * ca * 0.3);
    const float vb = inside(xb) * exp(-rb * rb * 1.4) * exp(-cb * cb * 0.3);
    const float wa = (1.0 - t) * va + 1e-4;
    const float wb = t * vb + 1e-4;
    const vec4 moved = (a * wa + b * wb) / (wa + wb);
    const vec4 still = mix(texture(u_a, v_uv), texture(u_b, v_uv), t);
    // Só compara as imagens onde os dois lados afirmam ter o pixel.
    const float disagree = min(va, vb) * smoothstep(0.10, 0.28, abs(tone(a.rgb) - tone(b.rgb)));
    const float lost = 1.0 - smoothstep(0.02, 0.15, max(va, vb));
    o_color = mix(moved, still, max(lost, disagree));
}
