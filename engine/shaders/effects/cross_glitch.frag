#version 450
// =============================================================================
//  Aurea / shaders / effects / cross_glitch.frag  (Fase 7.3 §50)
//
//  Glitch em CRUZ: uma faixa vertical e uma horizontal varrem a imagem, e o
//  que passa por baixo delas é lido do lugar errado. As duas cruzam, e no
//  cruzamento o defeito é o dobro.
//
//  A varredura anda com `progresso` (0 → 1), então animar esse parâmetro faz
//  a cruz atravessar o quadro — é assim que o efeito vira uma transição de
//  entrada ou de saída, sem precisar de um segundo clipe.
//
//  Fora das faixas o efeito é leve de propósito: um glitch que cobre tudo não
//  é um glitch, é ruído.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = progresso (0..1), y = largura da faixa (0..1), z = deslocamento máximo (px), w = ruído de fundo (0..1)
    vec4 p1;   // x = separação RGB (px), y = semente, z = frequência (quadros), w = mistura
    vec4 p2;   // x = 1 faixa vertical, y = 1 faixa horizontal, zw livres
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

void main() {
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 src = unpremultiply(texture(u_tex0, inUv));

    const float step = floor(p.p3.x / max(p.p1.z, 1.0));
    const uint salt = uint(p.p1.y) * 2654435761u + uint(step) * 40503u;

    // As duas faixas: uma em x, outra em y, ambas seguindo `progresso`.
    const float halfW = max(p.p0.y, 1e-3) * 0.5;
    const float vx = p.p2.x > 0.5 ? 1.0 - smoothstep(halfW, halfW * 1.6, abs(inUv.x - p.p0.x)) : 0.0;
    const float hy = p.p2.y > 0.5 ? 1.0 - smoothstep(halfW, halfW * 1.6, abs(inUv.y - p.p0.x)) : 0.0;
    // A cruz: onde as duas se encontram, o defeito soma.
    const float cross = clamp(vx + hy, 0.0, 1.6);

    // Em cada faixa, uma leitura torta. Onde elas cruzam, a soma das duas.
    const float rv = aurea_hash(uvec2(uint(int(inUv.y * 90.0)), salt));
    const float rh = aurea_hash(uvec2(uint(int(inUv.x * 90.0)) + 71u, salt));
    vec2 off = vec2(rv * 2.0 - 1.0, rh * 2.0 - 1.0) * p.p0.z * cross * uvPerLayer;

    // Ruído de fundo: fora da cruz, um tremor pequeno constante.
    off += vec2(aurea_hash(uvec2(salt, 5u)) * 2.0 - 1.0,
                aurea_hash(uvec2(salt, 9u)) * 2.0 - 1.0) * p.p0.w * 8.0 * uvPerLayer;

    // Separação RGB proporcional à força local.
    const float sep = p.p1.x * (0.2 + cross) * uvPerLayer.x;
    vec4 c;
    c.r = unpremultiply(texture(u_tex0, inUv + off + vec2(sep, 0.0))).r;
    c.g = unpremultiply(texture(u_tex0, inUv + off)).g;
    c.b = unpremultiply(texture(u_tex0, inUv + off - vec2(sep, 0.0))).b;
    c.a = unpremultiply(texture(u_tex0, inUv + off)).a;

    // Na cruz, uma linha clara varre: é a marca do efeito.
    const float line = max(vx, hy);
    c.rgb += vec3(line) * p.color.rgb * 0.55 * (p.p0.w * 2.0 + 0.2);

    const float k = clamp(p.p1.w, 0.0, 1.0);
    o_color = premultiply(vec4(max(mix(src.rgb, c.rgb, k), vec3(0.0)), mix(src.a, c.a, k)));
}
