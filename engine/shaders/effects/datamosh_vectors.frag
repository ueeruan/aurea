#version 450
// =============================================================================
//  Aurea / shaders / effects / datamosh_vectors.frag
//
//  DATAMOSH, passe 1 — um vetor de movimento por BLOCO. O alvo tem o tamanho
//  da grade (um texel = um bloco). Para o bloco do quadro atual (tex0), acha o
//  deslocamento que melhor o reconstrói a partir da referência (tex1, a camada
//  num quadro anterior): soma das diferenças de luminância em 16 pontos do
//  bloco. Busca grossa (9×9, passo ½ bloco = ±2 blocos) e fina (5×5, passo ⅛
//  de bloco em volta do melhor). O vetor zero ganha um desconto (o "skip" dos
//  codecs): o que está parado continua parado.
//
//  Sem referência (camada parada no tempo), o campo vem de ruído coerente por
//  bloco, com direção e força sorteadas por (bloco, semente, época).
//
//  Saída: xy = deslocamento em uv da entrada (onde buscar na referência),
//  z = custo médio do melhor candidato, w = 1.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_now;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_ref;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;   // uv da entrada → uv da referência: * xy + zw
    vec4 grid;    // xy blocos, zw tamanho do bloco em uv da entrada
    vec4 a;       // x 1 = há referência, y desconto do vetor zero, z magnitude sem referência (blocos), w semente
    vec4 b;       // x época
} p;

float mosh_luma(vec4 c) {
    // Raiz da luminância linear: perto do que o olho (e o codec) compara.
    return sqrt(max(dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0));
}

void main() {
    const vec2 blk = floor(v_uv * p.grid.xy);
    const vec2 origin = blk * p.grid.zw;

    if (p.a.x < 0.5) {
        // Sem passado: campo de ruído coerente (blocos vizinhos andam juntos)
        // com um tremor por bloco e alguns blocos parados.
        const uint seed = uint(p.a.w);
        const vec2 cell = blk / 5.0 + vec2(float(seed % 97u) * 3.1, float(seed % 89u) * 1.7) + vec2(p.b.x * 7.3, p.b.x * 2.9);
        const float ang = aurea_value_noise(cell) * AUREA_TAU * 2.0;
        const float mag = mix(0.25, 1.0, aurea_value_noise(cell * 1.9 + 13.0));
        const uvec2 q = uvec2(ivec2(blk) + ivec2(4096)) ^ uvec2(seed * 747796405u + uint(p.b.x) * 2891336453u);
        const float jitter = aurea_hash(q);
        const float still = step(0.22, aurea_hash(q.yx + uvec2(17u, 31u)));
        vec2 dir = vec2(cos(ang), sin(ang));
        dir += (vec2(aurea_hash(q + uvec2(5u, 0u)), aurea_hash(q + uvec2(0u, 9u))) - 0.5) * 0.6 * jitter;
        o_color = vec4(dir * mag * p.a.z * p.grid.zw * still, 0.0, 1.0);
        return;
    }

    // 16 pontos do bloco atual.
    float cur[16];
    for (int i = 0; i < 16; ++i) {
        const vec2 pt = origin + (vec2(float(i & 3), float(i >> 2)) + 0.5) * 0.25 * p.grid.zw;
        cur[i] = mosh_luma(texture(u_now, pt));
    }

    // Custo do deslocamento `off` (uv da entrada).
    vec2 bestOff = vec2(0.0);
    float best = 0.0;
    for (int i = 0; i < 16; ++i) {
        const vec2 pt = origin + (vec2(float(i & 3), float(i >> 2)) + 0.5) * 0.25 * p.grid.zw;
        best += abs(cur[i] - mosh_luma(texture(u_ref, pt * p.uvMap.xy + p.uvMap.zw)));
    }
    best *= p.a.y;

    // Busca grossa: ±2 blocos em passos de meio bloco.
    vec2 coarse = vec2(0.0);
    for (int y = -4; y <= 4; ++y) {
        for (int x = -4; x <= 4; ++x) {
            if (x == 0 && y == 0) continue;
            const vec2 off = vec2(float(x), float(y)) * 0.5 * p.grid.zw;
            float c = 0.0;
            for (int i = 0; i < 16; ++i) {
                const vec2 pt = origin + (vec2(float(i & 3), float(i >> 2)) + 0.5) * 0.25 * p.grid.zw + off;
                c += abs(cur[i] - mosh_luma(texture(u_ref, pt * p.uvMap.xy + p.uvMap.zw)));
            }
            // Um pouco de preferência pelos vetores curtos (desempate estável).
            c *= 1.0 + 0.01 * float(abs(x) + abs(y));
            if (c < best) { best = c; bestOff = off; }
        }
    }
    // Busca fina em volta do melhor: ±¼ de bloco em passos de ⅛.
    coarse = bestOff;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if (x == 0 && y == 0) continue;
            const vec2 off = coarse + vec2(float(x), float(y)) * 0.125 * p.grid.zw;
            float c = 0.0;
            for (int i = 0; i < 16; ++i) {
                const vec2 pt = origin + (vec2(float(i & 3), float(i >> 2)) + 0.5) * 0.25 * p.grid.zw + off;
                c += abs(cur[i] - mosh_luma(texture(u_ref, pt * p.uvMap.xy + p.uvMap.zw)));
            }
            if (c < best) { best = c; bestOff = off; }
        }
    }
    o_color = vec4(bestOff, best / 16.0, 1.0);
}
