#version 450
// =============================================================================
//  Aurea / shaders / effects / stroke_outline.frag
//
//  Contorno da silhueta, passe 2: fecha a distância euclidiana à silhueta
//  (min sobre a coluna de R² + dy² do passe 1) com sinal — negativa dentro,
//  positiva fora — e pinta a faixa do traço: fora [0, largura], centro
//  [−largura/2, largura/2], dentro [−largura, 0]. Um pixel de antialiasing
//  em cada borda da faixa, mais a suavidade.
//
//  Composição: o que está fora fica ATRÁS da camada (o traço não cobre a
//  borda antialiasada dela); o que está dentro fica por cima.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // distâncias do passe 1 (R opaco, G transparente)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a camada

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;   // xy = 1/tamanho da textura de distâncias, zw = texels por px desta saída
    vec4 p0;      // x = amostras para cada lado (texels de distância), y = px por texel de distância, z = largura (px), w = suavidade (px)
    vec4 p1;      // x = posição (0 fora, 1 centro, 2 dentro), y = opacidade, z = alcance (px)
    vec4 p2;      // região da ENTRADA (px da camada)
    vec4 p3;      // região da SAÍDA (px da camada)
    vec4 color;   // cor do traço (linear, alfa reto)
} p;

void main() {
    const vec2 point = p.p3.xy + v_uv * p.p3.zw;
    const int taps = int(clamp(p.p0.x, 1.0, 48.0));
    const float px = max(p.p0.y, 1e-4);
    const float far = max(p.p1.z, 1.0) * 4.0;
    float outSq = far * far;
    float inSq = far * far;
    for (int dy = -48; dy <= 48; ++dy) {
        if (dy < -taps || dy > taps) continue;
        const vec2 uv = v_uv + vec2(0.0, float(dy) * p.texel.y);
        if (uv.y < 0.0 || uv.y > 1.0) continue;
        const vec2 d = texture(u_tex0, uv).rg;
        // Cada texel é um quadrado de meio texel para cada lado: a distância
        // vertical é até a BORDA dele, como a horizontal do passe 1.
        const float dyPx = max(abs(float(dy)) - 0.5, 0.0) * px;
        outSq = min(outSq, d.x * d.x + dyPx * dyPx);
        inSq = min(inSq, d.y * d.y + dyPx * dyPx);
    }

    const vec2 layerUv = (point - p.p2.xy) / max(p.p2.zw, vec2(1e-4));
    const bool insideImage = all(greaterThanEqual(layerUv, vec2(0.0))) && all(lessThanEqual(layerUv, vec2(1.0)));
    const vec4 layer = insideImage ? texture(u_tex1, layerUv) : vec4(0.0);
    // Distância com sinal: dentro da silhueta é −(distância ao transparente).
    const float sd = layer.a >= 0.5 ? -sqrt(inSq) : sqrt(outSq);

    const float width = max(p.p0.z, 0.0);
    const int position = int(p.p1.x + 0.5);
    const float center = position == 0 ? 0.5 * width : (position == 1 ? 0.0 : -0.5 * width);
    // Um pixel da camada de antialiasing, mais a suavidade pedida.
    const float aa = 1.0 / max(p.texel.z, 1e-4) + max(p.p0.w, 0.0);
    const float cov = aurea_band_coverage(sd - center, 0.5 * width, aa) * clamp(p.p1.y, 0.0, 1.0) * p.color.a;

    const vec4 stroke = vec4(p.color.rgb, 1.0) * cov;
    const vec4 behind = layer + stroke * (1.0 - layer.a);   // fora: atrás da camada
    const vec4 over = stroke + layer * (1.0 - cov);         // dentro: por cima
    const float inside = clamp(0.5 - sd * p.texel.z, 0.0, 1.0);
    o_color = mix(behind, over, inside);
}
