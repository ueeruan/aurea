#version 450
// =============================================================================
//  Aurea / shaders / effects / saber_core.frag
//
//  Saber, passe 1: o NÚCLEO. Cada texel diz quanto do traço passa por ele
//  (G = cobertura) e quanta luz ele solta (R = cobertura × brilho do caminho
//  × cintilação × brilho do deslocamento) — o R alimenta a pirâmide do brilho.
//
//  Três fontes de núcleo:
//    0 único      um traço entre "Início" e "Fim" (uniforms);
//    1 máscaras   os caminhos das máscaras da camada, achatados na CPU em
//                 trechos e distribuídos numa grade (dado RGBA32F): cada
//                 texel só olha os trechos da sua célula;
//    2 camada     o alfa da camada (ou de outra): contorno de largura
//                 "Tamanho do núcleo", ou o preenchimento ("Usar alfa");
//    3 nenhum     a outra camada escolhida não aparece neste instante.
//
//  Janela (deslocamento inicial/final): só o pedaço [início, fim] do caminho
//  acende — o "desenhar" do Saber. As pontas da janela levam o
//  arredondamento (1 = redonda, 0 = corte reto); as junções entre trechos,
//  não. A distorção desloca o ponto ANTES da distância: o núcleo (e o brilho
//  que sai dele) ondula.
// =============================================================================
#include "saber_common.glsl"

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada (entrada)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_data;   // trechos das máscaras (RGBA32F)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_map;    // outra camada (px da composição)

vec4 fetch_data(int k) {
    const int w = max(textureSize(u_data, 0).x, 1);
    return texelFetch(u_data, ivec2(k % w, k / w), 0);
}

/// Cobertura de um trecho a→b (posição ta→tb no caminho, 0..1) dentro de UM
/// intervalo [w0, w1] da janela. `ws`/`wlen` = início e tamanho da janela
/// inteira (para o afinamento), `len` = comprimento do caminho (px),
/// `closed` = caminho fechado (as pontas 0/1 são costura, não ponta).
/// Atualiza `lit` com a luz (cobertura × brilho).
float stroke_piece(vec2 q, vec2 a, vec2 b, float ta, float tb, float w0, float w1, float ws, float wlen,
                   float len, bool closed, float bright, float aa, inout float lit) {
    const float lo = max(ta, w0), hi = min(tb, w1);
    if (hi < lo || w1 <= w0) return 0.0;
    const vec2 ab = b - a;
    const float l2 = dot(ab, ab);
    const float segLen = sqrt(max(l2, 1e-12));
    const float span = tb - ta;
    // Distorção do caminho: a linha anda na normal do trecho, pela posição ao
    // longo do caminho (contínua de um trecho ao outro). Caminho aberto: as
    // pontas ficam presas (o raio sai e chega nos pontos).
    if (p.dist.x > 0.5 && p.dist.y > 0.0 && l2 > 1e-12) {
        const float hp = clamp(dot(q - a, ab) / l2, 0.0, 1.0);
        const float along = (ta + hp * span) * len;
        const float pin = closed ? 1.0 : clamp(min(along, len - along) / max(0.08 * len, 1.0), 0.0, 1.0);
        q -= vec2(-ab.y, ab.x) / segLen * (p.dist.y * saber_wave(along / max(p.dist.z, 1.0)) * pin);
    }
    float h0 = 0.0, h1 = 1.0;
    if (span > 1e-9) { h0 = (lo - ta) / span; h1 = (hi - ta) / span; }
    const float hraw = l2 > 1e-12 ? dot(q - a, ab) / l2 : 0.0;
    const float h = clamp(hraw, h0, h1);
    const vec2 c = a + ab * h;
    // Ponta da janela (ou do caminho aberto) → arredondamento; junção → distância comum.
    const bool lowEnd = (w0 > ta) || (ta <= 1e-6 && !closed);
    const bool highEnd = (w1 < tb) || (tb >= 1.0 - 1e-6 && !closed);
    float d;
    if (l2 > 1e-12 && ((hraw < h0 && lowEnd) || (hraw > h1 && highEnd))) {
        const float rnd = max(hraw < h0 ? p.caps.x : p.caps.y, 0.04);
        const float perp = abs((q.x - a.x) * ab.y - (q.y - a.y) * ab.x) / segLen;
        const float over = abs(hraw - h) * segLen;
        d = length(vec2(perp, over / rnd));
    } else {
        d = length(q - c);
    }
    // Onde o ponto cai na janela: 0 = início, 1 = fim (o afinamento e a cabeça).
    const float tc = ta + h * span;
    float u = closed ? fract(tc - ws + 2.0) : (tc - ws);
    u = clamp(u / max(wlen, 1e-6), 0.0, 1.0);
    const float r = p.core.x * max(mix(p.core.z, p.core.w, u), 0.0);
    if (r <= 0.0) return 0.0;
    const float soft = clamp(p.core.y, 0.0, 1.0);
    const float cov = 1.0 - smoothstep(r * (1.0 - soft), r * (1.0 + soft) + aa, d);
    // Brilho do deslocamento: a cabeça do traço (o fim da janela) acende mais.
    const float head = (1.0 - u) * max(wlen, 0.0) * max(len, 0.0);
    const float boost = 1.0 + p.window.z * 3.0 * exp(-head / (4.0 * r + 4.0));
    lit = max(lit, cov * bright * boost);
    return cov;
}

float alpha_at(vec2 lp) {
    if (p.window.w > 0.5) {
        const vec2 comp = vec2(p.affA.x * lp.x + p.affA.z * lp.y + p.affB.x, p.affA.y * lp.x + p.affA.w * lp.y + p.affB.y);
        return texture(u_map, (comp - p.mapRegion.xy) / max(p.mapRegion.zw, vec2(1e-6))).a;
    }
    return texture(u_tex0, (lp - p.src.xy) / max(p.src.zw, vec2(1e-6))).a;
}

void main() {
    const vec2 px = saber_px();
    const float aa = max(p.info.x, 1e-3);
    const int type = int(p.info.y + 0.5);
    // Núcleo de caminho: a distorção anda na normal (stroke_piece). Núcleo de
    // camada: o plano inteiro ondula.
    const vec2 q = type == 2 ? px + saber_warp(px, 1.0) : px;
    float cov = 0.0, lit = 0.0;
    if (type == 0) {
        const float len = length(p.seg.zw - p.seg.xy);
        cov = stroke_piece(q, p.seg.xy, p.seg.zw, 0.0, 1.0, p.window.x, p.window.y, p.window.x,
                           p.window.y - p.window.x, len, false, 1.0, aa, lit);
    } else if (type == 1) {
        const vec4 head0 = fetch_data(0), head1 = fetch_data(1), head2 = fetch_data(2);
        const vec2 cellF = (q - head1.xy) / max(head1.zw, vec2(1e-3));
        if (head0.x > 0.5 && cellF.x >= 0.0 && cellF.y >= 0.0 && cellF.x < head0.z && cellF.y < head0.w) {
            const int ci = int(floor(cellF.y)) * int(head0.z + 0.5) + int(floor(cellF.x));
            const vec4 cell = fetch_data(int(head2.z + 0.5) + ci);
            const int first = int(cell.x + 0.5);
            const int count = min(int(cell.y + 0.5), 192);
            const int segBase = int(head2.x + 0.5), pathBase = int(head2.y + 0.5), idxBase = int(head2.w + 0.5);
            for (int k = 0; k < count; ++k) {
                const int idx = first + k;
                const vec4 packed = fetch_data(idxBase + idx / 4);
                const int lane = idx - (idx / 4) * 4;
                const int si = int(dot(packed, vec4(equal(ivec4(lane), ivec4(0, 1, 2, 3)))) + 0.5);
                const vec4 s0 = fetch_data(segBase + 2 * si), s1 = fetch_data(segBase + 2 * si + 1);
                const int pi = int(s1.z + 0.5);
                const vec4 p0 = fetch_data(pathBase + 3 * pi), p1 = fetch_data(pathBase + 3 * pi + 1);
                const float bright = fetch_data(pathBase + 3 * pi + 2).x;
                const bool closed = p0.w > 0.5;
                float c = stroke_piece(q, s0.xy, s0.zw, s1.x, s1.y, p1.x, p1.y, p0.x, p0.y, p0.z, closed, bright, aa, lit);
                if (p1.z <= p1.w)
                    c = max(c, stroke_piece(q, s0.xy, s0.zw, s1.x, s1.y, p1.z, p1.w, p0.x, p0.y, p0.z, closed, bright, aa, lit));
                cov = max(cov, c);
            }
        }
    } else if (type == 2) {
        // Camada / texto: o alfa vira núcleo (contorno ou preenchimento).
        const float r = max(p.core.x, 0.5 * aa);
        const float a0 = alpha_at(q);
        if (p.info.z > 0.5) {
            cov = a0;
        } else {
            // Contorno de meia largura r em volta da borda (alfa 0,5): há alfa
            // alto E baixo a até r daqui. Dois anéis (r e r/2) e o centro.
            float mx = a0, mn = a0;
            for (int i = 0; i < 12; ++i) {
                const float ang = float(i) * 0.523598776 + 0.13;
                const float v = alpha_at(q + vec2(cos(ang), sin(ang)) * r);
                mx = max(mx, v); mn = min(mn, v);
            }
            for (int i = 0; i < 6; ++i) {
                const float ang = float(i) * 1.047197551 + 0.4;
                const float v = alpha_at(q + vec2(cos(ang), sin(ang)) * (0.5 * r));
                mx = max(mx, v); mn = min(mn, v);
            }
            const float edge = min(mx, 1.0 - mn);
            const float w = 0.08 + clamp(p.core.y, 0.0, 1.0) * 0.4;
            cov = smoothstep(0.5 - w, 0.5 + w, edge);
        }
        // A janela vira uma varredura da esquerda para a direita da camada.
        if (p.window.x > 0.0 || p.window.y < 1.0) {
            const float u = (q.x - p.box.x) / max(p.box.z, 1.0);
            const float e = max(aa / max(p.box.z, 1.0), 0.004);
            cov *= smoothstep(p.window.x - e, p.window.x + e, u) * (1.0 - smoothstep(p.window.y - e, p.window.y + e, u));
            const float headPx = max(p.window.y - u, 0.0) * p.box.z;
            lit = cov * (1.0 + p.window.z * 3.0 * exp(-headPx / (4.0 * r + 4.0)));
        } else {
            lit = cov;
        }
    }
    o_color = vec4(lit * p.info.w, cov, 0.0, cov);
}
