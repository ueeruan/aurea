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
    // 0.4.5 — controles do Glitchify original (build/reference/glitchify/SPEC.md).
    // Todos com padrão neutro: um projeto antigo desenha igual.
    vec4 trans;   // x canal da transição (0 = faixas), y velocidade da transformação (×), z compor sobre o original, w passos do tremor
    vec4 split;   // x modo da separação (0 = RGB), y velocidade da separação, z canal da escala (0 = uniforme)
    vec4 corrupt; // x recuo em bytes, y força (0..1), z opacidade, w largura da camada (px)
    vec4 slice;   // x altura (0..1), y posição (0..1), z qualidade (0..1), w deslocamento aleatório (0..1)
    vec4 block;   // x quantidade (0..1), y velocidade, z largura (px), w altura (px)
    vec4 extra;   // x direção dos blocos, y compressão (0..1), z pontilhado (0..1), w mostrar o ruído
    vec4 noise;   // x oitavas, y ganho, z lacunaridade, w tempo do ruído
    vec4 layer;   // x largura da camada (px), y altura (px)
} p;

// --- Motor de ruído (0.4.5) ---------------------------------------------------
// fBm de ruído de valor 3D (x, y em alturas da camada; z = tempo). Própria: só
// os controles (oitavas, ganho, lacunaridade) vêm da referência.
float gfy_hash3(ivec3 c, uint seed) {
    return aurea_hash(uvec2(uint(c.x) * 73856093u ^ uint(c.z) * 83492791u ^ seed,
                            uint(c.y) * 19349663u + seed * 2654435761u));
}
float gfy_vnoise3(vec3 q, uint seed) {
    ivec3 i = ivec3(floor(q));
    vec3 f = fract(q);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float a = mix(gfy_hash3(i, seed), gfy_hash3(i + ivec3(1, 0, 0), seed), u.x);
    float b = mix(gfy_hash3(i + ivec3(0, 1, 0), seed), gfy_hash3(i + ivec3(1, 1, 0), seed), u.x);
    float c = mix(gfy_hash3(i + ivec3(0, 0, 1), seed), gfy_hash3(i + ivec3(1, 0, 1), seed), u.x);
    float d = mix(gfy_hash3(i + ivec3(0, 1, 1), seed), gfy_hash3(i + ivec3(1, 1, 1), seed), u.x);
    return mix(mix(a, b, u.y), mix(c, d, u.y), u.z);
}
float gfy_noise(vec3 q) {
    uint seed = uint(p.p1.y) * 747796405u + 2891336453u;
    int octaves = clamp(int(p.noise.x + 0.5), 1, 16);
    float lac = clamp(p.noise.z, 0.01, 30.0), gain = clamp(p.noise.y, 0.0, 5.0);
    float sum = 0.0, norm = 0.0, amp = 1.0;
    for (int k = 0; k < 16; ++k) {
        if (k >= octaves) break;
        sum += amp * gfy_vnoise3(q, seed + uint(k) * 1013u);
        norm += amp;
        q *= lac;
        amp *= gain;
    }
    // Contraste: a soma de oitavas se concentra em 0,5; o mapa usa a faixa toda.
    float v = norm > 0.0 ? sum / norm : 0.5;
    return clamp((v - 0.5) * 2.2 + 0.5, 0.0, 1.0);
}
// O "mapa" 0..1 com os extremos comprimidos (o que "Mostrar o ruído" pinta).
float gfy_noise_map(vec2 layerPx, float t) {
    float v = gfy_noise(vec3(layerPx / max(p.layer.y, 1.0), t));
    if (v > 0.9) v = v * 0.2 + 0.72;
    if (v < 0.1) v = v * 0.2 + 0.08;
    return clamp(v, 0.0, 1.0);
}
// Tremor −1..1 por "sal", quantizado em `steps` níveis (0 = contínuo).
float gfy_wiggle(uint salt, float t, float steps) {
    float s = 0.0;
    for (int k = 0; k < 4; ++k)
        s += gfy_noise(vec3(float(salt % 977u) + float(k) * 31.7, float(salt / 977u) * 1.37 + float(k), t));
    float w = clamp((s - 2.0) / 1.2, -1.0, 1.0);
    return steps > 0.5 ? trunc(w * steps) / steps : w;
}
vec3 gfy_rgb2hsv(vec3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), d = mx - mn;
    float h = 0.0;
    if (d > 1e-6) {
        if (mx == c.r)      h = fract((c.g - c.b) / d / 6.0);
        else if (mx == c.g) h = ((c.b - c.r) / d + 2.0) / 6.0;
        else                h = ((c.r - c.g) / d + 4.0) / 6.0;
    }
    return vec3(h, mx > 1e-6 ? d / mx : 0.0, mx);
}
vec3 gfy_hsv2rgb(vec3 c) {
    vec3 k = abs(fract(vec3(c.x) + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0;
    return c.z * mix(vec3(1.0), clamp(k, 0.0, 1.0), c.y);
}
vec3 gfy_rgb2ycc(vec3 c) {
    float y = dot(c, vec3(0.299, 0.587, 0.114));
    return vec3(y, (c.b - y) * 0.564, (c.r - y) * 0.713);
}
vec3 gfy_ycc2rgb(vec3 c) {
    float r = c.x + c.z / 0.713, b = c.x + c.y / 0.564;
    return vec3(r, (c.x - 0.299 * r - 0.114 * b) / 0.587, b);
}
// Valor 0..1 de um canal de uma cor (alfa reto): 1 R, 2 G, 3 B, 4 A, 5 luma,
// 6 claridade, 7 matiz, 8 saturação (as listas da transição e da escala).
float gfy_channel_key(vec4 c, int mode) {
    vec3 s = clamp(aurea_linear_to_srgb(c.rgb), 0.0, 1.0);
    if (mode == 1) return s.r;
    if (mode == 2) return s.g;
    if (mode == 3) return s.b;
    if (mode == 4) return c.a;
    if (mode == 5) return dot(s, vec3(0.2126, 0.7152, 0.0722));
    if (mode == 6) return 0.5 * (max(s.r, max(s.g, s.b)) + min(s.r, min(s.g, s.b)));
    vec3 hsv = gfy_rgb2hsv(s);
    return mode == 7 ? hsv.x : hsv.y;
}

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

// Média aproximada do bloco 8×8 (px da camada) em volta de `px`, lida a partir
// de `uv` (onde o pixel do glitch foi lido): 4 amostras bilineares = 16 texels.
vec4 gfy_block_mean(vec2 layerPx, vec2 uv, vec2 uvPerLayer) {
    vec2 inBlock = layerPx - floor(layerPx / 8.0) * 8.0;
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 4; ++k) {
        vec2 tap = vec2(k % 2 == 0 ? 2.0 : 6.0, k < 2 ? 2.0 : 6.0);
        sum += source(uv + (tap - inBlock) * uvPerLayer);
    }
    return sum * 0.25;
}

void main() {
    vec2 uvPerLayer = max(p.texel.xy, vec2(1e-9));
    vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 layerPx = inUv / uvPerLayer + p.texel.zw;
    float event = p.p1.z > 0.5 ? 0.0 : floor(p.p3.x / max(p.p1.x, 0.25));
    uint salt = uint(p.p1.y) * 2654435761u + uint(int(event)) * 40503u;
    float noiseT = p.p1.z > 0.5 ? 0.0 : p.noise.w;
    // "Mostrar o ruído": o mapa do motor de ruído, em cinza (para ajustar).
    if (p.extra.w > 0.5) {
        float n = gfy_noise_map(layerPx, noiseT);
        o_color = vec4(vec3(aurea_srgb_to_linear(n)), 1.0);
        return;
    }
    // Transformação: o tremor tem velocidade própria (1 = a do glitch).
    float transformEvent = p.p1.z > 0.5 ? 0.0 : floor(p.p3.x * max(p.trans.y, 0.0) / max(p.p1.x, 0.25));
    uint transformSalt = uint(p.p1.y) * 2654435761u + uint(int(transformEvent)) * 40503u;
    vec2 randomMove = vec2(aurea_hash(uvec2(transformSalt,937u)),aurea_hash(uvec2(transformSalt,1873u)))*2.0-1.0;
    if (p.trans.w > 0.5) randomMove = trunc(randomMove * p.trans.w) / p.trans.w;
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
    // Blocos copiados (Glitch Block): o ruído decide quais blocos trocam e de
    // qual vizinho (−3..+2 blocos, com volta na borda da camada) vêm.
    if (p.block.x > 0.0) {
        vec2 bs = max(p.block.zw, vec2(4.0));
        vec2 extent = max(p.layer.xy, bs);
        vec2 n = ceil(extent / bs);
        vec2 b = floor(layerPx / bs);
        float v = gfy_noise_map(b * bs * 5.0, noiseT * p.block.y / 25.0);
        if (v > 1.0 - p.block.x * p.block.x) {
            float k = floor(v * 20.0);
            vec2 shift = vec2(p.extra.x < 0.5 || p.extra.x > 1.5 ? mod(k, 6.0) - 3.0 : 0.0,
                              p.extra.x > 0.5 ? mod(k + 4.0, 6.0) - 3.0 : 0.0);
            vec2 from = mod(b + shift, n);
            sourcePx += (from - b) * bs;
        }
    }
    // Fatia comprimida (Glitch Slice): uma faixa da largura toda, numa altura
    // que treme; a partir de um ponto sorteado o "decodificador" perde a
    // sincronia e o resto da faixa escorrega de lado.
    bool inSlice = false, sliceDesync = false;
    float sliceDamage = 0.0;
    if (p.slice.x > 0.0) {
        float H = max(p.layer.y, 1.0), W = max(p.layer.x, 1.0);
        float a = clamp(p.slice.x, 0.0, 1.0);
        float top = p.slice.y * (H - 1.0) + gfy_wiggle(4409u, noiseT * 4.0, 8.0) * (1.0 - a * a) * p.slice.w * H;
        top = clamp(top, 0.0, H - 1.0);
        float bottom = min(top + max(a * H, 1.0), H);
        if (layerPx.y >= top && layerPx.y < bottom) {
            inSlice = true;
            sliceDamage = 1.0 - clamp(p.slice.z, 0.0, 1.0);
            float stripPixels = max((bottom - top) * W, 1.0);
            float at = ((floor(layerPx.y) - floor(top)) * W + floor(layerPx.x)) / stripPixels;
            float cut = aurea_hash(uvec2(salt, 6151u));
            if (at > cut) {
                sliceDesync = true;
                float dx = (aurea_hash(uvec2(salt, 6173u)) * 2.0 - 1.0) * W * (0.08 + 0.3 * sliceDamage);
                sourcePx.x = mod(sourcePx.x + dx, W);
                sourcePx.y = floor(sourcePx.y / 8.0) * 8.0 + 4.0;
            }
        }
    }
    if (aurea_hash(uvec2(uint(int(bandIndex)),salt+227u)) < p.image.y) {
        if (p.image.x > .5) sourcePx.y = floor(sourcePx.y/128.0)*128.0 + r1*128.0;
        else sourcePx.x = floor(sourcePx.x/128.0)*128.0 + r1*128.0;
    }
    if (p.p2.z > 1.0) sourcePx = (floor(sourcePx / p.p2.z) + 0.5) * p.p2.z;
    vec2 displaced = (sourcePx - p.texel.zw) * uvPerLayer;
    float sep = p.p0.w * (0.4 + r1 * 1.2);
    vec2 sepUv = (p.p2.x > 0.5 ? vec2(0.0, sep) : vec2(sep, 0.0)) * uvPerLayer;
    sepUv.y += p.channel.x*uvPerLayer.y*(r2*2.0-1.0);
    // Escala por canal (Channel Scale): cada pixel anda em direção ao centro
    // na proporção do valor do canal escolhido (o mais claro anda mais).
    // Inversa do mapeamento x' = c + (x − c)(1 − s·k), resolvida em 2 passos.
    int scaleKey = int(p.split.z + 0.5);
    if (scaleKey > 0 && (p.p2.w > 0.0 || p.channel.y > 0.0)) {
        vec2 amount = vec2(p.p2.w, p.p2.w + p.channel.y);
        vec2 q = displaced;
        for (int it = 0; it < 2; ++it) {
            int mode = scaleKey <= 4 ? scaleKey + 4 : scaleKey - 4;   // Luma, Claridade, Matiz, Saturação, R, G, B, A
            float k = gfy_channel_key(unpremultiply(source(q)), mode);
            q = p.channel.zw + (displaced - p.channel.zw) / max(vec2(0.05), vec2(1.0) - amount * k);
        }
        displaced = q;
    }
    vec2 centered = displaced - p.channel.zw;
    vec2 scale = scaleKey > 0 ? vec2(0.0) : vec2(p.p2.w,p.p2.w+p.channel.y);
    vec4 base = sortedPixel(displaced, uvPerLayer, salt);
    int splitMode = int(p.split.x + 0.5);
    bool legacySplit = splitMode == 0 && (p.p0.w > 0.0 || scale.x > 0.0 || p.channel.x > 0.0 || scale.y > 0.0);
    vec4 red = legacySplit ? source(p.channel.zw + centered / (vec2(1.0) + scale) + sepUv) : base;
    vec4 blue = legacySplit ? source(p.channel.zw + centered / max(vec2(0.1), vec2(1.0) - scale) - sepUv) : base;
    vec4 s = vec4(unpremultiply(red).r, unpremultiply(base).g, unpremultiply(blue).b, max(base.a, max(red.a, blue.a)));
    // Separação de UM componente (Channel Split): R/G/B, Y′/Cb/Cr ou H/S/V vem
    // de (x + dx, y + dy); o resto fica. Com velocidade, o ruído faz o
    // deslocamento oscilar entre 0 e 2× no tempo.
    if (splitMode > 0 && (p.p0.w != 0.0 || p.channel.x != 0.0)) {
        vec2 d = vec2(p.p0.w, p.channel.x);
        if (p.split.y > 0.0) {
            float e = 1.0 - exp(-0.4 * p.split.y);
            d *= vec2(1.0) - e * vec2(gfy_noise(vec3(1.234, 5.678, noiseT * 0.4 * p.split.y)) * 2.0 - 1.0,
                                      gfy_noise(vec3(5.678, 1.234, noiseT * 0.4 * p.split.y)) * 2.0 - 1.0);
        }
        vec4 shifted = unpremultiply(source(displaced + d * uvPerLayer));
        vec4 b0 = unpremultiply(base);
        if (splitMode <= 3) {
            s = b0;
            s[splitMode - 1] = shifted[splitMode - 1];
            s.a = max(b0.a, shifted.a);
        } else {
            vec3 bs = clamp(aurea_linear_to_srgb(b0.rgb), 0.0, 1.0);
            vec3 ss = clamp(aurea_linear_to_srgb(shifted.rgb), 0.0, 1.0);
            vec3 mixed;
            if (splitMode <= 6) {
                vec3 by = gfy_rgb2ycc(bs);
                by[splitMode - 4] = gfy_rgb2ycc(ss)[splitMode - 4];
                mixed = gfy_ycc2rgb(by);
            } else {
                vec3 bh = gfy_rgb2hsv(bs);
                bh[splitMode - 7] = gfy_rgb2hsv(ss)[splitMode - 7];
                mixed = gfy_hsv2rgb(bh);
            }
            s = vec4(aurea_srgb_to_linear(clamp(mixed, 0.0, 1.0)), b0.a);
        }
    }
    // Fatia: blocos 8×8 achatados pela qualidade e, depois do ponto da perda de
    // sincronia, a cor de cada bloco 16×16 desviada.
    if (inSlice && sliceDamage > 0.0) {
        vec4 mean = unpremultiply(gfy_block_mean(layerPx, displaced, uvPerLayer));
        s.rgb = mix(s.rgb, mean.rgb, sliceDamage * (sliceDesync ? 1.0 : 0.6));
        if (sliceDesync) {
            uvec2 cell = uvec2(ivec2(floor(layerPx / 16.0)));
            vec3 tint = vec3(aurea_hash(cell ^ uvec2(salt, 71u)), aurea_hash(cell ^ uvec2(salt, 113u)),
                             aurea_hash(cell ^ uvec2(salt, 197u))) - 0.5;
            vec3 srgb = clamp(aurea_linear_to_srgb(s.rgb) + tint * 0.5 * sliceDamage, 0.0, 1.0);
            s.rgb = aurea_srgb_to_linear(srgb);
        }
    }
    if (r2 > 1.0 - p.p2.y * 0.35)
        s.rgb = mix(s.rgb, s.gbr, aurea_hash(uvec2(salt, uint(int(bandIndex)))));
    float exposure = 1.0 + (aurea_hash(uvec2(salt, 1709u)) * 2.0 - 1.0) * p.color.x;
    s.rgb *= max(0.0, exposure);
    s.rgb += (aurea_hash(uvec2(ivec2(floor(layerPx)))^uvec2(salt))-0.5)*p.options.z/16.0;
    // Corrupção de cor (Color Glitch): o quadro como fluxo de bytes sRGB; cada
    // byte soma o byte `recuo` posições antes (lido como com sinal) e dá a
    // volta em 8 bits — as faixas de cor "estouradas". Um toque só (a
    // recorrência do original não paraleliza).
    if (p.corrupt.y > 0.0 && p.corrupt.z > 0.0) {
        int W = max(int(p.corrupt.w + 0.5), 1);
        ivec2 lp = ivec2(floor(layerPx));
        int back = int(p.corrupt.x);
        int idx = lp.y * W + lp.x - back / 4;
        vec2 from = vec2(mod(float(idx), float(W)), floor(float(idx) / float(W))) + 0.5;
        vec4 prev = unpremultiply(source((from - p.texel.zw) * uvPerLayer));
        vec3 pb = floor(clamp(aurea_linear_to_srgb(prev.rgb), 0.0, 1.0) * 255.0 + 0.5);
        int rot = back % 4;
        if (rot == 1) pb = pb.brg; else if (rot == 2) pb = pb.gbr; else if (rot == 3) pb = pb.bgr;
        pb -= 256.0 * step(128.0, pb);
        vec3 cb = floor(clamp(aurea_linear_to_srgb(max(s.rgb, vec3(0.0))), 0.0, 1.0) * 255.0 + 0.5);
        float k = p.corrupt.y * (0.8 + 0.2 * gfy_noise(vec3(50.0 / max(p.layer.y, 1.0), 0.0, noiseT)));
        vec3 nb = mod(cb + floor(pb * k), 256.0);
        s.rgb = mix(s.rgb, aurea_srgb_to_linear(nb / 255.0), clamp(p.corrupt.z, 0.0, 1.0));
    }
    // Compressão (JPEG): luma quantizada em volta da média do bloco 8×8 e a cor
    // do bloco inteiro (subamostragem de croma).
    if (p.extra.y > 0.0) {
        float c = clamp(p.extra.y, 0.0, 1.0);
        vec3 mean = clamp(aurea_linear_to_srgb(unpremultiply(gfy_block_mean(layerPx, displaced, uvPerLayer)).rgb), 0.0, 1.0);
        vec3 cur = gfy_rgb2ycc(clamp(aurea_linear_to_srgb(max(s.rgb, vec3(0.0))), 0.0, 1.0));
        vec3 blk = gfy_rgb2ycc(mean);
        float qs = mix(0.004, 0.45, c * c);
        cur.x = blk.x + floor((cur.x - blk.x) / qs + 0.5) * qs;
        cur.yz = mix(cur.yz, floor(blk.yz / (qs * 0.5) + 0.5) * qs * 0.5, c);
        s.rgb = aurea_srgb_to_linear(clamp(gfy_ycc2rgb(cur), 0.0, 1.0));
    }
    // Pontilhado da paleta: menos cores, com matriz de Bayer 4×4.
    if (p.extra.z > 0.0) {
        const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        ivec2 bp = ivec2(floor(layerPx)) & ivec2(3);
        float threshold = (bayer[bp.y * 4 + bp.x] + 0.5) / 16.0;
        float levels = mix(32.0, 2.0, clamp(p.extra.z, 0.0, 1.0));
        vec3 g = clamp(aurea_linear_to_srgb(max(s.rgb, vec3(0.0))), 0.0, 1.0);
        g = floor(g * (levels - 1.0) + threshold) / (levels - 1.0);
        s.rgb = aurea_srgb_to_linear(clamp(g, 0.0, 1.0));
    }
    float crop = abs(randomMove.y)*p.transform.z*.5;
    float visible = step(crop,inUv.x)*step(crop,inUv.y)*step(inUv.x,1.0-crop)*step(inUv.y,1.0-crop);
    vec4 src = source(inUv);
    int transitionChannel = int(p.trans.x + 0.5);
    if (p.options.x >= 1.0) visible = 0;
    else if (p.options.x > 0.0) {
        if (transitionChannel == 0) {
            float soft = max(.001,p.options.y*.5);
            visible *= smoothstep(p.options.x-soft,p.options.x+soft,r0);
        } else {
            // Pelo canal da camada (ou pelo ruído): os valores baixos somem
            // primeiro, numa rampa da largura da suavidade.
            float v = transitionChannel == 9 ? gfy_noise_map(layerPx, noiseT)
                                              : gfy_channel_key(unpremultiply(src), transitionChannel);
            float soft = max(0.001, p.options.y);
            visible *= clamp(1.0 + (v - p.options.x * (1.0 + soft)) / soft, 0.0, 1.0);
        }
    }
    s.a *= visible;
    vec4 glitched = premultiply(vec4(max(s.rgb, vec3(0.0)), s.a));
    // "Compor sobre o original": o que o glitch esvaziou mostra a camada.
    if (p.trans.z > 0.5) glitched += src * (1.0 - glitched.a);
    o_color = mix(src, glitched, clamp(p.p1.w, 0.0, 1.0));
}
