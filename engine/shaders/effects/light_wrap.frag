#version 450
// =============================================================================
//  Aurea / shaders / effects / light_wrap.frag
//
//  Envolver com luz (Light Wrap, 0.4.5): a luz do FUNDO vaza para dentro das
//  bordas da camada, como acontece com um objeto filmado de verdade na frente
//  de uma fonte clara. Só age por dentro da silhueta (alfa da camada) e só
//  perto da borda: o peso é o que falta no alfa desfocado (u_tex1) — 0 no
//  miolo da camada, ~1 na borda.
//
//  A luz vem da camada de fundo escolhida, já desfocada e desenhada na
//  composição (u_tex2, px da composição; a camada vai para lá pela linha
//  afim de compFromLayer em p1/p2). Sem camada: a cor do efeito.
//
//  p0 = intensidade, só o envoltório (0/1), modo de mistura, —
//  p1 = linha x de compFromLayer (a, b, c), há fundo (0/1)
//  p2 = linha y de compFromLayer (d, e, f), —
//  p3 = região do alfa desfocado (px da camada)
//  p4 = região do fundo (px da composição)
//  color = cor da luz quando não há camada (linear)
// =============================================================================
#include "cc_common.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a camada desfocada (alfa)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_tex2;   // o fundo desfocado

void main() {
    const vec2 px = cc_px();
    const vec4 c = cc_at(px);
    if (c.a <= 1e-5) { o_color = p.p0.y > 0.5 ? vec4(0.0) : c; return; }

    const float blurred = texture(u_tex1, (px - p.p3.xy) / max(p.p3.zw, vec2(1e-6))).a;
    // Na borda o alfa desfocado vale ~0,5: o dobro de "o que falta" leva a 1 ali.
    const float edge = clamp(2.0 * (1.0 - blurred), 0.0, 1.0);
    const float w = clamp(edge * edge * p.p0.x, 0.0, 2.0);

    vec3 light = p.color.rgb;
    if (p.p1.w > 0.5) {
        const vec2 comp = vec2(dot(p.p1.xyz, vec3(px, 1.0)), dot(p.p2.xyz, vec3(px, 1.0)));
        const vec4 bg = texture(u_tex2, (comp - p.p4.xy) / max(p.p4.zw, vec2(1e-6)));
        light = bg.rgb;   // pré-multiplicado: fundo transparente não acende nada
    }

    if (p.p0.y > 0.5) {
        // Só o envoltório (para compor por cima com outro modo de mistura).
        const float k = min(w, 1.0) * c.a;
        o_color = vec4(light * k, k);
        return;
    }
    const vec3 s = c.rgb / c.a;
    const vec3 mixed = aurea_blend_generated(s, light, int(p.p0.z + 0.5));
    const vec3 outRgb = max(s + (mixed - s) * w, vec3(0.0));
    o_color = vec4(outRgb * c.a, c.a);
}
