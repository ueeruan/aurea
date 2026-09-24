// =============================================================================
//  Aurea / media / YuvLayout.hpp
//
//  O layout do buffer de ENTRADA do encoder, sem supor nada.
//
//  O quadro sai da GPU como dois planos (Y 8 bits, CbCr 8 bits intercalado) e
//  precisa ser escrito no buffer que o MediaCodec vai ler. Esse buffer NÃO é
//  do tamanho da imagem: ele tem passo de linha (`stride`) e fatia
//  (`slice-height`) escolhidos pelo codec, com alinhamento e folga próprios.
//  Escrever cada linha com passo = largura quando o passo verdadeiro é 1152 (e
//  a largura é 1080) empurra 72 bytes por linha: a imagem sai em listras
//  diagonais, porque cada linha desliza um pouco em relação à anterior.
//
//  Aqui o cálculo é PURO (nenhum header do Android) por dois motivos: o sink
//  do Android usa exatamente este código, e o host consegue testá-lo com passo
//  diferente da largura — 1080/1152, retrato, paisagem, altura ímpar, I420.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <cstddef>

namespace aurea::media {

/// Como o codec guarda a croma no buffer de entrada.
enum class ChromaLayout : u8 {
    /// NV12 (semi-planar): U e V intercalados, um par por amostra 2×2.
    SemiPlanar,
    /// I420 (planar): dois planos separados, com metade da largura cada.
    Planar,
};

/// O que o codec DECLAROU sobre o buffer de entrada. `stride`/`sliceHeight`
/// vêm do `getInputFormat`/`getInputImage`; nunca são chutados aqui.
struct YuvInputLayout {
    u32 width = 0;                  ///< largura da imagem, em pixels
    u32 height = 0;                 ///< altura da imagem, em pixels
    u32 stride = 0;                 ///< bytes por linha do plano de luma
    u32 sliceHeight = 0;            ///< linhas alocadas por plano
    ChromaLayout chroma = ChromaLayout::SemiPlanar;
    /// Capacidade do buffer que o codec entregou (0 = não conferida).
    std::size_t capacity = 0;
    /// true quando o passo/fatia vieram do próprio codec (e não de um palpite).
    bool fromCodec = false;
};

/// O que a cópia precisa saber. Tudo em BYTES, tudo calculado com o passo REAL.
struct YuvCopyPlan {
    // Plano de luma: uma linha de origem tem `width` bytes; o destino tem
    // `stride` bytes por linha e começa em `yOffset`.
    u32 yRowBytes = 0;              ///< bytes copiados por linha (largura)
    u32 yRows = 0;                  ///< linhas copiadas
    std::size_t yOffset = 0;
    std::size_t yPitch = 0;         ///< passo do destino (declarado pelo codec)

    // Croma (NV12: um par U,V por linha; I420: duas linhas de metade da largura).
    u32 cRows = 0;                  ///< linhas de croma copiadas
    std::size_t cOffset = 0;        ///< onde a croma começa no buffer
    std::size_t cPitch = 0;         ///< passo do destino na croma
    u32 cRowBytes = 0;              ///< bytes copiados por linha de croma
    /// I420: onde começa o plano V (0 em NV12).
    std::size_t cSecondOffset = 0;
    /// I420: passo do plano V (0 em NV12).
    std::size_t cSecondPitch = 0;

    /// Quantos bytes declarar no `queueInputBuffer` (o quadro inteiro no
    /// layout do codec, não o tamanho da imagem).
    std::size_t totalBytes = 0;

    [[nodiscard]] bool valid() const noexcept { return totalBytes > 0; }
};

/// Monta o plano de cópia a partir do layout declarado. Recusa (devolvendo
/// `false` e explicando em `why`) o que não pode ser escrito com segurança:
/// passo menor que a largura, fatia menor que a altura, quadro maior que o
/// buffer. Silêncio aqui é vídeo listrado; recusar é exportar certo.
bool plan_yuv_copy(const YuvInputLayout& in, YuvCopyPlan& out, const char** why) noexcept;

/// Passo derivado da CAPACIDADE do buffer, para quando o aparelho não deixa
/// perguntar o formato (Android < 8.1, sem `AMediaCodec_getInputFormat`).
///
/// O buffer de um quadro NV12 ocupa `stride · sliceHeight · 3/2` (ou
/// `stride · height · 3/2` com fatia = altura). Daí `stride = 2·cap/(3·altura)`.
/// O resultado é arredondado para BAIXO num múltiplo de `alignment` (os codecs
/// alinham o passo em 16/32/64/128 bytes) e só vale se não ficar menor que a
/// largura; qualquer outra coisa é `false` — melhor falhar alto do que escrever
/// torto. `capacity` é o que `AMediaCodec_getInputBuffer` devolveu.
bool stride_from_capacity(u32 width, u32 height, ChromaLayout chroma, std::size_t capacity, u32 alignment,
                          u32& outStride, u32& outSliceHeight) noexcept;

} // namespace aurea::media
