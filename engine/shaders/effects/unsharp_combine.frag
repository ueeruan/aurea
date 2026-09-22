#version 450
// =============================================================================
//  Aurea / shaders / effects / unsharp_combine.frag
//
//  A segunda metade da Máscara de nitidez (Fase 7.3 §47): original + ganho ×
//  (original − borrado). O limiar existe para não amplificar o grão do que já
//  é liso — abaixo dele, a diferença é considerada ruído e não é tocada.
//
//  A conta é em linear: realces e sombras reagem igual, sem o halo branco que
//  a nitidez feita no valor codificado produz nas bordas escuras.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // original
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // borrado

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = ganho, y = limiar (linear), z = teto do ganho, w = só detalhe
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    const vec4 a = unpremultiply(texture(u_tex0, v_uv));
    const vec4 b = unpremultiply(texture(u_tex1, v_uv));

    const vec3 detail = a.rgb - b.rgb;
    const float mag = length(detail);
    // Abaixo do limiar não é borda, é textura fina: o ganho cai a zero de
    // forma suave, para não aparecer um degrau em imagem granulada.
    const float gate = p.p0.y > 1e-5 ? smoothstep(0.0, p.p0.y, mag) : 1.0;
    const float amount = clamp(p.p0.x, 0.0, p.p0.z) * gate;

    vec3 outC = p.p0.w > 0.5 ? detail * amount : a.rgb + detail * amount;
    outC = max(outC, vec3(0.0));
    o_color = premultiply(vec4(outC, a.a));
}
