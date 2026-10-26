// =============================================================================
//  Aurea / export / ExportCheckpoint.hpp
//
//  CheckpointManager do Export Engine 2.0 (0.3.2): onde mora o registro de
//  pacotes (export/ExportPacketLog.hpp) de um export retomável, a identidade
//  que decide se um export novo pode continuá-lo e o pequeno manifesto que
//  acompanha cada registro.
//
//  Segmento = GOP fechado do registro. Um checkpoint só vale depois de o GOP
//  inteiro estar no disco com o checksum de cada pacote (o registro faz fsync
//  antes de gravar o keyframe que fecha o GOP anterior).
//
//  Identidade (sessionKey, gravada no cabeçalho do registro e no nome do
//  arquivo): o projeto congelado normalizado (sem data, playhead, zoom,
//  seleção...), as configurações EFETIVAS do export (tamanho, fps, codec,
//  taxa, perfil, degrau da escada, áudio, dither, quadros), a impressão de
//  cada fonte (tamanho + hash do conteúdo inteiro, export_recovery) e a versão
//  do motor. Qualquer diferença = outro registro: o export recomeça do zero e
//  o registro antigo é apagado. O encoder é conferido à parte, pelo próprio
//  registro (SPS/PPS iguais, StreamInfo::same_stream): outro encoder recomeça
//  do quadro 0, porque fluxos de encoders diferentes não podem ser emendados
//  numa faixa MP4.
//
//  Arquivos, numa pasta do app (EngineConfig::exportTemporaryDirectory; sem
//  ela, `<cache>/export-checkpoints`; sem cache, a pasta da saída):
//    aurea-ckpt-<chave>.aurea-pkt        registro de pacotes
//    aurea-ckpt-<chave>.aurea-pkt.pcm    PCM do render (só sessão do zero)
//    aurea-ckpt-<chave>.aurea-pkt.ckpt   manifesto (texto, gravado atômico)
//  Só existe um checkpoint por vez: um export novo apaga os de outra chave
//  (o disco ocupado nunca passa de um export).
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/project/Project.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace aurea::export_checkpoint {

/// Quantas vezes um MESMO checkpoint pode ser continuado depois de uma falha
/// (o encoder caiu, o processo morreu). Pausas (o iOS foi para segundo plano)
/// não contam. Sem progresso desde a última continuação, nenhuma: a escada
/// segue para o próximo degrau.
inline constexpr u32 kMaxFailureResumes = 3;

enum class SessionState : u32 {
    Running = 0,   ///< uma sessão está gravando (ou morreu sem dizer nada)
    Paused = 1,    ///< parou pela suspensão do app: continua sem contar falha
    Failed = 2,    ///< falhou com quadros salvos (a falha já foi contada)
};

struct Manifest {
    u64 sessionKey = 0;
    u64 requestKey = 0;        ///< projeto do editor + configurações pedidas, sem o degrau
    u32 safeMode = 0;          ///< degrau da escada (ExportSettings::safeMode) que gravou
    u32 totalFrames = 0;
    SessionState state = SessionState::Running;
    u32 failureResumes = 0;    ///< continuações depois de falha/morte do processo
    u32 lastResumeFrame = 0;   ///< de onde a última sessão partiu (0 = do zero)
};

/// Pasta dos checkpoints (ver o cabeçalho). Vazio = nenhuma utilizável.
[[nodiscard]] std::string directory(const std::string& temporaryDirectory, const std::string& cacheDirectory,
                                    const std::string& outputPath);
[[nodiscard]] std::string log_path(const std::string& directory, u64 sessionKey);
[[nodiscard]] inline std::string manifest_path(const std::string& logPath) { return logPath + ".ckpt"; }
/// O nome que o FFmpegExportSink dá ao PCM do render.
[[nodiscard]] inline std::string spill_path(const std::string& logPath) { return logPath + ".pcm"; }

[[nodiscard]] Status write_manifest(const std::string& logPath, const Manifest& manifest) noexcept;
[[nodiscard]] bool read_manifest(const std::string& logPath, Manifest& manifest) noexcept;
/// Apaga registro, PCM e manifesto. Idempotente.
void remove(const std::string& logPath) noexcept;
/// Apaga todo checkpoint da pasta exceto `keepLog` (vazio = todos). Só toca em
/// arquivos com o prefixo e as extensões acima. Devolve quantos registros saíram.
u32 remove_others(const std::string& directory, const std::string& keepLog) noexcept;

/// Identidade do PROJETO: os bytes serializados, recarregados e regravados sem
/// o que não aparece no vídeo (datas, playhead, velocidade/loop da prévia,
/// zoom/rolagem/seleção da timeline, ajustes de export guardados no
/// documento). Reabrir o mesmo projeto depois de o app morrer dá a mesma
/// chave. 0 = bytes ilegíveis (nunca continua).
[[nodiscard]] u64 project_key(const std::vector<u8>& projectBytes) noexcept;
/// Projeto + composição + configurações PEDIDAS (o degrau da escada fica de
/// fora: é o que o manifesto informa ao app que reabriu).
[[nodiscard]] u64 request_key(u64 projectKey, CompositionId composition, const ExportSettings& requested) noexcept;
/// Mistura um valor numa chave (FNV-1a dos 8 bytes).
[[nodiscard]] u64 mix(u64 key, u64 value) noexcept;
[[nodiscard]] u64 mix_bytes(u64 key, const void* data, usize bytes) noexcept;
/// Versão e build do motor: outro build pode renderizar diferente.
[[nodiscard]] u64 build_identity() noexcept;

/// Identidade LIMITADA de uma fonte para a chave do checkpoint: tamanho, data
/// de modificação (quando a plataforma dá) e o hash de blocos amostrados em
/// posições fixas — o primeiro e o último MiB e 16 blocos de 64 KiB em frações
/// fixas do tamanho (até 3 MiB lidos por arquivo, qualquer que seja o tamanho;
/// um arquivo menor que isso é lido inteiro). O "Preparando" não cresce com a
/// mídia do projeto. O hash do arquivo INTEIRO continua no pacote de
/// recuperação (export_recovery::fingerprint_sources).
struct SourceIdentity {
    u64 bytes = 0;
    i64 modified = 0;      ///< data de modificação na unidade da plataforma; 0 = desconhecida
    u64 sampleHash = 0;
};
inline constexpr u64 kSampleEdgeBytes = 1ull << 20;
inline constexpr u32 kSampleBlocks = 16;
inline constexpr u64 kSampleBlockBytes = 64ull << 10;
/// Bytes que a identidade de um arquivo de `size` bytes lê.
[[nodiscard]] u64 sampled_bytes(u64 size) noexcept;
/// Por caminho de arquivo (std::filesystem: tamanho e data). `bytesDone`
/// anda com os blocos lidos; `cancel` é conferido a cada bloco.
[[nodiscard]] Status file_identity(const std::string& path, SourceIdentity& out,
                                   const std::atomic<bool>* cancel = nullptr, std::atomic<u64>* bytesDone = nullptr) noexcept;
/// Por descritor (Android: o fd que o ContentResolver deu para um content://):
/// fstat (só arquivo regular, com tamanho e data) + os mesmos blocos por
/// leitura posicional. Descritor sem tamanho/posição (pipe, provedor virtual)
/// = NotSupported: a fonte não é verificável e o export não retoma.
[[nodiscard]] Status fd_identity(int fd, SourceIdentity& out,
                                 const std::atomic<bool>* cancel = nullptr, std::atomic<u64>* bytesDone = nullptr) noexcept;
/// Os mesmos blocos amostrados de um buffer na memória (pixels já retidos).
[[nodiscard]] u64 memory_sample_hash(const u8* data, usize size) noexcept;
[[nodiscard]] u64 mix_identity(u64 key, const SourceIdentity& identity) noexcept;

/// O que o app consulta antes de escolher o degrau depois de o processo
/// morrer: um checkpoint deste pedido que vale a pena continuar.
struct Candidate {
    bool valid = false;
    u32 resumeFrame = 0;   ///< último keyframe íntegro (de onde continua)
    u32 totalFrames = 0;
    u32 safeMode = 0;      ///< o degrau que gravou: é nele que continua
};
/// Procura na pasta o manifesto com `requestKey` e confere o registro. Só vale
/// com progresso desde a última continuação e dentro de kMaxFailureResumes.
[[nodiscard]] Candidate find_candidate(const std::string& directory, u64 requestKey) noexcept;

/// Espaço para o export com registro: o registro inteiro (≈ o vídeo), o PCM
/// do render e o MP4 montado no fim, juntos, menos o que já está gravado. A
/// mesma folga da conferência do caminho direto (metade da estimativa: o
/// bitrate pedido é pessimista).
[[nodiscard]] bool log_fits(u64 available, u64 videoEstimate, u64 pcmBytes, u64 loggedBytes) noexcept;
/// Só testes: o espaço livre que a decisão do checkpoint enxerga (0 = o real).
/// A conferência do MP4 direto continua com o espaço real.
void set_available_bytes_for_tests(u64 bytes) noexcept;
[[nodiscard]] u64 available_bytes_for_tests() noexcept;

} // namespace aurea::export_checkpoint
