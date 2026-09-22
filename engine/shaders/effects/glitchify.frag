#version 450
// =============================================================================
//  Aurea / shaders / effects / glitchify.frag  (Fase 7.3 §43)
//
//  O glitch digital: a imagem se parte em BLOCOS que deslizam na horizontal,
//  os canais se separam, e de vez em quando um bloco sai inteiro. Não é ruído
//  por cima — é a imagem sendo lida do lugar errado.
//
//  Tudo vem de uma hash determinística por (bloco, faixa, quadro), então o
//  efeito FERVE sozinho com o tempo e para de ferver quando o tempo para —
//  o que faz dele a mesma coisa no preview e no export (§77).
//
//  `blocos` = a largura das faixas, `rasgo` = quanto elas deslizam, `pico` =
//  a chance de um bloco sair inteiro, `canal` = a separação RGB.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = altura da faixa (px), y = deslocamento máximo (px), z = chance de pico (0..1), w = separação RGB (px)
    vec4 p1;   // x = frequência (quadros por evento), y = semente, z = 1 manter o quadro estável, w = mistura
    vec4 p2;   // x = blocos verticais também, y = corrupção de cor (0..1), zw livres
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

void main() {
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec2 layerPx = inUv / uvPerLayer;

    // O tempo do glitch anda em PASSOS: o efeito troca de estado a cada
    // `frequência` quadros e fica parado no meio — é o que dá o estalo.
    const float step = floor(p.p3.x / max(p.p1.x, 0.25));
    const uint salt = uint(p.p1.y) * 2654435761u + uint(step) * 40503u;

    // Uma faixa horizontal de `altura` pixels; `blocos verticais` troca por
    // colunas, que é o rasgo de outra família de glitch.
    const float bandH = max(p.p0.x, 1.0);
    const float bandIndex = p.p2.x > 0.5 ? floor(layerPx.x / bandH) : floor(layerPx.y / bandH);

    const float r0 = aurea_hash(uvec2(uint(int(bandIndex)), salt));
    const float r1 = aurea_hash(uvec2(uint(int(bandIndex)) + 7919u, salt));
    const float r2 = aurea_hash(uvec2(uint(int(bandIndex)) + 31337u, salt));

    // Deslocamento da faixa: só as que passam do limiar andam, e as "pico"
    // saem inteiras para fora.
    float shift = 0.0;
    if (r0 < 0.45) shift = (r1 * 2.0 - 1.0) * p.p0.y;
    if (r2 < p.p0.z) shift += (r1 > 0.5 ? 1.0 : -1.0) * p.p0.y * 6.0;
    const vec2 off = p.p2.x > 0.5 ? vec2(0.0, shift) : vec2(shift, 0.0);

    // Separação RGB: cada canal lê de um ponto diferente. A separação é
    // modulada por faixa, então ela "quebra" junto com os blocos.
    const float sep = p.p0.w * (0.4 + r1 * 1.2);
    const vec2 sepUv = (p.p2.x > 0.5 ? vec2(0.0, sep) : vec2(sep, 0.0)) * uvPerLayer;

    vec4 s;
    s.r = unpremultiply(texture(u_tex0, inUv + (off + sepUv) * uvPerLayer)).r;
    s.g = unpremultiply(texture(u_tex0, inUv + off * uvPerLayer)).g;
    s.b = unpremultiply(texture(u_tex0, inUv + (off - sepUv) * uvPerLayer)).b;
    s.a = unpremultiply(texture(u_tex0, inUv + off * uvPerLayer)).a;

    // Corrupção de cor: uma faixa rara troca os canais de lugar.
    if (r2 > 1.0 - p.p2.y * 0.35) {
        const float w = aurea_hash(uvec2(salt, uint(int(bandIndex))));
        s.rgb = mix(s.rgb, s.gbr, w);
    }

    const vec4 src = unpremultiply(texture(u_tex0, inUv));
    const float k = clamp(p.p1.w, 0.0, 1.0);
    o_color = premultiply(vec4(max(mix(src.rgb, s.rgb, k), vec3(0.0)), mix(src.a, s.a, k)));
}
