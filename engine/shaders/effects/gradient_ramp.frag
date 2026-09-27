#version 450
// =============================================================================
//  Aurea / shaders / effects / gradient_ramp.frag
//
//  Degradê entre dois pontos do plano da camada: linear (a projeção do pixel
//  na reta inicial→final) ou radial (a distância ao ponto inicial, com o
//  final marcando o raio). A dispersão soma ruído por pixel à posição na
//  rampa — quebra a banda de cor de um degradê longo em 8 bits.
//
//  As duas cores chegam lineares; a mistura entre elas é em linear (o que
//  bate com o compositor). Saída pré-multiplicada pela cobertura da camada.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // xy = ponto inicial, zw = ponto final (px da camada)
    vec4 p1;   // x = forma (0 linear, 1 radial), y = dispersão (px), z = mistura com o original 0..1, w = modo de mistura
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;
    vec4 color;   // cor inicial (linear)
    vec4 q0;      // cor final (linear)
    vec4 q1;      // x = preencher a caixa toda
    vec4 q2;
    vec4 q3;
} p;

void main() {
    const vec4 src = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    const float cover = p.q1.x > 0.5 ? 1.0 : src.a;
    const float amount = 1.0 - clamp(p.p1.z, 0.0, 1.0);
    if (cover <= 0.0 || amount <= 0.0) {
        o_color = src;
        return;
    }

    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const vec2 d = p.p0.zw - p.p0.xy;
    const vec2 v = point - p.p0.xy;
    float t = p.p1.x > 0.5 ? length(v) / max(length(d), 1e-4)
                           : dot(v, d) / max(dot(d, d), 1e-8);
    // Dispersão: ruído de pixel (determinístico pela posição na camada) na
    // unidade da rampa — `dispersão` px ao longo do comprimento dela.
    const float scatter = max(p.p1.y, 0.0);
    if (scatter > 0.0) {
        const float noise = aurea_hash(uvec2(ivec2(floor(point)))) - 0.5;
        t += noise * scatter / max(length(d), 1e-4);
    }
    t = clamp(t, 0.0, 1.0);

    const vec3 base = src.a > 1e-6 ? src.rgb / src.a : vec3(0.0);
    const vec3 gen = mix(p.color.rgb, p.q0.rgb, t);
    const vec3 blended = clamp(aurea_blend_generated(base, gen, int(p.p1.w + 0.5)), 0.0, 1.0);
    const vec3 outc = mix(base, blended, amount);
    o_color = vec4(outc * cover, cover);
}
