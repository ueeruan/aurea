// =============================================================================
//  Aurea / export / ExportPacketLog.hpp
//
//  Registro durável dos pacotes de vídeo do export (Missão ASTRA, Export
//  Recovery v2). O encoder grava aqui cada pacote, em ordem, com checksum; o
//  MP4 final é montado deste registro só no fim (FFmpegExportSink).
//
//  Por que um registro e não o MP4 direto: um MP4 só é legível depois do
//  trailer. Se o app morre no quadro 8000 de 9000, o MP4 parcial não serve
//  para nada e o export recomeçava do zero. O registro é legível até o último
//  byte íntegro: a varredura valida cada pacote e corta no último keyframe do
//  trecho íntegro — dali o export continua com uma sessão nova do encoder (que
//  sempre começa por um keyframe). Nada é "consertado": pacote que não confere
//  é descartado e o trecho dele é renderizado de novo.
//
//  Durabilidade: antes de gravar cada keyframe o registro faz fflush + fsync,
//  então todo GOP completo anterior está no disco. Custo: um fsync por GOP
//  (~2 s de vídeo).
//
//  Formato (little-endian):
//    "AUREAPKT" u32 versão, u32 bytes do corpo, corpo, u64 FNV-1a do corpo
//    corpo: codec, largura, altura, taxa (num/den), base de tempo (num/den),
//           u64 chave da sessão, u32 quadros totais, u32 bytes de extradata,
//           extradata
//    registro: u32 'AURP', u32 quadro, u32 bytes, u32 flags (bit 0 = key),
//              u64 FNV-1a (quadro, bytes, flags, dados), dados
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace aurea::export_log {

inline constexpr u32 kVersion = 1;
/// Teto de um pacote (um quadro 8K intra de altíssima taxa cabe com folga).
inline constexpr u32 kMaxPacketBytes = 64u << 20;
inline constexpr u32 kMaxExtradataBytes = 1u << 20;

struct StreamInfo {
    u32 codec = 0;          ///< ExportCodec
    u32 width = 0;
    u32 height = 0;
    i32 rateNum = 0, rateDen = 0;           ///< taxa exata de saída
    i32 timebaseNum = 0, timebaseDen = 0;   ///< base de tempo dos pacotes
    /// Identidade do export (configuração + projeto congelado): um registro
    /// de OUTRO export nunca é continuado.
    u64 sessionKey = 0;
    u32 totalFrames = 0;                    ///< quadros do export inteiro (a tela mostra o quanto já foi)
    std::vector<u8> extradata;              ///< SPS/PPS (avcC/hvcC) do encoder

    /// Mesmo fluxo: tudo igual, inclusive os parâmetros do encoder. Sessões
    /// com SPS/PPS diferentes não podem dividir uma faixa MP4.
    [[nodiscard]] bool same_stream(const StreamInfo& other) const noexcept;
};

struct ScanResult {
    StreamInfo info;
    /// Quadros 0..validFrames−1 presentes, em ordem, com checksum correto.
    u32 validFrames = 0;
    /// Último keyframe do trecho íntegro: daqui a sessão nova continua (os
    /// quadros antes dele formam GOPs completos). 0 = recomeçar do zero.
    u32 resumeFrame = 0;
    /// Byte onde começa o registro de `resumeFrame` (o arquivo é cortado aqui).
    u64 resumeOffset = 0;
    u64 fileBytes = 0;
    u32 keyframes = 0;
};

/// Lê o cabeçalho e valida os registros em ordem. Para no primeiro registro
/// truncado/corrompido ou fora de ordem (o que vem depois não conta). Erro só
/// quando o cabeçalho não existe ou não confere.
[[nodiscard]] Status scan(const std::string& path, ScanResult& out) noexcept;

class Writer {
public:
    Writer() = default;
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer();

    /// Registro novo (apaga o anterior do mesmo caminho).
    [[nodiscard]] Status create(const std::string& path, const StreamInfo& info) noexcept;
    /// Continua um registro varrido: corta em `scan.resumeOffset` e espera o
    /// quadro `scan.resumeFrame`, que precisa ser keyframe. O fluxo da sessão
    /// nova precisa ser o mesmo (`StreamInfo::same_stream`).
    [[nodiscard]] Status resume(const std::string& path, const StreamInfo& info, const ScanResult& scan) noexcept;
    /// Um pacote, na ordem dos quadros. Keyframe: grava em disco (fsync) tudo
    /// o que veio antes, ANTES de escrevê-lo.
    [[nodiscard]] Status append(u32 frame, const u8* data, usize bytes, bool key) noexcept;
    /// fflush + fsync do que já foi escrito.
    [[nodiscard]] Status commit() noexcept;
    /// Fecha com commit. Idempotente.
    [[nodiscard]] Status close() noexcept;

    [[nodiscard]] bool open() const noexcept { return file_ != nullptr; }
    /// Próximo quadro esperado (= quadros no registro).
    [[nodiscard]] u32 frames() const noexcept { return next_; }
    /// Quadros já garantidos em disco (o último keyframe confirmado).
    [[nodiscard]] u32 durable_frames() const noexcept { return durable_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    std::FILE* file_ = nullptr;
    std::string path_;
    u32 next_ = 0;
    u32 durable_ = 0;
    bool needKey_ = true;
    Status fail(Errc code, const char* what) noexcept;
};

struct Packet {
    u32 frame = 0;
    bool key = false;
    std::vector<u8> data;
};

class Reader {
public:
    Reader() = default;
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    ~Reader();
    [[nodiscard]] Status open(const std::string& path) noexcept;
    [[nodiscard]] const StreamInfo& info() const noexcept { return info_; }
    /// Próximo pacote. `end` = fim do registro (sem erro). Checksum errado ou
    /// quadro fora de ordem = CorruptData (o registro mudou depois da varredura).
    [[nodiscard]] Status next(Packet& out, bool& end) noexcept;
    void close() noexcept;

private:
    std::FILE* file_ = nullptr;
    StreamInfo info_;
    u32 expected_ = 0;
};

/// Chave de sessão: FNV-1a de bytes arbitrários (configuração + projeto).
[[nodiscard]] u64 session_key(const void* data, usize bytes, u64 seed = 1469598103934665603ull) noexcept;

} // namespace aurea::export_log
