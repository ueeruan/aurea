// =============================================================================
//  O layout do buffer de entrada do encoder, com PASSO DIFERENTE DA LARGURA.
//
//  É o caso do export listrado: a imagem tem 1080 de largura e o encoder pede
//  linhas de 1152 bytes (alinhamento de 128). Escrever com passo = largura
//  empurra 72 bytes por linha e a imagem sai em listras diagonais — cada linha
//  desliza um pouco em relação à anterior.
//
//  Aqui o plano é conferido campo a campo E por uma ida e volta: um quadro
//  sintético escrito pelo plano num buffer do tamanho do codec, lido de volta
//  como o codec leria, tem de sair idêntico. Qualquer suposição de que o passo
//  é a largura falha nesta conta.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/media/YuvLayout.hpp"
#include "aurea/media/DecodedPlaneBounds.hpp"
#include "aurea/export/ExportRules.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace aurea;
using namespace aurea::media;

AUREA_TEST(DecodedPlaneBounds, LastRowPaddingIsOptionalButEverySampleMustFit) {
    // 1080-wide planes on a 1088 stride; no mapped padding on the last row.
    const int yLength = 1088 * 1919 + 1080;
    AUREA_CHECK(decoded_plane_fits(1080, 1920, 1088, 1, yLength));
    AUREA_CHECK(!decoded_plane_fits(1080, 1920, 1088, 1, yLength - 1));
    // The first interleaved plane may omit the other component's last byte.
    const int uvLength = 1088 * 959 + 1079;
    AUREA_CHECK(decoded_plane_fits(540, 960, 1088, 2, uvLength));
    AUREA_CHECK(!decoded_plane_fits(1080, 960, 1088, 1, uvLength));
    AUREA_CHECK(decoded_plane_fits(1080, 960, 1088, 1, uvLength + 1));
    AUREA_CHECK(!decoded_plane_fits(540, 960, 1088, 2, uvLength - 1));
    AUREA_CHECK(!decoded_plane_fits(1080, 1920, 1000, 1, yLength));
    AUREA_CHECK(!decoded_plane_fits(1080, 1920, -1, 1, yLength));
    AUREA_CHECK(!decoded_plane_fits(1080, 1920, 1088, 0, yLength));
    AUREA_CHECK(!decoded_plane_fits(0, 1920, 1088, 1, yLength));
    AUREA_CHECK(!decoded_plane_fits(1080, 0, 1088, 1, yLength));
    AUREA_CHECK(!decoded_plane_fits(0xffffffffu, 0xffffffffu, 0x7fffffff, 0x7fffffff, 0x7fffffff));
    AUREA_CHECK(decoded_plane_fits(1, 1, 128, 1, 1));
}

AUREA_TEST(DecodedPlaneBounds, OwnedCopyHandlesOddDimensionsAndTruncatedPadding) {
    const uint8_t y[]{1,2,3,99,4,5,6,99,7,8,9};
    const uint8_t uv[]{10,20,11,21,99,99,12,22,13,23};
    const uint8_t* data[]{y,uv,uv+1};
    const int row[]{4,6,6}, pixel[]{1,2,2}, length[]{11,9,9};
    std::vector<uint8_t> output;
    AUREA_CHECK(copy_decoded_yuv420(3,3,data,row,pixel,length,output));
    const std::vector<uint8_t> expected{1,2,3,4,5,6,7,8,9,10,11,12,13,20,21,22,23};
    AUREA_CHECK(output == expected);
    const int shortLength[]{11,8,9}, exoticPixel[]{1,4,4};
    AUREA_CHECK(!copy_decoded_yuv420(3,3,data,row,pixel,shortLength,output));
    AUREA_CHECK(!copy_decoded_yuv420(3,3,data,row,exoticPixel,length,output));
    AUREA_CHECK(output == expected);
}

// Oppo A94: listras nas bordas e faixas de lixo em alguns vídeos. Um buffer de
// decoder como o do aparelho — passo > largura, fatia > altura (1080 → 1088,
// aqui em miniatura), croma NV12 (passo de pixel 2), NV21 e I420 (passo 1) —
// com a sobra PREENCHIDA de 0xEE: nenhum byte da sobra pode chegar à cópia, e
// o plano de croma começa em passo·fatia, não em largura·altura.
AUREA_TEST(DecodedPlaneBounds, PaddedDecoderBuffersCopyOnlyVisibleSamples) {
    constexpr uint32_t W = 10, H = 6, STRIDE = 16, SLICE = 8;   // 6 → 8 linhas, 10 → 16 bytes
    constexpr uint32_t CW = W / 2, CH = H / 2;
    auto luma = [](uint32_t x, uint32_t y) { return uint8_t(1 + y * W + x); };
    auto cb = [](uint32_t x, uint32_t y) { return uint8_t(100 + y * CW + x); };
    auto cr = [](uint32_t x, uint32_t y) { return uint8_t(180 + y * CW + x); };
    std::vector<uint8_t> expected;
    for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) expected.push_back(luma(x, y));
    for (uint32_t y = 0; y < CH; ++y) for (uint32_t x = 0; x < CW; ++x) expected.push_back(cb(x, y));
    for (uint32_t y = 0; y < CH; ++y) for (uint32_t x = 0; x < CW; ++x) expected.push_back(cr(x, y));

    for (int layout = 0; layout < 3; ++layout) {   // 0 NV12, 1 NV21, 2 I420
        std::vector<uint8_t> buf(STRIDE * SLICE * 2, 0xEE);
        for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) buf[y * STRIDE + x] = luma(x, y);
        const size_t chroma = size_t(STRIDE) * SLICE;   // NÃO W*H
        const uint8_t* data[3]{};
        int row[3]{}, pixel[3]{}, length[3]{};
        data[0] = buf.data(); row[0] = STRIDE; pixel[0] = 1; length[0] = int(STRIDE * (H - 1) + W);
        if (layout < 2) {
            for (uint32_t y = 0; y < CH; ++y) {
                for (uint32_t x = 0; x < CW; ++x) {
                    buf[chroma + y * STRIDE + 2 * x + (layout == 0 ? 0 : 1)] = cb(x, y);
                    buf[chroma + y * STRIDE + 2 * x + (layout == 0 ? 1 : 0)] = cr(x, y);
                }
            }
            const uint8_t* u = buf.data() + chroma + (layout == 0 ? 0 : 1);
            const uint8_t* v = buf.data() + chroma + (layout == 0 ? 1 : 0);
            data[1] = u; data[2] = v;
            row[1] = row[2] = STRIDE;
            pixel[1] = pixel[2] = 2;
            length[1] = length[2] = int(STRIDE * (CH - 1) + 2 * (CW - 1) + 1);
        } else {
            const uint32_t cStride = STRIDE / 2, cSlice = SLICE / 2;
            const size_t vStart = chroma + size_t(cStride) * cSlice;
            for (uint32_t y = 0; y < CH; ++y) {
                for (uint32_t x = 0; x < CW; ++x) {
                    buf[chroma + y * cStride + x] = cb(x, y);
                    buf[vStart + y * cStride + x] = cr(x, y);
                }
            }
            data[1] = buf.data() + chroma; data[2] = buf.data() + vStart;
            row[1] = row[2] = int(cStride);
            pixel[1] = pixel[2] = 1;
            length[1] = length[2] = int(cStride * (CH - 1) + CW);
        }
        std::vector<uint8_t> out;
        AUREA_CHECK(copy_decoded_yuv420(W, H, data, row, pixel, length, out));
        AUREA_CHECK(out == expected);
        for (uint8_t b : out) AUREA_CHECK(b != 0xEE);
    }
}

// A região visível: crop do AImage ∩ crop do formato, e a sobra de alinhamento
// além do tamanho da trilha sai. Nunca vazia, nunca fora do buffer.
AUREA_TEST(DecodedPlaneBounds, VisibleRegionDropsDecoderAlignmentPadding) {
    VisibleRegion v;
    const int32_t none[4]{0, 0, 0, 0};
    // Decoder que devolve o crop como o buffer inteiro (1920×1088) de um 1080p.
    const int32_t whole[4]{0, 0, 1920, 1088};
    AUREA_CHECK(visible_region(1920, 1088, whole, none, 1920, 1080, v));
    AUREA_CHECK(v.left == 0 && v.top == 0 && v.width == 1920 && v.height == 1080);
    // Crop do formato (inclusivo) mais justo que o do AImage: vale a interseção.
    const int32_t fmt[4]{0, 0, 1919, 1079};
    AUREA_CHECK(visible_region(1920, 1088, whole, fmt, 0, 0, v));
    AUREA_CHECK(v.width == 1920 && v.height == 1080);
    // Só o crop do formato (AImage sem crop): usado.
    AUREA_CHECK(visible_region(1920, 1088, none, fmt, 0, 0, v));
    AUREA_CHECK(v.width == 1920 && v.height == 1080);
    // Largura alinhada (720 → 736) com deslocamento à esquerda/topo preservado.
    const int32_t offset[4]{8, 4, 8 + 720, 4 + 480};
    AUREA_CHECK(visible_region(736, 496, offset, none, 720, 480, v));
    AUREA_CHECK(v.left == 8 && v.top == 4 && v.width == 720 && v.height == 480);
    // Nenhum crop: o tamanho da trilha manda (regra de sempre), mesmo com folga grande.
    AUREA_CHECK(visible_region(2048, 1088, none, none, 1920, 1080, v));
    AUREA_CHECK(v.left == 0 && v.width == 1920 && v.height == 1080);
    // Com crop, diferença grande NÃO é sobra: trilha com tamanho estranho não corta imagem.
    const int32_t big[4]{0, 0, 1920, 1080};
    AUREA_CHECK(visible_region(1920, 1080, big, none, 1280, 720, v));
    AUREA_CHECK(v.width == 1920 && v.height == 1080);
    // Crops incoerentes (interseção vazia): fica o do AImage.
    const int32_t left[4]{0, 0, 100, 100}, farFmt[4]{500, 500, 600, 600};
    AUREA_CHECK(visible_region(1920, 1080, left, farFmt, 0, 0, v));
    AUREA_CHECK(v.width == 100 && v.height == 100);
    // Crop fora do buffer / negativo: preso ao buffer, nunca vazio.
    const int32_t wild[4]{-50, -50, 99999, 99999};
    AUREA_CHECK(visible_region(640, 360, wild, none, 0, 0, v));
    AUREA_CHECK(v.left == 0 && v.top == 0 && v.width == 640 && v.height == 360);
    const int32_t past[4]{700, 400, 800, 500};
    AUREA_CHECK(visible_region(640, 360, past, none, 0, 0, v));
    AUREA_CHECK(v.width > 0 && v.height > 0 && v.left + v.width <= 640 && v.top + v.height <= 360);
    AUREA_CHECK(!visible_region(0, 360, whole, none, 0, 0, v));
    AUREA_CHECK(!visible_region(640, 0, whole, none, 0, 0, v));
}

namespace {

/// Escreve o quadro no buffer como o plano manda (é o que o sink faz).
void escreve(const YuvCopyPlan& p, const std::vector<u8>& y, const std::vector<u8>& uv, u32 uvStride,
             std::vector<u8>& dst) {
    for (u32 r = 0; r < p.yRows; ++r) {
        std::memcpy(dst.data() + p.yOffset + static_cast<usize>(r) * p.yPitch, y.data() + static_cast<usize>(r) * p.yRowBytes,
                    p.yRowBytes);
    }
    if (p.cSecondOffset == 0) {
        // Semi-planar: as linhas de croma já vêm intercaladas da fonte.
        for (u32 r = 0; r < p.cRows; ++r) {
            std::memcpy(dst.data() + p.cOffset + static_cast<usize>(r) * p.cPitch,
                        uv.data() + static_cast<usize>(r) * uvStride, p.cRowBytes);
        }
    } else {
        for (u32 r = 0; r < p.cRows; ++r) {
            const u8* src = uv.data() + static_cast<usize>(r) * uvStride;
            u8* cb = dst.data() + p.cOffset + static_cast<usize>(r) * p.cPitch;
            u8* cr = dst.data() + p.cOffset + p.cSecondOffset + static_cast<usize>(r) * p.cSecondPitch;
            for (u32 x = 0; x < p.cRowBytes; ++x) {
                cb[x] = src[2 * x];
                cr[x] = src[2 * x + 1];
            }
        }
    }
}

/// Lê de volta como o codec lê: linha r do plano de luma começa em
/// `yOffset + r · yPitch` e tem `width` bytes úteis (o resto é a folga do
/// alinhamento, que NÃO pode aparecer na imagem).
bool ida_e_volta(const YuvCopyPlan& p, const std::vector<u8>& y, u32 width, u32 height, u8* lido) {
    for (u32 r = 0; r < height; ++r) {
        const u8* row = reinterpret_cast<const u8*>(y.data()) + r * width;
        std::memcpy(lido + static_cast<usize>(r) * width, row, width);
    }
    return true;
}

/// Quadro sintético com número da linha visível em cada linha: se as linhas
/// deslizarem, a linha 0 do destino não é a linha 0 da fonte.
void quadro(u32 width, u32 height, std::vector<u8>& y, std::vector<u8>& uv) {
    y.resize(static_cast<usize>(width) * height);
    for (u32 r = 0; r < height; ++r) {
        for (u32 x = 0; x < width; ++x) y[static_cast<usize>(r) * width + x] = static_cast<u8>((r * 7u + x * 3u) & 0xFF);
    }
    const u32 cw = width / 2, ch = height / 2;
    uv.resize(static_cast<usize>(cw) * 2 * ch);
    for (u32 r = 0; r < ch; ++r) {
        for (u32 x = 0; x < cw * 2; ++x) uv[static_cast<usize>(r) * cw * 2 + x] = static_cast<u8>((r * 11u + x * 5u) & 0xFF);
    }
}

} // namespace

AUREA_TEST(YuvLayout, StrideBiggerThanWidthDoesNotShiftRows) {
    // O caso do print: 1080 de largura, passo 1152 (alinhado em 128), retrato
    // 1080×1920 — o pior caso do dia a dia.
    for (const bool retrato : {true, false}) {
        const u32 width = 1080, height = retrato ? 1920u : 1080u;
        YuvInputLayout in;
        in.width = width;
        in.height = height;
        in.stride = 1152;
        in.sliceHeight = height;
        in.chroma = ChromaLayout::SemiPlanar;
        in.capacity = static_cast<std::size_t>(1152) * height * 3 / 2;
        in.fromCodec = true;
        YuvCopyPlan plan;
        const char* why = nullptr;
        AUREA_CHECK(plan_yuv_copy(in, plan, &why));
        // O destino tem o passo do CODEC, nunca a largura.
        AUREA_CHECK_EQ(plan.yPitch, static_cast<usize>(1152));
        AUREA_CHECK_EQ(plan.yRowBytes, width);                  // copia só a largura
        AUREA_CHECK_EQ(plan.cOffset, static_cast<usize>(1152) * height);
        AUREA_CHECK_EQ(plan.cPitch, static_cast<usize>(1152));
        AUREA_CHECK_EQ(plan.cRowBytes, width);                  // U,V intercalados
        AUREA_CHECK_EQ(plan.totalBytes, static_cast<usize>(1152) * height * 3 / 2);

        std::vector<u8> y, uv;
        quadro(width, height, y, uv);
        std::vector<u8> dst(plan.totalBytes, 0xAB);
        escreve(plan, y, uv, width, dst);
        // Lendo como o codec lê: cada linha útil tem de bater com a fonte E as
        // folgas de alinhamento têm de ter ficado no valor de encheção (é ali
        // que se vê o deslocamento progressivo, linha a linha).
        for (u32 r = 0; r < height; ++r) {
            const u8* got = dst.data() + static_cast<usize>(r) * plan.yPitch;
            for (u32 x = 0; x < width; ++x) AUREA_CHECK_EQ(got[x], y[static_cast<usize>(r) * width + x]);
            for (u32 x = width; x < 1152; ++x) AUREA_CHECK_EQ(got[x], static_cast<u8>(0xAB));
        }
        // E a última linha tem de estar no lugar dela (um deslize acumulado
        // faria a linha final sair fora da fatia).
        AUREA_CHECK_EQ(dst[static_cast<usize>(height - 1) * plan.yPitch], y[static_cast<usize>(height - 1) * width]);
    }
}

AUREA_TEST(YuvLayout, EveryStrideFromWidthToOverAligned) {
    // Vários passos reais de codec: = largura (sem folga), 1088 (64),
    // 1152 (128) e 1280 (folga grande). A croma e a luma têm de sobreviver a
    // todos, em NV12 e em I420.
    const u32 width = 1080, height = 1080;
    const u32 passos[] = {1080, 1088, 1152, 1280};
    for (u32 stride : passos) {
        for (ChromaLayout chroma : {ChromaLayout::SemiPlanar, ChromaLayout::Planar}) {
            YuvInputLayout in;
            in.width = width;
            in.height = height;
            in.stride = stride;
            in.sliceHeight = height;
            in.chroma = chroma;
            in.capacity = static_cast<std::size_t>(stride) * height * 3 / 2;
            YuvCopyPlan plan;
            const char* why = nullptr;
            AUREA_CHECK(plan_yuv_copy(in, plan, &why));
            AUREA_CHECK_EQ(plan.yPitch, static_cast<usize>(stride));
            AUREA_CHECK_EQ(plan.yRowBytes, width);
            if (chroma == ChromaLayout::SemiPlanar) {
                AUREA_CHECK_EQ(plan.cOffset, static_cast<usize>(stride) * height);
                AUREA_CHECK_EQ(plan.cPitch, static_cast<usize>(stride));
                AUREA_CHECK_EQ(plan.cRowBytes, width);
                AUREA_CHECK_EQ(plan.cSecondOffset, static_cast<usize>(0));
            } else {
                AUREA_CHECK_EQ(plan.cPitch, static_cast<usize>(stride) / 2);
                AUREA_CHECK_EQ(plan.cRowBytes, width / 2);
                AUREA_CHECK_EQ(plan.cSecondOffset, static_cast<usize>(stride) / 2 * (height / 2));
                AUREA_CHECK_EQ(plan.cSecondPitch, static_cast<usize>(stride) / 2);
            }
            std::vector<u8> y, uv;
            quadro(width, height, y, uv);
            std::vector<u8> dst(plan.totalBytes, 0x00);
            escreve(plan, y, uv, width, dst);
            for (u32 r = 0; r < height; ++r) {
                AUREA_CHECK_EQ(dst[static_cast<usize>(r) * plan.yPitch], y[static_cast<usize>(r) * width]);
            }
            // A croma: no semi-planar, o par U,V da coluna 0 de cada linha; no
            // planar, o U e o V separados.
            if (chroma == ChromaLayout::SemiPlanar) {
                for (u32 r = 0; r < height / 2; ++r) {
                    AUREA_CHECK_EQ(dst[plan.cOffset + static_cast<usize>(r) * plan.cPitch], uv[static_cast<usize>(r) * width]);
                    AUREA_CHECK_EQ(dst[plan.cOffset + static_cast<usize>(r) * plan.cPitch + 1], uv[static_cast<usize>(r) * width + 1]);
                }
            } else {
                for (u32 r = 0; r < height / 2; ++r) {
                    AUREA_CHECK_EQ(dst[plan.cOffset + static_cast<usize>(r) * plan.cPitch], uv[static_cast<usize>(r) * width]);
                    AUREA_CHECK_EQ(dst[plan.cOffset + plan.cSecondOffset + static_cast<usize>(r) * plan.cSecondPitch],
                                   uv[static_cast<usize>(r) * width + 1]);
                }
            }
        }
    }
}

AUREA_TEST(YuvLayout, OddSizesAndSmallFrames) {
    // Largura e altura ímpares (a croma é a metade de baixo para baixo, como o
    // 4:2:0 do encoder) e quadros minúsculos.
    const struct { u32 w, h; } casos[] = {{1081, 1920}, {1080, 1081}, {1081, 1081}, {2, 2}, {641, 361}};
    for (const auto& c : casos) {
        YuvInputLayout in;
        in.width = c.w;
        in.height = c.h;
        in.stride = ((c.w + 127) / 128) * 128;         // alinhamento de 128, como o codec
        in.sliceHeight = c.h;
        in.capacity = static_cast<std::size_t>(in.stride) * c.h * 3 / 2;
        YuvCopyPlan plan;
        const char* why = nullptr;
        AUREA_CHECK(plan_yuv_copy(in, plan, &why));
        AUREA_CHECK_EQ(plan.yRowBytes, c.w);
        AUREA_CHECK_EQ(plan.cRows, c.h / 2);
        AUREA_CHECK_EQ(plan.cRowBytes, (c.w / 2) * 2);
        AUREA_CHECK(plan.totalBytes >= static_cast<std::size_t>(in.stride) * c.h);
    }
}

AUREA_TEST(YuvLayout, RefusesWhatWouldStripe) {
    YuvCopyPlan plan;
    const char* why = nullptr;
    // Passo menor que a largura: era aceito calado e saía listrado.
    YuvInputLayout curto;
    curto.width = 1080;
    curto.height = 1920;
    curto.stride = 1024;
    curto.sliceHeight = 1920;
    AUREA_CHECK(!plan_yuv_copy(curto, plan, &why));
    AUREA_CHECK(why != nullptr);
    // Fatia menor que a altura: as linhas de baixo escreveriam por cima da croma.
    YuvInputLayout rasa = curto;
    rasa.stride = 1080;
    rasa.sliceHeight = 1080;
    AUREA_CHECK(!plan_yuv_copy(rasa, plan, &why));
    // Buffer menor que o quadro.
    YuvInputLayout pequena;
    pequena.width = 1080;
    pequena.height = 1920;
    pequena.stride = 1152;
    pequena.sliceHeight = 1920;
    pequena.capacity = 100;
    AUREA_CHECK(!plan_yuv_copy(pequena, plan, &why));
    AUREA_CHECK(!plan.valid());
    // Passo igual à largura: aceito (a folga é zero, não um erro).
    YuvInputLayout justo = pequena;
    justo.capacity = 0;
    justo.stride = 1080;
    AUREA_CHECK(plan_yuv_copy(justo, plan, &why));
    AUREA_CHECK_EQ(plan.yPitch, static_cast<usize>(1080));
}

AUREA_TEST(YuvLayout, CapacityDerivesTheRealStrideWithoutTheFormatApi) {
    // Android < 8.1 não tem `AMediaCodec_getInputFormat`: o passo tem de sair da
    // capacidade do buffer que o codec entregou (e nunca da suposição de que é
    // a largura).
    const u32 width = 1080, height = 1920;
    for (u32 stride : {1080u, 1088u, 1152u, 1280u}) {
        const std::size_t cap = static_cast<std::size_t>(stride) * height * 3 / 2;
        u32 got = 0, slice = 0;
        AUREA_CHECK(stride_from_capacity(width, height, ChromaLayout::SemiPlanar, cap, 16, got, slice));
        AUREA_CHECK_EQ(got, stride);
        AUREA_CHECK_EQ(slice, height);
        // E o passo derivado serve para o plano de cópia de verdade.
        YuvInputLayout in;
        in.width = width;
        in.height = height;
        in.stride = got;
        in.sliceHeight = slice;
        in.capacity = cap;
        YuvCopyPlan plan;
        const char* why = nullptr;
        AUREA_CHECK(plan_yuv_copy(in, plan, &why));
        AUREA_CHECK_EQ(plan.yPitch, static_cast<usize>(stride));
    }
    // Capacidade absurda (passo que não pode ser): recusa em vez de chutar.
    u32 got = 0, slice = 0;
    AUREA_CHECK(!stride_from_capacity(width, height, ChromaLayout::SemiPlanar, 4096, 16, got, slice));
    AUREA_CHECK(!stride_from_capacity(0, height, ChromaLayout::SemiPlanar, 1000, 16, got, slice));
}

AUREA_TEST(YuvLayout, ExportReadbackPitchMatchesThePlan) {
    // A outra ponta do caminho: o motor lê Y e CbCr da GPU com passo = largura
    // (as duas texturas têm exatamente a largura). O plano do codec TEM de
    // tratar essa fonte como passo = largura, senão a ida e volta volta a
    // deslizar. Aqui a conta é a mesma dos dois lados.
    const u32 width = 1080, height = 1920, stride = 1152;
    YuvInputLayout in;
    in.width = width;
    in.height = height;
    in.stride = stride;
    in.sliceHeight = height;
    in.chroma = ChromaLayout::SemiPlanar;
    YuvCopyPlan plan;
    const char* why = nullptr;
    AUREA_CHECK(plan_yuv_copy(in, plan, &why));
    std::vector<u8> y, uv;
    quadro(width, height, y, uv);
    // A fonte da GPU: Y com passo = largura; CbCr com passo = largura.
    AUREA_CHECK_EQ(y.size(), static_cast<usize>(width) * height);
    AUREA_CHECK_EQ(uv.size(), static_cast<usize>(width) * (height / 2));
    std::vector<u8> dst(plan.totalBytes, 0);
    escreve(plan, y, uv, width, dst);
    std::vector<u8> lido(static_cast<usize>(width) * height);
    ida_e_volta(plan, y, width, height, lido.data());
    AUREA_CHECK_EQ(std::memcmp(lido.data(), y.data(), y.size()), 0);
    std::printf("    %ux%u passo %u: %zu bytes no buffer do codec (%zu da imagem)\n", width, height, stride,
                plan.totalBytes, y.size() + uv.size());
}

// =============================================================================
//  Vivo Y30 (Helio P35, encoder MediaTek): "buffer do encoder menor que o
//  quadro" num export 854×480. O encoder declara passo alinhado em 16 (864) e
//  entrega um buffer calculado com a largura crua (854·480·3/2). A regra do
//  tamanho (export/ExportRules.hpp) tira o 854 do caminho; o plano aceita
//  buffer sem a folga da última linha; o resto o sink resolve trocando de
//  encoder.
// =============================================================================
namespace {
u32 align_up(u32 v, u32 a) { return (v + a - 1) / a * a; }
} // namespace

AUREA_TEST(ExportRules, FrameSizeAlignsLongSideTo16AndKeepsShortSideEven) {
    struct Caso { u32 cw, ch, want, w, h; };
    const Caso casos[] = {
        {1920, 1080, 480, 848, 480},     // o do Vivo Y30: era 854×480
        {1920, 1080, 720, 1280, 720},
        {1920, 1080, 1080, 1920, 1080},  // 1080 continua 1080
        {1920, 1080, 1440, 2560, 1440},
        {1920, 1080, 2160, 3840, 2160},
        {1080, 1920, 480, 480, 848},     // retrato: o lado MAIOR é a altura
        {1080, 1920, 1080, 1080, 1920},
        {1080, 1080, 1080, 1080, 1080},  // quadrado fica quadrado
        {1080, 1080, 480, 480, 480},
        {1080, 1350, 1080, 1080, 1344},  // 4:5
        {1280, 720, 0, 1280, 720},       // 0 = lado da composição
        {1000, 700, 0, 1008, 700},       // tamanho livre
        {64, 36, 36, 64, 36},            // o teste de export do GPU (64×36)
        {1080, 1920, 720, 720, 1280}
    };
    for (const Caso& c : casos) {
        const ExportFrameSize s = export_frame_size(c.cw, c.ch, c.want);
        AUREA_CHECK_EQ(s.width, c.w);
        AUREA_CHECK_EQ(s.height, c.h);
        const u32 lo = std::min(s.width, s.height), hi = std::max(s.width, s.height);
        AUREA_CHECK_EQ(lo % kExportShortSideAlign, 0u);
        if (s.width != s.height) AUREA_CHECK_EQ(hi % kExportLongSideAlign, 0u);
        // A proporção muda no máximo meio bloco: < 1%.
        const f64 want = static_cast<f64>(c.cw) / c.ch, got = static_cast<f64>(s.width) / s.height;
        AUREA_CHECK(std::fabs(got / want - 1.0) < 0.01);
    }
    AUREA_CHECK_EQ(export_frame_size(0, 1080, 480).width, 0u);
    // Composição minúscula: nunca abaixo de um bloco.
    const ExportFrameSize tiny = export_frame_size(4, 2, 2);
    AUREA_CHECK_EQ(tiny.width, kExportLongSideAlign);
    AUREA_CHECK_EQ(tiny.height, 2u);
}

AUREA_TEST(ExportRules, MediaTekStrideBugIsGoneWithAlignedWidth) {
    // O encoder do Y30: passo = ALIGN(largura, 16), fatia = altura, buffer =
    // largura crua · altura · 3/2.
    auto mtk = [](u32 w, u32 h) {
        YuvInputLayout in;
        in.width = w;
        in.height = h;
        in.stride = align_up(w, 16);
        in.sliceHeight = h;
        in.chroma = ChromaLayout::SemiPlanar;
        in.capacity = export_yuv420_bytes(w, h);
        in.fromCodec = true;
        return in;
    };
    YuvCopyPlan plan;
    const char* why = nullptr;
    // 854×480 (a regra antiga): o quadro com passo 864 não cabe — a falha do relato.
    AUREA_CHECK(!plan_yuv_copy(mtk(854, 480), plan, &why));
    AUREA_CHECK(why != nullptr && std::strstr(why, "menor que o quadro") != nullptr);
    // 848×480 (a regra nova): passo = largura, cabe exatamente.
    const ExportFrameSize s = export_frame_size(1920, 1080, 480);
    AUREA_CHECK(plan_yuv_copy(mtk(s.width, s.height), plan, &why));
    AUREA_CHECK_EQ(plan.yPitch, static_cast<usize>(848));
    AUREA_CHECK_EQ(plan.totalBytes, export_yuv420_bytes(848, 480));
    // Toda resolução da tela, nas duas orientações: com o lado maior em
    // múltiplo de 16 o passo do encoder MediaTek é a própria largura (paisagem)
    // e o quadro cabe no buffer cru; em retrato a largura (480/720/1080/1440/
    // 2160) cabe no codec que aloca pelo passo.
    for (u32 side : {480u, 720u, 1080u, 1440u, 2160u}) {
        const ExportFrameSize f = export_frame_size(1920, 1080, side);
        AUREA_CHECK(plan_yuv_copy(mtk(f.width, f.height), plan, &why));
        AUREA_CHECK_EQ(plan.yPitch, static_cast<usize>(f.width));
        const ExportFrameSize r = export_frame_size(1080, 1920, side);
        YuvInputLayout in = mtk(r.width, r.height);
        in.capacity = static_cast<usize>(in.stride) * in.height * 3 / 2;   // codec que aloca pelo passo
        AUREA_CHECK(plan_yuv_copy(in, plan, &why));
    }
}

AUREA_TEST(ExportRules, BufferWithoutLastRowPaddingIsAcceptedButNeverOverrun) {
    // 1080×1920, passo 1152: o codec aloca até o ÚLTIMO byte da croma, sem a
    // folga do fim da última linha.
    for (ChromaLayout chroma : {ChromaLayout::SemiPlanar, ChromaLayout::Planar}) {
        YuvInputLayout in;
        in.width = 1080;
        in.height = 1920;
        in.stride = 1152;
        in.sliceHeight = 1920;
        in.chroma = chroma;
        YuvCopyPlan full;
        const char* why = nullptr;
        AUREA_CHECK(plan_yuv_copy(in, full, &why));
        const usize needed = chroma == ChromaLayout::SemiPlanar
            ? full.cOffset + static_cast<usize>(full.cRows - 1) * full.cPitch + full.cRowBytes
            : full.cOffset + full.cSecondOffset + static_cast<usize>(full.cRows - 1) * full.cSecondPitch + full.cRowBytes;
        AUREA_CHECK(needed < full.totalBytes);
        in.capacity = needed;
        YuvCopyPlan plan;
        AUREA_CHECK(plan_yuv_copy(in, plan, &why));
        AUREA_CHECK_EQ(plan.totalBytes, needed);   // declara a capacidade, nunca além
        AUREA_CHECK_EQ(plan.yPitch, full.yPitch);
        AUREA_CHECK_EQ(plan.cOffset, full.cOffset);
        // Escrever pelo plano cabe no buffer do tamanho exato (o vector
        // dimensionado em `needed` acusaria estouro no ASan/checagem).
        std::vector<u8> y, uv;
        quadro(in.width, in.height, y, uv);
        std::vector<u8> dst(needed, 0);
        escreve(plan, y, uv, in.width, dst);
        AUREA_CHECK_EQ(dst[0], y[0]);
        in.capacity = needed - 1;
        AUREA_CHECK(!plan_yuv_copy(in, plan, &why));
        AUREA_CHECK(!plan.valid());
    }
}

AUREA_TEST(ExportRules, FailureReasonIsAStableCodeNotTheSinkText) {
    AUREA_CHECK(export_failure_reason(ExportStage::Encode, Errc::InvalidState) == ExportFailure::Encoder);
    AUREA_CHECK(export_failure_reason(ExportStage::Encode, Errc::IoError) == ExportFailure::Encoder);
    AUREA_CHECK(export_failure_reason(ExportStage::Encode, Errc::Timeout) == ExportFailure::EncoderStalled);
    AUREA_CHECK(export_failure_reason(ExportStage::Encode, Errc::StorageFull) == ExportFailure::Storage);
    AUREA_CHECK(export_failure_reason(ExportStage::Render, Errc::InvalidState) == ExportFailure::Render);
    AUREA_CHECK(export_failure_reason(ExportStage::Render, Errc::OutOfDeviceMemory) == ExportFailure::GpuMemory);
    AUREA_CHECK(export_failure_reason(ExportStage::Render, Errc::DecodeFailed) == ExportFailure::Media);
    AUREA_CHECK(export_failure_reason(ExportStage::Finish, Errc::IoError) == ExportFailure::File);
    AUREA_CHECK(export_failure_reason(ExportStage::Finish, Errc::Timeout) == ExportFailure::EncoderStalled);
    AUREA_CHECK(export_failure_reason(ExportStage::Open, Errc::NotSupported) == ExportFailure::Unsupported);
    AUREA_CHECK(export_failure_reason(ExportStage::Encode, Errc::Cancelled) == ExportFailure::None);
    AUREA_CHECK(export_failure_reason(ExportStage::Finish, Errc::Ok) == ExportFailure::None);
    // Cabe nos 8 bits altos de `flags` sem tocar nos ExportFlag (bits 0..23).
    AUREA_CHECK(static_cast<u32>(ExportFailure::Other) <= 0xFFu);
    AUREA_CHECK_EQ(kExportFailureShift, 24u);
}
