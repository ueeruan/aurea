// =============================================================================
//  Aurea / shaders / video / yuv_convert.glsl
//
//  Frame do decoder → textura da layer, no espaço de trabalho (linear, BT.709,
//  pré-multiplicado).
//
//  DUAS ENTRADAS, UMA MATEMÁTICA:
//
//   AUREA_EXTERNAL  a imagem do MediaCodec importada sem cópia (AHardwareBuffer).
//                   O sampler imutável faz SÓ a reconstrução do croma; ele
//                   entrega os códigos crus em (R=Cr, G=Y, B=Cb).
//   (sem define)    planos enviados pela CPU — o caminho de fallback quando o
//                   aparelho não importa AHardwareBuffer, e o caminho dos
//                   testes no host. NV12, NV21, I420 e P010.
//
//  A conta de cor (faixa, matriz, curva, primárias, tone map) é a mesma função
//  nos dois casos. Um vídeo tem a mesma cor com ou sem zero-copy — é isso que o
//  teste `VideoColor.ExternalMatchesPlanar` compara no aparelho.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // Y (planar) ou imagem externa
#ifndef AUREA_EXTERNAL
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // CbCr (ou Cb no I420)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_tex2;   // Cr no I420
#endif

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 coeffs;     // x=Kr  y=Kb  z=faixa completa (0/1)  w=bits
    vec4 transfer;   // x=curva  y=primárias  z=tone map (0/1)  w=pico HDR (trabalho)
    vec4 crop;       // xy=deslocamento  zw=escala da região visível no quadro codificado
    vec4 rot;        // matriz 2x2 de orientação (xy = coluna 0, zw = coluna 1)
    vec4 sampling;   // x=layout planar (0 NV12, 1 NV21, 2 I420)  y=escala de código (P010)  z=taps de redução (0/1)
    vec4 texel;      // xy=tamanho do texel de luma em uv  zw=tamanho do pixel de saída em uv da fonte
} p;

vec3 fetch_ycc(vec2 uv) {
#ifdef AUREA_EXTERNAL
    vec4 s = texture(u_tex0, uv);
    return vec3(s.g, s.b, s.r);
#else
    float y = texture(u_tex0, uv).r;
    vec2 c;
    int layoutKind = int(p.sampling.x + 0.5);
    if (layoutKind == 2) {
        c = vec2(texture(u_tex1, uv).r, texture(u_tex2, uv).r);
    } else {
        c = texture(u_tex1, uv).rg;
        if (layoutKind == 1) c = c.yx;
    }
    // P010 guarda 10 bits no topo de 16: amostrado como unorm16 o valor sai
    // multiplicado por 64/65535*1023. A escala devolve o código de 10 bits.
    return vec3(y, c) * p.sampling.y;
#endif
}

vec2 source_uv(vec2 dstUv) {
    // Orientação do vídeo (metadado de rotação do container) e recorte da
    // região visível dentro do quadro codificado (o decoder alinha a largura
    // em múltiplos de 16 e a sobra não pode aparecer).
    vec2 c = dstUv - 0.5;
    vec2 oriented = vec2(p.rot.x * c.x + p.rot.z * c.y, p.rot.y * c.x + p.rot.w * c.y) + 0.5;
    return p.crop.xy + oriented * p.crop.zw;
}

void main() {
    vec2 uv = source_uv(v_uv);
    vec3 ycc;
    if (p.sampling.z > 0.5) {
        // Redução grande (4K para um preview de 960 px): uma amostra bilinear
        // pula texels e o vídeo cintila. Quatro amostras bilineares nos
        // quartos do pixel de saída cobrem uma caixa de 4x4 texels.
        vec2 d = p.texel.zw * 0.25;
        ycc = 0.25 * (fetch_ycc(uv + vec2(-d.x, -d.y)) + fetch_ycc(uv + vec2(d.x, -d.y))
                    + fetch_ycc(uv + vec2(-d.x, d.y)) + fetch_ycc(uv + vec2(d.x, d.y)));
    } else {
        ycc = fetch_ycc(uv);
    }

    vec3 rgb = ycbcr_to_rgb(ycc, p.coeffs.x, p.coeffs.y, p.coeffs.z > 0.5, p.coeffs.w);
    vec3 lin = decode_transfer(clamp(rgb, 0.0, 1.0), int(p.transfer.x + 0.5));
    lin = primaries_to_bt709(lin, int(p.transfer.y + 0.5));
    if (p.transfer.z > 0.5) lin = tonemap_to_sdr(lin, p.transfer.w);
    o_color = vec4(max(lin, vec3(0.0)), 1.0);
}
