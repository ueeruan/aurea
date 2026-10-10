#pragma once

#include "aurea/project/Project.hpp"

#include <functional>
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
    u64 contentHash = 0; // FNV-1a over every byte, for accidental-change detection.
};
struct Package {
    ExportSettings settings{};
    CompositionId composition{};
    State state = State::Prepared;
    Errc result = Errc::Ok;
    u32 acceptedFrames = 0; // Diagnostic only; NOT a resume boundary.
    std::string outputPath;
    std::vector<Dependency> dependencies;
    std::vector<u8> projectBytes;
};
using PathResolver = std::function<std::string(const std::string&)>;
inline constexpr usize kMaxProjectBytes = 64ull << 20;
inline constexpr usize kMaxPackageBytes = kMaxProjectBytes + (4ull << 20);

// Caller holds the model lock. The strict serialization clone preserves IDs,
// nested compositions and all authored state. Source paths and the effective
// fonts are frozen in the clone; the open editor document is never modified.
[[nodiscard]] Status capture(const Project& project, CompositionId composition,
                             const ExportSettings& settings, const PathResolver& resolve,
                             std::unique_ptr<Project>& frozen, Package& package) noexcept;
// Expensive source hashing runs outside the model lock, with a fixed 64 KiB
// buffer. Missing, unsupported URI or concurrently changing sources fail.
[[nodiscard]] Status fingerprint_sources(Package& package) noexcept;
[[nodiscard]] Status validate_sources(const Package& package) noexcept;
[[nodiscard]] Status write(const std::string& path, const Package& package) noexcept;
// Rejects unknown versions, checksum/truncation, partial projects and changed
// source bytes before publishing any result to the caller.
[[nodiscard]] Status read(const std::string& path, Package& package,
                          std::unique_ptr<Project>& frozen) noexcept;
[[nodiscard]] Status write_project(const std::string& path, const Package& package) noexcept;

} // namespace aurea::export_recovery
