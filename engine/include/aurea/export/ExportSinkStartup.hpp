#pragma once

#include "aurea/export/ExportSink.hpp"
#include "aurea/export/ExportWatchdog.hpp"

namespace aurea {

struct ExportSinkStartupLimits {
    u64 stallNs = kExportWorkerHangNs;
    u64 hardNs = 120'000'000'000ull;
    u64 pollNs = 5'000'000ull;
};

// Diagnostics belong to this result, rather than to a destroyed platform sink.
// Call status() on the current result after moving it, never retain its view.
struct ExportSinkStartupResult {
    std::unique_ptr<ExportSink> sink;
    Errc code = Errc::Ok;
    char detail[256]{};
    [[nodiscard]] Status status() const noexcept { return Status{code, detail}; }
};

// The platform sink opens a unique sibling staging file on a worker. If its
// open call hangs, the detached worker retains all input/state/atomics and may
// only abort its own staging file AFTER open returns. Success returns a proxy
// that publishes the requested filename only after finish succeeds.
[[nodiscard]] ExportSinkStartupResult open_export_sink_startup(
    std::unique_ptr<ExportSink> sink, const char* outputPath,
    const VideoStreamConfig& video, const AudioStreamConfig* audio,
    const std::atomic<bool>* cancellation, std::atomic<u64>* heartbeat,
    ExportSinkStartupLimits limits = {}) noexcept;

} // namespace aurea
