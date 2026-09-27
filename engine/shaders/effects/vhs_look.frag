#version 450
// =============================================================================
//  Aurea / shaders / effects / vhs_look.frag
//
//  VHS (Estilizar): o visual do tutorial clássico de fita, pronto de fábrica.
//  UM passe, oito leituras horizontais da textura:
//
//    1. tremor por linha e faixas de tracking deslocam a linha inteira
//    2. luma suavizada na horizontal (5 leituras simétricas)
//    3. croma que chega ATRASADA: 3 leituras só à ESQUERDA — a cor escorre
//       para a direita, o vermelho mais que o azul (as bordas se separam)
//    4. tom quente e desbotado (preto levantado, branco creme) e saturação
//    5. vinheta suave
//    6. OSD do videocassete desenhado aqui mesmo, de uma fonte 5×7 em bits:
//       "PLAY ▶" no alto à esquerda, velocidade (SP/LP/EP) embaixo à esquerda,
//       tempo local hh:mm:ss embaixo à direita — com um leve rastro de sinal
//    7. linhas de varredura e ruído fino de fita por cima de tudo
//
//  Tudo medido em px da camada na resolução cheia (prévia reduzida e export
//  desenham o mesmo) e sorteado por hash INTEIRO do quadro e da semente: o
//  mesmo quadro dá os mesmos bits em qualquer aparelho.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = intensidade (0..1), y = sangramento de cor (px), z = suavização (px), w = ruído (0..1)
    vec4 p1;   // x = varredura (0..1), y = tracking (0..1), z = tremor (px), w = tom (0..1)
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;   // xy = tamanho da camada (px), z = quadro local, w = segundos (quadro / fps)
    vec4 color; // cor do OSD, linear
    vec4 q0;   // x = saturação (0..2), y = vinheta (0..1), z = sobreposição (0 nenhuma, 1 PLAY, 2 REC, 3 PAUSE), w = mostrar tempo
    vec4 q1;   // x = rótulo (0 SP, 1 LP, 2 EP), y = pixel da fonte (px da camada), z = semente, w = texels por px da camada
    vec4 q2;   // xyz = horas, minutos e segundos do tempo local
    vec4 q3;
} p;

// --- Fonte do OSD --------------------------------------------------------------
// 5×7, uma linha de 5 bits por vez (bit 4 = coluna da esquerda): linhas 0..3
// em x, 4..6 em y. Índices: 0..9 dígitos, 10 ':', 11 P, 12 L, 13 A, 14 Y,
// 15 R, 16 E, 17 C, 18 U, 19 S, 20 ▶, 21 ●, 22 ❚❚; 23 em diante = espaço.
const uvec2 kVhsFont[23] = uvec2[23](
    uvec2(0x8c62eu, 0x3a31u),   //  0
    uvec2(0x21184u, 0x3884u),   //  1
    uvec2(0x1062eu, 0x7d04u),   //  2
    uvec2(0x1105fu, 0x3a21u),   //  3
    uvec2(0x928c2u, 0x085fu),   //  4
    uvec2(0x0fa1fu, 0x3a21u),   //  5
    uvec2(0xf4106u, 0x3a31u),   //  6
    uvec2(0x2083fu, 0x2108u),   //  7
    uvec2(0x7462eu, 0x3a31u),   //  8
    uvec2(0x7c62eu, 0x3041u),   //  9
    uvec2(0x03180u, 0x018cu),   // 10 :
    uvec2(0xf463eu, 0x4210u),   // 11 P
    uvec2(0x84210u, 0x7e10u),   // 12 L
    uvec2(0xfc62eu, 0x4631u),   // 13 A
    uvec2(0x22a31u, 0x1084u),   // 14 Y
    uvec2(0xf463eu, 0x4654u),   // 15 R
    uvec2(0xf421fu, 0x7e10u),   // 16 E
    uvec2(0x8422eu, 0x3a30u),   // 17 C
    uvec2(0x8c631u, 0x3a31u),   // 18 U
    uvec2(0x7420fu, 0x7821u),   // 19 S
    uvec2(0xf7310u, 0x431cu),   // 20 ▶
    uvec2(0xffdc0u, 0x01dfu),   // 21 ●
    uvec2(0xdef7bu, 0x6f7bu)    // 22 ❚❚
);

const int kSpace = 23;

float vhs_glyph_bit(int g, int col, int row) {
    if (g < 0 || g >= kSpace || col < 0 || col > 4 || row < 0 || row > 6) return 0.0;
    const uvec2 bits = kVhsFont[g];
    const uint rowBits = row < 4 ? (bits.x >> uint(row * 5)) : (bits.y >> uint((row - 4) * 5));
    return float((rowBits >> uint(4 - col)) & 1u);
}

/// Dois dígitos de um número 0..99: dezena (i = 0) ou unidade (i = 1).
int vhs_digit(float v, int i) {
    const int n = clamp(int(v + 0.5), 0, 99);
    return i == 0 ? n / 10 : n - (n / 10) * 10;
}

/// O caractere `i` de cada texto do OSD. 0..2 = PLAY ▶, REC ●, PAUSE ❚❚;
/// 3 = hh:mm:ss; 4..6 = SP, LP, EP.
int vhs_char(int text, int i) {
    if (text == 0) {   // P L A Y _ ▶
        if (i == 0) return 11; if (i == 1) return 12; if (i == 2) return 13;
        if (i == 3) return 14; if (i == 5) return 20; return kSpace;
    }
    if (text == 1) {   // R E C _ ●
        if (i == 0) return 15; if (i == 1) return 16; if (i == 2) return 17;
        if (i == 4) return 21; return kSpace;
    }
    if (text == 2) {   // P A U S E _ ❚❚
        if (i == 0) return 11; if (i == 1) return 13; if (i == 2) return 18;
        if (i == 3) return 19; if (i == 4) return 16; if (i == 6) return 22; return kSpace;
    }
    if (text == 3) {   // h h : m m : s s
        if (i == 2 || i == 5) return 10;
        if (i < 2) return vhs_digit(p.q2.x, i);
        if (i < 5) return vhs_digit(p.q2.y, i - 3);
        return vhs_digit(p.q2.z, i - 6);
    }
    // Velocidade da fita: S/L/E + P.
    if (i == 1) return 11;
    if (i == 0) return text == 4 ? 19 : (text == 5 ? 12 : 16);
    return kSpace;
}

/// Cobertura (0/1) de um texto de `count` caracteres com o canto de cima à
/// esquerda em `origin`. Cada caractere ocupa 6×7 pixels da fonte.
float vhs_text(vec2 pt, vec2 origin, int text, int count, float fp) {
    const vec2 g = (pt - origin) / fp;
    if (g.x < 0.0 || g.y < 0.0 || g.y >= 7.0 || g.x >= float(count * 6)) return 0.0;
    const int cx = int(floor(g.x));
    const int idx = cx / 6;
    return vhs_glyph_bit(vhs_char(text, idx), cx - idx * 6, int(floor(g.y)));
}

/// O OSD inteiro num ponto (px da camada).
float vhs_osd(vec2 pt, vec2 size, float fp) {
    const float mx = size.y * 0.085;
    const float my = size.y * 0.075;
    const float bottom = size.y - my - 7.0 * fp;
    float c = 0.0;
    const int overlay = int(p.q0.z + 0.5);
    if (overlay > 0) {
        const int count = overlay == 1 ? 6 : (overlay == 2 ? 5 : 7);
        c = max(c, vhs_text(pt, vec2(mx, my), overlay - 1, count, fp));
        // A velocidade da fita acompanha a sobreposição: sem ela, só o tempo.
        c = max(c, vhs_text(pt, vec2(mx, bottom), 4 + clamp(int(p.q1.x + 0.5), 0, 2), 2, fp));
    }
    if (p.q0.w > 0.5) {
        c = max(c, vhs_text(pt, vec2(size.x - mx - 47.0 * fp, bottom), 3, 8, fp));
    }
    return c;
}

// --- Hash ----------------------------------------------------------------------
float vhs_hash(uint a, uint b, uint c) { return aurea_hash(uvec2(a, b ^ (c * 0x9E3779B9u))); }

vec3 vhs_rgb(vec2 uv) { return unpremultiply(texture(u_tex0, uv)).rgb; }

void main() {
    const vec2 size = max(p.p3.xy, vec2(1.0));
    const vec2 pt = p.p2.xy + v_uv * p.p2.zw;                       // px da camada
    const vec2 uvPerPx = p.uvMap.xy / max(p.p2.zw, vec2(1e-6));     // uv de entrada por px
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const uint frame = uint(max(p.p3.z, 0.0));
    const uint seed = uint(max(p.q1.z, 0.0));
    const float secs = max(p.p3.w, 0.0);
    const float amount = clamp(p.p0.x, 0.0, 1.0);

    const vec4 src = texture(u_tex0, inUv);

    // As linhas da fita: 480 na altura da camada, qualquer que seja a resolução.
    const float lineH = max(size.y / 480.0, 1.0);
    const uint lineU = uint(int(floor(pt.y / lineH)) + 1048576);

    // --- 1. Tremor por linha: sorteio por linha e quadro + uma onda lenta.
    float dx = 0.0;
    const float jit = max(p.p1.z, 0.0);
    if (jit > 0.0) {
        dx += (vhs_hash(lineU, frame, seed ^ 0x2Au) * 2.0 - 1.0) * jit * 0.6;
        dx += sin(AUREA_TAU * (pt.y / size.y * 3.0 + fract(secs * 0.7))) * jit * 0.4;
    }

    // --- Faixas de tracking.
    const float trk = clamp(p.p1.y, 0.0, 1.0);
    float band = 0.0;
    if (trk > 0.0) {
        const float yN = pt.y / size.y;
        // Uma faixa que rola devagar pela imagem e pulsa.
        const float yA = fract(0.78 + secs * 0.09 + float(seed % 97u) * 0.0103);
        const float hA = 0.02 + 0.035 * trk;
        const float wA = smoothstep(hA, 0.0, abs(fract(yN - yA + 0.5) - 0.5))
                       * (0.45 + 0.55 * vhs_hash(uint(floor(secs * 6.0)), seed, 0x51u));
        // Uma ocasional: surge em alguns trechos, num lugar sorteado.
        const uint epoch = uint(floor(secs * 1.5));
        const float on = step(vhs_hash(epoch, seed, 0xB5u), 0.1 + 0.45 * trk);
        const float yB = vhs_hash(epoch, seed, 0x3Cu);
        const float hB = 0.008 + 0.04 * vhs_hash(epoch, seed, 0x77u);
        const float wB = on * smoothstep(hB, 0.0, abs(yN - yB));
        // Chaveamento das cabeças: as últimas linhas do quadro entortam.
        const float wC = smoothstep(0.972, 1.0, yN);
        const float wAB = max(wA, wB);
        band = clamp(max(wAB, wC), 0.0, 1.0) * trk;
        const float lr = vhs_hash(lineU, frame, seed ^ 0xA5u);
        dx += (wAB * (0.25 + 0.75 * lr) + wC * (0.5 + 0.5 * lr)) * trk * size.x * 0.035;
    }

    // --- 2 e 3. Luma suave, croma atrasada.
    const vec2 base = inUv + vec2(dx * uvPerPx.x, 0.0);
    const vec4 c0 = texture(u_tex0, base);
    const vec3 s0 = unpremultiply(c0).rgb;
    const float y0 = aurea_luma(s0);

    const float sx = max(p.p0.z, 0.0) * uvPerPx.x;
    float Y = y0;
    if (sx > 0.0) {
        Y = y0 * 0.36
          + (aurea_luma(vhs_rgb(base + vec2(sx * 0.5, 0.0))) + aurea_luma(vhs_rgb(base - vec2(sx * 0.5, 0.0)))) * 0.22
          + (aurea_luma(vhs_rgb(base + vec2(sx, 0.0))) + aurea_luma(vhs_rgb(base - vec2(sx, 0.0)))) * 0.10;
    }

    vec3 chroma = s0 - vec3(y0);
    const float bx = max(p.p0.y, 0.0) * uvPerPx.x;
    if (bx > 0.0) {
        // As três leituras vão SÓ para a esquerda, com um sorteio por linha
        // que desfaz a escada entre elas.
        const float j = vhs_hash(lineU, seed, 0xC3u);
        const vec3 s1 = vhs_rgb(base - vec2(bx * (0.0 + j) / 3.0, 0.0));
        const vec3 s2 = vhs_rgb(base - vec2(bx * (1.0 + j) / 3.0, 0.0));
        const vec3 s3 = vhs_rgb(base - vec2(bx * (2.0 + j) / 3.0, 0.0));
        const vec3 k0 = chroma;
        const vec3 k1 = s1 - vec3(aurea_luma(s1));
        const vec3 k2 = s2 - vec3(aurea_luma(s2));
        const vec3 k3 = s3 - vec3(aurea_luma(s3));
        // O vermelho atrasa mais que o azul: as bordas de cor se separam.
        const vec3 kr = k0 * 0.14 + k1 * 0.24 + k2 * 0.30 + k3 * 0.32;
        const vec3 kb = k0 * 0.34 + k1 * 0.30 + k2 * 0.22 + k3 * 0.14;
        chroma = vec3(kr.r, 0.5 * (kr.g + kb.g), kb.b);
    }
    vec3 enc = aurea_linear_to_srgb(max(vec3(Y) + chroma, vec3(0.0)));

    // --- 4. Tom: preto quente levantado, branco creme, meios-tons em âmbar.
    const float tone = clamp(p.p1.w, 0.0, 1.0);
    if (tone > 0.0) {
        enc = clamp(enc, 0.0, 1.0);
        const vec3 lo = vec3(0.090, 0.066, 0.050) * tone;
        const vec3 hi = mix(vec3(1.0), vec3(0.97, 0.93, 0.80), tone);
        enc = lo + enc * (hi - lo);
        enc = pow(max(enc, vec3(1e-5)), vec3(1.0 - 0.07 * tone, 1.0 + 0.01 * tone, 1.0 + 0.12 * tone));
    }
    const float yEnc = dot(enc, vec3(0.2126, 0.7152, 0.0722));
    enc = mix(vec3(yEnc), enc, clamp(p.q0.x, 0.0, 2.0));

    // --- 5. Vinheta.
    const float vig = clamp(p.q0.y, 0.0, 1.0);
    if (vig > 0.0) {
        const vec2 vn = (pt / size - 0.5) * 2.0;
        enc *= 1.0 - vig * 0.6 * smoothstep(0.5, 1.45, length(vn * vec2(1.0, 0.92)));
    }

    // --- Faixa de tracking: riscos brancos e chuvisco, o preto sobe.
    const float nAmt = clamp(p.p0.w, 0.0, 1.0);
    const float cell = max(size.y / 540.0, 1.0);    // o grão da fita
    if (band > 0.002) {
        const float snow = vhs_hash(uint(int(floor(pt.x / (cell * 1.5))) + 1048576), lineU, frame ^ seed);
        const float len = size.x * (0.015 + 0.09 * vhs_hash(lineU, frame, seed ^ 0x77u));
        const float streak = vhs_hash(uint(int(floor(pt.x / len)) + 4096), lineU, frame * 3u + seed);
        const float white = step(1.0 - 0.22 * band, streak) * (0.55 + 0.45 * snow)
                          + step(1.0 - 0.30 * band, snow) * 0.75;
        enc = mix(enc + 0.07 * band, vec3(0.93, 0.93, 0.90), clamp(white, 0.0, 1.0) * min(band * 1.6, 1.0));
    }

    // --- 6. OSD do videocassete, com um rastro curto para a direita.
    const float fp = p.q1.y;
    float osdA = 0.0;
    if (fp > 0.0 && (p.q0.z > 0.5 || p.q0.w > 0.5)) {
        const float core = vhs_osd(pt, size, fp);
        const float trail = 0.5 * (vhs_osd(pt - vec2(0.7 * fp, 0.0), size, fp)
                                 + vhs_osd(pt - vec2(1.5 * fp, 0.0), size, fp));
        const float shadow = vhs_osd(pt - vec2(0.45 * fp, 0.55 * fp), size, fp);
        const vec3 oc = aurea_linear_to_srgb(clamp(p.color.rgb, 0.0, 1.0));
        enc = mix(enc, enc * 0.45, shadow * (1.0 - core) * 0.55);
        enc = mix(enc, oc * vec3(1.0, 0.80, 0.74), trail * (1.0 - core) * 0.38);
        enc = mix(enc, oc, core);
        osdA = max(core, trail * 0.38);
    }

    // --- 7. Varredura e ruído por cima (o OSD também passa pela fita).
    const float scan = clamp(p.p1.x, 0.0, 1.0);
    if (scan > 0.0) {
        const float period = lineH * 2.0;
        // Abaixo de ~2 texels por período a listra vira moiré: some suave.
        const float fade = smoothstep(1.5, 3.0, period * max(p.q1.w, 1e-3));
        const float s = 0.5 - 0.5 * cos(AUREA_TAU * fract(pt.y / period));
        enc *= 1.0 - scan * 0.32 * s * fade;
    }
    if (nAmt > 0.0) {
        const uint fs = frame * 747796405u + seed;
        const float n = vhs_hash(uint(int(floor(pt.x / (cell * 2.0))) + 1048576), lineU, fs) - 0.5;
        const float nc = vhs_hash(uint(int(floor(pt.x / (cell * 7.0))) + 1048576), uint(int(floor(pt.y / (cell * 2.0))) + 1048576), fs ^ 0x5151u) - 0.5;
        const float dark = 1.0 - 0.5 * clamp(yEnc, 0.0, 1.0);   // a fita chia mais no escuro
        enc += vec3(n) * nAmt * 0.16 * dark;
        enc.r += nc * nAmt * 0.07;
        enc.b -= nc * nAmt * 0.07;
    }

    const vec3 lin = aurea_srgb_to_linear(clamp(enc, 0.0, 1.0));
    const vec4 orig = unpremultiply(src);
    const float alpha = max(mix(orig.a, c0.a, amount), osdA * amount);
    o_color = premultiply(vec4(mix(orig.rgb, lin, amount), alpha));
}
