#version 450
// =============================================================================
//  Aurea / shaders / hyper / me_search.frag
//
//  Hyper Motion / Hyper Clean: vetores de movimento por BLOCO de 16x16 px,
//  busca hierárquica (código próprio; nada do minterpolate do FFmpeg).
//
//  Um texel de saída = um bloco da grade fixa (fw x fh blocos de `block` px da
//  resolução cheia). Em cada nível da pirâmide de luma (R = A, G = B):
//    * busca de +-`full` texels do nível em volta do zero, em passo 2 com o
//      refino +-1 (no nível mais grosso ela é a única; no intermediário pega o
//      objeto PEQUENO e rápido que o nível grosso, dominado pelo fundo, não viu);
//    * os PREDITORES (o vetor do próprio bloco e dos 4 vizinhos no campo do
//      nível anterior) com +-R em volta, e +-1 em volta do melhor;
//    * com `sub` > 0, meio texel em volta do melhor (amostras bilineares).
//  Custo: média do |A − B| numa janela do nível centrada no bloco (passo
//  `step`) + lambda·|v| (desempata a favor do movimento pequeno). A janela de
//  A é lida UMA vez por bloco; cada candidato só lê B.
//
//  Sentido: dir 0 = A -> B (onde o conteúdo do bloco de A está em B); dir 1 =
//  B -> A (o mesmo com os canais trocados). Saída RG = vetor em px da
//  resolução CHEIA, B = custo, A = 1.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_level;   // RG = luma A, B
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_prev;    // campo anterior (com preditores)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 level;    // x = px cheios por texel do nível, yz = tamanho do nível, w = 1 usa os preditores
    vec4 search;   // x = raio dos preditores (<= 2), y = livre, z = passo da janela 5x5, w = sentido
    vec4 block;    // x = bloco (px cheios), yz = fw, fh, w = lambda
    vec4 sub;      // x = passo subpixel (0 = só inteiros), y = raio da busca em volta do zero (<= 8, 0 = nenhuma)
} p;

// Janela FIXA 5x5 (o laço desenrola e as 25 leituras saem juntas); a
// cobertura muda pelo passo: 4 texels no nível cheio (17 px), 2 no 1/2,
// 1 no 1/4 e no 1/8.
const int kHalf = 2;
const int kTaps = (2 * kHalf + 1) * (2 * kHalf + 1);
vec2 g_center;
vec2 g_texel;
bool g_swap;
float g_step;
float g_a[kTaps];

float dst_at(vec2 pos) {
    const vec2 v = textureLod(u_level, pos * g_texel, 0.0).rg;
    return g_swap ? v.x : v.y;
}

float cost(vec2 v) {
    float sum = 0.0;
    for (int j = -kHalf; j <= kHalf; ++j)
        for (int i = -kHalf; i <= kHalf; ++i)
            sum += abs(g_a[(j + kHalf) * (2 * kHalf + 1) + (i + kHalf)] - dst_at(g_center + vec2(float(i), float(j)) * g_step + v));
    return sum / float(kTaps) + p.block.w * length(v) * 0.01;
}

void consider(vec2 v, inout vec2 best, inout float bestCost) {
    const float c = cost(v);
    if (c < bestCost) { bestCost = c; best = v; }
}

void main() {
    const ivec2 blk = ivec2(gl_FragCoord.xy);
    const float s = p.level.x;
    g_texel = 1.0 / p.level.yz;
    g_swap = p.search.w > 0.5;
    g_step = max(p.search.z, 1.0);
    // Centro do bloco (px cheios) -> posição no nível.
    g_center = (vec2(blk) + 0.5) * p.block.x / s;
    for (int j = -kHalf; j <= kHalf; ++j)
        for (int i = -kHalf; i <= kHalf; ++i) {
            const vec2 t = textureLod(u_level, (g_center + vec2(float(i), float(j)) * g_step) * g_texel, 0.0).rg;
            g_a[(j + kHalf) * (2 * kHalf + 1) + (i + kHalf)] = g_swap ? t.y : t.x;
        }
    vec2 best = vec2(0.0);
    float bestCost = cost(best);
    // Busca em volta do zero: passo 2, depois +-1 em volta do melhor.
    const int F = clamp(int(p.sub.y), 0, 8);
    if (F > 0) {
        for (int y = -F; y <= F; y += 2)
            for (int x = -F; x <= F; x += 2) {
                if (x == 0 && y == 0) continue;
                consider(vec2(float(x), float(y)), best, bestCost);
            }
        const vec2 c = best;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                if (x != 0 || y != 0) consider(c + vec2(float(x), float(y)), best, bestCost);
    }
    if (p.level.w > 0.5) {
        const int R = clamp(int(p.search.x), 0, 2);
        const ivec2 fsz = ivec2(p.block.yz);
        vec2 pred[5];
        pred[0] = texelFetch(u_prev, blk, 0).rg / s;
        pred[1] = texelFetch(u_prev, clamp(blk + ivec2(-1, 0), ivec2(0), fsz - 1), 0).rg / s;
        pred[2] = texelFetch(u_prev, clamp(blk + ivec2(1, 0), ivec2(0), fsz - 1), 0).rg / s;
        pred[3] = texelFetch(u_prev, clamp(blk + ivec2(0, -1), ivec2(0), fsz - 1), 0).rg / s;
        pred[4] = texelFetch(u_prev, clamp(blk + ivec2(0, 1), ivec2(0), fsz - 1), 0).rg / s;
        for (int k = 0; k < 5; ++k) {
            const vec2 base = floor(pred[k] + 0.5);
            for (int y = -R; y <= R; ++y)
                for (int x = -R; x <= R; ++x) consider(base + vec2(float(x), float(y)), best, bestCost);
        }
        const vec2 c = best;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                if (x != 0 || y != 0) consider(c + vec2(float(x), float(y)), best, bestCost);
    }
    if (p.sub.x > 0.0) {
        const vec2 c = best;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                if (x != 0 || y != 0) consider(c + vec2(float(x), float(y)) * p.sub.x, best, bestCost);
    }
    o_color = vec4(best * s, bestCost, 1.0);
}
