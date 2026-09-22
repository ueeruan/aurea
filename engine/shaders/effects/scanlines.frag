#version 450
// =============================================================================
//  Aurea / shaders / effects / scanlines.frag
//
//  Varredura de tela (Fase 7.3 §45). Não é uma tira escura por cima: a linha
//  escurece o que está ATRÁS dela, com contraste próprio, largura em pixels e
//  deslocamento — que é o que separa uma varredura de um papel de parede.
//
//  A fase anda com o tempo (`p3.x` = quadro local), então a varredura corre
//  sozinha quando animada — e fica parada quando ninguém anima.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = altura da linha (px), y = intensidade (0..1), z = deslocamento (px), w = suavidade (px)
    vec4 p1;   // x = contraste, y = fase (quadros), z = canal: 0 todos, 1 só R, 2 só G, 3 só B
    vec4 p2;   // x = desfoque vertical (px), y = rolagem (px), zw livres
    vec4 p3;
    vec4 color;
} p;

void main() {
    // A rolagem desloca a imagem verticalmente: a varredura "prende" na tela
    // enquanto a imagem anda, como a sincronia vertical fora de ajuste.
    // A rolagem é medida em pixels da LAYER, não da textura: assim o preview
    // em 1/4 e o export em 4K rolam a mesma distância.
    vec2 uv = vec2(v_uv.x, fract(v_uv.y + p.p2.y * p.texel.y * max(p.texel.w, 1e-6)));
    vec4 src = unpremultiply(texture(u_tex0, uv * p.uvMap.xy + p.uvMap.zw));
    vec3 c = src.rgb;
    float alpha = src.a;

    // Coordenada em pixels da LAYER: a altura da linha não pode depender da
    // resolução de trabalho (era o defeito de medir em texels).
    const float yPix = v_uv.y / max(p.texel.y, 1e-6) / max(p.texel.w, 1e-6);
    const float period = max(p.p0.x, 0.5);
    const float phase = yPix - p.p0.z - p.p1.y;

    // Onda triangular 0..1 dentro do período: 1 no meio da linha.
    const float t = fract(phase / period);
    float line = 1.0 - abs(t * 2.0 - 1.0);
    line = pow(line, max(p.p1.w, 0.01));   // suavidade: 1 = triangular, >1 = estreita

    // Desfoque vertical: uma varredura de tubo não tem borda dura.
    const float blur = p.p2.x;
    if (blur > 0.01) {
        float acc = line, wsum = 1.0;
        for (int i = 1; i <= 3; ++i) {
            const float o = float(i) * blur / 3.0;
            const float wgt = 1.0 - float(i) / 4.0;
            const float t1 = 1.0 - abs(fract((phase - o) / period) * 2.0 - 1.0);
            const float t2 = 1.0 - abs(fract((phase + o) / period) * 2.0 - 1.0);
            acc += wgt * (pow(clamp(t1, 0.0, 1.0), max(p.p1.w, 0.01)) + pow(clamp(t2, 0.0, 1.0), max(p.p1.w, 0.01)));
            wsum += 2.0 * wgt;
        }
        line = acc / wsum;
    }

    // A varredura MULTIPLICA: onde a linha está cheia, o pixel vira
    // `pixel × cor`. Com a cor quase preta (o padrão) isso é a linha escura
    // clássica; com a cor quente, o risquinho de fósforo do CRT.
    const float k = clamp(p.p0.y, 0.0, 1.0) * line;
    vec3 shaded = c * mix(vec3(1.0), max(p.color.rgb, vec3(0.0)), k);

    // Canal: a varredura pode agir só numa cor (o vermelho separado do CRT).
    const int ch = int(p.p1.z + 0.5);
    vec3 outc = c;
    if (ch == 0)      outc = shaded;
    else if (ch == 1) outc = vec3(shaded.r, c.g, c.b);
    else if (ch == 2) outc = vec3(c.r, shaded.g, c.b);
    else              outc = vec3(c.r, c.g, shaded.b);

    // Contraste por cima, em torno do cinza médio codificado.
    if (abs(p.p1.x) > 1e-4) {
        vec3 enc = aurea_linear_to_srgb(outc);
        enc = (enc - 0.5) * (1.0 + p.p1.x) + 0.5;
        outc = aurea_srgb_to_linear(clamp(enc, 0.0, 1.0));
    }
    o_color = premultiply(vec4(max(outc, vec3(0.0)), alpha));
}
