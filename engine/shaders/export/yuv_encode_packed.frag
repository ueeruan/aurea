#version 450
// =============================================================================
//  Aurea / shaders / export / yuv_encode_packed.frag
//
//  O MESMO Y'CbCr 4:2:0 do `yuv_encode.frag`, com 4 bytes por fragmento.
//
//  Medido no moto g52 (Adreno 610, 1080x1920): o passe Y custava 15,4 ms e o
//  CbCr 7,3 ms de um quadro de 28 ms — o custo era por FRAGMENTO (o CbCr lê os
//  mesmos texels com 1/4 dos fragmentos e custava a metade). Aqui cada
//  fragmento escreve 4 bytes seguidos da linha num alvo RGBA8 de 1/4 da
//  largura: a memória do alvo é byte a byte a do R8/RG8 de antes, a cópia para
//  o buffer de leitura é a mesma e a conta de cada pixel é a mesma (inclusive
//  o dither, calculado no centro do pixel original) — o arquivo não muda.
//
//    modo 0 (Y):    alvo (largura/4) x altura; o fragmento (x, y) escreve
//                   Y[4x..4x+3, y] em R, G, B, A;
//    modo 1 (CbCr): alvo (largura/4) x (altura/2); o fragmento (x, y)
//                   escreve Cb, Cr dos blocos 2x2 de croma 2x e 2x+1.
//
//  Exige largura múltipla de 4 (o motor usa o yuv_encode.frag nas outras).
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

vec3 encoded_rgb(ivec2 texel, ivec2 size) {
    vec4 c = texelFetch(u_tex0, clamp(texel, ivec2(0), size - 1), 0);
    return linear_to_srgb(clamp(c.rgb, 0.0, 1.0));
}

// Dither do pixel de saída `px` (o gl_FragCoord.xy que o passe de 1 byte via).
float dither_at(ivec2 px) {
    return p.cfg.y > 0.5 ? (interleaved_gradient_noise(vec2(px) + 0.5) - 0.5) / 255.0 : 0.0;
}

void main() {
    float kr = p.matrix.x;
    float kb = p.matrix.y;
    float kg = 1.0 - kr - kb;
    ivec2 size = textureSize(u_tex0, 0);
    ivec2 q = ivec2(gl_FragCoord.xy);

    if (p.cfg.x < 0.5) {
        vec4 outY;
        for (int i = 0; i < 4; ++i) {
            ivec2 px = ivec2(q.x * 4 + i, q.y);
            vec3 rgb = encoded_rgb(px, size);
            float y = kr * rgb.r + kg * rgb.g + kb * rgb.b;
            outY[i] = clamp((16.0 + 219.0 * y) / 255.0 + dither_at(px), 0.0, 1.0);
        }
        o_color = outY;
        return;
    }

    vec4 outC;
    for (int j = 0; j < 2; ++j) {
        ivec2 c = ivec2(q.x * 2 + j, q.y);   // pixel do plano CbCr (metade de cada lado)
        ivec2 base = c * 2;
        vec3 rgb = (encoded_rgb(base, size) + encoded_rgb(base + ivec2(1, 0), size) +
                    encoded_rgb(base + ivec2(0, 1), size) + encoded_rgb(base + ivec2(1, 1), size)) * 0.25;
        float y = kr * rgb.r + kg * rgb.g + kb * rgb.b;
        float cb = (rgb.b - y) / (2.0 * (1.0 - kb));
        float cr = (rgb.r - y) / (2.0 * (1.0 - kr));
        float d = dither_at(c);
        outC[2 * j] = clamp((128.0 + 224.0 * cb) / 255.0 + d, 0.0, 1.0);
        outC[2 * j + 1] = clamp((128.0 + 224.0 * cr) / 255.0 + d, 0.0, 1.0);
    }
    o_color = outC;
}
