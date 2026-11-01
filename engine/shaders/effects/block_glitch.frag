#version 450
// =============================================================================
//  Aurea / shaders / effects / block_glitch.frag  (0.4.5, pedido de usuário)
//
//  Block Glitch no estilo do DaVinci Resolve: a imagem é uma grade de blocos
//  retangulares; a cada evento, uma parte deles (quantidade) é DESLOCADA e
//  pode ter os canais trocados e separados. Alguns blocos se partem em metades
//  ou quartos (variação de tamanho), o que quebra a grade regular.
//
//  O sorteio é hash de (bloco, evento, semente); o evento muda `frequência`
//  vezes por segundo — igual no preview e no export, em qualquer qps.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;   // xy = uv por px da camada, zw = origem da região (px da camada)
    vec4 p0;      // x quantidade (0..1), y largura do bloco (px), z altura (px), w variação de tamanho (0..1)
    vec4 p1;      // x deslocamento (px), y troca de canais (0..1), z separação RGB (px), w direção (0 H, 1 V, 2 ambas)
    vec4 p2;      // x semente, y evento, z mistura
    vec4 p3;
    vec4 color;
} p;

vec4 src_at(vec2 layerPx) {
    return texture(u_tex0, (layerPx - p.texel.zw) * p.texel.xy);
}

float rnd(uvec2 cell, uint salt) { return aurea_hash(cell ^ uvec2(salt, salt * 2654435761u + 911u)); }

void main() {
    vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 px = inUv / max(p.texel.xy, vec2(1e-9)) + p.texel.zw;
    vec4 original = texture(u_tex0, inUv);
    float amount = clamp(p.p0.x, 0.0, 1.0);
    if (amount <= 0.0) { o_color = original; return; }
    uint salt = uint(p.p2.x) * 2654435761u + uint(int(p.p2.y)) * 40503u + 5u;

    // A célula: o bloco da grade, partido em 2 ou 4 se a variação mandar.
    vec2 size = max(p.p0.yz, vec2(2.0));
    vec2 cell = floor(px / size);
    uvec2 id = uvec2(ivec2(cell));
    float split = rnd(id, salt + 3u);
    if (split < p.p0.w * 0.6) {
        vec2 sub = split < p.p0.w * 0.3 ? vec2(2.0, 2.0) : vec2(2.0, 1.0);
        vec2 subCell = floor(px / (size / sub));
        id = uvec2(ivec2(subCell)) * 3u + uvec2(1u, 2u);
    }

    vec4 s = unpremultiply(original);
    if (rnd(id, salt + 13u) < amount) {
        // Deslocamento do bloco inteiro (na direção pedida).
        float a = rnd(id, salt + 17u) * 2.0 - 1.0, b = rnd(id, salt + 19u) * 2.0 - 1.0;
        vec2 dir = p.p1.w < 0.5 ? vec2(1.0, 0.0) : p.p1.w < 1.5 ? vec2(0.0, 1.0) : vec2(1.0, 1.0);
        vec2 move = vec2(a, p.p1.w > 1.5 ? b : a) * dir * p.p1.x;
        vec2 from = px - move;
        vec4 c = unpremultiply(src_at(from));
        // Separação RGB dentro do bloco.
        if (p.p1.z > 0.0) {
            float k = p.p1.z * (0.5 + rnd(id, salt + 23u));
            vec4 r = unpremultiply(src_at(from + vec2(k, 0.0)));
            vec4 bl = unpremultiply(src_at(from - vec2(k, 0.0)));
            c = vec4(r.r, c.g, bl.b, max(c.a, max(r.a, bl.a)));
        }
        // Troca de canais: uma das 5 permutações não triviais.
        if (rnd(id, salt + 29u) < p.p1.y) {
            int perm = int(floor(rnd(id, salt + 31u) * 5.0));
            if (perm == 0) c.rgb = c.gbr;
            else if (perm == 1) c.rgb = c.brg;
            else if (perm == 2) c.rgb = c.rbg;
            else if (perm == 3) c.rgb = c.bgr;
            else c.rgb = c.grb;
        }
        s = c;
    }
    vec4 glitched = premultiply(vec4(max(s.rgb, vec3(0.0)), clamp(s.a, 0.0, 1.0)));
    o_color = mix(original, glitched, clamp(p.p2.z, 0.0, 1.0));
}
