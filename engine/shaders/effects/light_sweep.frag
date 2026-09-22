#version 450
// =============================================================================
//  Aurea / shaders / effects / light_sweep.frag
//
//  Faixa de luz (Fase 7.3 §37): uma lâmina de luz atravessa a imagem num
//  ângulo. O que a torna útil não é a faixa em si, é ela reagir à IMAGEM:
//  `relevo` faz a luz acender só onde a superfície está virada para ela, como
//  um brilho especular de verdade em vez de uma tira colada por cima.
//
//  O centro é normalizado em relação ao lado maior, então a largura da faixa
//  é a mesma em qualquer proporção.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = centro (-1..2), y = largura (0..1), z = intensidade, w = suavidade da borda
    vec4 p1;   // x = ângulo (graus), y = relevo (0..1), z = 1 multiplicar, w = 1 acompanhar a luz da imagem
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 src = unpremultiply(texture(u_tex0, inUv));
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));

    // Eixo da faixa, girado pelo ângulo. O centro anda de -1 a 2, então a luz
    // entra e sai do quadro sozinha quando o parâmetro é animado.
    const float ang = radians(p.p1.x);
    const vec2 axis = vec2(cos(ang), sin(ang));
    const float along = dot(inUv - vec2(0.5), axis) + 0.5;
    const float d = abs(along - p.p0.x);

    const float half_width = max(p.p0.y, 1e-4) * 0.5;
    const float soft = max(p.p0.w, 1e-4);
    float band = 1.0 - smoothstep(half_width - soft, half_width + soft, d);

    // Relevo: a luz acende onde a superfície aponta para ela. A normal vem do
    // gradiente da própria imagem — é o que faz a faixa revelar volume.
    if (p.p1.y > 1e-4) {
        const vec2 e = vec2(1.5) * uvPerLayer;
        const float lx = aurea_luma(max(unpremultiply(texture(u_tex0, inUv + vec2(e.x, 0.0))).rgb, vec3(0.0)))
                       - aurea_luma(max(unpremultiply(texture(u_tex0, inUv - vec2(e.x, 0.0))).rgb, vec3(0.0)));
        const float ly = aurea_luma(max(unpremultiply(texture(u_tex0, inUv + vec2(0.0, e.y))).rgb, vec3(0.0)))
                       - aurea_luma(max(unpremultiply(texture(u_tex0, inUv - vec2(0.0, e.y))).rgb, vec3(0.0)));
        // A normal é a perpendicular do gradiente; o realce vem do produto
        // dela com o eixo da luz.
        const vec3 n = normalize(vec3(-lx, -ly, 0.25));
        const vec3 l = normalize(vec3(axis * 0.35, 1.0));
        band *= mix(1.0, clamp(dot(n, l) * 1.8 + 0.35, 0.0, 1.6), clamp(p.p1.y, 0.0, 1.0));
    }

    // Acompanhar a luz da imagem: a faixa não passa por cima do preto.
    if (p.p1.w > 0.5) {
        band *= smoothstep(0.02, 0.35, aurea_luma(aurea_linear_to_srgb(max(src.rgb, vec3(0.0)))));
    }

    const vec3 light = max(p.color.rgb, vec3(0.0)) * band * p.p0.z;
    const vec3 outc = p.p1.z > 0.5 ? src.rgb * (1.0 + light) : src.rgb + light;
    o_color = premultiply(vec4(max(outc, vec3(0.0)), src.a));
}
