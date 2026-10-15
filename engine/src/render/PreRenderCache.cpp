#include "aurea/render/PreRenderCache.hpp"
#include "aurea/export/ImageEncode.hpp"
#include "stb_image.h" // implementation is already compiled in GltfImporter.cpp
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <mutex>
#include <random>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace aurea {
namespace {
namespace fs = std::filesystem;
constexpr u32 kChunk = 64u << 10;
constexpr u64 kScratch = 2ull << 20; // conservative compressor workspace bound
constexpr u64 kMaxDisk = 8ull << 30, kMinReserve = 16ull << 20;
constexpr usize kHeader = 96, kManifest = 128;
constexpr char kFrameMagic[8]{'A','U','R','P','R','F','0','1'};
constexpr char kManifestMagic[8]{'A','U','R','P','R','M','0','1'};
constexpr char kOwnerMagic[8]{'A','U','R','P','R','O','0','1'};
constexpr usize kOwner = 32;
void put32(u8* p, u32 n) { for (u32 i = 0; i < 4; ++i) p[i] = static_cast<u8>(n >> (i * 8)); }
void put64(u8* p, u64 n) { for (u32 i = 0; i < 8; ++i) p[i] = static_cast<u8>(n >> (i * 8)); }
u32 get32(const u8* p) { u32 n = 0; for (u32 i = 0; i < 4; ++i) n |= u32(p[i]) << (i * 8); return n; }
u64 get64(const u8* p) { u64 n = 0; for (u32 i = 0; i < 8; ++i) n |= u64(p[i]) << (i * 8); return n; }
u64 key_hash(const PreRenderCache::Key& key) {
    u64 h = 1469598103934665603ull;
    for (u64 n : {key.projectSession, key.composition, key.modelRevision, u64(key.width), u64(key.height),
                  u64(key.fpsNumerator), u64(key.fpsDenominator), u64(key.renderVersion)}) {
        for (u32 i = 0; i < 8; ++i) h = (h ^ static_cast<u8>(n >> (8 * i))) * 1099511628211ull;
    }
    return h;
}
std::FILE* open_file(const fs::path& path, bool write) {
#ifdef _WIN32
    return _wfopen(path.c_str(), write ? L"wb" : L"rb");
#else
    return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
}
struct File {
    std::FILE* value = nullptr;
    explicit File(const fs::path& path, bool write) : value(open_file(path, write)) {}
    ~File() { if (value) std::fclose(value); }
    bool close() { if (!value) return false; const bool flushed = std::fflush(value) == 0;
        const bool closed = std::fclose(value) == 0; value = nullptr; return flushed && closed; }
};
// Both locks are nonblocking. A session lease survives every frame/read and
// is released by the OS after a crash. The namespace lease serializes file
// accounting, admissions and cleanup across cache objects and processes.
struct Lease {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool acquire(const fs::path& path, bool create) {
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            create ? OPEN_ALWAYS : OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        FILE_ATTRIBUTE_TAG_INFO info{};
        if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info))
            || (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { release(); return false; }
        return true;
    }
    void release() { if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); handle = INVALID_HANDLE_VALUE; } }
#else
    int handle = -1;
    bool acquire(const fs::path& path, bool create) {
        handle = ::open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | (create ? O_CREAT : 0), 0600);
        if (handle < 0) return false;
        if (::flock(handle, LOCK_EX | LOCK_NB)) { release(); return false; }
        return true;
    }
    void release() { if (handle >= 0) { ::close(handle); handle = -1; } }
#endif
    ~Lease() { release(); }
};
bool plain(const fs::path& path, bool directory) {
    std::error_code ec; const auto status = fs::symlink_status(path, ec);
    if (ec || fs::is_symlink(status) || (directory ? !fs::is_directory(status) : !fs::is_regular_file(status))) return false;
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
    return true;
}
bool owned_name(const fs::path& path) {
    const auto name = path.filename().string();
    if (name.size() != 39 || name.substr(0, 6) != "owned-" || name[22] != '-') return false;
    for (usize i = 6; i < name.size(); ++i) if (i != 22
        && !((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
    return true;
}
bool ownership(const fs::path& directory) {
    if (!owned_name(directory) || !plain(directory, true) || !plain(directory / "owner.bin", false)) return false;
    File file(directory / "owner.bin", false); std::array<u8, kOwner> bytes{};
    return file.value && std::fread(bytes.data(), 1, bytes.size(), file.value) == bytes.size()
        && std::fgetc(file.value) == EOF && !std::memcmp(bytes.data(), kOwnerMagic, 8)
        && get32(bytes.data() + 8) == 1 && get32(bytes.data() + 28) == crc32_update(0, bytes.data(), 28);
}
Status mark_owned(const fs::path& directory) {
    std::array<u8, kOwner> bytes{}; std::memcpy(bytes.data(), kOwnerMagic, 8);
    put32(bytes.data() + 8, 1); put32(bytes.data() + 28, crc32_update(0, bytes.data(), 28));
    File file(directory / "owner.bin", true);
    return file.value && std::fwrite(bytes.data(), 1, bytes.size(), file.value) == bytes.size() && file.close()
        ? OkStatus : Status{Errc::IoError, "nao foi possivel registrar a pasta privada PreRender"};
}
// This class creates a flat directory of regular files. Refuse recursive
// deletion if a directory, symlink or Windows junction was inserted there.
bool flat_owned(const fs::path& directory) {
    if (!ownership(directory)) return false;
    std::error_code ec; fs::directory_iterator it(directory, ec), end;
    while (!ec && it != end) { if (!plain(it->path(), false)) return false; it.increment(ec); }
    return !ec;
}
std::error_code remove_owned(const fs::path& directory) {
    if (!flat_owned(directory)) return std::make_error_code(std::errc::operation_not_permitted);
    std::error_code ec, failure; fs::directory_iterator it(directory, ec), end;
    // Keep the validated owner marker until all payload files have gone.
    // A sharing violation must not turn a partially removed cache into an
    // unrecognized directory that the next process can no longer reclaim.
    while (!ec && it != end) {
        const auto path = it->path(); it.increment(ec);
        if (path.filename() == "owner.bin" || path.filename() == "session.lease") continue;
        std::error_code removed; fs::remove(path, removed);
        if (removed) failure = removed;
    }
    if (ec) return ec;
    if (failure) return failure;
    fs::remove(directory / "session.lease", ec); if (ec) return ec;
    fs::remove(directory / "owner.bin", ec); if (ec) return ec;
    fs::remove(directory, ec); return ec;
}
Status namespace_bytes(const fs::path& parent, u64& bytes) {
    bytes = 0; std::error_code ec;
    fs::recursive_directory_iterator it(parent, fs::directory_options::none, ec), end;
    while (!ec && it != end) {
        const auto path = it->path(); const auto status = it->symlink_status(ec);
        if (ec) break;
        if (fs::is_symlink(status) || (fs::is_directory(status) && !plain(path, true))) it.disable_recursion_pending();
        else if (fs::is_regular_file(status) && plain(path, false)) {
            const auto size = it->file_size(ec);
            if (ec || size > std::numeric_limits<u64>::max() - bytes) return Errc::IoError;
            bytes += size;
        }
        it.increment(ec);
    }
    return ec ? Status{Errc::IoError, "nao foi possivel contabilizar o cache PreRender"} : OkStatus;
}
Status collect_orphans(const fs::path& parent) {
    std::error_code ec; fs::directory_iterator it(parent, ec), end;
    while (!ec && it != end) {
        const auto path = it->path(); it.increment(ec);
        if (!flat_owned(path)) continue;
        Lease lease;
        // A pre-marker crash is deliberately unowned. A complete marker with
        // no lease file can be safely reclaimed under the namespace lease.
        if (!lease.acquire(path / "session.lease", true)) continue;
        lease.release(); // Windows forbids deleting the exclusively held file.
        const auto removed = remove_owned(path); (void)removed;
        // Failed removal remains counted by namespace_bytes; never erase the
        // quota charge or allow unbounded admissions after a failed cleanup.
    }
    return ec ? Status{Errc::IoError, "nao foi possivel examinar caches PreRender antigos"} : OkStatus;
}
std::string unique_name() {
    static std::atomic<u64> sequence{0};
    std::random_device random;
    const u64 now = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
    const u64 nonce = (u64(random()) << 32) ^ random() ^ ++sequence;
    char text[64]; std::snprintf(text, sizeof(text), "owned-%016llx-%016llx",
        static_cast<unsigned long long>(now), static_cast<unsigned long long>(nonce));
    return text;
}
fs::path frame_path(const fs::path& directory, u64 frame) {
    char name[40]; std::snprintf(name, sizeof(name), "frame-%020llu.bin", static_cast<unsigned long long>(frame));
    return directory / name;
}
Status disk_room(const fs::path& directory, u64 used, u64 extra, const PreRenderCache::Limits& limits) {
    if (used > limits.diskBytes || extra > limits.diskBytes - used)
        return {Errc::StorageFull, "PreRender atingiu o limite de cache em disco"};
    std::error_code error; const auto space = fs::space(directory, error);
    if (error || space.available == static_cast<std::uintmax_t>(-1))
        return {Errc::IoError, "nao foi possivel consultar o espaco do cache PreRender"};
    if (space.available < limits.reserveFreeBytes || extra > space.available - limits.reserveFreeBytes)
        return {Errc::StorageFull, "espaco livre insuficiente; PreRender preservou a reserva do aparelho"};
    return OkStatus;
}
// CRC de A||B a partir de crc(A), crc(B) e |B| (a álgebra GF(2) do zlib).
// A leitura confere cada bloco e o quadro inteiro sem passar duas vezes nos
// 4 MB descomprimidos de um quadro 540p.
u32 gf2_times(const u32* matrix, u32 vector) {
    u32 sum = 0;
    for (; vector; vector >>= 1, ++matrix) if (vector & 1) sum ^= *matrix;
    return sum;
}
void gf2_square(u32* square, const u32* matrix) { for (u32 n = 0; n < 32; ++n) square[n] = gf2_times(matrix, matrix[n]); }
u32 crc32_combine(u32 first, u32 second, u64 secondBytes) {
    if (!secondBytes) return first;
    u32 even[32], odd[32];
    odd[0] = 0xEDB88320u;
    for (u32 n = 1, row = 1; n < 32; ++n, row <<= 1) odd[n] = row;
    gf2_square(even, odd); gf2_square(odd, even);
    do {
        gf2_square(even, odd);
        if (secondBytes & 1) first = gf2_times(even, first);
        secondBytes >>= 1;
        if (!secondBytes) break;
        gf2_square(odd, even);
        if (secondBytes & 1) first = gf2_times(odd, first);
        secondBytes >>= 1;
    } while (secondBytes);
    return first ^ second;
}
// Espera curta e limitada pelo lock (os leitores só o seguram para mexer nas
// filas, nunca durante I/O). Um try_lock seco no render fazia o quadro em
// reprodução cair na composição ao vivo só porque um leitor estava publicando.
bool brief_lock(std::unique_lock<std::mutex>& lock) {
    for (u32 attempt = 0; attempt < 64; ++attempt) {
        if (lock.try_lock()) return true;
        std::this_thread::yield();
    }
    return false;
}
constexpr u64 kReadWorkspace = kChunk * 2 + 16384;
} // namespace

struct PreRenderCache::Impl {
    struct Session {
        Key key; Limits limits; fs::path parent, directory;
        Lease lease;
        u64 token = 0, total = 0, written = 0, bytes = kManifest + kOwner, frameBytes = 0;
        i32 direction = 1; std::atomic<bool> complete{false};
        Errc error = Errc::Ok; char detail[192]{};
        std::chrono::steady_clock::time_point cleanupRetry{};
    };
    struct Read { std::shared_ptr<Session> session; u64 frame = 0; };
    struct Ready { u64 token = 0, frame = 0; std::vector<u16> pixels; Status result; };
    mutable std::mutex mutex;
    std::condition_variable wake;
    // Leitores em paralelo: um quadro 540p (zlib + CRC) custa mais que um
    // intervalo de 30 fps num só núcleo. O primeiro também limpa as pastas.
    std::vector<std::thread> workers;
    std::atomic<u64> token{0}, generation{0};
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::shared_ptr<Session> active;
    std::deque<Read> requests;
    std::deque<Ready> ready;
    std::deque<std::shared_ptr<Session>> garbage;
    bool stopping = false;
    u64 resident = 0, working = 0, peak = 0;
    u32 cleaning = 0;
    std::vector<std::pair<u64, u64>> inflight; // token, frame being read
    bool cleanupFailed = false;
    Impl() {
        const u32 hw = std::thread::hardware_concurrency();
        const u32 count = std::clamp<u32>(hw ? hw / 2 : 2, 2, 3);
        inflight.reserve(count);
        for (u32 i = 0; i < count; ++i) workers.emplace_back([this, i] { run(i == 0); });
    }
    ~Impl() {
        generation.fetch_add(1, std::memory_order_acq_rel); token.store(0, std::memory_order_release);
        { std::lock_guard lock(mutex); stopping = true; }
        wake.notify_all();
        for (auto& w : workers) if (w.joinable()) w.join();
    }
    bool in_flight(u64 t, u64 frame) const {
        for (const auto& [ft, ff] : inflight) if (ft == t && ff == frame) return true;
        return false;
    }
    bool valid(u64 t) const { return t && generation.load(std::memory_order_acquire) == t && token.load(std::memory_order_acquire) == t; }
    void error(Session& session, Status result) {
        if (result.ok()) return;
        session.error = result.code(); const auto text = result.detail().empty() ? result.message() : result.detail();
        std::snprintf(session.detail, sizeof(session.detail), "%.*s", static_cast<int>(text.size()), text.data());
        session.complete = false;
    }
    void retire_locked() {
        if (active && !valid(active->token)) {
            garbage.push_back(std::move(active)); requests.clear(); ready.clear();
            resident = 0; // active read is charged separately in working
        }
    }
    bool charge_locked(u64 bytes, const Session& session) {
        if (resident + working > session.limits.memoryBytes || bytes > session.limits.memoryBytes - resident - working) return false;
        working += bytes; peak = std::max(peak, resident + working); return true;
    }
    Status manifest(const Session& session, bool completed, u64 namespaceBytes) {
        std::array<u8, kManifest> data{};
        std::memcpy(data.data(), kManifestMagic, 8); put32(data.data() + 8, 1);
        put64(data.data() + 16, session.token); put64(data.data() + 24, key_hash(session.key));
        put64(data.data() + 32, session.total); put64(data.data() + 40, session.written);
        put64(data.data() + 48, session.bytes + (completed ? kManifest : 0)); put32(data.data() + 56, completed ? 1 : 0);
        put32(data.data() + 60, static_cast<u32>(session.direction));
        put64(data.data() + 64, session.key.projectSession); put64(data.data() + 72, session.key.composition);
        put64(data.data() + 80, session.key.modelRevision); put32(data.data() + 88, session.key.width);
        put32(data.data() + 92, session.key.height); put32(data.data() + 96, session.key.fpsNumerator);
        put32(data.data() + 100, session.key.fpsDenominator); put32(data.data() + 104, session.key.renderVersion);
        put32(data.data() + 124, crc32_update(0, data.data(), 124));
        if (!valid(session.token)) return Errc::Cancelled;
        if (const Status room = disk_room(session.directory, std::max(session.bytes, namespaceBytes), data.size(), session.limits); !room.ok()) return room;
        const fs::path temporary = session.directory / "manifest.part", destination = session.directory / "manifest.bin";
        File file(temporary, true);
        if (!file.value) return {Errc::IoError, "nao foi possivel criar o manifesto PreRender"};
        if (std::fwrite(data.data(), 1, data.size(), file.value) != data.size() || !file.close())
            return {Errc::StorageFull, "falha ao gravar o manifesto PreRender"};
        if (!valid(session.token)) return Errc::Cancelled;
        // Initial and completed manifests have distinct names, so rename never
        // deletes/replaces a file and is atomic on both Windows and Unix.
        const fs::path published = completed ? session.directory / "complete.bin" : destination;
        std::error_code ec; fs::rename(temporary, published, ec);
        return ec ? Status{Errc::IoError, "falha ao publicar o manifesto PreRender"} : OkStatus;
    }
    Status read(const Session& session, u64 frame, std::vector<u16>& output) {
        if (!valid(session.token)) return Errc::Cancelled;
        const bool completed = session.complete.load(std::memory_order_acquire);
        File manifestFile(session.directory / (completed ? "complete.bin" : "manifest.bin"), false);
        std::array<u8, kManifest> manifestData{};
        if (!manifestFile.value || std::fread(manifestData.data(), 1, manifestData.size(), manifestFile.value) != manifestData.size()
            || std::memcmp(manifestData.data(), kManifestMagic, 8) || get32(manifestData.data() + 8) != 1
            || get32(manifestData.data() + 124) != crc32_update(0, manifestData.data(), 124)
            || get64(manifestData.data() + 16) != session.token || get64(manifestData.data() + 24) != key_hash(session.key)
            || get64(manifestData.data() + 32) != session.total || get32(manifestData.data() + 56) != (completed ? 1u : 0u)
            || (completed && get64(manifestData.data() + 40) != session.total)
            || std::fgetc(manifestFile.value) != EOF)
            return {Errc::CorruptData, "manifesto PreRender invalido"};
        File file(frame_path(session.directory, frame), false);
        if (!file.value) return {Errc::IoError, "quadro PreRender ausente"};
        std::array<u8, kHeader> header{};
        if (std::fread(header.data(), 1, header.size(), file.value) != header.size()
            || std::memcmp(header.data(), kFrameMagic, 8) || get32(header.data() + 8) != 1
            || get32(header.data() + 92) != crc32_update(0, header.data(), 92)
            || get64(header.data() + 16) != session.token || get64(header.data() + 24) != key_hash(session.key)
            || get64(header.data() + 32) != frame || get64(header.data() + 40) != session.total
            || get64(header.data() + 48) != session.frameBytes)
            return {Errc::CorruptData, "cabecalho do quadro PreRender invalido"};
        const u32 count = get32(header.data() + 60);
        if (count != (session.frameBytes + kChunk - 1) / kChunk) return Errc::CorruptData;
        output.resize(static_cast<usize>(session.frameBytes / 2));
        std::vector<u8> compressed; compressed.reserve(kChunk * 2 + 1024);
        u64 offset = 0; u32 crc = 0;
        for (u32 n = 0; n < count; ++n) {
            if (!valid(session.token)) return Errc::Cancelled;
            u8 block[12];
            if (std::fread(block, 1, 12, file.value) != 12) return Errc::CorruptData;
            const u32 raw = get32(block), encoded = get32(block + 4);
            if (raw != std::min<u64>(kChunk, session.frameBytes - offset) || !encoded || encoded > kChunk * 2 + 1024)
                return {Errc::CorruptData, "tamanho do bloco PreRender invalido"};
            compressed.resize(encoded);
            if (std::fread(compressed.data(), 1, encoded, file.value) != encoded) return Errc::CorruptData;
            u8* destination = reinterpret_cast<u8*>(output.data()) + offset;
            if (stbi_zlib_decode_buffer(reinterpret_cast<char*>(destination), static_cast<int>(raw),
                    reinterpret_cast<const char*>(compressed.data()), static_cast<int>(encoded)) != static_cast<int>(raw))
                return {Errc::CorruptData, "bloco PreRender nao pode ser descomprimido"};
            const u32 blockCrc = crc32_update(0, destination, raw);
            if (blockCrc != get32(block + 8)) return Errc::ChecksumMismatch;
            crc = n ? crc32_combine(crc, blockCrc, raw) : blockCrc; offset += raw;
        }
        if (crc != get32(header.data() + 56) || std::fgetc(file.value) != EOF) return Errc::ChecksumMismatch;
        return valid(session.token) ? OkStatus : Status{Errc::Cancelled};
    }
    void run(bool cleaner) {
        for (;;) {
            Read job; std::shared_ptr<Session> disposable; u64 charge = 0; bool idle = false;
            {
                std::unique_lock lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(10), [&] {
                    return stopping || !requests.empty() || (active && !valid(active->token)) || (cleaner && !garbage.empty());
                });
                retire_locked();
                // Só o primeiro leitor apaga pastas: uma remoção por vez.
                if (cleaner && !garbage.empty() && garbage.front().use_count() == 1
                    && std::chrono::steady_clock::now() >= garbage.front()->cleanupRetry) {
                    disposable = std::move(garbage.front()); garbage.pop_front(); ++cleaning;
                } else if (!requests.empty()) {
                    const u64 need = requests.front().session->frameBytes + kReadWorkspace;
                    // Sem espaço no orçamento: o pedido espera um quadro pronto
                    // sair (take/prefetch). Não é erro — antes marcava a sessão
                    // com BudgetExceeded e o PreRender inteiro caía.
                    if (charge_locked(need, *requests.front().session)) {
                        job = std::move(requests.front()); requests.pop_front(); charge = need;
                        inflight.emplace_back(job.session->token, job.frame);
                    } else idle = true;
                } else if (stopping && (!cleaner || (!active && garbage.empty() && !working && !cleaning))) break;
                else idle = true;
            }
            if (disposable) {
                // Only an exact unique child created by begin. Never delete the
                // caller's cache root, siblings, project files or source media.
                std::error_code error;
                Lease admission;
                bool deferred = false;
                if (!admission.acquire(disposable->parent / "namespace.lease", true)) {
                    error = std::make_error_code(std::errc::device_or_resource_busy);
                    deferred = true;
                } else if (disposable->directory.parent_path() == disposable->parent && flat_owned(disposable->directory)) {
                    disposable->lease.release();
                    error = remove_owned(disposable->directory);
                } else {
                    error = std::make_error_code(std::errc::operation_not_permitted);
                }
                { std::lock_guard lock(mutex); --cleaning;
                  if (error && !stopping) {
                      if (!deferred) cleanupFailed = true;
                      disposable->cleanupRetry = std::chrono::steady_clock::now()
                          + (deferred ? std::chrono::milliseconds(10) : std::chrono::milliseconds(1000));
                      garbage.push_back(std::move(disposable));
                  } else if (garbage.empty()) cleanupFailed = false; }
            } else if (job.session) {
                Ready result; result.token = job.session->token; result.frame = job.frame;
                result.result = read(*job.session, job.frame, result.pixels);
                std::lock_guard lock(mutex);
                if (const auto it = std::find(inflight.begin(), inflight.end(), std::pair<u64, u64>{job.session->token, job.frame});
                    it != inflight.end()) inflight.erase(it);
                if (valid(job.session->token) && active == job.session) {
                    if (!result.result.ok()) { error(*job.session, result.result); std::vector<u16>{}.swap(result.pixels); }
                    resident += result.pixels.capacity() * sizeof(u16);
                    ready.push_back(std::move(result));
                } else std::vector<u16>{}.swap(result.pixels);
                working -= charge; peak = std::max(peak, resident + working);
                wake.notify_all();
            } else if (idle) {
                // In-flight producer lease / read budget full: do not busy-spin.
                std::unique_lock lock(mutex); wake.wait_for(lock, std::chrono::milliseconds(5));
            }
        }
    }
};

PreRenderCache::PreRenderCache() : impl_(std::make_unique<Impl>()) {}
PreRenderCache::~PreRenderCache() = default;
Result<u64> PreRenderCache::begin(const std::string& root, Key key, u64 frames, i32 direction, Limits limits) noexcept {
    if (root.empty() || !key.width || !key.height || !key.fpsNumerator || !key.fpsDenominator || !frames
        || (direction != -1 && direction != 1) || std::endian::native != std::endian::little) return Status{Errc::InvalidArgument};
    if (key.width > 16384 || key.height > 16384) return Status{Errc::OutOfRange};
    const u64 bytes = u64(key.width) * key.height * 8;
    if (bytes > static_cast<u64>(std::numeric_limits<i32>::max()) || bytes > limits.memoryBytes
        || bytes + kScratch + kChunk * 2 + 16384 > limits.memoryBytes) return Status{Errc::BudgetExceeded};
    limits.diskBytes = std::min(limits.diskBytes, kMaxDisk);
    limits.reserveFreeBytes = std::max(limits.reserveFreeBytes, kMinReserve);
    // Cada quadro da janela custa os pixels e, enquanto é lido, o espaço de
    // descompressão; o gravador (kScratch) pode estar ativo ao mesmo tempo.
    limits.residentFrames = std::clamp(limits.residentFrames, 1u, kMaxResidentFrames);
    while (limits.residentFrames > 1 && (bytes + kReadWorkspace) * limits.residentFrames + kScratch > limits.memoryBytes)
        --limits.residentFrames;
    if (impl_->writing.test_and_set(std::memory_order_acquire)) return Status{Errc::Busy};
    struct Writer { Impl& impl; ~Writer() { impl.writing.clear(std::memory_order_release); impl.wake.notify_all(); } } writer{*impl_};
    const u64 epoch = impl_->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    impl_->token.store(0, std::memory_order_release); impl_->wake.notify_all();
    { std::lock_guard lock(impl_->mutex); impl_->retire_locked();
      if (impl_->cleanupFailed) return Status{Errc::IoError, "nao foi possivel remover o cache PreRender anterior"};
      if (impl_->working || !impl_->garbage.empty() || impl_->cleaning) return Status{Errc::Busy}; }
    auto session = std::make_shared<Impl::Session>(); session->key = key; session->limits = limits;
    session->total = frames; session->direction = direction; session->frameBytes = bytes;
    std::error_code ec; session->parent = fs::absolute(fs::u8path(root), ec) / "pre-render";
    if (ec) return Status{Errc::IoError};
    fs::create_directories(session->parent, ec); if (ec) return Status{Errc::IoError};
    if (!plain(session->parent, true)) return Status{Errc::IoError, "pasta PreRender nao pode ser link"};
    Lease admission;
    if (!admission.acquire(session->parent / "namespace.lease", true)) return Status{Errc::Busy};
    if (const Status collected = collect_orphans(session->parent); !collected.ok()) return collected;
    u64 namespaceBytes = 0;
    if (const Status counted = namespace_bytes(session->parent, namespaceBytes); !counted.ok()) return counted;
    if (const Status room = disk_room(session->parent, namespaceBytes, kOwner + kManifest, limits); !room.ok()) return room;
    session->directory = session->parent / unique_name();
    if (!fs::create_directory(session->directory, ec) || ec) return Status{Errc::IoError};
    if (!session->lease.acquire(session->directory / "session.lease", true)) return Status{Errc::IoError};
    if (const Status marked = mark_owned(session->directory); !marked.ok()) return marked;
    namespaceBytes += kOwner;
    {
        std::lock_guard lock(impl_->mutex); impl_->retire_locked();
        if (impl_->generation.load(std::memory_order_acquire) != epoch) {
            impl_->garbage.push_back(session); impl_->wake.notify_all(); return Status{Errc::Cancelled};
        }
        session->token = epoch;
        impl_->active = session; impl_->token.store(session->token, std::memory_order_release); impl_->peak = 0;
    }
    const Status initialized = impl_->manifest(*session, false, namespaceBytes);
    if (!initialized.ok()) { std::lock_guard lock(impl_->mutex); impl_->error(*session, initialized); return initialized; }
    return session->token;
}
Status PreRenderCache::write(u64 token, u64 frame, std::span<const u16> rgba, const std::atomic<bool>* cancel) noexcept {
    if (!impl_->valid(token)) return Errc::Cancelled;
    if (impl_->writing.test_and_set(std::memory_order_acquire)) return Errc::Busy;
    struct Writer { Impl& impl; ~Writer() { impl.writing.clear(std::memory_order_release); impl.wake.notify_all(); } } writer{*impl_};
    std::shared_ptr<Impl::Session> session;
    { std::lock_guard lock(impl_->mutex); session = impl_->active;
      if (!session || session->token != token || !impl_->valid(token)) return Errc::Cancelled;
      if (session->complete || session->written >= session->total) return Errc::InvalidState;
      if (frame != (session->direction > 0 ? session->written : session->total - 1 - session->written)) return Errc::InvalidArgument;
      if (rgba.size_bytes() != session->frameBytes) return Errc::InvalidArgument;
      if (!impl_->charge_locked(kScratch, *session)) return Errc::BudgetExceeded; }
    struct Charge { Impl& impl; ~Charge() { std::lock_guard lock(impl.mutex); impl.working -= kScratch; } } charge{*impl_};
    Lease admission;
    if (!admission.acquire(session->parent / "namespace.lease", true)) return Errc::Busy;
    u64 namespaceBytes = 0;
    if (const Status counted = namespace_bytes(session->parent, namespaceBytes); !counted.ok()) return counted;
    const auto cancelled = [&] { return !impl_->valid(token) || (cancel && cancel->load(std::memory_order_acquire)); };
    auto execute = [&]() -> Status {
        if (cancelled()) return Errc::Cancelled;
        const u8* pixels = reinterpret_cast<const u8*>(rgba.data());
        const u32 count = static_cast<u32>((session->frameBytes + kChunk - 1) / kChunk);
        std::array<u8, kHeader> header{}; std::memcpy(header.data(), kFrameMagic, 8);
        put32(header.data() + 8, 1); put64(header.data() + 16, token); put64(header.data() + 24, key_hash(session->key));
        put64(header.data() + 32, frame); put64(header.data() + 40, session->total); put64(header.data() + 48, session->frameBytes);
        put32(header.data() + 56, crc32_update(0, pixels, static_cast<usize>(session->frameBytes)));
        put32(header.data() + 60, count); put32(header.data() + 92, crc32_update(0, header.data(), 92));
        const fs::path destination = frame_path(session->directory, frame); fs::path temporary = destination; temporary += ".part";
        File file(temporary, true); if (!file.value) return {Errc::IoError, "nao foi possivel gravar o quadro PreRender"};
        if (const Status room = disk_room(session->directory, std::max(session->bytes, namespaceBytes), header.size(), session->limits); !room.ok()) return room;
        if (std::fwrite(header.data(), 1, header.size(), file.value) != header.size()) return Errc::StorageFull;
        u64 fileBytes = header.size(); std::vector<u8> compressed; compressed.reserve(kChunk * 2 + 1024);
        for (u64 offset = 0; offset < session->frameBytes; offset += kChunk) {
            if (cancelled()) return Errc::Cancelled;
            const auto raw = static_cast<u32>(std::min<u64>(kChunk, session->frameBytes - offset));
            compressed.clear(); zlib_compress(pixels + offset, raw, compressed);
            if (compressed.size() > kChunk * 2 + 1024) return Errc::EncodeFailed;
            if (const Status room = disk_room(session->directory, std::max(session->bytes, namespaceBytes) + fileBytes,
                    compressed.size() + 12, session->limits); !room.ok()) return room;
            u8 block[12]; put32(block, raw); put32(block + 4, static_cast<u32>(compressed.size()));
            put32(block + 8, crc32_update(0, pixels + offset, raw));
            if (std::fwrite(block, 1, 12, file.value) != 12
                || std::fwrite(compressed.data(), 1, compressed.size(), file.value) != compressed.size()) return Errc::StorageFull;
            fileBytes += compressed.size() + 12;
        }
        if (!file.close()) return Errc::StorageFull;
        if (cancelled()) return Errc::Cancelled;
        std::error_code error; fs::rename(temporary, destination, error); if (error) return Errc::IoError;
        std::lock_guard lock(impl_->mutex);
        if (cancelled() || impl_->active != session) return Errc::Cancelled;
        session->bytes += fileBytes; ++session->written; return OkStatus;
    };
    const Status result = execute();
    if (!result.ok()) {
        std::error_code ignored; auto path = frame_path(session->directory, frame); path += ".part"; fs::remove(path, ignored);
        std::lock_guard lock(impl_->mutex); impl_->error(*session, result);
    }
    return result;
}
Status PreRenderCache::finish(u64 token) noexcept {
    if (!impl_->valid(token)) return Errc::Cancelled;
    if (impl_->writing.test_and_set(std::memory_order_acquire)) return Errc::Busy;
    struct Writer { Impl& impl; ~Writer() { impl.writing.clear(std::memory_order_release); impl.wake.notify_all(); } } writer{*impl_};
    std::shared_ptr<Impl::Session> session;
    { std::lock_guard lock(impl_->mutex); session = impl_->active;
      if (!session || !impl_->valid(token)) return Errc::Cancelled;
      if (session->error != Errc::Ok) return Status{session->error, "PreRender incompleto; veja o diagnostico do cache"};
      if (session->written != session->total) return Status{Errc::InvalidState, "PreRender ainda nao gravou todos os quadros"};
      if (session->complete) return OkStatus; }
    Lease admission;
    if (!admission.acquire(session->parent / "namespace.lease", true)) return Errc::Busy;
    u64 namespaceBytes = 0;
    if (const Status counted = namespace_bytes(session->parent, namespaceBytes); !counted.ok()) return counted;
    const Status result = impl_->manifest(*session, true, namespaceBytes);
    std::lock_guard lock(impl_->mutex);
    if (!impl_->valid(token) || impl_->active != session) return Errc::Cancelled;
    if (session->error != Errc::Ok) return {session->error, "PreRender incompleto; veja o diagnostico do cache"};
    if (result.ok()) { session->complete = true; session->bytes += kManifest; }
    else impl_->error(*session, result);
    return result;
}
Status PreRenderCache::request(u64 token, u64 frame) noexcept {
    if (!impl_->valid(token)) return Errc::Cancelled;
    std::unique_lock lock(impl_->mutex, std::defer_lock); if (!brief_lock(lock)) return Errc::Busy;
    const auto& session = impl_->active;
    if (!session || !impl_->valid(token) || session->token != token) return Errc::Cancelled;
    if (frame >= session->total) return Errc::OutOfRange;
    const bool written = session->direction > 0 ? frame < session->written : frame >= session->total - session->written;
    if (!written) return Errc::NotFound;
    for (const auto& ready : impl_->ready) if (ready.token == token && ready.frame == frame) return OkStatus;
    for (const auto& job : impl_->requests) if (job.session == session && job.frame == frame) return OkStatus;
    if (impl_->in_flight(token, frame)) return OkStatus;
    const auto occupied = [&] { return impl_->requests.size() + impl_->ready.size() + impl_->inflight.size(); };
    while (!impl_->ready.empty() && occupied() >= session->limits.residentFrames) {
        impl_->resident -= impl_->ready.front().pixels.capacity() * sizeof(u16); impl_->ready.pop_front();
    }
    if (occupied() >= session->limits.residentFrames) return Errc::Busy;
    impl_->requests.push_back({session, frame}); impl_->wake.notify_all(); return OkStatus;
}
u32 PreRenderCache::prefetch(u64 token, u64 frame, i32 direction, u32 count, bool wrap) noexcept {
    if (!impl_->valid(token)) return 0;
    std::unique_lock lock(impl_->mutex, std::defer_lock); if (!brief_lock(lock)) return 0;
    Impl& im = *impl_;
    const auto session = im.active;
    if (!session || !im.valid(token) || session->token != token || frame >= session->total) return 0;
    const u64 total = session->total;
    const u32 limit = session->limits.residentFrames;
    const u32 wanted = static_cast<u32>(std::min<u64>({u64(std::max(count, 1u)), u64(limit), total}));
    // A janela: do quadro pedido em diante, no sentido do relógio.
    std::array<u64, kMaxResidentFrames> window{}; u32 size = 0;
    const i64 step = direction < 0 ? -1 : 1;
    i64 at = static_cast<i64>(frame);
    for (u32 k = 0; k < wanted; ++k) {
        if (at < 0 || static_cast<u64>(at) >= total) {
            if (!wrap) break;
            at = at < 0 ? static_cast<i64>(total) - 1 : 0;
        }
        const u64 f = static_cast<u64>(at);
        const bool written = session->direction > 0 ? f < session->written : f >= total - session->written;
        if (!written) break;
        window[size++] = f; at += step;
    }
    const auto position = [&](u64 f) -> u32 { for (u32 i = 0; i < size; ++i) if (window[i] == f) return i; return kMaxResidentFrames; };
    // Fora da janela (o relógio já passou, seek, sentido trocado): sai.
    for (auto it = im.ready.begin(); it != im.ready.end();) {
        if (it->token != token || position(it->frame) == kMaxResidentFrames) {
            im.resident -= it->pixels.capacity() * sizeof(u16); it = im.ready.erase(it);
        } else ++it;
    }
    std::erase_if(im.requests, [&](const Impl::Read& job) { return job.session != session || position(job.frame) == kMaxResidentFrames; });
    bool queued = false;
    for (u32 i = 0; i < size; ++i) {
        const u64 f = window[i];
        bool have = im.in_flight(token, f);
        for (const auto& r : im.ready) if (!have && r.frame == f) have = true;
        for (const auto& job : im.requests) if (!have && job.frame == f) have = true;
        if (have) continue;
        if (im.requests.size() + im.ready.size() + im.inflight.size() >= limit) break;
        im.requests.push_back({session, f}); queued = true;
    }
    // O mais perto do relógio é lido primeiro.
    std::stable_sort(im.requests.begin(), im.requests.end(),
        [&](const Impl::Read& a, const Impl::Read& b) { return position(a.frame) < position(b.frame); });
    if (queued) im.wake.notify_all();
    u32 run = 0;
    for (u32 i = 0; i < size; ++i) {
        bool ok = false;
        for (const auto& r : im.ready) if (r.frame == window[i] && r.result.ok()) { ok = true; break; }
        if (!ok) break;
        ++run;
    }
    return run;
}
Status PreRenderCache::take(u64 token, u64 frame, std::vector<u16>& rgba) noexcept {
    if (!impl_->valid(token)) return Errc::Cancelled;
    std::unique_lock lock(impl_->mutex, std::defer_lock); if (!brief_lock(lock)) return Errc::Busy;
    if (!impl_->valid(token)) return Errc::Cancelled;
    for (auto it = impl_->ready.begin(); it != impl_->ready.end(); ++it) if (it->token == token && it->frame == frame) {
        const Status result = it->result;
        impl_->resident -= it->pixels.capacity() * sizeof(u16);
        rgba = std::move(it->pixels); impl_->ready.erase(it); return result;
    }
    return Errc::NotFound;
}
void PreRenderCache::invalidate() noexcept {
    impl_->generation.fetch_add(1, std::memory_order_acq_rel);
    impl_->token.store(0, std::memory_order_release); impl_->wake.notify_all();
}
bool PreRenderCache::matches(u64 token, const Key& key) const noexcept {
    if (!impl_->valid(token)) return false;
    std::unique_lock lock(impl_->mutex, std::defer_lock);
    return brief_lock(lock) && impl_->valid(token) && impl_->active && impl_->active->token == token && impl_->active->key == key;
}
PreRenderCache::Stats PreRenderCache::stats() const noexcept {
    Stats result;
    // Lock normal: os leitores só seguram o mutex para mexer nas filas, nunca
    // durante I/O. Devolver Busy aqui fazia a prévia achar o cache incompleto
    // e montar a composição ao vivo só porque um leitor publicava um quadro.
    std::lock_guard lock(impl_->mutex);
    if (!impl_->active || !impl_->valid(impl_->active->token)) {
        if (impl_->cleanupFailed) { result.lastError = Errc::IoError;
            std::snprintf(result.errorDetail, sizeof(result.errorDetail), "%s", "nao foi possivel remover o cache PreRender anterior"); }
        return result;
    }
    const auto& session = *impl_->active;
    result.token = session.token; result.framesTotal = session.total; result.framesWritten = session.written;
    result.diskBytes = session.bytes; result.residentBytes = impl_->resident; result.workingBytes = impl_->working;
    const u32 reading = static_cast<u32>(impl_->inflight.size());
    result.peakOwnedBytes = impl_->peak; result.pendingReads = static_cast<u32>(impl_->requests.size()) + reading;
    result.residentFrames = static_cast<u32>(impl_->ready.size()) + reading;
    result.residentLimit = session.limits.residentFrames;
    result.complete = session.complete; result.lastError = session.error;
    std::memcpy(result.errorDetail, session.detail, sizeof(result.errorDetail)); return result;
}
} // namespace aurea
