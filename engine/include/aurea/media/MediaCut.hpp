// =============================================================================
//  Aurea / media / MediaCut.hpp
//
//  1.0.2 — "importar só o trecho cortado" (pedido de um usuário: cortar 10 min
//  de um vídeo de 1 h da galeria e o app copiar a hora inteira para dentro do
//  projeto). Em vez da cópia byte a byte do arquivo inteiro, o trecho é
//  REMUXADO (cópia de pacotes, sem recodificar): do keyframe ≤ entrada − folga
//  até o primeiro keyframe depois de saída + folga, vídeo e áudio, com os
//  instantes deslocados para o trecho começar em 0. Matriz de rotação, HDR,
//  Dolby Vision e a etiqueta hvc1 seguem iguais.
//
//  O projeto continua na régua do ORIGINAL: o instante do 1º quadro do trecho
//  vira `Asset::windowStartUs` e os decoders somam esse instante de volta
//  (SourceWindow.hpp). Nada de offset, keyframe ou curva de tempo muda.
//
//  Qualquer falha (container que não é MP4/MOV, arquivo sem seek, remux
//  recusado, trecho grande demais para valer a pena) devolve erro e o app faz
//  a cópia inteira de sempre.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"

#include <atomic>
#include <string>

namespace aurea {

struct MediaCutRequest {
    /// O original. Caminho que o libavformat abre (no Android, "/proc/self/fd/N"
    /// de um descritor aberto pelo app).
    std::string source;
    /// O arquivo do trecho (.mp4 ou .mov; outra extensão = o formato da fonte).
    /// Gravado num temporário ao lado e renomeado no fim.
    std::string destination;
    /// Corte do usuário, na régua do original (µs).
    i64 inUs = 0;
    i64 outUs = 0;
    /// Folga de cada lado. < 0 = a política (cut_margin_us).
    i64 marginUs = -1;
    /// Só corta quando a janela (com folga) é menor que esta fração da fonte.
    f64 maxFraction = 0.7;
    const std::atomic<bool>* cancel = nullptr;
};

struct MediaCutResult {
    /// Instante, na régua do original, do 1º quadro do trecho (o keyframe).
    i64 windowStartUs = 0;
    /// Até onde o trecho cobre, na régua do original.
    i64 windowEndUs = 0;
    i64 sourceDurationUs = 0;
    u64 sourceBytes = 0;
    u64 bytes = 0;
};

/// Folga de cada lado do corte: max(2 s, 5% do trecho).
[[nodiscard]] i64 cut_margin_us(i64 inUs, i64 outUs) noexcept;

/// Remuxa o trecho. NotSupported = não vale a pena / container sem suporte;
/// outros erros = falha de leitura/escrita. Em qualquer erro nada fica no destino.
[[nodiscard]] Status cut_media(const MediaCutRequest& request, MediaCutResult& result) noexcept;

// -----------------------------------------------------------------------------
// Importação com reaproveitamento (o que os apps chamam)
// -----------------------------------------------------------------------------
struct TrimmedImportRequest {
    std::string source;      ///< como em MediaCutRequest::source
    std::string directory;   ///< pasta de mídia do app (o índice `.aurea-cuts` mora nela)
    std::string originRef;   ///< identidade estável do original (URI + tamanho, id do PHAsset + data)
    std::string extension;   ///< "mp4" / "mov" (vazio = "mp4")
    i64 inUs = 0;
    i64 outUs = 0;
    const std::atomic<bool>* cancel = nullptr;
};

struct TrimmedImportResult {
    std::string path;        ///< o arquivo do trecho (novo ou reaproveitado)
    i64 windowStartUs = 0;
    i64 windowEndUs = 0;
    u64 bytes = 0;
    u64 sourceBytes = 0;
    bool reused = false;     ///< um trecho que já cobria o corte
};

/// O trecho do corte, reaproveitando um trecho do MESMO original que já o
/// cobre; se só sobrepõe, o novo trecho cobre a união (os próximos cortes ali
/// reaproveitam). Erro = o app copia o arquivo inteiro, como antes.
[[nodiscard]] Status import_trimmed_media(const TrimmedImportRequest& request, TrimmedImportResult& result) noexcept;

} // namespace aurea
