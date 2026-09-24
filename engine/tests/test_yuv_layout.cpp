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

#include <cstdio>
#include <cstring>
#include <vector>

using namespace aurea;
using namespace aurea::media;

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
