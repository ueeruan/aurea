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

// Original bounded sort: reorder a 16-pixel row segment by straight luminance.
// Only enabled blocks pay the texture/sorting cost; seed and time select blocks.
vec4 sortedPixel(vec2 uv, vec2 stepUV, uint salt) {
    vec2 px = uv / stepUV;
    float segment = floor(px.x / 16.0);
    uint row = uint(int(floor(px.y)));
    if (p.color.y <= 0.0 || aurea_hash(uvec2(uint(int(segment)) ^ row, salt + 701u)) >= p.color.y)
        return texture(u_tex0, uv);
    vec4 samples[16];
    float light[16];
    for (int i = 0; i < 16; ++i) {
        samples[i] = texture(u_tex0, vec2(segment * 16.0 + float(i) + 0.5, floor(px.y) + 0.5) * stepUV);
        light[i] = dot(unpremultiply(samples[i]).rgb, vec3(0.2126, 0.7152, 0.0722));
    }
    int slot = clamp(int(floor(px.x - segment * 16.0)), 0, 15);
    // Stable rank selection preserves equal luminance ordering and premultiplied alpha.
    for (int i = 0; i < 16; ++i) {
        int rank = 0;
        for (int j = 0; j < 16; ++j)
            if (light[j] < light[i] || (light[j] == light[i] && j < i)) ++rank;
        if (rank == slot) return samples[i];
    }
    return samples[slot];
}

void main() {
    vec2 uvPerLayer = max(p.texel.xy, vec2(1e-9));
    vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 layerPx = inUv / uvPerLayer + p.texel.zw;
    float event = p.p1.z > 0.5 ? 0.0 : floor(p.p3.x / max(p.p1.x, 0.25));
    uint salt = uint(p.p1.y) * 2654435761u + uint(int(event)) * 40503u;
    float bandH = max(p.p0.x, 1.0);
    float bandIndex = p.p2.x > 0.5 ? floor(layerPx.x / bandH) : floor(layerPx.y / bandH);
    float r0 = aurea_hash(uvec2(uint(int(bandIndex)), salt));
    float r1 = aurea_hash(uvec2(uint(int(bandIndex)) + 7919u, salt));
    float r2 = aurea_hash(uvec2(uint(int(bandIndex)) + 31337u, salt));
    float shift = r0 < 0.45 ? (r1 * 2.0 - 1.0) * p.p0.y : 0.0;
    if (r2 < p.p0.z) shift += (r1 > 0.5 ? 1.0 : -1.0) * p.p0.y * 6.0;
    vec2 off = p.p2.x > 0.5 ? vec2(0.0, shift) : vec2(shift, 0.0);
    uvec2 block = uvec2(ivec2(floor(layerPx / vec2(max(p.p3.y, 4.0), bandH))));
    float blockRandom = aurea_hash(block ^ uvec2(salt, salt + 419u));
    if (blockRandom < 0.6) off += vec2(blockRandom * 2.0 - 1.0,
        aurea_hash(block ^ uvec2(salt + 919u, salt)) * 2.0 - 1.0) * p.p3.z;
    float tearBand = floor(layerPx.y / max(2.0, bandH * 0.18));
    float tearRandom = aurea_hash(uvec2(uint(int(tearBand)), salt + 3251u));
    if (tearRandom < 0.22) off.x += (tearRandom / 0.22 * 2.0 - 1.0) * p.p3.w;
    vec2 sourcePx = layerPx + off;
    if (p.p2.z > 1.0) sourcePx = (floor(sourcePx / p.p2.z) + 0.5) * p.p2.z;
    vec2 displaced = (sourcePx - p.texel.zw) * uvPerLayer;
    float sep = p.p0.w * (0.4 + r1 * 1.2);
    vec2 sepUv = (p.p2.x > 0.5 ? vec2(0.0, sep) : vec2(sep, 0.0)) * uvPerLayer;
    vec2 centered = displaced - vec2(0.5);
    vec4 base = sortedPixel(displaced, uvPerLayer, salt);
    vec4 red = p.p0.w > 0.0 || p.p2.w > 0.0 ? texture(u_tex0, vec2(0.5) + centered / (1.0 + p.p2.w) + sepUv) : base;
    vec4 blue = p.p0.w > 0.0 || p.p2.w > 0.0 ? texture(u_tex0, vec2(0.5) + centered / max(0.1, 1.0 - p.p2.w) - sepUv) : base;
    vec4 s = vec4(unpremultiply(red).r, unpremultiply(base).g, unpremultiply(blue).b, max(base.a, max(red.a, blue.a)));
    if (r2 > 1.0 - p.p2.y * 0.35)
        s.rgb = mix(s.rgb, s.gbr, aurea_hash(uvec2(salt, uint(int(bandIndex)))));
    float exposure = 1.0 + (aurea_hash(uvec2(salt, 1709u)) * 2.0 - 1.0) * p.color.x;
    s.rgb *= max(0.0, exposure);
    vec4 src = texture(u_tex0, inUv);
    o_color = mix(src, premultiply(vec4(max(s.rgb, vec3(0.0)), s.a)), clamp(p.p1.w, 0.0, 1.0));
}
