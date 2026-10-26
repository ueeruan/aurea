#include "aurea/export/ExportCheckpoint.hpp"
#include "aurea/export/ExportPacketLog.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Version.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <new>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace aurea::export_checkpoint {
namespace {
constexpr char kPrefix[] = "aurea-ckpt-";
constexpr char kLogExtension[] = ".aurea-pkt";
constexpr u32 kManifestVersion = 1;
constexpr u64 kSeed = 1469598103934665603ull;

std::string utf8(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
bool ends_with(const std::string& s, const char* suffix) {
    const usize n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}
} // namespace

u64 mix_bytes(u64 key, const void* data, usize bytes) noexcept {
    return export_log::session_key(data, bytes, key ? key : kSeed);
}
u64 mix(u64 key, u64 value) noexcept { return mix_bytes(key, &value, sizeof(value)); }

u64 build_identity() noexcept {
    // Versão do motor + o build desta unidade: um app atualizado entre a
    // interrupção e a retomada pode renderizar diferente, então não emenda.
    static constexpr char id[] = AUREA_VERSION_LABEL " " __DATE__ " " __TIME__;
    return mix_bytes(kSeed, id, sizeof(id) - 1);
}

namespace {
struct Range { u64 offset, bytes; };
/// As faixas amostradas de um arquivo de `size` bytes, em ordem fixa.
std::vector<Range> sample_ranges(u64 size) {
    std::vector<Range> ranges;
    const u64 whole = 2 * kSampleEdgeBytes + kSampleBlocks * kSampleBlockBytes;
    if (size <= whole) {
        for (u64 at = 0; at < size; at += kSampleBlockBytes) ranges.push_back({at, std::min(kSampleBlockBytes, size - at)});
        return ranges;
    }
    for (u64 at = 0; at < kSampleEdgeBytes; at += kSampleBlockBytes) ranges.push_back({at, kSampleBlockBytes});
    for (u32 k = 0; k < kSampleBlocks; ++k) {
        // Frações (k+1)/(n+1) do tamanho, centradas no bloco.
        const u64 center = size / (kSampleBlocks + 1) * (k + 1);
        ranges.push_back({std::min(center - kSampleBlockBytes / 2, size - kSampleBlockBytes), kSampleBlockBytes});
    }
    for (u64 at = size - kSampleEdgeBytes; at < size; at += kSampleBlockBytes) ranges.push_back({at, kSampleBlockBytes});
    return ranges;
}

/// Leitura posicional com deslocamento de 64 bits em todo ABI.
bool read_at(int fd, u64 offset, u8* out, usize bytes) noexcept {
#if defined(_WIN32)
    if (_lseeki64(fd, static_cast<__int64>(offset), SEEK_SET) < 0) return false;
    usize done = 0;
    while (done < bytes) {
        const int got = _read(fd, out + done, static_cast<unsigned>(bytes - done));
        if (got <= 0) return false;
        done += static_cast<usize>(got);
    }
    return true;
#else
    usize done = 0;
    while (done < bytes) {
#if defined(__ANDROID__) && !defined(__LP64__)
        const ssize_t got = ::pread64(fd, out + done, bytes - done, static_cast<off64_t>(offset + done));
#else
        const ssize_t got = ::pread(fd, out + done, bytes - done, static_cast<off_t>(offset + done));
#endif
        if (got <= 0) return false;
        done += static_cast<usize>(got);
    }
    return true;
#endif
}

Status sample_fd(int fd, u64 size, u64& hash, const std::atomic<bool>* cancel, std::atomic<u64>* bytesDone) noexcept {
    try {
        std::vector<u8> block(kSampleBlockBytes);
        u64 h = mix(kSeed, size);
        for (const Range& r : sample_ranges(size)) {
            if (cancel && cancel->load(std::memory_order_acquire)) return Errc::Cancelled;
            if (!read_at(fd, r.offset, block.data(), static_cast<usize>(r.bytes)))
                return Status{Errc::IoError, "ler bloco amostrado da fonte"};
            h = mix(mix_bytes(h, block.data(), static_cast<usize>(r.bytes)), r.offset);
            if (bytesDone) bytesDone->fetch_add(r.bytes, std::memory_order_relaxed);
        }
        hash = h ? h : 1;
        return OkStatus;
    } catch (const std::bad_alloc&) { return Errc::OutOfMemory; } catch (...) { return Errc::IoError; }
}
} // namespace

u64 sampled_bytes(u64 size) noexcept {
    const u64 whole = 2 * kSampleEdgeBytes + kSampleBlocks * kSampleBlockBytes;
    return size <= whole ? size : whole;
}

Status file_identity(const std::string& path, SourceIdentity& out, const std::atomic<bool>* cancel,
                     std::atomic<u64>* bytesDone) noexcept {
    try {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path file = fs::u8path(path);
        if (!fs::is_regular_file(file, ec) || ec) return Errc::MediaSourceMissing;
        const u64 size = static_cast<u64>(fs::file_size(file, ec));
        if (ec) return Errc::IoError;
        const auto time = fs::last_write_time(file, ec);
        if (ec) return Errc::IoError;
        std::FILE* f = fileio::open_file(path, "rb");
        if (!f) return Errc::MediaSourceMissing;
        SourceIdentity id;
        id.bytes = size;
        id.modified = static_cast<i64>(time.time_since_epoch().count());
#if defined(_WIN32)
        const Status sampled = sample_fd(_fileno(f), size, id.sampleHash, cancel, bytesDone);
#else
        const Status sampled = sample_fd(fileno(f), size, id.sampleHash, cancel, bytesDone);
#endif
        std::fclose(f);
        if (!sampled.ok()) return sampled;
        out = id;
        return OkStatus;
    } catch (...) { return Errc::IoError; }
}

Status fd_identity(int fd, SourceIdentity& out, const std::atomic<bool>* cancel, std::atomic<u64>* bytesDone) noexcept {
    if (fd < 0) return Errc::InvalidArgument;
    SourceIdentity id;
#if defined(_WIN32)
    struct _stat64 st{};
    if (_fstat64(fd, &st) != 0 || !(st.st_mode & _S_IFREG) || st.st_size <= 0)
        return Status{Errc::NotSupported, "fonte sem arquivo regular verificavel"};
    id.bytes = static_cast<u64>(st.st_size);
    id.modified = static_cast<i64>(st.st_mtime);
#else
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
        return Status{Errc::NotSupported, "fonte sem arquivo regular verificavel"};
    id.bytes = static_cast<u64>(st.st_size);
#if defined(__APPLE__)
    id.modified = static_cast<i64>(st.st_mtimespec.tv_sec) * 1000000000ll + st.st_mtimespec.tv_nsec;
#else
    id.modified = static_cast<i64>(st.st_mtim.tv_sec) * 1000000000ll + st.st_mtim.tv_nsec;
#endif
#endif
    if (const Status s = sample_fd(fd, id.bytes, id.sampleHash, cancel, bytesDone); !s.ok()) return s;
    out = id;
    return OkStatus;
}

u64 memory_sample_hash(const u8* data, usize size) noexcept {
    if (!data && size) return 0;
    try {
        u64 h = mix(kSeed, size);
        for (const Range& r : sample_ranges(size))
            h = mix(mix_bytes(h, data + r.offset, static_cast<usize>(r.bytes)), r.offset);
        return h ? h : 1;
    } catch (...) { return 0; }
}

u64 mix_identity(u64 key, const SourceIdentity& id) noexcept {
    return mix(mix(mix(key, id.bytes), static_cast<u64>(id.modified)), id.sampleHash);
}

std::string directory(const std::string& temporaryDirectory, const std::string& cacheDirectory,
                      const std::string& outputPath) {
    try {
        std::filesystem::path dir;
        if (!temporaryDirectory.empty()) dir = std::filesystem::u8path(temporaryDirectory);
        else if (!cacheDirectory.empty()) dir = std::filesystem::u8path(cacheDirectory) / "export-checkpoints";
        else if (!outputPath.empty()) dir = std::filesystem::u8path(outputPath).parent_path();
        if (dir.empty()) return {};
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!std::filesystem::is_directory(dir, ec) || ec) return {};
        return utf8(dir);
    } catch (...) { return {}; }
}

std::string log_path(const std::string& dir, u64 sessionKey) {
    char name[64];
    std::snprintf(name, sizeof(name), "%s%016" PRIx64 "%s", kPrefix, sessionKey, kLogExtension);
    return utf8(std::filesystem::u8path(dir) / name);
}

Status write_manifest(const std::string& logPath, const Manifest& m) noexcept {
    try {
        char text[384];
        const int n = std::snprintf(text, sizeof(text),
            "aurea-checkpoint %u\nsession %016" PRIx64 "\nrequest %016" PRIx64 "\nsafeMode %u\ntotalFrames %u\n"
            "state %u\nfailureResumes %u\nlastResumeFrame %u\n",
            kManifestVersion, m.sessionKey, m.requestKey, m.safeMode, m.totalFrames,
            static_cast<u32>(m.state), m.failureResumes, m.lastResumeFrame);
        if (n <= 0 || n >= static_cast<int>(sizeof(text))) return Errc::InvalidArgument;
        return fileio::write_atomic(manifest_path(logPath), text, static_cast<usize>(n), {true, false});
    } catch (const std::bad_alloc&) { return Errc::OutOfMemory; } catch (...) { return Errc::IoError; }
}

bool read_manifest(const std::string& logPath, Manifest& out) noexcept {
    try {
        std::vector<u8> bytes;
        if (!fileio::read_all(manifest_path(logPath), bytes, 4096)) return false;
        std::string text(bytes.begin(), bytes.end());
        Manifest m;
        unsigned version = 0, safeMode = 0, total = 0, state = 0, failures = 0, last = 0;
        unsigned long long session = 0, request = 0;
        if (std::sscanf(text.c_str(),
                "aurea-checkpoint %u\nsession %llx\nrequest %llx\nsafeMode %u\ntotalFrames %u\nstate %u\n"
                "failureResumes %u\nlastResumeFrame %u", &version, &session, &request, &safeMode, &total, &state,
                &failures, &last) != 8 || version != kManifestVersion || state > 2)
            return false;
        m.sessionKey = session; m.requestKey = request; m.safeMode = safeMode; m.totalFrames = total;
        m.state = static_cast<SessionState>(state); m.failureResumes = failures; m.lastResumeFrame = last;
        out = m;
        return true;
    } catch (...) { return false; }
}

void remove(const std::string& logPath) noexcept {
    if (logPath.empty()) return;
    try {
        // O manifesto primeiro: sem ele o registro nunca é continuado, mesmo
        // que o arquivo do registro não saia agora.
        (void)fileio::remove_file(manifest_path(logPath));
        (void)fileio::remove_file(logPath);
        (void)fileio::remove_file(spill_path(logPath));
    } catch (...) {}
}

u32 remove_others(const std::string& dir, const std::string& keepLog) noexcept {
    if (dir.empty()) return 0;
    u32 removed = 0;
    try {
        namespace fs = std::filesystem;
        std::error_code ec;
        std::vector<std::string> logs;
        for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
            const auto status = it->symlink_status(ec);
            if (ec) break;
            if (!fs::is_regular_file(status)) continue;
            const std::string name = utf8(it->path().filename());
            if (name.rfind(kPrefix, 0) != 0) continue;
            // O registro e as companhias: o mesmo prefixo + .aurea-pkt[.pcm|.ckpt].
            std::string log = utf8(it->path());
            if (ends_with(log, ".pcm") || ends_with(log, ".ckpt")) log.erase(log.rfind('.'));
            if (!ends_with(log, kLogExtension) || log == keepLog) continue;
            if (std::find(logs.begin(), logs.end(), log) == logs.end()) logs.push_back(log);
        }
        for (const auto& log : logs) {
            if (fileio::exists(log)) ++removed;
            remove(log);
        }
    } catch (...) {}
    return removed;
}

u64 project_key(const std::vector<u8>& projectBytes) noexcept {
    try {
        if (projectBytes.empty()) return 0;
        Project project;
        LoadReport report;
        if (!ProjectSerializer::load_bytes(project, projectBytes.data(), projectBytes.size(), LoadOptions{}, &report).ok() ||
            report.partial)
            return 0;
        project.metadata().createdUnixMs = project.metadata().modifiedUnixMs = 0;
        project.export_settings() = ExportSettings{};
        project.editor_settings() = EditorSettings{};
        project.timeline().set_playhead(FrameIndex{0});
        project.timeline().set_speed(1.0f);
        project.timeline().set_loop(false);
        std::vector<u8> bytes;
        SaveOptions save;
        save.writeThumbnail = false;
        if (!ProjectSerializer::encode(project, save, bytes).ok() || bytes.empty()) return 0;
        const u64 key = mix_bytes(kSeed, bytes.data(), bytes.size());
        return key ? key : 1;
    } catch (...) { return 0; }
}

u64 request_key(u64 projectKey, CompositionId composition, const ExportSettings& s) noexcept {
    if (!projectKey) return 0;
    // Campo a campo (nunca os bytes da struct: o preenchimento não é definido).
    const u64 fields[] = {
        2u /* versão desta regra */, projectKey, composition.pack(), s.width, s.height, std::bit_cast<u64>(s.fps),
        static_cast<u64>(s.videoCodec), s.videoBitrateMbps, s.rateMode, s.keyframeIntervalFrames,
        static_cast<u64>(s.audioCodec), s.audioBitrateKbps, s.audioSampleRate, s.audioChannels, s.container,
        static_cast<u64>(s.outputColorSpace), s.toneMapToSdr ? 1u : 0u, s.parallelSegments, s.motionBlurSamples,
        s.opticalFlowQuality, std::bit_cast<u32>(s.scale), s.dither ? 1u : 0u, s.aiUpscale,
        s.upscaleTemporal ? 1u : 0u, s.trimToContent ? 1u : 0u, s.quality, s.softwareDecode ? 1u : 0u,
    };
    const u64 key = mix_bytes(kSeed, fields, sizeof(fields));
    return key ? key : 1;
}

Candidate find_candidate(const std::string& dir, u64 requestKey) noexcept {
    Candidate best;
    if (dir.empty() || !requestKey) return best;
    try {
        namespace fs = std::filesystem;
        std::error_code ec;
        for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
            const std::string path = utf8(it->path());
            const std::string name = utf8(it->path().filename());
            if (name.rfind(kPrefix, 0) != 0 || !ends_with(path, ".ckpt")) continue;
            const std::string log = path.substr(0, path.size() - 5);
            Manifest m;
            if (!read_manifest(log, m) || m.requestKey != requestKey) continue;
            export_log::ScanResult scanned;
            if (!export_log::scan(log, scanned).ok() || scanned.info.sessionKey != m.sessionKey ||
                scanned.info.totalFrames != m.totalFrames || scanned.resumeFrame == 0 ||
                scanned.resumeFrame >= m.totalFrames)
                continue;
            // Uma sessão que morreu (Running) conta como falha nesta continuação.
            const u32 failures = m.failureResumes + (m.state == SessionState::Running ? 1u : 0u);
            if (m.state != SessionState::Paused &&
                (scanned.resumeFrame <= m.lastResumeFrame || failures > kMaxFailureResumes)) continue;
            if (!best.valid || scanned.resumeFrame > best.resumeFrame)
                best = Candidate{true, scanned.resumeFrame, m.totalFrames, m.safeMode};
        }
    } catch (...) { return Candidate{}; }
    return best;
}

bool log_fits(u64 available, u64 videoEstimate, u64 pcmBytes, u64 loggedBytes) noexcept {
    const u64 remainingLog = videoEstimate > loggedBytes ? videoEstimate - loggedBytes : 0;
    const u64 need = remainingLog + pcmBytes + videoEstimate;
    return available >= need / 2 + (64ull << 20);
}

namespace {
std::atomic<u64> availableForTests{0};
} // namespace
void set_available_bytes_for_tests(u64 bytes) noexcept { availableForTests.store(bytes, std::memory_order_relaxed); }
u64 available_bytes_for_tests() noexcept { return availableForTests.load(std::memory_order_relaxed); }

} // namespace aurea::export_checkpoint
