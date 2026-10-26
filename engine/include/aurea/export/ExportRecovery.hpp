#pragma once

#include "aurea/project/Project.hpp"

#include <functional>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace aurea::export_recovery {

// Version 1 deliberately restarts from zero. Codec input acceptance is not a
// durable MP4 checkpoint; no incomplete container is ever appended/reused.
enum class State : u32 { Prepared = 0, Failed = 1, Complete = 2 };
struct Dependency {
    std::string path;
    u64 bytes = 0;
    /// Hash of every byte (four interleaved FNV-1a lanes over 64-bit words plus
    /// the tail), for accidental-change detection; ~8x faster than byte FNV.
    u64 contentHash = 0;
};
struct Package {
    ExportSettings settings{};
    CompositionId composition{};
    State state = State::Prepared;
    Errc result = Errc::Ok;
    u32 acceptedFrames = 0; // Diagnostic only; NOT a resume boundary.
    std::string outputPath;
    std::vector<Dependency> dependencies;
    /// Fontes que a plataforma abre por URI (content:// do Android), sem
    /// recuperação: não viram dependência verificada aqui. O checkpoint do
    /// export (export/ExportCheckpoint.hpp) as identifica pela plataforma ou
    /// não retoma. Não é gravado no pacote.
    std::vector<std::string> uriSources;
    std::vector<u8> projectBytes;
};
using PathResolver = std::function<std::string(const std::string&)>;
inline constexpr usize kMaxProjectBytes = 64ull << 20;
inline constexpr usize kMaxPackageBytes = kMaxProjectBytes + (4ull << 20);

// Only assets reachable from `composition` (nested included) pass through the
// resolver. `retainedImages` lists packed image AssetIds whose decoded pixels
// the export already owns; without recovery they skip the resolver too.
// Caller holds the model lock only while serializing. The strict clone preserves IDs,
// nested compositions and all authored state. Source paths and the effective
// fonts are frozen in the clone; the open editor document is never modified.
[[nodiscard]] Status capture(const Project& project, CompositionId composition,
                             const ExportSettings& settings, const PathResolver& resolve,
                             std::unique_ptr<Project>& frozen, Package& package,
                             bool requirePersistentSources = true,
                             const std::vector<u64>* retainedImages = nullptr) noexcept;
// Serialize the editor under its lock, then hand ownership of those immutable
// bytes to this operation. URI materialization, fonts and dependency reads may
// block; they must not hold the live editor's model/status lock.
[[nodiscard]] Status capture_snapshot(std::vector<u8> initial, CompositionId composition,
                             const ExportSettings& settings, const PathResolver& resolve,
                             std::unique_ptr<Project>& frozen, Package& package,
                             bool requirePersistentSources = true,
                             const std::vector<u64>* retainedImages = nullptr) noexcept;
/// Progresso do hash das fontes (o "Preparando" do export). `bytesTotal` é a
/// soma dos tamanhos, conhecida antes do primeiro byte lido; `bytesDone` anda a
/// cada bloco de 64 KiB e `heartbeatNs` recebe o relógio monotônico no mesmo
/// passo, para quem vigia a preparação distinguir "lendo um vídeo grande" de
/// "travado". Todos opcionais.
struct FingerprintProgress {
    std::atomic<u64>* bytesDone = nullptr;
    std::atomic<u64>* bytesTotal = nullptr;
    std::atomic<u64>* heartbeatNs = nullptr;
};
// Expensive source hashing runs outside the model lock, with a fixed 64 KiB
// buffer (memory never grows with the source size or the video duration).
// Missing, unsupported URI or concurrently changing sources fail. `cancel`
// is polled between blocks: cancelling returns Errc::Cancelled within one
// 64 KiB read and leaves `package` unchanged.
[[nodiscard]] Status fingerprint_sources(Package& package, const std::atomic<bool>* cancel = nullptr,
                                         const FingerprintProgress* progress = nullptr) noexcept;
[[nodiscard]] Status validate_sources(const Package& package) noexcept;
[[nodiscard]] Status write(const std::string& path, const Package& package) noexcept;
// Rejects unknown versions, checksum/truncation, partial projects and changed
// source bytes before publishing any result to the caller.
[[nodiscard]] Status read(const std::string& path, Package& package,
                          std::unique_ptr<Project>& frozen) noexcept;
[[nodiscard]] Status write_project(const std::string& path, const Package& package) noexcept;
// Direct regular outputs inside an explicitly owned scratch directory only.
// Preserve current output, restart packages/companions, subdirectories/symlinks
// and every in-process startup staging file (native open may outlive timeout).
[[nodiscard]] Result<u32> cleanup_temporary_outputs(const std::string& directory,
    const std::string& currentOutput, const std::atomic<bool>* cancel = nullptr) noexcept;

} // namespace aurea::export_recovery
