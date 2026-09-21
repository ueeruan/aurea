// =============================================================================
//  Aurea / shaders / common / color.glsl
//
//  O Color Pipeline do Aurea, do lado da GPU.
//
//  ESPAÇO DE TRABALHO: RGB LINEAR, primárias BT.709, alfa PRÉ-MULTIPLICADO,
//  RGBA16F. Tudo entre a conversão do vídeo e o passe de saída vive aqui.
//
//  A CURVA SDR É A MESMA NA ENTRADA E NA SAÍDA (sRGB IEC 61966-2-1). É uma
//  decisão, não um descuido: um vídeo sem efeito nenhum tem que sair com os
//  MESMOS códigos que entrou — no preview e no export. Usar a OETF do BT.709
//  para linearizar e a do sRGB para codificar desloca o contraste de todo
//  vídeo importado, e o usuário vê o clipe "diferente do original" sem ter
//  mexido em nada. O teste `ColorPipeline.RoundTripSdr` trava isso.
// =============================================================================
#ifndef AUREA_COLOR_GLSL
#define AUREA_COLOR_GLSL

// Curvas de transferência — os valores batem com `TransferFunction` no C++.
#define AUREA_TF_SRGB    0   // sRGB / BT.709 / BT.601 / não informado (SDR)
#define AUREA_TF_LINEAR  1
#define AUREA_TF_PQ      2   // SMPTE ST 2084 (HDR10)
#define AUREA_TF_HLG     3   // ARIB STD-B67

// Primárias — batem com `ColorPrimaries` no C++.
#define AUREA_PRIM_BT709   0
#define AUREA_PRIM_BT2020  1
#define AUREA_PRIM_P3      2
#define AUREA_PRIM_BT601   3

// Branco de referência para HDR: 203 nits = 1.0 no espaço de trabalho
// (ITU-R BT.2408). É o que deixa um vídeo HDR com o mesmo "branco de papel"
// de um SDR ao lado dele na timeline.
const float kHdrReferenceWhiteNits = 203.0;

float srgb_to_linear1(float c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
float linear_to_srgb1(float c) {
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}
vec3 srgb_to_linear(vec3 c) {
    return vec3(srgb_to_linear1(c.r), srgb_to_linear1(c.g), srgb_to_linear1(c.b));
}
vec3 linear_to_srgb(vec3 c) {
    c = max(c, vec3(0.0));
    return vec3(linear_to_srgb1(c.r), linear_to_srgb1(c.g), linear_to_srgb1(c.b));
}

// PQ (ST 2084): código → nits absolutos.
vec3 pq_to_nits(vec3 e) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    vec3 p = pow(clamp(e, 0.0, 1.0), vec3(1.0 / m2));
    vec3 num = max(p - c1, vec3(0.0));
    vec3 den = c2 - c3 * p;
    return 10000.0 * pow(num / den, vec3(1.0 / m1));
}

// HLG (BT.2100): código → luz de cena relativa, e OOTF para 1000 nits.
vec3 hlg_to_nits(vec3 e) {
    const float a = 0.17883277;
    const float b = 0.28466892;
    const float c = 0.55991073;
    vec3 scene;
    for (int i = 0; i < 3; ++i) {
        float v = clamp(e[i], 0.0, 1.0);
        scene[i] = v <= 0.5 ? (v * v) / 3.0 : (exp((v - c) / a) + b) / 12.0;
    }
    // OOTF com gamma 1.2 (display de 1000 nits), aplicada na luminância.
    float ys = dot(scene, vec3(0.2627, 0.6780, 0.0593));
    return 1000.0 * scene * pow(max(ys, 1e-6), 0.2);
}

// Tone mapping HDR → SDR para o preview e para export SDR. Curva de Reinhard
// estendida NA LUMINÂNCIA (preserva matiz; aplicar por canal desbota o
// vermelho e o azul saturados). `peak` é o pico do conteúdo em unidades de
// trabalho.
vec3 tonemap_to_sdr(vec3 rgb, float peak) {
    float l = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    if (l <= 0.0) return rgb;
    float w2 = peak * peak;
    float lt = l * (1.0 + l / w2) / (1.0 + l);
    return rgb * (lt / l);
}

// Primárias → BT.709, em linear. Coeficientes de BT.2087 e SMPTE EG 432-1.
vec3 primaries_to_bt709(vec3 rgb, int primaries) {
    if (primaries == AUREA_PRIM_BT2020) {
        return mat3( 1.6605, -0.1246, -0.0182,
                    -0.5876,  1.1329, -0.1006,
                    -0.0728, -0.0083,  1.1187) * rgb;
    }
    if (primaries == AUREA_PRIM_P3) {
        return mat3( 1.2249, -0.0421, -0.0196,
                    -0.2247,  1.0421, -0.0786,
                     0.0000,  0.0000,  1.0982) * rgb;
    }
    // BT.601 (SMPTE 170M) e BT.709 diferem pouco o bastante para que a
    // conversão de primárias seja, na prática, identidade — é o que os
    // players fazem, e é o que o usuário espera ver.
    return rgb;
}

// Linearização para o espaço de trabalho.
vec3 decode_transfer(vec3 encoded, int tf) {
    if (tf == AUREA_TF_LINEAR) return encoded;
    if (tf == AUREA_TF_PQ) return pq_to_nits(encoded) / kHdrReferenceWhiteNits;
    if (tf == AUREA_TF_HLG) return hlg_to_nits(encoded) / kHdrReferenceWhiteNits;
    return srgb_to_linear(encoded);
}

// Y'CbCr → R'G'B'. Recebe os CÓDIGOS normalizados (0..1) exatamente como
// estão no arquivo: sem expansão de faixa e sem matriz — quem faz as duas
// contas é esta função, com os metadados do próprio vídeo. A conversão YCbCr
// do Vulkan é configurada como identidade de propósito: o driver só
// reconstrói o croma; a matemática de cor é do Aurea, idêntica em todo
// aparelho.
//
//   kr, kb      coeficientes da matriz (BT.601: .299/.114, BT.709: .2126/.0722,
//               BT.2020: .2627/.0593)
//   fullRange   faixa completa (0..255) ou limitada (16..235 / 16..240)
//   bitDepth    8, 10 ou 12
vec3 ycbcr_to_rgb(vec3 ycc, float kr, float kb, bool fullRange, float bitDepth) {
    float maxCode = exp2(bitDepth) - 1.0;
    float scale = exp2(bitDepth - 8.0);
    float y, cb, cr;
    if (fullRange) {
        float mid = exp2(bitDepth - 1.0) / maxCode;
        y = ycc.x;
        cb = ycc.y - mid;
        cr = ycc.z - mid;
    } else {
        y  = (ycc.x * maxCode - 16.0 * scale) / (219.0 * scale);
        cb = (ycc.y * maxCode - 128.0 * scale) / (224.0 * scale);
        cr = (ycc.z * maxCode - 128.0 * scale) / (224.0 * scale);
    }
    float kg = 1.0 - kr - kb;
    float r = y + 2.0 * (1.0 - kr) * cr;
    float b = y + 2.0 * (1.0 - kb) * cb;
    float g = (y - kr * r - kb * b) / kg;
    return vec3(r, g, b);
}

float luminance709(vec3 linearRgb) {
    return dot(linearRgb, vec3(0.2126, 0.7152, 0.0722));
}

// Alfa pré-multiplicado: todo efeito trabalha na cor "reta" e devolve
// pré-multiplicada. Dividir por zero seria NaN na borda transparente.
vec4 unpremultiply(vec4 c) {
    return c.a > 1e-6 ? vec4(c.rgb / c.a, c.a) : vec4(0.0);
}
vec4 premultiply(vec4 c) {
    return vec4(c.rgb * c.a, c.a);
}

#endif
