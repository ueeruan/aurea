// =============================================================================
//  Aurea / media / YuvLayout.cpp
// =============================================================================
#include "aurea/media/YuvLayout.hpp"

namespace aurea::media {
namespace {

/// Passo arredondado para baixo no alinhamento (o codec alinha o passo).
u32 align_down(u32 v, u32 a) noexcept { return a == 0 ? v : v - (v % a); }

} // namespace

bool plan_yuv_copy(const YuvInputLayout& in, YuvCopyPlan& out, const char** why) noexcept {
    const char* erro = nullptr;
    out = YuvCopyPlan{};
    if (in.width == 0 || in.height == 0) {
        erro = "quadro de tamanho zero";
    } else if (in.stride < in.width) {
        // O caso que produzia listras: escrever `width` bytes por linha num
        // buffer cujo passo é menor empurra a sobra para a linha seguinte.
        erro = "passo do encoder menor que a largura";
    } else if (in.sliceHeight < in.height) {
        erro = "fatia do encoder menor que a altura";
    }
    if (erro) {
        if (why) *why = erro;
        return false;
    }

    const u32 cw = in.width / 2;                 // largura da croma (4:2:0)
    const u32 ch = in.height / 2;                // altura da croma
    const std::size_t ySize = static_cast<std::size_t>(in.stride) * in.sliceHeight;

    out.yOffset = 0;
    out.yPitch = in.stride;
    out.yRowBytes = in.width;
    out.yRows = in.height;

    if (in.chroma == ChromaLayout::SemiPlanar) {
        // NV12: U e V intercalados, uma linha de croma a cada duas de luma.
        out.cRows = ch;
        out.cOffset = ySize;
        out.cPitch = in.stride;
        out.cRowBytes = cw * 2;                  // par U,V por amostra
        out.cSecondOffset = 0;
        out.cSecondPitch = 0;
        out.totalBytes = ySize + static_cast<std::size_t>(in.stride) * ch;
    } else {
        // I420: dois planos de meia largura; o destino tem passo próprio, que é
        // o passo do codec dividido por dois (cada amostra é 1 byte).
        const std::size_t cs = in.stride / 2;
        out.cRows = ch;
        out.cOffset = ySize;
        out.cPitch = cs;
        out.cRowBytes = cw;
        out.cSecondOffset = cs * (in.sliceHeight / 2);
        out.cSecondPitch = cs;
        out.totalBytes = ySize + 2 * cs * (in.sliceHeight / 2);
    }

    if (in.capacity != 0 && out.totalBytes > in.capacity) {
        if (why) *why = "buffer do encoder menor que o quadro";
        out = YuvCopyPlan{};
        return false;
    }
    if (out.cRowBytes == 0 || out.cRows == 0) {
        if (why) *why = "croma de tamanho zero";
        out = YuvCopyPlan{};
        return false;
    }
    return true;
}

bool stride_from_capacity(u32 width, u32 height, ChromaLayout chroma, std::size_t capacity, u32 alignment,
                          u32& outStride, u32& outSliceHeight) noexcept {
    outStride = 0;
    outSliceHeight = 0;
    if (width == 0 || height == 0 || capacity == 0) return false;
    // NV12 ocupa 3/2 do plano de luma e o I420 ocupa o mesmo total (o plano de
    // luma mais duas metades). Nos dois casos: cap = stride · altura · 3/2.
    const std::size_t denom = static_cast<std::size_t>(height) * 3u;
    if (denom == 0) return false;
    const u32 exato = static_cast<u32>(capacity * 2u / denom);
    const auto cabe = [&](u32 s) {
        return s >= width && static_cast<std::size_t>(s) * height * 3u / 2u <= capacity;
    };
    // 1) O quociente exato: o codec alocou justamente o quadro. Vale mesmo sem
    //    alinhamento redondo (há encoder com passo 1080 para largura 1080).
    if (cabe(exato)) {
        outStride = exato;
        outSliceHeight = height;
        return true;
    }
    // 2) Passo alinhado: os codecs alinham em 16/32/64/128. Desce do maior
    //    alinhamento para o menor e aceita o primeiro que caiba.
    const u32 a = alignment ? alignment : 128u;
    for (u32 passo = a; passo >= 8u; passo /= 2u) {
        const u32 s = align_down(exato, passo);
        if (cabe(s)) {
            outStride = s;
            outSliceHeight = height;
            return true;
        }
    }
    // Não dá para deduzir com segurança: quem chamou decide o que fazer (falhar
    // alto, nunca escrever com passo chutado).
    (void)chroma;
    return false;
}

} // namespace aurea::media
