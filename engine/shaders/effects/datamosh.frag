#version 450
// =============================================================================
//  Aurea / shaders / effects / datamosh.frag
//
//  DATAMOSH, passe 2 — reconstrução SEM RESÍDUO. Cada pixel segue o campo de
//  vetores por bloco (tex2, amostrado no centro do bloco) em 6 passos — o
//  P-frame repetido — e pega a cor da REFERÊNCIA (tex1) no fim do caminho.
//  Onde o campo é uniforme isso é só um deslocamento; nas bordas entre blocos
//  que andam diferente a imagem estica e derrete, que é o visual do datamosh.
//
//  Depois:
//    - corrupção: blocos sorteados (bloco, semente, par de quadros) viram cor
//      chapada (o DC do bloco), um bloco de outro lugar, ou canais trocados e
//      posterizados;
//    - sangria de cor: a crominância (YCbCr em sRGB) vem de mais adiante no
//      caminho do vetor que a luminância;
//    - intensidade: mistura com a camada original (tex0).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_now;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_ref;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_vectors;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;   // uv da entrada → uv da referência: * xy + zw
    vec4 grid;    // xy blocos, zw tamanho do bloco em uv da entrada
    vec4 a;       // x intensidade, y arraste, z corrupção, w sangria de cor
    vec4 b;       // x semente, y época da corrupção
} p;

vec2 mosh_vector(vec2 uv) {
    const vec2 blk = clamp(floor(uv * p.grid.xy), vec2(0.0), p.grid.xy - 1.0);
    return texture(u_vectors, (blk + 0.5) / p.grid.xy).xy;
}

vec4 mosh_ref(vec2 uv) {
    return unpremultiply(texture(u_ref, uv * p.uvMap.xy + p.uvMap.zw));
}

vec3 mosh_to_ycc(vec3 s) {
    const float y = dot(s, vec3(0.299, 0.587, 0.114));
    return vec3(y, (s.b - y) * 0.564, (s.r - y) * 0.713);
}
vec3 mosh_from_ycc(vec3 c) {
    const float r = c.x + 1.403 * c.z;
    const float b = c.x + 1.773 * c.y;
    const float g = (c.x - 0.299 * r - 0.114 * b) / 0.587;
    return vec3(r, g, b);
}

void main() {
    const vec4 nowP = texture(u_now, v_uv);
    const float drag = p.a.y;

    // O caminho pelo campo de vetores (6 passos).
    vec2 pos = v_uv;
    for (int k = 0; k < 6; ++k) pos += mosh_vector(pos) * (drag / 6.0);

    // Corrupção por bloco.
    const vec2 blk = floor(v_uv * p.grid.xy);
    const uint seed = uint(p.b.x);
    const uvec2 q = uvec2(ivec2(blk) + ivec2(4096)) ^ uvec2(seed * 2654435761u, uint(p.b.y) * 1597334677u + seed);
    const float hit = aurea_hash(q);
    int kind = -1;
    if (hit < p.a.z * 0.35) kind = int(aurea_hash(q.yx + uvec2(11u, 3u)) * 3.0);
    if (kind == 1) {
        // Bloco de outro lugar (±8 blocos), alinhado à grade.
        const vec2 jump = floor((vec2(aurea_hash(q + uvec2(7u, 0u)), aurea_hash(q + uvec2(0u, 13u))) - 0.5) * 16.0);
        pos += jump * p.grid.zw;
    } else if (kind == 0) {
        // Só o DC: a cor do centro do bloco, chapada.
        pos = (floor(pos * p.grid.xy) + 0.5) / p.grid.xy;
    }

    const vec4 ref = mosh_ref(pos);
    vec3 s = aurea_linear_to_srgb(clamp(ref.rgb, 0.0, 1.0));

    // Sangria de cor: a crominância de mais adiante no caminho.
    const float bleed = p.a.w;
    if (bleed > 0.001) {
        const vec2 qpos = pos + (pos - v_uv) * bleed * 1.5 + mosh_vector(pos) * bleed * 2.0
                        + vec2(p.grid.z, 0.0) * bleed * 0.5;
        const vec3 sq = aurea_linear_to_srgb(clamp(mosh_ref(qpos).rgb, 0.0, 1.0));
        const vec3 a = mosh_to_ycc(s);
        const vec3 c = mosh_to_ycc(sq);
        const vec2 chroma = mix(a.yz, c.yz, bleed) * (1.0 + 0.35 * bleed);
        s = mosh_from_ycc(vec3(a.x, chroma));
    }

    if (kind == 2) {
        // Canais trocados e posterizados (o bloco com coeficientes errados).
        s = floor(s.gbr * 5.0 + 0.5) / 5.0;
    }

    const vec3 rgb = aurea_srgb_to_linear(clamp(s, 0.0, 1.0));
    const vec4 moshP = premultiply(vec4(rgb, ref.a));
    o_color = mix(nowP, moshP, clamp(p.a.x, 0.0, 1.0));
}
