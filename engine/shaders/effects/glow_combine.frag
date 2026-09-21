#version 450
// =============================================================================
//  Aurea / shaders / effects / glow_combine.frag
//
//  Última etapa do Glow: soma o brilho borrado (em resolução reduzida, lido
//  com filtro bilinear) à imagem original. Soma em LINEAR e pré-multiplicado —
//  é como a luz se soma de verdade, e é por isso que o glow não "acinzenta" as
//  áreas escuras como um blend em espaço codificado faria.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // original
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // brilho borrado

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMapSrc;    // uv da original = v_uv * xy + zw
    vec4 uvMapGlow;   // uv do brilho   = v_uv * xy + zw
    vec4 tint;        // rgb = cor multiplicadora, a = intensidade
} p;

void main() {
    vec4 src = texture(u_tex0, v_uv * p.uvMapSrc.xy + p.uvMapSrc.zw);
    vec4 glow = texture(u_tex1, v_uv * p.uvMapGlow.xy + p.uvMapGlow.zw);
    vec3 add = glow.rgb * p.tint.rgb * p.tint.a;
    // A luz somada também cobre: onde a original é transparente, o brilho
    // aparece (é o halo em volta de um texto, por exemplo).
    float a = clamp(src.a + glow.a * p.tint.a * (1.0 - src.a), 0.0, 1.0);
    o_color = vec4(src.rgb + add, a);
}
