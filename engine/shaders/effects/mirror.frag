#version 450
// =============================================================================
//  Aurea / shaders / effects / mirror.frag
//
//  Espelho: uma reta passa pelo centro, no ângulo pedido. O lado de "trás" da
//  reta fica como está; o lado da frente é substituído pelo reflexo dele. É
//  uma reflexão exata (sem esticar nem reamostrar a metade que fica).
//
//  O que o reflexo buscaria fora da imagem é transparente — nunca a borda
//  esticada.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // xy = centro (px da camada), zw = normal da reta (aponta para o lado refletido)
    vec4 p1;
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;
    vec4 color;
} p;

void main() {
    vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const vec2 n = p.p0.zw;
    const float d = dot(point - p.p0.xy, n);
    if (d > 0.0) point -= 2.0 * d * n;
    const vec2 uv = (point - p.p2.xy) / max(p.p2.zw, vec2(1e-3));
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) { o_color = vec4(0.0); return; }
    o_color = texture(u_tex0, uv);
}
