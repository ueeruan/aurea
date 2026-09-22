#version 450
// =============================================================================
//  Aurea / shaders / effects / deep_glow_combine.frag
//
//  A montagem do Brilho profundo (Fase 7.3 §29). O que separa este brilho de
//  um glow comum são três coisas:
//
//   1. DUAS PASSADAS de desfoque em raios diferentes — um halo apertado
//      (núcleo) e um largo (ambiente). Um raio só dá o brilho "chapado".
//   2. A COR do halo é aplicada por PESO da luz, não somada: é o que permite
//      um brilho quente num núcleo frio.
//   3. O núcleo pode ser somado em ADIÇÃO e o halo em TELA: o núcleo estoura,
//      o ambiente preenche.
//
//  `u_tex0` = original, `u_tex1` = halo apertado, `u_tex2` = halo largo.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_tex2;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = intensidade do núcleo, y = intensidade do halo, z = limite de saturação, w = preservar sombras
    vec4 p1;   // x = tela (1) ou adição (0) no halo, y = 1 tingir o núcleo com a cor, z = 1 tingir o halo, w = só o brilho (sem o original)
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    const vec4 src = unpremultiply(texture(u_tex0, v_uv));
    const vec3 core = max(unpremultiply(texture(u_tex1, v_uv)).rgb, vec3(0.0));
    const vec3 halo = max(unpremultiply(texture(u_tex2, v_uv)).rgb, vec3(0.0));

    const vec3 tint = max(p.color.rgb, vec3(0.0));
    // Tingir por PESO: multiplica a cor do halo mas mantém o que ele já tinha.
    const vec3 coreC = p.p1.y > 0.5 ? core * tint : core;
    const vec3 haloC = p.p1.z > 0.5 ? halo * tint : halo;

    // Tela: 1-(1-a)(1-b) — o ambiente PREENCHE sem estourar o que já é claro.
    const vec3 screened = 1.0 - (1.0 - src.rgb) * (1.0 - haloC * p.p0.y);
    const vec3 halved = mix(src.rgb + haloC * p.p0.y, screened, p.p1.x);

    vec3 outc = halved + coreC * p.p0.x;

    // Preservar as sombras: o brilho não pode levantar o preto, senão some o
    // contraste de um letreiro neon sobre fundo escuro.
    if (p.p0.w > 0.5) {
        const float luma = aurea_luma(aurea_linear_to_srgb(max(src.rgb, vec3(0.0))));
        outc = mix(src.rgb, outc, smoothstep(0.0, 0.25, luma));
    }

    // Limite: acima dele a cor desliza para o branco (o estouro do sensor).
    const float lim = max(p.p0.z, 1e-3);
    const float peak = max(outc.r, max(outc.g, outc.b));
    if (peak > lim) {
        outc = mix(outc, vec3(peak), clamp((peak - lim) / lim, 0.0, 1.0));
    }

    o_color = premultiply(vec4(max(p.p1.w > 0.5 ? outc - src.rgb : outc, vec3(0.0)), src.a));
}
