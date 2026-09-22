#version 450
// =============================================================================
//  Aurea / shaders / effects / signal.frag  (Fase 7.3 §44)
//
//  Interferência de sinal: a imagem chega com DEFEITO DE TRANSMISSÃO, não de
//  fita. Três coisas a separam do VHS:
//
//   1. BANDAS de sinal perdido, largas e horizontais, que caem em sequência
//   2. DESLOCAMENTO de sincronia: a imagem inteira escorrega de lado por um
//      instante e volta
//   3. DERIVA: a imagem anda devagar para um lado, sempre, como um sinal fora
//      de frequência
//
//  Cada banda é uma leitura do mesmo quadro numa posição errada — por isso a
//  imagem parece "rasgada" e não "borrada".
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = banda (0..1), y = deslocamento máximo (px), z = deriva (px/quadro), w = altura da banda (px)
    vec4 p1;   // x = ruído de sinal (0..1), y = semente, z = frequência (quadros), w = mistura
    vec4 p2;   // x = 1 sincronia fora (a imagem inteira escorrega), y = separação (px), zw livres
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

void main() {
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 src = unpremultiply(texture(u_tex0, inUv));

    const float step = floor(p.p3.x / max(p.p1.z, 1.0));
    const uint salt = uint(p.p1.y) * 2654435761u + uint(step) * 40503u;

    // Deriva: sempre presente, devagar. É o que faz o sinal "vivo".
    float shift = p.p0.z * p.p3.x;

    // Bandas: uma faixa de `altura` pixels que perdeu o sinal.
    const float bandH = max(p.p0.w * p.texel.y, 1e-6);
    const float bandIndex = floor(inUv.y / bandH);
    const float rb = aurea_hash(uvec2(uint(int(bandIndex)), salt));
    if (rb < p.p0.x * 0.4) {
        shift += (aurea_hash(uvec2(uint(int(bandIndex)) + 3u, salt)) * 2.0 - 1.0) * p.p0.y;
    }

    // Sincronia fora: por um instante, TUDO escorrega.
    float frameShift = 0.0;
    if (p.p2.x > 0.5) {
        const float rs = aurea_hash(uvec2(salt, 0x5EEDu));
        if (rs < 0.12) frameShift = (aurea_hash(uvec2(salt, 0x1234u)) * 2.0 - 1.0) * p.p0.y * 3.0;
    }
    shift += frameShift;

    // Separação de croma, só nas bandas: a cor é o primeiro a se perder.
    const float sep = p.p2.y * (rb < p.p0.x * 0.4 ? 1.0 : 0.15) * uvPerLayer.x;
    const vec2 o = vec2(shift * uvPerLayer.x, 0.0);
    vec4 c;
    c.r = unpremultiply(texture(u_tex0, inUv + o + vec2(sep, 0.0))).r;
    c.g = unpremultiply(texture(u_tex0, inUv + o)).g;
    c.b = unpremultiply(texture(u_tex0, inUv + o - vec2(sep, 0.0))).b;
    c.a = unpremultiply(texture(u_tex0, inUv + o)).a;

    // Ruído de sinal: granulado fino, mais forte onde a imagem é escura.
    if (p.p1.x > 0.0) {
        const vec2 px = inUv / uvPerLayer + vec2(shift, 0.0);
        const float n = aurea_hash3(px * 1.5, salt) * 2.0 - 1.0;
        const float dark = 1.0 - clamp(aurea_luma(c.rgb), 0.0, 1.0) * 0.55;
        c.rgb += vec3(n) * p.p1.x * 0.25 * dark;
        // Linhas brancas ocasionais: o "chiado" do sinal fraco.
        const float rl = aurea_hash(uvec2(uint(int(floor(px.y))), salt));
        if (rl < p.p1.x * 0.03) c.rgb += vec3(0.35);
    }

    const float k = clamp(p.p1.w, 0.0, 1.0);
    o_color = premultiply(vec4(max(mix(src.rgb, c.rgb, k), vec3(0.0)), mix(src.a, c.a, k)));
}
