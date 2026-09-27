#version 450
// =============================================================================
//  Aurea / shaders / effects / shape_wipe.frag
//
//  Transições por FORMA: íris (círculo ou polígono), caixa e persianas.
//
//  Cada pixel ganha um "limiar" em 0..1 — a ordem em que ele some. A conclusão
//  anda de 0 a 1 e esconde quem tem limiar abaixo dela; a suavidade vira uma
//  rampa em volta da borda. É a mesma regra das varreduras linear e radial:
//  0% e 100% são EXATOS (inteiro e vazio), qualquer que seja a suavidade.
//
//  Coordenadas no plano da camada (px da resolução cheia), então a prévia em
//  1/4 e o export em 4K cortam no mesmo lugar. A cor fica pré-multiplicada:
//  só se escala, nunca se divide pelo alfa.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = conclusão 0..1, y = suavidade 0..1, z = 1 inverte, w = forma (0 íris, 1 caixa, 2 persianas)
    vec4 p1;   // xy = centro (px da camada), z = lados da íris / faixas, w = rotação (rad)
    vec4 p2;   // região da imagem (px da camada): x, y, largura, altura
    vec4 p3;   // xy = tamanho natural da camada (px)
    vec4 color;
} p;

/// Distância "da forma": 1 na borda de uma forma de tamanho 1.
float shape_metric(vec2 v, int mode, float sides, vec2 size) {
    if (mode == 1) {
        // Caixa com a proporção da camada: os quatro cantos chegam juntos.
        return max(abs(v.x) / max(size.x, 1.0), abs(v.y) / max(size.y, 1.0));
    }
    const int n = int(sides + 0.5);
    if (n < 3) return length(v);
    // Polígono regular pela apótema: todo ponto da borda tem a mesma medida.
    const float sect = AUREA_TAU / float(n);
    const float a = atan(v.y, v.x);
    return length(v) * cos(mod(a + AUREA_TAU, sect) - sect * 0.5);
}

void main() {
    const vec4 source = texture(u_tex0, v_uv);
    const float completion = p.p0.x;
    if (completion <= 0.0) { o_color = source; return; }
    if (completion >= 1.0) { o_color = vec4(0.0); return; }

    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const vec2 size = max(p.p3.xy, vec2(1.0));
    const int mode = int(p.p0.w + 0.5);
    const mat2 unrotate = aurea_rot2(p.p1.w);   // gira o ponto para o referencial da forma

    float threshold;
    if (mode == 2) {
        // Persianas: faixas paralelas perpendiculares à direção, todas fecham
        // do mesmo lado ao mesmo tempo.
        const vec2 axis = vec2(cos(p.p1.w), sin(p.p1.w));
        const float span = dot(abs(axis), size);
        const float band = span / max(p.p1.z, 1.0);
        threshold = fract(dot(point - size * 0.5, axis) / max(band, 1e-3));
    } else {
        const vec2 center = p.p1.xy;
        const vec2 halfSize = size * 0.5;
        const float here = shape_metric(unrotate * (point - center), mode, p.p1.z, halfSize);
        // Normaliza pelo canto mais distante: com 0% a forma cobre a camada
        // inteira, onde quer que esteja o centro.
        float reach = 0.0;
        reach = max(reach, shape_metric(unrotate * (vec2(0.0, 0.0) - center), mode, p.p1.z, halfSize));
        reach = max(reach, shape_metric(unrotate * (vec2(size.x, 0.0) - center), mode, p.p1.z, halfSize));
        reach = max(reach, shape_metric(unrotate * (vec2(0.0, size.y) - center), mode, p.p1.z, halfSize));
        reach = max(reach, shape_metric(unrotate * (size - center), mode, p.p1.z, halfSize));
        // O centro some por último: fecha de fora para dentro.
        threshold = 1.0 - clamp(here / max(reach, 1e-3), 0.0, 1.0);
    }
    if (p.p0.z > 0.5) threshold = 1.0 - threshold;

    const float feather = p.p0.y * 0.5;
    const float edge = mix(-feather, 1.0 + feather, completion);
    const float keep = feather > 0.00001 ? smoothstep(edge - feather, edge + feather, threshold)
                                         : step(edge, threshold);
    o_color = source * keep;
}
