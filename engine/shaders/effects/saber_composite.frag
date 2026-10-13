#version 450
// =============================================================================
//  Aurea / shaders / effects / saber_composite.frag
//
//  Saber, passe final: núcleo + brilho → luz, e a luz sobre a camada.
//
//  A luz é somada em HDR (núcleo branco-quente + brilho na cor escolhida) e
//  passa por um mapa de tom exponencial POR CANAL: onde a luz é forte, os
//  canais saturam no branco — o miolo azul do sabre fica branco e a borda
//  fica azul, sem cortar seco.
//
//  O brilho é lido com a distorção num ruído mais largo que o do núcleo: o
//  halo ondula em volta do traço (fumaça, fogo).
//
//  Composição: 0 = somar sobre a camada; 1 = transparente (só o sabre, alfa
//  = canal mais forte da luz, pré-multiplicado correto); 2 = sobre preto.
// =============================================================================
#include "saber_common.glsl"

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada (entrada)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_core;   // núcleo (mesma região da saída)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_glow;   // brilho somado das oitavas (mesma região)

void main() {
    const vec2 px = saber_px();
    const vec4 src = texture(u_tex0, (px - p.src.xy) / max(p.src.zw, vec2(1e-6)));
    const vec4 core = texture(u_core, v_uv);
    float glow = 0.0;
    if (p.look.y > 0.5) {
        // Ruído no dobro da escala e com menos força que o do núcleo: o halo
        // ondula em volta do traço sem se descolar dele (no elétrico, menos).
        const vec2 off = saber_warp(px + vec2(101.0, 57.0), 2.0) * (int(p.dist.x + 0.5) == 3 ? 0.35 : 0.8);
        glow = max(texture(u_glow, v_uv + off / max(p.dst.zw, vec2(1.0))).r, 0.0);
    }
    const vec3 glowCol = max(p.glowColor.rgb, vec3(0.0));
    vec3 light = glowCol * (glow * p.glowColor.w);
    // Núcleo branco-quente; a saturação puxa para o tom do brilho.
    const float peak = max(max(glowCol.r, glowCol.g), max(glowCol.b, 1e-4));
    const vec3 tint = mix(vec3(1.0), glowCol / peak, clamp(p.coreColor.w, 0.0, 1.0));
    light += max(p.coreColor.rgb, vec3(0.0)) * tint * (max(core.r, 0.0) * p.look.z);
    const vec3 lit = vec3(1.0) - exp(-light);
    const float a = clamp(max(max(lit.r, lit.g), lit.b), 0.0, 1.0);
    const int mode = int(p.look.x + 0.5);
    if (mode == 1) {
        o_color = vec4(min(lit, vec3(a)), a);
    } else if (mode == 2) {
        o_color = vec4(lit, 1.0);
    } else {
        const float oa = clamp(src.a + a * (1.0 - src.a), 0.0, 1.0);
        o_color = vec4(min(src.rgb + lit, vec3(oa)), oa);
    }
}
