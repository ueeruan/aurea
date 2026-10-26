#version 450
// =============================================================================
//  Aurea / shaders / hyper / deint.frag
//
//  Hyper Clean (Turbo), desentrelaçar: código próprio (nada do yadif/w3fdif).
//  Mantém o campo de cima (linhas pares da fonte) e refaz as linhas do outro
//  campo SÓ onde há pente (o vídeo parado continua com a resolução vertical
//  inteira — "weave" adaptativo):
//    * pente local: o centro fora do intervalo das linhas de cima e de baixo
//      do outro campo, (Y − Y↑)(Y − Y↓) > 0, nos 3 vizinhos da linha;
//    * a linha refeita por DIREÇÃO DE BORDA: dos 5 ângulos (−2..2 px) fica o
//      de menor diferença entre a linha de cima deslocada e a de baixo no
//      sentido oposto (janela de 3), e o pixel é a média dos dois — a borda
//      inclinada continua inteira, sem a escada da interpolação vertical.
//  Automático: só com a pontuação de pente do quadro (deint_score) acima do
//  limiar; Sempre: em todo quadro (ainda adaptativo ao movimento).
//  Densidade abaixo de 3/4 (prévia reduzida): identidade.
// =============================================================================
#include "../common/bindings.glsl"
#include "hyper_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_score;   // 1x1: R = pente, G = mesmo campo

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;    // xy = tamanho
    vec4 flags;   // x = modo (1 automático, 2 sempre), y = texels por px da camada, z = paridade mantida (0 pares)
} p;

ivec2 g_size;
vec4 L(int x, int y) { return texelFetch(u_src, hy_clampi(ivec2(x, y), g_size), 0); }
float Yp(vec4 c) { return hy_luma(hy_to_perc(c).rgb); }

void main() {
    g_size = ivec2(p.size.xy);
    const ivec2 at = ivec2(gl_FragCoord.xy);
    const vec4 c = L(at.x, at.y);
    if (p.flags.y < 0.75 || (at.y & 1) == int(p.flags.z) || at.y == 0 || at.y + 1 >= g_size.y) {
        o_color = c;
        return;
    }
    float global = 1.0;
    if (p.flags.x < 1.5) {
        const vec4 s = texelFetch(u_score, ivec2(0), 0);
        const float ratio = s.r / (s.g + 0.25);
        global = smoothstep(1.3, 1.8, ratio) * smoothstep(1.0, 2.5, s.r);
    }
    if (global <= 0.0) { o_color = c; return; }
    // Pente local (luma perceptual), o máximo nos 3 vizinhos da linha.
    float comb = 0.0;
    for (int dx = -1; dx <= 1; ++dx) {
        const float yc = Yp(L(at.x + dx, at.y));
        const float yu = Yp(L(at.x + dx, at.y - 1));
        const float yd = Yp(L(at.x + dx, at.y + 1));
        comb = max(comb, (yc - yu) * (yc - yd));
    }
    const float m = smoothstep(0.0004, 0.004, comb) * global;
    if (m <= 0.0) { o_color = c; return; }
    // Interpolação pela direção da borda.
    float best = 1e9;
    int bestK = 0;
    for (int k = -2; k <= 2; ++k) {
        float cost = 0.0;
        for (int j = -1; j <= 1; ++j)
            cost += abs(Yp(L(at.x + k + j, at.y - 1)) - Yp(L(at.x - k + j, at.y + 1)));
        // Desempate para a vertical (ângulo pequeno ganha).
        cost += 0.002 * float(abs(k));
        if (cost < best) { best = cost; bestK = k; }
    }
    const vec4 interp = 0.5 * (L(at.x + bestK, at.y - 1) + L(at.x - bestK, at.y + 1));
    o_color = mix(c, interp, m);
}
