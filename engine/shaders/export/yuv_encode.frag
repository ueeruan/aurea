#version 450
// =============================================================================
//  Aurea / shaders / export / yuv_encode.frag
//
//  Composição (linear, pré-multiplicada, já com o fundo da composição) →
//  Y'CbCr 4:2:0 de 8 bits, faixa limitada, para o encoder.
//
//  A curva é a INVERSA EXATA da que o decode aplica em vídeo SDR
//  (`decode_transfer` → sRGB): o arquivo exportado tem os mesmos códigos que o
//  preview mostra, e reimportar o export devolve a mesma cor.
//
//  Dois passes com o mesmo shader:
//    modo 0 (Y):    alvo R8 do tamanho do vídeo; um texel da composição por
//                   pixel (texelFetch — nenhum filtro mexe no valor);
//    modo 1 (CbCr): alvo RG8 com metade de cada lado; média dos 4 texels R'G'B'
//                   do bloco 2×2 (croma no centro do bloco, o "MPEG-1" — o que
//                   os encoders de hardware esperam de uma entrada NV12).
//
//  Dither de 1/2 LSB antes de quantizar: um degradê de céu que no preview fica
//  liso vira faixa no arquivo de 8 bits sem ele. Desligado nos testes.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 cfg;      // x = modo (0 Y, 1 CbCr), y = dither (0/1)
    vec4 matrix;   // x = kr, y = kb (BT.709: .2126 / .0722)
} p;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

float interleaved_gradient_noise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// R'G'B' codificado de um texel da composição. Fundo opaco atrás do que ficou
// transparente (a composição já traz a cor de fundo dela; isto só cobre
// "fundo transparente", que um MP4 não guarda).
vec3 encoded_rgb(ivec2 texel) {
    ivec2 size = textureSize(u_tex0, 0);
    vec4 c = texelFetch(u_tex0, clamp(texel, ivec2(0), size - 1), 0);
    return linear_to_srgb(clamp(c.rgb, 0.0, 1.0));
}

void main() {
    float kr = p.matrix.x;
    float kb = p.matrix.y;
    float kg = 1.0 - kr - kb;
    float dither = p.cfg.y > 0.5 ? (interleaved_gradient_noise(gl_FragCoord.xy) - 0.5) / 255.0 : 0.0;
    ivec2 px = ivec2(gl_FragCoord.xy);

    if (p.cfg.x < 0.5) {
        vec3 rgb = encoded_rgb(px);
        float y = kr * rgb.r + kg * rgb.g + kb * rgb.b;
        o_color = vec4(clamp((16.0 + 219.0 * y) / 255.0 + dither, 0.0, 1.0), 0.0, 0.0, 1.0);
        return;
    }

    ivec2 base = px * 2;
    vec3 rgb = (encoded_rgb(base) + encoded_rgb(base + ivec2(1, 0)) +
                encoded_rgb(base + ivec2(0, 1)) + encoded_rgb(base + ivec2(1, 1))) * 0.25;
    float y = kr * rgb.r + kg * rgb.g + kb * rgb.b;
    float cb = (rgb.b - y) / (2.0 * (1.0 - kb));
    float cr = (rgb.r - y) / (2.0 * (1.0 - kr));
    o_color = vec4(clamp((128.0 + 224.0 * cb) / 255.0 + dither, 0.0, 1.0),
                   clamp((128.0 + 224.0 * cr) / 255.0 + dither, 0.0, 1.0), 0.0, 1.0);
}
