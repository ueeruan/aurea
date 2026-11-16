// =============================================================================
//  Aurea / export / EncoderPacketTiming.hpp
//
//  0.5.3 (P0-5): o pacote que o encoder devolve × os quadros que o sink entregou.
//  O sink exige um pacote por quadro, na ordem. Antes, qualquer diferença no
//  tempo do pacote ("omitiu, duplicou ou reordenou quadro") encerrava a
//  tentativa e a escada descia para o mesmo encoder em Baseline e depois para o
//  software — minutos de export nos aparelhos cujo encoder:
//
//   - REARREDONDA o tempo (milissegundos, grade da taxa de quadros): o pacote é
//     do quadro esperado com o relógio dele. O sink usa o relógio PRÓPRIO
//     (pendingVideo_), então basta reconhecer: dentro de meio quadro = o mesmo.
//   - DESCARTA quadros no controle de taxa (o OMX da Qualcomm em VBR, medido no
//     Moto g52; outros encoders nunca foram medidos): o pacote é de um quadro
//     posterior. Não há como inventar o quadro que faltou no fluxo H.264/HEVC;
//     o sink devolve "refazer" e a MESMA tentativa refaz o trecho a partir do
//     último GOP íntegro, com o modo de taxa constante aprendido para aquele
//     encoder (FFmpegExportSink: rung memory "cbr").
//
//  Função pura (testes do host).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <cmath>

namespace aurea::export_timing {

enum class PacketMatch : u8 {
    Exact = 0,    ///< o tempo do quadro mais antigo pendente
    Retimed = 1,  ///< o mesmo quadro, com o tempo rearredondado pelo encoder (até meio quadro)
    Dropped = 2,  ///< um quadro POSTERIOR: `skipped` quadros pendentes nunca vão sair
    Unknown = 3,  ///< nenhum quadro pendente tem esse tempo (duplicado, reordenado, lixo)
};

struct MatchResult {
    PacketMatch kind = PacketMatch::Unknown;
    u32 skipped = 0;   ///< Dropped: quantos quadros pendentes ficaram para trás
};

/// Tolerância do "mesmo quadro": meio quadro menos 1 µs (nunca alcança o vizinho).
[[nodiscard]] inline i64 retime_tolerance_us(f64 fps) noexcept {
    if (!(fps > 0.0) || !std::isfinite(fps)) return 0;
    const i64 half = static_cast<i64>(std::floor(500'000.0 / fps)) - 1;
    return half > 0 ? half : 0;
}

/// `expectedUs(i)`: o tempo (µs) do i-ésimo quadro pendente, do mais antigo;
/// `count`: quantos estão pendentes. Olha no máximo `lookahead` quadros adiante.
template <typename ExpectedAt>
[[nodiscard]] MatchResult match_packet(ExpectedAt&& expectedUs, u32 count, i64 packetUs, i64 toleranceUs,
                                       u32 lookahead = 8) noexcept {
    MatchResult r;
    if (!count) return r;
    const i64 head = expectedUs(0u);
    if (packetUs == head) { r.kind = PacketMatch::Exact; return r; }
    const auto near = [&](i64 a, i64 b) noexcept { return (a > b ? a - b : b - a) <= toleranceUs; };
    if (near(packetUs, head)) {
        // Mais perto do mais antigo que do seguinte (dois quadros nunca cabem na tolerância).
        r.kind = PacketMatch::Retimed;
        return r;
    }
    const u32 last = count < lookahead + 1 ? count : lookahead + 1;
    for (u32 i = 1; i < last; ++i) {
        const i64 at = expectedUs(i);
        if (packetUs == at || near(packetUs, at)) {
            r.kind = PacketMatch::Dropped;
            r.skipped = i;
            return r;
        }
    }
    return r;
}

} // namespace aurea::export_timing
