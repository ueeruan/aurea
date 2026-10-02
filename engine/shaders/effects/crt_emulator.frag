#version 450
// =============================================================================
//  Aurea / shaders / effects / crt_emulator.frag
//
//  Emulador CRT: a tela de tubo inteira num passe.
//
//   1. Tela curva: cada eixo curva pelo quadrado do outro; o meio das bordas
//      fica no lugar e os cantos caem para fora (pretos, com borda suave).
//   2. Convergência: vermelho e azul lidos deslocados em sentidos opostos.
//   3. Brilho: 16 amostras em volta; o que passa do limiar vaza por cima.
//   4. Linhas de varredura na posição DA FONTE (curvam com a tela), mais
//      largas onde a imagem é clara; somem suavemente quando a densidade
//      passa do que a resolução de trabalho mostra (sem moiré no preview).
//   5. Máscara de fósforo na posição da TELA: grade de abertura (colunas
//      RGB), máscara de sombra (pontos em triângulo) ou fenda (colunas com
//      quebras alternadas).
//   6. Vinheta, cintilação por quadro, faixa clara rolando, chiado, brilho e
//      contraste — no valor codificado, que é o que o olho julga.
//
//  Tempo: `screen.z` = segundos, `screen.w` = quadro local, `d.x` = posição
//  da faixa (0..1). Nada de estado: o mesmo quadro sai igual no export.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;    // x = 1/largura, y = 1/altura, z/w = texels por px da camada
    vec4 region;   // região da entrada/saída em px da camada (x, y, w, h)
    vec4 screen;   // x/y = tamanho da camada (px), z = segundos, w = quadro
    vec4 a;        // x = curvatura 0..1, y = linhas 0..1, z = densidade (linhas), w = máscara (0..3)
    vec4 b;        // x = máscara 0..1, y = tamanho da máscara (px), z = vinheta 0..1, w = convergência (px)
    vec4 c;        // x = brilho 0..1, y = raio do brilho (px), z = cintilação 0..1, w = faixa 0..1
    vec4 d;        // x = posição da faixa 0..1, y = ruído 0..1, z = brilho geral -1..1, w = contraste -1..1
    vec4 f;        // x = mistura
} p;

vec4 tap(vec2 layerPx) {
    return texture(u_tex0, ((layerPx - p.region.xy) / p.region.zw) * p.uvMap.xy + p.uvMap.zw);
}

void main() {
    const vec2 layerPx = p.region.xy + v_uv * p.region.zw;
    const vec2 size = max(p.screen.xy, vec2(1.0));
    const vec4 original = tap(layerPx);

    // 1. Tela curva.
    const vec2 q = layerPx / size * 2.0 - 1.0;
    const float k = clamp(p.a.x, 0.0, 1.0) * 0.45;
    const vec2 sq = q * (1.0 + k * q.yx * q.yx);
    const vec2 s = sq * 0.5 + 0.5;               // 0..1 na fonte
    const vec2 srcPx = s * size;
    // Borda da tela com ~1,5 texel de suavidade.
    const vec2 texelsPerUnit = size * max(p.texel.zw, vec2(1e-6)) * 0.5;
    const vec2 edge = clamp((1.0 - abs(sq)) * texelsPerUnit / 1.5, 0.0, 1.0);
    const float inside = edge.x * edge.y;

    // 2. Convergência.
    const vec2 conv = vec2(p.b.w, 0.0);
    const vec4 cr = tap(srcPx + conv);
    const vec4 cg = tap(srcPx);
    const vec4 cb = tap(srcPx - conv);
    float alpha = max(cg.a, max(cr.a, cb.a));
    vec3 col = vec3(cr.r, cg.g, cb.b) / max(alpha, 1e-5);

    // 3. Brilho das áreas claras.
    if (p.c.x > 0.001 && p.c.y > 0.01) {
        vec3 acc = vec3(0.0);
        for (int i = 0; i < 16; ++i) {
            const float ang = float(i) * (AUREA_TAU / 8.0) + (i >= 8 ? 0.3927 : 0.0);
            const float rad = p.c.y * (i >= 8 ? 1.0 : 0.5);
            acc += tap(srcPx + vec2(cos(ang), sin(ang)) * rad).rgb;
        }
        acc /= 16.0;
        col += max(acc - vec3(0.35), vec3(0.0)) * (p.c.x * 1.6);
    }

    vec3 enc = aurea_linear_to_srgb(max(col, vec3(0.0)));

    // 4. Linhas de varredura.
    const float density = max(p.a.z, 1.0);
    const float texelsPerLine = size.y * max(p.texel.w, 1e-6) / density;
    const float lineFade = smoothstep(1.2, 2.6, texelsPerLine);
    const float sc = 0.5 + 0.5 * cos(AUREA_TAU * s.y * density);
    const float lum = dot(enc, vec3(0.2126, 0.7152, 0.0722));
    enc *= 1.0 - clamp(p.a.y, 0.0, 1.0) * lineFade * (1.0 - sc) * (1.0 - 0.55 * lum);

    // 5. Máscara de fósforo.
    const int maskType = int(p.a.w + 0.5);
    const float mi = clamp(p.b.x, 0.0, 1.0);
    if (maskType > 0 && mi > 0.001) {
        const float ms = max(p.b.y, 0.5);
        const float maskFade = smoothstep(0.8, 1.8, ms * max(p.texel.z, 1e-6));
        const vec2 mp = layerPx / ms;
        float colIdx;
        float gap = 0.0;
        if (maskType == 2) {
            // Máscara de sombra: fileiras de pontos defasadas meia tríade.
            const float row = floor(mp.y);
            colIdx = floor(mp.x + mod(row, 2.0) * 1.5);
            gap = smoothstep(0.35, 0.5, abs(fract(mp.y) - 0.5));
        } else {
            colIdx = floor(mp.x);
            if (maskType == 3) {
                // Fenda: colunas com quebra a cada 3 px, alternada por tríade.
                const float triad = floor(mp.x / 3.0);
                const float yy = fract((mp.y + mod(triad, 2.0) * 1.5) / 3.0);
                gap = step(0.84, yy);
            }
        }
        const int ch = int(mod(colIdx, 3.0));
        vec3 m = vec3(1.0 - mi);
        if (ch == 0) m.r = 1.0; else if (ch == 1) m.g = 1.0; else m.b = 1.0;
        m *= 1.0 - gap * mi * 0.6;
        // A máscara escurece em média 2/3·intensidade: devolve parte disso.
        const vec3 masked = m / (1.0 - mi * 0.45);
        enc *= mix(vec3(1.0), masked, maskFade);
    }

    // 6. Vinheta, cintilação, faixa, ruído, brilho e contraste.
    const vec2 vs = clamp(s, 0.0, 1.0);
    const float vig = clamp(16.0 * vs.x * (1.0 - vs.x) * vs.y * (1.0 - vs.y), 0.0, 1.0);
    enc *= mix(1.0, pow(vig, 0.45), clamp(p.b.z, 0.0, 1.0));

    const uint frame = uint(max(p.screen.w, 0.0));
    const float fh = aurea_hash(uvec2(frame, 0x51F3u));
    enc *= 1.0 + clamp(p.c.z, 0.0, 1.0) * 0.16 * (fh * 2.0 - 1.0);

    float dy = fract(s.y - p.d.x + 0.5) - 0.5;
    const float bar = exp(-(dy * dy) / (0.09 * 0.09));
    enc += vec3(clamp(p.c.w, 0.0, 1.0) * 0.16 * bar);

    if (p.d.y > 0.001) {
        const vec2 texelPos = floor(layerPx * max(p.texel.zw, vec2(1e-6)));
        const float n = aurea_hash(uvec2(ivec2(texelPos)) ^ uvec2(frame * 0x9E3779B9u, frame * 0x85EBCA6Bu + 7u));
        enc += vec3((n - 0.5) * clamp(p.d.y, 0.0, 1.0) * 0.3);
    }

    enc += vec3(p.d.z * 0.35);
    enc = (enc - 0.5) * (1.0 + clamp(p.d.w, -1.0, 1.0)) + 0.5;

    vec3 lin = aurea_srgb_to_linear(clamp(enc, 0.0, 1.0));
    // Fora da tela curva: o vidro preto, com a cobertura da própria camada.
    lin *= inside;
    alpha = mix(original.a, alpha, inside);
    const vec4 crt = premultiply(vec4(lin, clamp(alpha, 0.0, 1.0)));
    o_color = mix(original, crt, clamp(p.f.x, 0.0, 1.0));
}
