#pragma once

#include "aurea/export/ExportSink.hpp"

namespace aurea {

// Shared native exporter. The factory never substitutes a platform exporter.
// A build without the four FFmpeg libraries returns nullptr and reports
// unavailable; enabled builds still probe/open the requested encoders at runtime.
[[nodiscard]] bool ffmpeg_export_available() noexcept;
[[nodiscard]] std::unique_ptr<ExportSink> make_ffmpeg_export_sink(void* user) noexcept;

/// O degrau do encoder de vídeo que o ÚLTIMO export usou (escada de abertura
/// do FFmpegExportSink), para o log e o diagnóstico do relato de bug. Ex.:
/// "surface:c2.qti.avc.encoder ok", "buffer:c2.mtk.avc.encoder:0x15 ok |
/// falhas: surface:c2.mtk.avc.encoder=o encoder nao entregou o primeiro
/// pacote (partida travada)", "software:libopenh264 ok". Vazio = nenhum
/// export neste processo. Copia para `out` (terminado em zero) e devolve o
/// tamanho do texto.
usize last_export_encoder_path(char* out, usize capacity) noexcept;
/// Usado pelo sink; público só para a ponte de testes.
void set_last_export_encoder_path(const char* text) noexcept;
/// Só testes: a PRÓXIMA sessão do sink faz o 1o degrau segurar a confirmação
/// do 1o pacote até o quadro `frame` e então reprová-lo (CorruptData), como um
/// encoder de hardware cujo 1o pacote chega depois de o som já ter entrado.
/// Vale uma vez; 0 desliga.
void set_export_ladder_test_fault(u32 frame) noexcept;

} // namespace aurea
