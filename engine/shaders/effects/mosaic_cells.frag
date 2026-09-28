#version 450
// =============================================================================
//  Aurea / shaders / effects / mosaic_cells.frag
//
//  Mosaico / painel de LED: a camada vira uma grade de células de cor única
//  (a MÉDIA da célula, não um pixel sorteado — sem cintilar quando a imagem
//  anda meio pixel). Com vão entre as células, células redondas, sombreado de
//  lâmpada e vinheta por célula, a grade vira um painel de LED.
//
//  A grade é presa ao plano da camada (px da resolução cheia): a prévia em 1/4
//  e o export em 4K têm as mesmas células. A borda de cada célula é suavizada
//  em um pixel de SAÍDA (sem serrilhado em nenhuma resolução).
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = célula (px da camada), y = vão 0..0.9, z = 1 redonda, w = sombreado 0..1
    vec4 p1;   // x = vinheta por célula 0..1, y = 1 px de saída em px da camada
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;   // x = estilo (0 livre, 1 mosaico, 2 parede de LED, 3 matriz de pontos),
               // y = vinheta da caixa 0..1, zw = caixa da camada (px)
    vec4 color;   // fundo dos vãos (linear, direta; alfa 0 = transparente)
} p;

vec4 sample_layer(vec2 layerPoint) {
    return texture(u_tex0, (layerPoint - p.p2.xy) / max(p.p2.zw, vec2(1e-3)));
}

void main() {
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const float cellSize = max(p.p0.x, 1.0);
    const vec2 cell = floor(point / cellSize);
    const vec2 cellCenter = (cell + 0.5) * cellSize;

    // Média de 3x3 amostras dentro da célula.
    vec4 avg = vec4(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            avg += sample_layer(cellCenter + vec2(float(x), float(y)) * (cellSize / 3.0));
        }
    }
    avg /= 9.0;

    const vec2 local = (point - cellCenter) / (cellSize * 0.5);   // -1..1 dentro da célula
    const int style = int(p.p3.x + 0.5);
    const float aa = max(p.p1.y, 1e-3) / (cellSize * 0.5);

    // Vinheta sobre a caixa da camada: só a cor, o alfa fica.
    const vec2 halfBox = max(p.p3.zw * 0.5, vec2(1e-3));
    const float boxVig = 1.0 - clamp(p.p3.y, 0.0, 1.0)
                       * smoothstep(0.5, 1.35, length((point - halfBox) / halfBox));

    if (style != 0) {
        // Os looks prontos: vão, forma e sombreado do look; a borda da lâmpada
        // escurece em direção à placa (ou some, sem placa).
        const float sGap = style == 1 ? 0.0 : (style == 2 ? 0.15 : 0.3);
        const bool sRound = style == 3;
        const float sShade = style == 1 ? 0.0 : (style == 2 ? 0.4 : 0.5);
        const float sDist = sRound ? length(local) : max(abs(local.x), abs(local.y));
        const float sIn = 1.0 - sGap;
        const float shape = (style == 1) ? 1.0 : 1.0 - smoothstep(sIn - aa, sIn + aa, sDist);
        const float rr = clamp(sDist / max(sIn, 1e-3), 0.0, 1.0);
        const float glow = shape * (1.0 - sShade * rr * rr);
        vec4 outc;
        if (style == 2) {
            // A placa atrás das lâmpadas, dentro da silhueta (pela cobertura da célula).
            outc = vec4(mix(p.color.rgb * avg.a, avg.rgb, glow), avg.a);
        } else {
            outc = avg * glow;
        }
        o_color = vec4(outc.rgb * boxVig, outc.a);
        return;
    }

    const bool round = p.p0.z > 0.5;
    const float gap = clamp(p.p0.y, 0.0, 0.9);
    const float dist = round ? length(local) : max(abs(local.x), abs(local.y));
    const float halfIn = 1.0 - gap;
    // Sem vão e quadrada, a célula cobre tudo: nenhuma costura entre vizinhas.
    const float cover = (!round && gap <= 0.0) ? 1.0 : 1.0 - smoothstep(halfIn - aa, halfIn + aa, dist);

    const float r = clamp(dist / max(halfIn, 1e-3), 0.0, 1.0);
    const float bulb = mix(1.0, 1.2 - 0.55 * r * r, clamp(p.p0.w, 0.0, 1.0));
    const float vig = 1.0 - clamp(p.p1.x, 0.0, 1.0) * smoothstep(0.35, 1.0, r);
    const vec4 lit = vec4(avg.rgb * bulb * vig, avg.a);

    const vec4 background = vec4(p.color.rgb * p.color.a, p.color.a);
    o_color = mix(background, lit, cover);
    o_color.rgb *= boxVig;
}
