#include "aurea/export/ExportPacketLog.hpp"
#include "aurea/project/FileIO.hpp"

#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace aurea::export_log {
namespace {

constexpr char kMagic[8] = {'A', 'U', 'R', 'E', 'A', 'P', 'K', 'T'};
constexpr u32 kRecordMagic = 0x50525541u; // "AURP"
constexpr u32 kRecordHeader = 24;
constexpr u64 kFnvPrime = 1099511628211ull;

u64 fnv(u64 hash, const void* data, usize bytes) noexcept {
    const auto* p = static_cast<const u8*>(data);
    for (usize i = 0; i < bytes; ++i) { hash ^= p[i]; hash *= kFnvPrime; }
    return hash;
}
void put32(std::vector<u8>& out, u32 v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (8 * i))); }
void put64(std::vector<u8>& out, u64 v) { for (int i = 0; i < 8; ++i) out.push_back(static_cast<u8>(v >> (8 * i))); }
u32 get32(const u8* p) noexcept { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
u64 get64(const u8* p) noexcept { return u64(get32(p)) | u64(get32(p + 4)) << 32; }

std::vector<u8> encode_body(const StreamInfo& info) {
    std::vector<u8> body;
    body.reserve(52 + info.extradata.size());
    put32(body, info.codec); put32(body, info.width); put32(body, info.height);
    put32(body, static_cast<u32>(info.rateNum)); put32(body, static_cast<u32>(info.rateDen));
    put32(body, static_cast<u32>(info.timebaseNum)); put32(body, static_cast<u32>(info.timebaseDen));
    put64(body, info.sessionKey);
    put32(body, info.totalFrames);
    put32(body, static_cast<u32>(info.extradata.size()));
    body.insert(body.end(), info.extradata.begin(), info.extradata.end());
    return body;
}

u64 record_hash(u32 frame, u32 bytes, u32 flags, const u8* data) noexcept {
    u8 head[12];
    std::memcpy(head, &frame, 4); std::memcpy(head + 4, &bytes, 4); std::memcpy(head + 8, &flags, 4);
    return fnv(fnv(1469598103934665603ull, head, sizeof(head)), data, bytes);
}

int sync_file(std::FILE* f) noexcept {
    if (std::fflush(f) != 0) return errno ? errno : EIO;
#if defined(_WIN32)
    return _commit(_fileno(f)) == 0 ? 0 : (errno ? errno : EIO);
#else
    return ::fsync(fileno(f)) == 0 ? 0 : (errno ? errno : EIO);
#endif
}

bool truncate_file(const std::string& path, u64 bytes) noexcept {
    std::FILE* f = fileio::open_file(path, "rb+");
    if (!f) return false;
#if defined(_WIN32)
    const bool ok = _chsize_s(_fileno(f), static_cast<long long>(bytes)) == 0;
#elif defined(__ANDROID__) && !defined(__LP64__)
    const bool ok = ftruncate64(fileno(f), static_cast<off64_t>(bytes)) == 0;
#else
    const bool ok = ::ftruncate(fileno(f), static_cast<off_t>(bytes)) == 0;
#endif
    const bool synced = sync_file(f) == 0;
    std::fclose(f);
    return ok && synced;
}

Errc io_code(int err) noexcept {
#ifdef EDQUOT
    if (err == EDQUOT) return Errc::StorageFull;
#endif
    return err == ENOSPC ? Errc::StorageFull : Errc::IoError;
}

/// Lê o cabeçalho; `consumed` = bytes do cabeçalho.
Status read_header(std::FILE* f, StreamInfo& info, u64& consumed) noexcept {
    u8 head[16];
    if (std::fread(head, 1, sizeof(head), f) != sizeof(head) || std::memcmp(head, kMagic, 8) != 0)
        return Status{Errc::CorruptData, "registro de pacotes sem cabecalho"};
    if (get32(head + 8) != kVersion) return Status{Errc::UnsupportedFormat, "versao do registro de pacotes desconhecida"};
    const u32 bodyBytes = get32(head + 12);
    if (bodyBytes < 44 || bodyBytes > 44 + kMaxExtradataBytes)
        return Status{Errc::CorruptData, "cabecalho do registro de pacotes invalido"};
    std::vector<u8> body(bodyBytes + 8u);
    if (std::fread(body.data(), 1, body.size(), f) != body.size())
        return Status{Errc::CorruptData, "cabecalho do registro de pacotes truncado"};
    if (fnv(1469598103934665603ull, body.data(), bodyBytes) != get64(body.data() + bodyBytes))
        return Status{Errc::CorruptData, "cabecalho do registro de pacotes corrompido"};
    const u8* p = body.data();
    info.codec = get32(p); info.width = get32(p + 4); info.height = get32(p + 8);
    info.rateNum = static_cast<i32>(get32(p + 12)); info.rateDen = static_cast<i32>(get32(p + 16));
    info.timebaseNum = static_cast<i32>(get32(p + 20)); info.timebaseDen = static_cast<i32>(get32(p + 24));
    info.sessionKey = get64(p + 28);
    info.totalFrames = get32(p + 36);
    const u32 extra = get32(p + 40);
    if (extra != bodyBytes - 44) return Status{Errc::CorruptData, "extradata do registro de pacotes inconsistente"};
    info.extradata.assign(p + 44, p + 44 + extra);
    consumed = 16ull + bodyBytes + 8ull;
    return OkStatus;
}

} // namespace

bool StreamInfo::same_stream(const StreamInfo& o) const noexcept {
    return codec == o.codec && width == o.width && height == o.height && rateNum == o.rateNum &&
           rateDen == o.rateDen && timebaseNum == o.timebaseNum && timebaseDen == o.timebaseDen &&
           sessionKey == o.sessionKey && totalFrames == o.totalFrames && extradata == o.extradata;
}

u64 session_key(const void* data, usize bytes, u64 seed) noexcept { return fnv(seed, data, bytes); }

Status scan(const std::string& path, ScanResult& out) noexcept {
    out = ScanResult{};
    std::FILE* f = fileio::open_file(path, "rb");
    if (!f) return Status{Errc::NotFound, "registro de pacotes ausente"};
    struct Closer { std::FILE* f; ~Closer() { std::fclose(f); } } closer{f};
    u64 offset = 0;
    if (const Status s = read_header(f, out.info, offset); !s.ok()) return s;
    out.resumeOffset = offset;
    std::vector<u8> data;
    u64 lastKeyOffset = offset;
    u32 lastKey = 0;
    bool anyKey = false;
    for (;;) {
        u8 head[kRecordHeader];
        if (std::fread(head, 1, sizeof(head), f) != sizeof(head)) break;
        const u32 magic = get32(head), frame = get32(head + 4), bytes = get32(head + 8), flags = get32(head + 12);
        if (magic != kRecordMagic || frame != out.validFrames || bytes == 0 || bytes > kMaxPacketBytes || (flags & ~1u)) break;
        // O primeiro pacote do registro é sempre um keyframe.
        if (out.validFrames == 0 && !(flags & 1u)) break;
        data.resize(bytes);
        if (std::fread(data.data(), 1, bytes, f) != bytes) break;
        if (record_hash(frame, bytes, flags, data.data()) != get64(head + 16)) break;
        if (flags & 1u) { lastKey = frame; lastKeyOffset = offset; anyKey = true; ++out.keyframes; }
        offset += kRecordHeader + bytes;
        ++out.validFrames;
    }
    out.fileBytes = offset;
    if (anyKey) { out.resumeFrame = lastKey; out.resumeOffset = lastKeyOffset; }
    return OkStatus;
}

Writer::~Writer() { (void)close(); }

Status Writer::fail(Errc code, const char* what) noexcept {
    if (file_) { std::fclose(file_); file_ = nullptr; }
    return Status{code, what};
}

Status Writer::create(const std::string& path, const StreamInfo& info) noexcept {
    (void)close();
    if (!info.width || !info.height || info.rateNum <= 0 || info.rateDen <= 0 || info.timebaseNum <= 0 ||
        info.timebaseDen <= 0 || info.extradata.size() > kMaxExtradataBytes)
        return Status{Errc::InvalidArgument, "fluxo do registro de pacotes invalido"};
    path_ = path;
    file_ = fileio::open_file(path, "wb");
    if (!file_) return Status{io_code(errno), "criar registro de pacotes"};
    const std::vector<u8> body = encode_body(info);
    std::vector<u8> head(kMagic, kMagic + 8);
    put32(head, kVersion); put32(head, static_cast<u32>(body.size()));
    std::vector<u8> tail; put64(tail, fnv(1469598103934665603ull, body.data(), body.size()));
    if (std::fwrite(head.data(), 1, head.size(), file_) != head.size() ||
        std::fwrite(body.data(), 1, body.size(), file_) != body.size() ||
        std::fwrite(tail.data(), 1, tail.size(), file_) != tail.size())
        return fail(io_code(errno), "gravar cabecalho do registro de pacotes");
    if (const int err = sync_file(file_); err != 0) return fail(io_code(err), "sincronizar registro de pacotes");
    next_ = durable_ = 0; needKey_ = true;
    return OkStatus;
}

Status Writer::resume(const std::string& path, const StreamInfo& info, const ScanResult& scanned) noexcept {
    (void)close();
    if (!scanned.info.same_stream(info))
        return Status{Errc::UnsupportedFormat, "o encoder desta sessao difere do registro; o export recomeca do zero"};
    if (scanned.resumeFrame == 0 || scanned.resumeOffset == 0)
        return Status{Errc::InvalidState, "registro sem GOP completo para continuar"};
    if (!truncate_file(path, scanned.resumeOffset)) return Status{Errc::IoError, "cortar registro de pacotes no ultimo GOP"};
    path_ = path;
    file_ = fileio::open_file(path, "ab");
    if (!file_) return Status{io_code(errno), "reabrir registro de pacotes"};
    next_ = durable_ = scanned.resumeFrame; needKey_ = true;
    return OkStatus;
}

Status Writer::append(u32 frame, const u8* data, usize bytes, bool key) noexcept {
    if (!file_) return Status{Errc::InvalidState, "registro de pacotes fechado"};
    if (frame != next_ || !data || bytes == 0 || bytes > kMaxPacketBytes)
        return fail(Errc::InvalidArgument, "pacote fora de ordem ou invalido no registro");
    if (needKey_ && !key) return fail(Errc::CorruptData, "a sessao do encoder nao comecou por um keyframe");
    if (key && frame > durable_) {
        // Todo GOP anterior vai para o disco antes do keyframe que o fecha.
        if (const int err = sync_file(file_); err != 0) return fail(io_code(err), "sincronizar registro de pacotes");
        durable_ = frame;
    }
    const u32 size = static_cast<u32>(bytes), flags = key ? 1u : 0u;
    std::vector<u8> head;
    head.reserve(kRecordHeader);
    put32(head, kRecordMagic); put32(head, frame); put32(head, size); put32(head, flags);
    put64(head, record_hash(frame, size, flags, data));
    if (std::fwrite(head.data(), 1, head.size(), file_) != head.size() || std::fwrite(data, 1, bytes, file_) != bytes)
        return fail(io_code(errno), "gravar pacote no registro");
    needKey_ = false;
    ++next_;
    return OkStatus;
}

Status Writer::commit() noexcept {
    if (!file_) return Status{Errc::InvalidState, "registro de pacotes fechado"};
    if (const int err = sync_file(file_); err != 0) return fail(io_code(err), "sincronizar registro de pacotes");
    durable_ = next_;
    return OkStatus;
}

Status Writer::close() noexcept {
    if (!file_) return OkStatus;
    const int err = sync_file(file_);
    const bool closed = std::fclose(file_) == 0;
    file_ = nullptr;
    if (err != 0) return Status{io_code(err), "sincronizar registro de pacotes"};
    if (!closed) return Status{Errc::IoError, "fechar registro de pacotes"};
    durable_ = next_;
    return OkStatus;
}

Reader::~Reader() { close(); }

void Reader::close() noexcept {
    if (file_) std::fclose(file_);
    file_ = nullptr;
}

Status Reader::open(const std::string& path) noexcept {
    close();
    file_ = fileio::open_file(path, "rb");
    if (!file_) return Status{Errc::NotFound, "registro de pacotes ausente"};
    u64 consumed = 0;
    if (const Status s = read_header(file_, info_, consumed); !s.ok()) { close(); return s; }
    expected_ = 0;
    return OkStatus;
}

Status Reader::next(Packet& out, bool& end) noexcept {
    end = false;
    if (!file_) return Status{Errc::InvalidState, "registro de pacotes fechado"};
    u8 head[kRecordHeader];
    const usize got = std::fread(head, 1, sizeof(head), file_);
    if (got == 0 && std::feof(file_)) { end = true; return OkStatus; }
    if (got != sizeof(head)) return Status{Errc::CorruptData, "registro de pacotes truncado"};
    const u32 magic = get32(head), frame = get32(head + 4), bytes = get32(head + 8), flags = get32(head + 12);
    if (magic != kRecordMagic || frame != expected_ || bytes == 0 || bytes > kMaxPacketBytes || (flags & ~1u))
        return Status{Errc::CorruptData, "registro de pacotes fora de ordem"};
    out.data.resize(bytes);
    if (std::fread(out.data.data(), 1, bytes, file_) != bytes) return Status{Errc::CorruptData, "pacote truncado no registro"};
    if (record_hash(frame, bytes, flags, out.data.data()) != get64(head + 16))
        return Status{Errc::CorruptData, "pacote corrompido no registro"};
    out.frame = frame; out.key = (flags & 1u) != 0;
    ++expected_;
    return OkStatus;
}

} // namespace aurea::export_log
