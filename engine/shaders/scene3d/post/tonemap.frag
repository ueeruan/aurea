#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / tonemap.frag
//
//  Saída do grupo 3D: cena HDR (resolvida do MSAA, codificada para o resolve)
//  → exposição do grupo → bloom → tone map → + 2D de exibição (planos,
//  partículas, unlit) → linear 0..1 pré-multiplicado, o espaço de trabalho do
//  compositor.
//
//  Bloom: `modo` 1 = mistura conservadora (sem limiar, estilo COD/Unreal: a
//  luz que espalha sai do próprio pixel — nada fica mais claro que era, e o
//  emissivo > 1 brilha proporcionalmente); 2 = soma do que passou do limiar.
//
//  Tone map sobre a luz PRÉ-MULTIPLICADA (luz sobre preto): contínuo na borda
//  de cobertura e no halo do bloom (que tem alfa 0 — é luz somada, não uma
//  superfície). O alfa é o do alvo de exibição, idêntico ao da cena por
//  construção (todo desenho do passe escreve o mesmo alfa nos dois alvos).
// =============================================================================
#include "../../common/bindings.glsl"
#include "../common/hdr.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_scene;    // HDR codificado (resolvido)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_display;  // 2D de exibição (resolvido)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_bloom;    // nível 0 da cadeia (já somada)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 cfg;     // x = exposição, y = força do bloom, z = modo (0 sem, 1 mistura, 2 soma), w = operador
    vec4 bloom;   // x = 1/níveis (normaliza a soma da cadeia)
} p;

void main() {
    const vec4 scene = texture(u_scene, v_uv);
    const vec4 display = texture(u_display, v_uv);
    vec3 hdr = aurea_hdr_decode(scene.rgb) * p.cfg.x;
    if (p.cfg.z > 0.5) {
        const vec3 b = texture(u_bloom, v_uv).rgb * p.bloom.x;
        hdr = p.cfg.z < 1.5 ? mix(hdr, b, p.cfg.y) : hdr + b * p.cfg.y;
    }
    vec3 c = aurea_tonemap(hdr, p.cfg.w) + display.rgb;
    // NaN/Inf de um material quebrado não pode vazar para o compositor.
    if (any(isnan(c)) || any(isinf(c))) c = display.rgb;
    o_color = vec4(clamp(c, 0.0, 1.0), clamp(display.a, 0.0, 1.0));
}
