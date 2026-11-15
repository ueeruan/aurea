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
/// Só testes (0.5.3): o encoder FFmpeg da PRÓXIMA sessão rearredonda o tempo
/// (kind 1, meia tolerância) ou descarta (kind 2) o pacote do quadro `frame`,
/// como encoders de aparelho. Vale uma vez; kind 0 desliga.
void set_export_encoder_test_quirk(u32 kind, u32 frame) noexcept;

// -----------------------------------------------------------------------------
// 0.5.3 (P2-1): memória da escada de encoders ENTRE aberturas do app. Um
// arquivo pequeno por aparelho: a primeira linha guarda a identidade (aparelho +
// versão do sistema + versão do app); identidade diferente = começa do zero.
// Tipos: 'F' degrau que falhou (conta as vezes), 'G' degrau confirmado no 1o
// pacote (apaga as falhas dele), 'C' encoder cujo VBR descartou quadros (vai em
// CBR), 'N' encoder que só andou sem as dicas offline. Sem caminho, só memória
// do processo. Nada de projeto, mídia ou conta: nomes de codec e tamanhos.
// -----------------------------------------------------------------------------
void configure_export_rung_memory(const char* path, const char* identity) noexcept;
void export_rung_memory_note(char kind, const char* key) noexcept;
/// Quantas vezes (0 = nunca ou mais velho que `maxAgeSeconds`; 0 = sem idade).
[[nodiscard]] u32 export_rung_memory_count(char kind, const char* key, u64 maxAgeSeconds) noexcept;
void export_rung_memory_forget(char kind, const char* key) noexcept;
/// O conteúdo, uma entrada por linha ("F 2 surface:c2.x.avc.encoder|avc|1080"), para o relato local.
usize export_rung_memory_text(char* out, usize capacity) noexcept;

} // namespace aurea
