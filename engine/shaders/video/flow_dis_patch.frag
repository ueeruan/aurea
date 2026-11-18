#version 450
// =============================================================================
//  Aurea / shaders / video / flow_dis_patch.frag
//
//  DIS — busca inversa por bloco (1.0.3; algoritmo publicado em Kroeger,
//  Timofte, Dai e Van Gool, "Fast Optical Flow using Dense Inverse Search",
//  ECCV 2016; implementação própria). Um texel desta grade = um bloco 8×8 do
//  nível, a cada `stride` px (os blocos se sobrepõem). Cada bloco:
//
//   1. parte do melhor entre o vetor do nível de cima e o do par anterior
//      (contexto no tempo, quando houver) — o de menor erro no bloco;
//   2. Gauss-Newton na forma INVERSA composicional: a matriz (Hessiana) e o
//      gradiente são do bloco do quadro de origem, medidos uma vez; cada
//      iteração só reamostra o outro quadro. Brilho do bloco normalizado
//      (média tirada dos dois lados): mudança de exposição não vira vetor;
//   3. se o vetor andar mais que o tamanho do bloco, volta ao ponto de
//      partida (o bloco não achou par — oclusão ou região lisa).
//
//  Saída: RG = vetor do bloco (px deste nível), B = erro médio do bloco.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_level;   // R = atual, G = seguinte
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_init;    // RG = fluxo denso de partida (px do nível `init`)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_prior;   // RG = fluxo do par anterior (px do nível base)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho do nível, zw = tamanho
    vec4 blk;     // x = passo entre blocos (px), y = iterações, z = sentido trocado (0/1), w = tem partida (0/1)
    vec4 prior;   // x = px deste nível por px do nível base, y = tem par anterior (0/1), zw = tamanho da grade de blocos
} p;

float src_l(vec2 px) { const vec2 c = textureLod(u_level, px * p.texel.xy, 0.0).rg; return p.blk.z > 0.5 ? c.g : c.r; }
float dst_l(vec2 px) { const vec2 c = textureLod(u_level, px * p.texel.xy, 0.0).rg; return p.blk.z > 0.5 ? c.r : c.g; }

void main() {
    const vec2 origin = floor(v_uv * p.prior.zw) * p.blk.x;   // canto do bloco (px do nível)
    // Bloco 8×8 do quadro de origem com a borda de 1 px (para o gradiente).
    float tv[100];
    for (int j = 0; j < 10; ++j)
        for (int i = 0; i < 10; ++i)
            tv[j * 10 + i] = src_l(origin + vec2(float(i) - 0.5, float(j) - 0.5));
    float meanT = 0.0, mgx = 0.0, mgy = 0.0;
    for (int j = 1; j < 9; ++j)
        for (int i = 1; i < 9; ++i) {
            const int k = j * 10 + i;
            meanT += tv[k];
            mgx += 0.5 * (tv[k + 1] - tv[k - 1]);
            mgy += 0.5 * (tv[k + 10] - tv[k - 10]);
        }
    meanT /= 64.0; mgx /= 64.0; mgy /= 64.0;
    float hxx = 0.0, hxy = 0.0, hyy = 0.0;
    for (int j = 1; j < 9; ++j)
        for (int i = 1; i < 9; ++i) {
            const int k = j * 10 + i;
            const float gx = 0.5 * (tv[k + 1] - tv[k - 1]) - mgx;
            const float gy = 0.5 * (tv[k + 10] - tv[k - 10]) - mgy;
            hxx += gx * gx; hxy += gx * gy; hyy += gy * gy;
        }
    // Ponto de partida: o vetor do nível de cima (escalado) ou o do par anterior.
    const vec2 center = origin + 4.0;
    vec2 cand[3];
    int nc = 0;
    if (p.blk.w > 0.5) {
        const vec2 scale = p.texel.zw / max(vec2(textureSize(u_init, 0)), vec2(1.0));
        cand[nc++] = textureLod(u_init, center * p.texel.xy, 0.0).rg * scale;
    } else {
        cand[nc++] = vec2(0.0);
    }
    if (p.prior.y > 0.5) cand[nc++] = textureLod(u_prior, center * p.texel.xy, 0.0).rg * p.prior.x;
    vec2 u = cand[0];
    float best = 1e9;
    for (int c = 0; c < 3; ++c) {
        if (c >= nc) break;
        float meanW = 0.0, sq = 0.0, sum = 0.0;
        float wv[64];
        for (int j = 0; j < 8; ++j)
            for (int i = 0; i < 8; ++i) {
                wv[j * 8 + i] = dst_l(origin + vec2(float(i) + 0.5, float(j) + 0.5) + cand[c]);
                meanW += wv[j * 8 + i];
            }
        meanW /= 64.0;
        for (int j = 0; j < 8; ++j)
            for (int i = 0; i < 8; ++i) {
                const float r = (wv[j * 8 + i] - meanW) - (tv[(j + 1) * 10 + i + 1] - meanT);
                sq += r * r;
            }
        // A previsão do par anterior só troca a partida se for claramente
        // melhor: com rotação ou aceleração ela erra, e o bloco iteraria a
        // partir do lugar errado.
        if (c > 0) sq *= 1.25;
        if (sq < best) { best = sq; u = cand[c]; }
    }
    const vec2 start = u;
    // Gauss-Newton inverso composicional (amortecido em bloco liso).
    const float damp = 1e-4 + 0.02 * (hxx + hyy) / 64.0;
    const float a = hxx + damp, b = hxy, d = hyy + damp;
    const float det = a * d - b * b;
    float err = sqrt(best / 64.0);
    if (det > 1e-10) {
        const int iters = int(p.blk.y);
        for (int it = 0; it < 16; ++it) {
            if (it >= iters) break;
            float wv[64];
            float meanW = 0.0;
            for (int j = 0; j < 8; ++j)
                for (int i = 0; i < 8; ++i) {
                    wv[j * 8 + i] = dst_l(origin + vec2(float(i) + 0.5, float(j) + 0.5) + u);
                    meanW += wv[j * 8 + i];
                }
            meanW /= 64.0;
            float bx = 0.0, by = 0.0, sq = 0.0;
            for (int j = 0; j < 8; ++j)
                for (int i = 0; i < 8; ++i) {
                    const int k = (j + 1) * 10 + i + 1;
                    const float r = (wv[j * 8 + i] - meanW) - (tv[k] - meanT);
                    bx += (0.5 * (tv[k + 1] - tv[k - 1]) - mgx) * r;
                    by += (0.5 * (tv[k + 10] - tv[k - 10]) - mgy) * r;
                    sq += r * r;
                }
            err = sqrt(sq / 64.0);
            vec2 du = vec2(d * bx - b * by, -b * bx + a * by) / det;
            const float len = length(du);
            if (len > 2.0) du *= 2.0 / len;
            u -= du;
            if (len < 0.005) break;
        }
    }
    // Andou mais que o bloco: não achou par; fica com a partida.
    if (length(u - start) > 8.0) u = start;
    o_color = vec4(u, err, 1.0);
}
