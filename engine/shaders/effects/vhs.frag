#version 450
// =============================================================================
//  Aurea / shaders / effects / vhs.frag  (Fase 7.3 §30)
//
//  VHS de verdade, não um filtro de listras. A fita faz SETE coisas ao mesmo
//  tempo, e é a soma delas que dá o aspecto:
//
//    1. borrão horizontal (a largura de banda da luma é menor que a da croma)
//    2. croma atrasada e alargada (o famoso vermelho/azul separado)
//    3. instabilidade de tracking: linhas inteiras deslizam
//    4. dropouts: riscos brancos onde a fita perdeu o óxido
//    5. varredura de cabeçote (as listras finas)
//    6. ruído de luma e de croma, em faixas
//    7. degradação de cor: os pretos sobem e as cores saturam
//
//  Cada um tem o seu controle, e todos podem ir a zero — um VHS só com
//  tracking é um VHS, e não um efeito quebrado.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = borrão (px), y = croma (px), z = tracking (px), w = dropouts (0..1)
    vec4 p1;   // x = varredura (0..1), y = ruído (0..1), z = degradação (0..1), w = sangramento (0..1)
    vec4 p2;   // x = semente, y = velocidade do tracking, z = altura do dropout (px), w = mistura
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

void main() {
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const float frame = p.p3.x;

    // --- 3. Tracking: faixas inteiras deslizam, em passos de tempo.
    const float trackStep = floor(frame / 3.0);
    const float trackBand = floor(inUv.y * 34.0);
    const float trackR = aurea_hash(uvec2(uint(int(trackBand)), uint(int(trackStep)) ^ uint(p.p2.x)));
    float track = 0.0;
    if (trackR > 0.72) track = (aurea_hash(uvec2(uint(int(trackBand)) + 11u, uint(int(trackStep)))) * 2.0 - 1.0) * p.p0.z;
    // Uma ondulação lenta por cima: a fita nunca está perfeitamente alinhada.
    track += sin(inUv.y * 9.0 + frame * p.p2.y * 0.15) * p.p0.z * 0.25;

    const vec2 base = inUv + vec2(track * uvPerLayer.x, 0.0);

    // --- 1 e 2. Luma borrada e croma alargada, em espaços diferentes.
    vec4 src = unpremultiply(texture(u_tex0, base));
    vec3 luma = vec3(0.0);
    vec3 chroma = vec3(0.0);
    float wsum = 0.0;
    const int taps = 7;
    for (int i = 0; i < 7; ++i) {
        const float t = (float(i) / 6.0 - 0.5);
        const float w = 1.0 - abs(t) * 1.6;
        if (w <= 0.0) continue;
        const vec2 o = vec2(t * p.p0.x * uvPerLayer.x, 0.0);
        const vec3 c = unpremultiply(texture(u_tex0, base + o)).rgb;
        // Separa luma e croma: a luma borra pouco, a croma muito.
        const float y = dot(c, vec3(0.2126, 0.7152, 0.0722));
        luma += vec3(y) * w;
        const vec2 oc = vec2(t * p.p0.y * uvPerLayer.x, 0.0);
        chroma += (unpremultiply(texture(u_tex0, base + oc)).rgb - vec3(y)) * w;
        wsum += w;
    }
    if (wsum > 0.0) { luma /= wsum; chroma /= wsum; }
    vec3 c = mix(src.rgb, luma + chroma, clamp(p.p1.w, 0.0, 1.0) * 0.85 + 0.15);

    // --- 4. Dropouts: riscos brancos curtos onde a fita perdeu o óxido.
    if (p.p0.w > 0.0) {
        const float row = floor(inUv.y / max(p.p2.z * p.texel.y, 1e-6));
        const float seedRow = aurea_hash(uvec2(uint(int(row)), uint(int(floor(frame / 2.0))) ^ 0x9E3779B9u));
        if (seedRow < p.p0.w * 0.25) {
            const float x0 = aurea_hash(uvec2(uint(int(row)) + 7u, uint(int(floor(frame / 2.0)))));
            const float wdt = 0.01 + seedRow * 0.15;
            const float d = abs(fract(inUv.x - x0 + 0.5) - 0.5);
            c = mix(c, vec3(0.85, 0.86, 0.82), smoothstep(wdt, 0.0, d) * clamp(p.p0.w * 3.0, 0.0, 1.0));
        }
    }

    // --- 5. Varredura de cabeçote.
    if (p.p1.x > 0.0) {
        const float yPx = inUv.y / max(p.texel.y, 1e-6) / max(p.texel.w, 1e-6);
        c *= 1.0 - p.p1.x * 0.35 * (0.5 + 0.5 * sin(yPx * AUREA_TAU / 3.0));
    }

    // --- 6. Ruído: em faixas, e mais forte no escuro (é onde a fita tem menos
    // sinal). O ruído de croma anda junto com o de luma, mas em outra fase.
    if (p.p1.y > 0.0) {
        const vec2 px = inUv / uvPerLayer;
        const float n = aurea_hash3(px * 0.6, uint(p.p2.x) ^ uint(int(frame))) * 2.0 - 1.0;
        const float nc = aurea_hash3(px * 0.25 + 91.0, uint(p.p2.x) ^ uint(int(frame)) ^ 0x51u) * 2.0 - 1.0;
        const float dark = 1.0 - clamp(dot(c, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0) * 0.6;
        c += vec3(n) * p.p1.y * 0.22 * dark;
        c.rb += vec2(nc) * p.p1.y * 0.10 * dark;
    }

    // --- 7. Degradação de cor: preto levantado, branco cortado, cor saturada.
    if (p.p1.z > 0.0) {
        const float k = clamp(p.p1.z, 0.0, 1.0);
        vec3 enc = aurea_linear_to_srgb(max(c, vec3(0.0)));
        enc = mix(enc, enc * 0.92 + 0.06, k);             // preto sobe
        enc = clamp(enc * (1.0 + 0.25 * k), 0.0, 0.96);   // branco corta
        const float y = dot(enc, vec3(0.2126, 0.7152, 0.0722));
        enc = mix(vec3(y), enc, 1.0 + 0.45 * k);          // satura
        c = aurea_srgb_to_linear(clamp(enc, 0.0, 1.0));
    }

    const float mixAmt = clamp(p.p2.w, 0.0, 1.0);
    o_color = premultiply(vec4(max(mix(src.rgb, c, mixAmt), vec3(0.0)), src.a));
}
