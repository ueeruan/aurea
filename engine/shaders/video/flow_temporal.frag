#version 450
// =============================================================================
//  Aurea / shaders / video / flow_temporal.frag
//
//  Contexto no tempo (1.0.3). O par anterior (k−1 → k) já está no cache
//  quando o par k → k+1 é calculado em sequência (reprodução, exportação).
//  Com velocidade quase constante, o movimento de k para k+1 é o OPOSTO do
//  movimento de k para k−1 no mesmo pixel do quadro k: o BA do par anterior,
//  negado, é uma previsão do fluxo de ida, já na grade do quadro k. Para o
//  fluxo de volta (na grade de k+1) a previsão é levada pelo próprio fluxo.
//
//  mode = 1: monta a previsão deste sentido a partir do cache do par anterior.
//  mode = 0: puxa o fluxo calculado para a previsão onde os dois quase
//  concordam (filtro recursivo no tempo): o ruído do fluxo de um par para o
//  outro some — era o "tremido" de parede e pele paradas no quadro inventado —
//  e onde o movimento mudou de verdade o fluxo do par fica como está.
//
//  Saída: RG = fluxo (px do nível base), B = confiança (passa adiante).
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_flow;    // mode 0: RG = fluxo deste par, B = confiança; mode 1: o cache do par anterior
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_prior;   // mode 0: RG = previsão deste sentido

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 flags;   // x = sentido (0 ida, 1 volta), y = peso máximo da previsão, z = tolerância (px), w = mode
    vec4 texel;   // xy = uv por px do nível base
} p;

void main() {
    if (p.flags.w > 0.5) {
        // Previsão a partir do par anterior: F(x) ≈ −G_ant(x).
        vec2 v = -textureLod(u_flow, v_uv, 0.0).ba;
        if (p.flags.x > 0.5) {
            // Volta, na grade de k+1: G(y) ≈ −F(x) com x + F(x) = y.
            for (int i = 0; i < 3; ++i) v = -textureLod(u_flow, v_uv - v * p.texel.xy, 0.0).ba;
            v = -v;
        }
        if (textureLod(u_flow, v_uv, 0.0).r > 5000.0) v = vec2(0.0);   // corte no par anterior
        o_color = vec4(v, 0.0, 1.0);
        return;
    }
    const vec4 f = textureLod(u_flow, v_uv, 0.0);
    const vec2 prior = textureLod(u_prior, v_uv, 0.0).rg;
    const vec2 d = f.rg - prior;
    const float tol = max(p.flags.z, 1e-3);
    const float lambda = p.flags.y * exp(-dot(d, d) / (2.0 * tol * tol));
    o_color = vec4(mix(f.rg, prior, lambda), f.b, 1.0);
}
