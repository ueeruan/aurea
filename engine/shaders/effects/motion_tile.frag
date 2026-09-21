#version 450
// =============================================================================
//  Aurea / shaders / effects / motion_tile.frag
//
//  MOTION TILE — portado do Aurea antigo (`shaders/motion_tile.frag`), a ÚNICA
//  lógica de efeito que veio de lá. A grade, a fase, o espelho, o "esticar
//  bordas" e a trava de meio texel são os mesmos, conferidos contra renders do
//  After Effects (erro médio de 3–4/255, só de reamostragem).
//
//  O QUE MUDOU NO PORTE, e por quê:
//
//   - `p` (a coordenada normalizada na FONTE do pixel de saída) vinha de
//     `(uv-0.5)*uOutput+0.5`, que supunha a região de saída sempre centrada e
//     sempre inteira. Aqui ela vem de `uvMap`, calculado no C++ a partir do
//     retângulo que a textura de saída realmente cobre. O resultado é o mesmo
//     no caso centrado — e continua certo quando o motor recorta a região à
//     parte VISÍVEL do quadro (para não alocar 24x a layer quando ela está em
//     10% de escala).
//
//   - Sumiram `uFilter` (inversão do Impeller no GLES) e a ordem-de-declaração
//     dos uniforms (armadilha do `setFloat` do Flutter). Aqui é um bloco std140
//     com layout espelhado numa struct C++ com static_assert.
//
//  A CÓPIA CENTRAL SAI IDÊNTICA À FONTE: com ladrilho 100%, centro 0.5, fase 0
//  e sem espelho, `f == p` dentro de [0,1] — e a trava de meio texel é do
//  tamanho do texel DA ENTRADA, então não achata nenhum pixel da borda.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;     // p (coordenada normalizada na fonte) = v_uv * xy + zw
    vec4 tile;      // xy = tamanho do ladrilho (fração da fonte), zw = centro (fração da fonte)
    vec4 flags;     // x = espelhar, y = fase (voltas), z = deslocamento horizontal, w = esticar
    vec4 halfTexel; // xy = meio texel da textura de ENTRADA
} p;

void main() {
    vec2 pos = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 q = (pos - p.tile.zw) / p.tile.xy + 0.5;

    vec2 f;
    if (p.flags.w > 0.5) {
        // ESTICAR: não repete; a última coluna/linha puxa a cor da borda até o
        // fim. Vence o espelho quando os dois estão ligados.
        f = clamp(q, 0.0, 1.0);
    } else {
        if (p.flags.z > 0.5) q.x -= mod(floor(q.y), 2.0) * p.flags.y;
        else                 q.y -= mod(floor(q.x), 2.0) * p.flags.y;
        vec2 cell = floor(q);
        f = fract(q);
        if (p.flags.x > 0.5) f = mix(f, 1.0 - f, mod(cell, 2.0));
    }

    vec2 halfPixel = min(p.halfTexel.xy, vec2(0.5));
    vec2 sampleUv = clamp(f, halfPixel, 1.0 - halfPixel);
    o_color = texture(u_tex0, sampleUv);
}
