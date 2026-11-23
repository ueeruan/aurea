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
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = intensidade, y = largura (texels), z = 1 inverte, w = mistura
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

vec3 premultiplied_encoded(vec4 pixel) {
    return aurea_linear_to_srgb(max(unpremultiply(pixel).rgb, vec3(0.0))) * pixel.a;
}

vec3 chromatic_edge_sample(vec2 uv) {
    vec4 pixel = texture(u_tex0, uv);
    return p.p1.y > 0.5 ? pixel.rgb : premultiplied_encoded(pixel);
}

vec4 chromatic_edges() {
    const vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec2 dx = vec2(p.p0.y, 0.0);
    const vec2 dy = vec2(0.0, p.p0.z);
    vec3 x = chromatic_edge_sample(uv + dx) - chromatic_edge_sample(uv - dx);
    vec3 y = chromatic_edge_sample(uv + dy) - chromatic_edge_sample(uv - dy);
    vec3 edge = sqrt(x * x + y * y) * p.p2.rgb * p.p0.x;
    float gray = dot(edge, vec3(0.3125, 0.5, 0.1875));
    edge = mix(vec3(gray), edge, p.p2.w) - p.p0.w;
    float alpha = texture(u_tex1, v_uv).a;
    if (alpha <= 1e-6) return vec4(0.0);
    return vec4(aurea_srgb_to_linear(clamp(edge, 0.0, alpha) / alpha) * alpha, alpha);
}

vec3 encoded(vec2 uv) {
    const vec4 s = texture(u_tex0, uv);
    if (p.p1.x > 0.5) {
        // The measured native 32-bpc profile differentiates premultiplied
        // linear RGB directly, retaining fractional gradients and HDR input.
        return s.rgb;
    }
    // Pré-multiplicado codificado: a borda com o transparente também conta.
    return aurea_linear_to_srgb(max(s.rgb, vec3(0.0)));
}

void main() {
    if (p.p1.x > 2.5) {
        vec4 source = texture(u_tex0, v_uv);
        o_color = vec4(premultiplied_encoded(source), source.a);
        return;
    }
    if (p.p1.x > 1.5) {
        o_color = chromatic_edges();
        return;
    }
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
    if (p.p1.x > 0.5) {
        float alpha = clamp(source.a, 0.0, 1.0);
        vec3 edge = min(sqrt(gx * gx + gy * gy), vec3(alpha));
        if (p.p0.z > 0.5) edge = vec3(alpha) - edge;
        o_color = vec4(mix(source.rgb, edge, clamp(p.p0.w, 0.0, 1.0)), alpha);
        return;
    }
    vec3 edge = clamp(sqrt(gx * gx + gy * gy) * 0.25 * max(p.p0.x, 0.0), 0.0, 1.0);
    if (p.p0.z > 0.5) edge = vec3(1.0) - edge;

    const vec4 drawn = vec4(aurea_srgb_to_linear(edge) * source.a, source.a);
    o_color = mix(source, drawn, clamp(p.p0.w, 0.0, 1.0));
}
