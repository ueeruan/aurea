#version 450
// =============================================================================
//  Aurea / shaders / effects / vignette.frag
//
//  Vinheta: escurece (ou clareia, com intensidade negativa) em direção às
//  bordas da camada. A forma acompanha a proporção da camada (elipse); o
//  Arredondamento puxa para um círculo (+) ou para um retângulo de cantos
//  suaves (-).
//
//  Linear e pré-multiplicado: a cor da vinheta entra multiplicada pelo alfa,
//  então a vinheta não pinta onde a camada é transparente.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = intensidade -1..1, y = tamanho 0..1, z = suavidade 0..1, w = arredondamento -1..1
    vec4 p1;   // xy = centro (px da camada), zw = tamanho natural da camada (px)
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;
    vec4 color;   // cor da vinheta (linear, direta)
} p;

void main() {
    const vec4 source = texture(u_tex0, v_uv);
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    if (p.p3.x > 0.5) {
        vec2 q = abs(aurea_rot2(-p.p3.z) * (point - p.p1.xy)) / max(p.p1.zw, vec2(1e-4));
        float roundness = 1.0 - p.p0.w;
        float exponent = 2.0 + p.p0.w * p.p0.w / max(roundness, 1e-3);
        float scale = max(q.x, q.y);
        vec2 normalized = q / max(scale, 1e-4);
        float distanceFromCenter = scale * pow(pow(normalized.x, exponent) + pow(normalized.y, exponent),
                                               1.0 / exponent);
        float softness = p.p0.z;
        float t = softness > 1e-5 ? clamp((distanceFromCenter - p.p0.y) / softness + 0.5, 0.0, 1.0)
                                   : step(p.p0.y, distanceFromCenter);
        float mask = mix(t, 0.5 - 0.5 * cos(t * 3.14159265), p.p3.y) * p.p0.x * p.color.a;
        vec3 rgb = mix(linear_to_srgb(unpremultiply(source).rgb), linear_to_srgb(p.color.rgb), mask);
        o_color = premultiply(vec4(srgb_to_linear(rgb), source.a));
        return;
    }
    const vec2 halfSize = max(p.p1.zw * 0.5, vec2(1.0));
    const float roundness = clamp(p.p0.w, -1.0, 1.0);
    const vec2 radii = mix(halfSize, vec2(max(halfSize.x, halfSize.y)), max(roundness, 0.0));
    const vec2 q = abs(point - p.p1.xy) / radii;
    // Superelipse: expoente 2 = elipse; maior = mais quadrado.
    const float e = 2.0 + max(-roundness, 0.0) * 6.0;
    const float d = pow(pow(q.x, e) + pow(q.y, e), 1.0 / e);

    const float inner = clamp(p.p0.y, 0.0, 1.0) * 1.41421356;
    const float outer = inner + max(clamp(p.p0.z, 0.0, 1.0), 0.002) * 1.41421356;
    const float v = smoothstep(inner, outer, d);

    const float amount = clamp(p.p0.x, -1.0, 1.0);
    const vec3 target = amount >= 0.0 ? p.color.rgb * source.a : vec3(source.a);
    o_color = vec4(mix(source.rgb, target, v * abs(amount)), source.a);
}
