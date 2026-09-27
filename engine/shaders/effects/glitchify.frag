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
    vec4 transform; vec4 channel; vec4 options; vec4 image;
} p;

vec4 source(vec2 uv) {
    if (p.options.w > .5) uv = clamp(uv, vec2(0.5)/vec2(textureSize(u_tex0,0)),vec2(1)-vec2(0.5)/vec2(textureSize(u_tex0,0)));
    return texture(u_tex0,uv);
}
// Original bounded sort: reorder a 16-pixel row segment by straight luminance.
// Only enabled blocks pay the texture/sorting cost; seed and time select blocks.
vec4 sortedPixel(vec2 uv, vec2 stepUV, uint salt) {
    bool vertical = p.image.x > .5;
    vec2 px = (vertical ? uv.yx : uv) / (vertical ? stepUV.yx : stepUV);
    float segment = floor(px.x / 16.0);
    uint row = uint(int(floor(px.y)));
    if (p.color.y <= 0.0 || aurea_hash(uvec2(uint(int(segment)) ^ row, salt + 701u)) >= p.color.y)
        return source(uv);
    vec4 samples[16];
    float light[16];
    for (int i = 0; i < 16; ++i) {
        vec2 lookup = vec2(segment*16.0+float(i)+.5,floor(px.y)+.5);
        samples[i] = source((vertical ? lookup.yx : lookup)*stepUV);
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
    vec2 randomMove = vec2(aurea_hash(uvec2(salt,937u)),aurea_hash(uvec2(salt,1873u)))*2.0-1.0;
    vec2 transformed = (inUv-p.channel.zw)/max(.1,1.0+randomMove.x*p.transform.y)+p.channel.zw;
    transformed += randomMove*p.transform.x*uvPerLayer;
    layerPx = transformed/uvPerLayer+p.texel.zw;
    float bandH = max(p.p0.x, 1.0);
    float bandIndex = p.p2.x > 0.5 ? floor(layerPx.x / bandH) : floor(layerPx.y / bandH);
    float r0 = aurea_hash(uvec2(uint(int(bandIndex)), salt));
    float r1 = aurea_hash(uvec2(uint(int(bandIndex)) + 7919u, salt));
    float r2 = aurea_hash(uvec2(uint(int(bandIndex)) + 31337u, salt));
    float shift = r0 < p.transform.w*.9 ? (r1 * 2.0 - 1.0) * p.p0.y : 0.0;
    if (r2 < p.p0.z) shift += (r1 > 0.5 ? 1.0 : -1.0) * p.p0.y * 6.0;
    vec2 off = p.p2.x > 0.5 ? vec2(0.0, shift) : vec2(shift, 0.0);
    uvec2 block = uvec2(ivec2(floor(layerPx / vec2(max(p.p3.y, 4.0), bandH))));
    float blockRandom = aurea_hash(block ^ uvec2(salt, salt + 419u));
    if (blockRandom < p.transform.w*1.2) off += vec2(blockRandom * 2.0 - 1.0,
        aurea_hash(block ^ uvec2(salt + 919u, salt)) * 2.0 - 1.0) * p.p3.z;
    float tearBand = floor(layerPx.y / max(2.0, bandH * 0.18));
    float tearRandom = aurea_hash(uvec2(uint(int(tearBand)), salt + 3251u));
    if (tearRandom < p.transform.w*.44) off.x += (tearRandom / 0.22 * 2.0 - 1.0) * p.p3.w;
    vec2 sourcePx = layerPx + off;
    if (aurea_hash(uvec2(uint(int(bandIndex)),salt+227u)) < p.image.y) {
        if (p.image.x > .5) sourcePx.y = floor(sourcePx.y/128.0)*128.0 + r1*128.0;
        else sourcePx.x = floor(sourcePx.x/128.0)*128.0 + r1*128.0;
    }
    if (p.p2.z > 1.0) sourcePx = (floor(sourcePx / p.p2.z) + 0.5) * p.p2.z;
    vec2 displaced = (sourcePx - p.texel.zw) * uvPerLayer;
    float sep = p.p0.w * (0.4 + r1 * 1.2);
    vec2 sepUv = (p.p2.x > 0.5 ? vec2(0.0, sep) : vec2(sep, 0.0)) * uvPerLayer;
    sepUv.y += p.channel.x*uvPerLayer.y*(r2*2.0-1.0);
    vec2 centered = displaced - p.channel.zw;
    vec2 scale = vec2(p.p2.w,p.p2.w+p.channel.y);
    vec4 base = sortedPixel(displaced, uvPerLayer, salt);
    vec4 red = p.p0.w > 0.0 || p.p2.w > 0.0 || p.channel.x > 0.0 || p.channel.y > 0.0 ? source(p.channel.zw + centered / (vec2(1.0) + scale) + sepUv) : base;
    vec4 blue = p.p0.w > 0.0 || p.p2.w > 0.0 || p.channel.x > 0.0 || p.channel.y > 0.0 ? source(p.channel.zw + centered / max(vec2(0.1), vec2(1.0) - scale) - sepUv) : base;
    vec4 s = vec4(unpremultiply(red).r, unpremultiply(base).g, unpremultiply(blue).b, max(base.a, max(red.a, blue.a)));
    if (r2 > 1.0 - p.p2.y * 0.35)
        s.rgb = mix(s.rgb, s.gbr, aurea_hash(uvec2(salt, uint(int(bandIndex)))));
    float exposure = 1.0 + (aurea_hash(uvec2(salt, 1709u)) * 2.0 - 1.0) * p.color.x;
    s.rgb *= max(0.0, exposure);
    s.rgb += (aurea_hash(uvec2(ivec2(floor(layerPx)))^uvec2(salt))-0.5)*p.options.z/16.0;
    float crop = abs(randomMove.y)*p.transform.z*.5;
    float visible = step(crop,inUv.x)*step(crop,inUv.y)*step(inUv.x,1.0-crop)*step(inUv.y,1.0-crop);
    if (p.options.x >= 1.0) visible = 0;
    else if (p.options.x > 0.0) {
        float soft = max(.001,p.options.y*.5);
        visible *= smoothstep(p.options.x-soft,p.options.x+soft,r0);
    }
    s.a *= visible;
    vec4 src = source(inUv);
    o_color = mix(src, premultiply(vec4(max(s.rgb, vec3(0.0)), s.a)), clamp(p.p1.w, 0.0, 1.0));
}
