// =============================================================================
//  Aurea / media / SourceWindow.hpp
//
//  1.0.2 — "importar só o trecho cortado": o arquivo da mídia é um TRECHO do
//  original (MediaCut.hpp), e o projeto continua na régua do original.
//
//  A janela é uma propriedade do ARQUIVO (cada trecho tem nome próprio), então
//  o registro é por caminho e vale para o processo inteiro: o motor registra
//  ao importar e ao abrir o projeto (Asset::windowStartUs), e a fábrica do
//  motor (ImageSequenceFactory) embrulha TODO decoder aberto por esse caminho —
//  prévia, export, miniaturas, proxy, profundidade, legendas, waveform.
//
//  O embrulho:
//   - soma `startUs` aos instantes que o decoder entrega (vídeo e áudio) e à
//     duração que ele informa (e à da sondagem);
//   - tira `startUs` no seek;
//   - antes da janela segura o PRIMEIRO quadro: ele sai cobrindo [0, fim dele)
//     — um pedido de antes do trecho (curva de tempo, alça antiga) recebe uma
//     imagem exata em vez de esperar um quadro que o arquivo não tem.
//  O proxy de edição (outro caminho, já gerado na régua do original) não é
//  embrulhado de novo.
// =============================================================================
#pragma once

#include "aurea/audio/Audio.hpp"
#include "aurea/media/VideoSource.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace aurea {

struct MediaProbe;

/// Registra (ou troca) a janela do arquivo `path`. `startUs` ≤ 0 tira o registro.
void register_source_window(std::string_view path, i64 startUs);
/// A janela registrada para `path` (0 = arquivo inteiro / sem registro).
[[nodiscard]] i64 source_window_us(std::string_view path) noexcept;

/// Decoder de vídeo na régua do original. `startUs` ≤ 0 devolve `inner`.
[[nodiscard]] std::unique_ptr<VideoDecoderBackend> window_video_decoder(std::unique_ptr<VideoDecoderBackend> inner,
                                                                        i64 startUs);
/// Decoder de áudio na régua do original. `startUs` ≤ 0 devolve `inner`.
[[nodiscard]] std::unique_ptr<audio::AudioDecoderBackend> window_audio_decoder(
    std::unique_ptr<audio::AudioDecoderBackend> inner, i64 startUs);
/// A sondagem do trecho passa a medir até o FIM da janela na régua do original.
void window_probe(MediaProbe& probe, i64 startUs) noexcept;

} // namespace aurea
