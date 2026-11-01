#version 450
// =============================================================================
//  Aurea / shaders / effects / hyper_glitch.frag  (0.4.5, pedido de usuário)
//
//  Hyper Glitch: o glitch "pesado" — tudo de uma vez, guiado por UMA
//  intensidade. Fatias horizontais que escorregam, blocos lidos de outro
//  lugar, separação RGB, rastros de pixels ordenados pela luz, compressão em
//  blocos e canais trocados.
//
//  Tudo é sorteado por hash de (célula, evento, semente): o evento muda
//  `velocidade` vezes por segundo (em segundos, não em quadros — o mesmo
//  resultado a 24, 30 ou 60 qps) e para quando o tempo para.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;   // xy = uv por px da camada, zw = origem da região (px da camada)
    vec4 p0;      // x intensidade (0..1), y semente, z evento, w mistura
    vec4 p1;      // x tamanho do bloco (px), y blocos (0..1), z separação RGB (px), w fatias (0..1)
    vec4 p2;      // x altura da fatia (px), y rastro (0..1), z compressão (0..1), w troca de cor (0..1)
    vec4 p3;      // x largura da camada (px), y altura (px)
    vec4 color;
} p;

vec4 src_at(vec2 layerPx) {
    return texture(u_tex0, (layerPx - p.texel.zw) * p.texel.xy);
}

float rnd(uvec2 cell, uint salt) { return aurea_hash(cell ^ uvec2(salt, salt * 2654435761u + 977u)); }

void main() {
    vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 px = inUv / max(p.texel.xy, vec2(1e-9)) + p.texel.zw;
    vec4 original = texture(u_tex0, inUv);
    float I = clamp(p.p0.x, 0.0, 1.0);
    if (I <= 0.0) { o_color = original; return; }
    uint salt = uint(p.p0.y) * 2654435761u + uint(int(p.p0.z)) * 40503u + 17u;
    float W = max(p.p3.x, 1.0), H = max(p.p3.y, 1.0);
    vec2 from = px;

    // 1. Fatias: faixas finas que escorregam de lado, algumas muito.
    float sliceH = max(p.p2.x, 1.0);
    uvec2 band = uvec2(uint(int(floor(px.y / sliceH))) , 3u);
    if (rnd(band, salt + 11u) < p.p1.w * I * 0.7) {
        float r = rnd(band, salt + 23u) * 2.0 - 1.0;
        float big = rnd(band, salt + 31u) < 0.25 ? 3.0 : 1.0;
        from.x += r * W * 0.12 * big * I;
    }

    // 2. Blocos: retângulos (às vezes partidos ao meio) lidos de outro lugar.
    vec2 bs = vec2(max(p.p1.x, 4.0)) * vec2(1.0 + floor(rnd(uvec2(7u, 9u), salt) * 3.0), 1.0);
    uvec2 block = uvec2(ivec2(floor(px / bs)));
    if (rnd(block, salt + 41u) < 0.5) {
        // Bloco partido: metade da altura, cada metade com sorte própria.
        block = uvec2(ivec2(floor(px / vec2(bs.x, bs.y * 0.5)))) + uvec2(0u, 7919u);
    }
    bool hit = rnd(block, salt + 53u) < p.p1.y * I * 0.6;
    if (hit) {
        vec2 jump = vec2(rnd(block, salt + 61u) * 2.0 - 1.0, (rnd(block, salt + 67u) * 2.0 - 1.0) * 0.35);
        from += floor(jump * bs * (1.0 + 3.0 * I));
    }

    // 3. Rastro: nos blocos atingidos, o pixel vira o mais claro de uma
    //    sequência à esquerda — o "pixel sort" esticado do glitch.
    vec4 c = src_at(from);
    float streak = p.p2.y * I;
    if (streak > 0.0 && (hit || rnd(band, salt + 71u) < streak * 0.3)) {
        float reach = 4.0 + streak * 60.0;
        float best = aurea_luma(unpremultiply(c).rgb);
        for (int k = 1; k <= 8; ++k) {
            vec4 t = src_at(from - vec2(reach * float(k) / 8.0, 0.0));
            float l = aurea_luma(unpremultiply(t).rgb);
            if (l > best) { best = l; c = t; }
        }
    }

    // 4. Separação RGB: vermelho e azul saem do lugar, mais nos blocos.
    float split = p.p1.z * I * (hit ? 1.6 : 0.6) * (0.5 + rnd(band, salt + 83u));
    vec4 s = unpremultiply(c);
    if (split > 0.0) {
        vec4 r = unpremultiply(src_at(from + vec2(split, 0.0)));
        vec4 b = unpremultiply(src_at(from - vec2(split, 0.0)));
        s = vec4(r.r, s.g, b.b, max(s.a, max(r.a, b.a)));
    }

    // 5. Compressão: blocos 8×8 achatados e cores em degraus.
    float comp = p.p2.z * I;
    if (comp > 0.0) {
        vec2 cell = floor(from / 8.0) * 8.0;
        vec4 mean = 0.25 * (src_at(cell + vec2(2.0, 2.0)) + src_at(cell + vec2(6.0, 2.0)) +
                            src_at(cell + vec2(2.0, 6.0)) + src_at(cell + vec2(6.0, 6.0)));
        vec3 g = clamp(aurea_linear_to_srgb(mix(s.rgb, unpremultiply(mean).rgb, comp * 0.8)), 0.0, 1.0);
        float levels = mix(48.0, 5.0, comp);
        g = floor(g * levels + 0.5) / levels;
        s.rgb = aurea_srgb_to_linear(g);
    }

    // 6. Troca de cor: canais rodados ou invertidos em blocos sorteados.
    float swapChance = p.p2.w * I;
    if (swapChance > 0.0 && rnd(block, salt + 97u) < swapChance * 0.5) {
        float pick = rnd(block, salt + 101u);
        if (pick < 0.4) s.rgb = s.gbr;
        else if (pick < 0.8) s.rgb = s.brg;
        else s.rgb = aurea_srgb_to_linear(1.0 - clamp(aurea_linear_to_srgb(s.rgb), 0.0, 1.0));
    }

    vec4 glitched = premultiply(vec4(max(s.rgb, vec3(0.0)), clamp(s.a, 0.0, 1.0)));
    o_color = mix(original, glitched, clamp(p.p0.w, 0.0, 1.0));
}
