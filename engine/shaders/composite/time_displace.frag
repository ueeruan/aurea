#version 450
// =============================================================================
//  Aurea / shaders / composite / time_displace.frag
//
//  Um NÍVEL do Deslocamento no tempo (o Time Displacement do AE): a mesma
//  amostragem de composite/layer.frag, com o peso que este pixel dá a este
//  instante. O renderer desenha a camada uma vez por nível (cada um com a
//  textura do SEU instante, de −máx a +máx) somando no acumulador temporal.
//
//  O brilho do mapa diz o instante: L = 0 (preto) = −máx, 0,5 (cinza) = agora,
//  1 (branco) = +máx. Com N níveis, o pixel fica no nível f = L·(N−1):
//    sem suavizar, peso 1 no nível mais próximo (round(f));
//    suavizando, a tenda max(0, 1 − |f − nível|) — os dois níveis vizinhos
//    misturados, pesos somando 1.
//  O brilho é o do valor codificado (o cinza 50% da tela, não o linear) e o
//  transparente do mapa conta como cinza (sem deslocamento).
//
//  Mapas: 0 = camada do mapa (do tamanho da composição, lida no pixel da
//  composição), 1 = a própria camada no quadro de agora (no uv da camada),
//  2/3/4 = degradê horizontal/vertical/radial na caixa da camada.
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;   // x = mapa, y = este nível, z = número de níveis, w = opções (1 inverter, 2 suavizar, 4 mapa na composição)
    vec4 slice;    // sem uso
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada neste instante
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // o mapa

float encoded(float c) {
    c = max(c, 0.0);
    return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

float map_luma(vec4 c) {
    if (c.a <= 1e-4) return 0.5;
    const vec3 straight = c.rgb / c.a;
    const float l = clamp(encoded(dot(straight, vec3(0.2126, 0.7152, 0.0722))), 0.0, 1.0);
    return mix(0.5, l, clamp(c.a, 0.0, 1.0));
}

void main() {
    const int mode = int(pc.params.x + 0.5);
    const int flags = int(pc.params.w + 0.5);
    float lum;
    if (mode == 0 && (flags & 4) != 0) lum = map_luma(texelFetch(u_tex1, ivec2(gl_FragCoord.xy), 0));
    else if (mode == 2) lum = clamp(v_uv.x, 0.0, 1.0);
    else if (mode == 3) lum = clamp(v_uv.y, 0.0, 1.0);
    else if (mode == 4) lum = clamp(length(v_uv - vec2(0.5)) * 1.41421356, 0.0, 1.0);
    else lum = map_luma(texture(u_tex1, v_uv));
    if ((flags & 1) != 0) lum = 1.0 - lum;

    const float count = max(pc.params.z, 2.0);
    const float f = lum * (count - 1.0);
    float weight;
    if ((flags & 2) != 0) weight = max(0.0, 1.0 - abs(f - pc.params.y));
    else weight = abs(floor(f + 0.5) - pc.params.y) < 0.5 ? 1.0 : 0.0;
    if (weight <= 0.0) discard;
    o_color = texture(u_tex0, v_uv) * weight;
}
