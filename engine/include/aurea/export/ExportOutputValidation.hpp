#pragma once
#include "aurea/core/Types.hpp"
namespace aurea {
// Validate the finalized temporary container using a DIFFERENT decoder from
// the encoder. Frame timestamps are checked against this absolute output clock.
struct ExportOutputValidation {
    u32 width = 0, height = 0, frames = 0;
    f64 fps = 30;
    bool audio = false;
    u64 stallNs = 30'000'000'000ull;
    /// Estrito (testes de host, guardas do export retomável): qualquer
    /// divergência de PTS, duração, contagem, dimensão ou áudio reprova o
    /// arquivo. Tolerante (os apps, EngineConfig::strictExportVerification =
    /// false): encoders de hardware diferem legitimamente nisso (B-quadros,
    /// arredondamento de duração, padding, recorte de 16) — vira AVISO
    /// (ExportSink::validation_warnings) e o arquivo é publicado. Tolerante só
    /// reprova o que não serve: não abre, vazio, sem faixa de vídeo ou nenhum
    /// quadro decodificável.
    bool strict = true;
};
} // namespace aurea
