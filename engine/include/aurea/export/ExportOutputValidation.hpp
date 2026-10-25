#pragma once
#include "aurea/core/Types.hpp"
#include <atomic>
namespace aurea {
// Validate the finalized temporary container using a DIFFERENT decoder from
// the encoder. Frame timestamps are checked against this absolute output clock.
struct ExportOutputValidation {
    u32 width = 0, height = 0, frames = 0;
    f64 fps = 30;
    bool audio = false;
    u64 stallNs = 30'000'000'000ull;
    /// Estrito (padrão; testes de host e guardas do export retomável):
    /// qualquer divergência de PTS, duração, contagem, dimensão ou áudio
    /// reprova o arquivo. Tolerante: vira aviso (ExportSink::validation_warnings)
    /// e só reprova o que não abre, vazio, sem vídeo ou sem quadro decodificável.
    bool strict = true;
    /// Quadros de vídeo já conferidos (0.3.0): o motor o mostra no progresso.
    /// Sem isso a mensagem ficava parada em "validando" e as telas cancelavam
    /// por falta de progresso (180 s) um export longo ou a 200 qps já pronto.
    std::atomic<u64>* validatedFrames = nullptr;
};
} // namespace aurea
