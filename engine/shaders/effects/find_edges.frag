#version 450
// =============================================================================
//  Aurea / shaders / effects / find_edges.frag
//
//  Detectar bordas: gradiente de Sobel por canal, medido na cor CODIFICADA
//  (sRGB) — é onde o olho vê a borda; em linear, as sombras quase não teriam
//  contorno. "Inverter" dá o traço escuro sobre branco (desenho a lápis); sem
//  inverter, bordas acesas sobre preto.
//
//  O alfa da camada fica como estava: o efeito redesenha a cor, não recorta.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = intensidade, y = largura (texels), z = 1 inverte, w = mistura
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

vec3 encoded(vec2 uv) {
    const vec4 s = texture(u_tex0, uv);
    // Pré-multiplicado codificado: a borda com o transparente também conta.
    return aurea_linear_to_srgb(max(s.rgb, vec3(0.0)));
}

void main() {
    const vec4 source = texture(u_tex0, v_uv);
    const vec2 st = p.texel.xy * max(p.p0.y, 0.25);
    const vec3 tl = encoded(v_uv + vec2(-1.0, -1.0) * st);
    const vec3 tc = encoded(v_uv + vec2( 0.0, -1.0) * st);
    const vec3 tr = encoded(v_uv + vec2( 1.0, -1.0) * st);
    const vec3 ml = encoded(v_uv + vec2(-1.0,  0.0) * st);
    const vec3 mr = encoded(v_uv + vec2( 1.0,  0.0) * st);
    const vec3 bl = encoded(v_uv + vec2(-1.0,  1.0) * st);
    const vec3 bc = encoded(v_uv + vec2( 0.0,  1.0) * st);
    const vec3 br = encoded(v_uv + vec2( 1.0,  1.0) * st);
    const vec3 gx = (tr + 2.0 * mr + br) - (tl + 2.0 * ml + bl);
    const vec3 gy = (bl + 2.0 * bc + br) - (tl + 2.0 * tc + tr);
    vec3 edge = clamp(sqrt(gx * gx + gy * gy) * 0.25 * max(p.p0.x, 0.0), 0.0, 1.0);
    if (p.p0.z > 0.5) edge = vec3(1.0) - edge;

    const vec4 drawn = vec4(aurea_srgb_to_linear(edge) * source.a, source.a);
    o_color = mix(source, drawn, clamp(p.p0.w, 0.0, 1.0));
}
