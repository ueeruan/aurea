// =============================================================================
//  Aurea / project / ProjectPackage.cpp — ver ProjectPackage.hpp.
//
//  ZIP mínimo, só "stored" (método 0): cabeçalho local com CRC/tamanho
//  corrigidos depois do dado (seek para trás), diretório central e EOCD.
//  Sem ZIP64: um pacote passa de 4 GiB → erro claro, nunca um arquivo que
//  outro descompactador leria errado.
// =============================================================================
#include "aurea/project/ProjectPackage.hpp"

#include "aurea/project/FileIO.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/project/Serialization.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>

namespace aurea::package {
namespace {

namespace fs = std::filesystem;

constexpr u64 kZipLimit = 0xFFFFFFFFull;
constexpr u32 kSigLocal = 0x04034b50u;
constexpr u32 kSigCentral = 0x02014b50u;
constexpr u32 kSigEnd = 0x06054b50u;
constexpr u16 kFlagUtf8 = 0x0800;
std::mutex packageWriteMutex;

const std::array<u32, 256>& crc_table() {
    static const std::array<u32, 256> table = [] {
        std::array<u32, 256> t{};
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    return table;
}

u32 crc_update(u32 crc, const u8* data, usize n) {
    const auto& t = crc_table();
    crc = ~crc;
    for (usize i = 0; i < n; ++i) crc = t[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

bool seek(std::FILE* f, u64 pos) {
#if defined(_WIN32)
    return _fseeki64(f, static_cast<long long>(pos), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(pos), SEEK_SET) == 0;
#endif
}
bool seek_end(std::FILE* f) {
#if defined(_WIN32)
    return _fseeki64(f, 0, SEEK_END) == 0;
#else
    return fseeko(f, 0, SEEK_END) == 0;
#endif
}
u64 tell(std::FILE* f) {
#if defined(_WIN32)
    const long long p = _ftelli64(f);
#else
    const off_t p = ftello(f);
#endif
    return p < 0 ? 0 : static_cast<u64>(p);
}

struct File {
    std::FILE* f = nullptr;
    explicit File(std::FILE* file) : f(file) {}
    ~File() { if (f) std::fclose(f); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    std::FILE* release() { std::FILE* r = f; f = nullptr; return r; }
};

void put16(std::vector<u8>& b, u32 v) { b.push_back(static_cast<u8>(v)); b.push_back(static_cast<u8>(v >> 8)); }
void put32(std::vector<u8>& b, u32 v) { put16(b, v & 0xFFFFu); put16(b, v >> 16); }
u16 get16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
u32 get32(const u8* p) { return static_cast<u32>(get16(p)) | (static_cast<u32>(get16(p + 2)) << 16); }

std::string escape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    return o;
}

std::string unescape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (usize i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { o += s[i]; continue; }
        const char n = s[++i];
        o += n == 'n' ? '\n' : n == 'r' ? '\r' : n == 't' ? '\t' : n;
    }
    return o;
}

std::string utf8_of(const fs::path& p) {
    const auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

/// Nome de arquivo seguro para a entrada e para o disco: só o último pedaço,
/// sem separador, controle, `:` ou ponto na frente; até 100 bytes (a extensão
/// fica — o decodificador escolhe o demuxer por ela).
std::string safe_file_name(std::string name) {
    const usize slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    for (char& c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    }
    while (!name.empty() && (name.front() == '.' || name.front() == ' ')) name.erase(name.begin());
    if (name.size() > 100) {
        const usize dot = name.find_last_of('.');
        const std::string ext = dot != std::string::npos && name.size() - dot <= 10 ? name.substr(dot) : std::string{};
        usize keep = 100 - ext.size();
        while (keep > 0 && (static_cast<unsigned char>(name[keep]) & 0xC0) == 0x80) --keep;   // não parte UTF-8
        name = name.substr(0, keep) + ext;
    }
    return name.empty() ? std::string("media") : name;
}

bool packable(AssetKind k) {
    switch (k) {
        case AssetKind::Video: case AssetKind::Audio: case AssetKind::Image: case AssetKind::Font:
        case AssetKind::Model3D: case AssetKind::Environment: case AssetKind::Lut: case AssetKind::Shape:
            return true;
        default:
            return false;
    }
}

Status load_project(Project& p, const std::string& path) {
    LoadOptions o;
    o.lazyAssets = true;
    o.tolerateCorruptSections = false;
    LoadReport report;
    const Status s = ProjectSerializer::load(p, path, o, &report, nullptr);
    if (!s.ok()) return s;
    if (report.partial) return Status{Errc::CorruptData, "secoes faltando"};
    return OkStatus;
}

// --- Escrita -------------------------------------------------------------------

struct CentralEntry {
    std::string name;
    u32 crc = 0;
    u32 size = 0;
    u32 offset = 0;
};

class ZipWriter {
public:
    explicit ZipWriter(std::FILE* f) : f_(f) {}

    /// Uma entrada inteira a partir de uma fonte que entrega pedaços.
    template <class Reader>
    Status add(const std::string& name, Reader&& read) {
        const u64 start = tell(f_);
        if (start > kZipLimit) return Status{Errc::NotSupported, "pacote maior que 4 GB"};
        std::vector<u8> h;
        put32(h, kSigLocal); put16(h, 20); put16(h, kFlagUtf8); put16(h, 0);
        put16(h, 0); put16(h, 0x21);                 // hora 00:00, data 1980-01-01
        put32(h, 0); put32(h, 0); put32(h, 0);        // crc / tamanhos: corrigidos no fim
        put16(h, static_cast<u32>(name.size())); put16(h, 0);
        h.insert(h.end(), name.begin(), name.end());
        if (std::fwrite(h.data(), 1, h.size(), f_) != h.size()) return write_error();

        u32 crc = 0;
        u64 size = 0;
        std::vector<u8> buf(1u << 20);
        for (;;) {
            usize n = 0;
            const Status rs = read(buf.data(), buf.size(), n);
            if (!rs.ok()) return rs;
            if (n == 0) break;
            crc = crc_update(crc, buf.data(), n);
            size += n;
            if (start + h.size() + size > kZipLimit) return Status{Errc::NotSupported, "pacote maior que 4 GB"};
            if (std::fwrite(buf.data(), 1, n, f_) != n) return write_error();
        }
        const u64 end = tell(f_);
        std::vector<u8> fix;
        put32(fix, crc); put32(fix, static_cast<u32>(size)); put32(fix, static_cast<u32>(size));
        if (!seek(f_, start + 14) || std::fwrite(fix.data(), 1, fix.size(), f_) != fix.size() || !seek(f_, end)) {
            return write_error();
        }
        entries_.push_back({name, crc, static_cast<u32>(size), static_cast<u32>(start)});
        return OkStatus;
    }

    Status add_bytes(const std::string& name, const std::string& bytes) {
        usize pos = 0;
        return add(name, [&](u8* dst, usize cap, usize& n) -> Status {
            n = std::min(cap, bytes.size() - pos);
            std::memcpy(dst, bytes.data() + pos, n);
            pos += n;
            return OkStatus;
        });
    }

    Status add_file(const std::string& name, std::FILE* src) {
        return add(name, [&](u8* dst, usize cap, usize& n) -> Status {
            n = std::fread(dst, 1, cap, src);
            if (n == 0 && std::ferror(src)) return Status{Errc::IoError, "midia ilegivel"};
            return OkStatus;
        });
    }

    Status finish() {
        const u64 cdStart = tell(f_);
        std::vector<u8> cd;
        for (const CentralEntry& e : entries_) {
            put32(cd, kSigCentral); put16(cd, 20); put16(cd, 20); put16(cd, kFlagUtf8); put16(cd, 0);
            put16(cd, 0); put16(cd, 0x21);
            put32(cd, e.crc); put32(cd, e.size); put32(cd, e.size);
            put16(cd, static_cast<u32>(e.name.size())); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0);
            put32(cd, 0); put32(cd, e.offset);
            cd.insert(cd.end(), e.name.begin(), e.name.end());
        }
        if (cdStart + cd.size() > kZipLimit || entries_.size() > 0xFFFF) return Status{Errc::NotSupported, "pacote maior que 4 GB"};
        const usize cdBytes = cd.size();
        put32(cd, kSigEnd); put16(cd, 0); put16(cd, 0);
        put16(cd, static_cast<u32>(entries_.size())); put16(cd, static_cast<u32>(entries_.size()));
        put32(cd, static_cast<u32>(cdBytes)); put32(cd, static_cast<u32>(cdStart)); put16(cd, 0);
        if (std::fwrite(cd.data(), 1, cd.size(), f_) != cd.size()) return write_error();
        if (std::fflush(f_) != 0) return write_error();
        return OkStatus;
    }

private:
    static Status write_error() {
        return errno == ENOSPC ? Status{Errc::StorageFull, "sem espaco"} : Status{Errc::IoError, "falha ao gravar"};
    }
    std::FILE* f_;
    std::vector<CentralEntry> entries_;
};

// --- Leitura -------------------------------------------------------------------

struct ZipEntry {
    std::string name;
    u16 method = 0;
    u32 crc = 0;
    u32 size = 0;
    u32 compressed = 0;
    u32 localOffset = 0;
    u32 dataLimit = 0;
};

Status read_directory(std::FILE* f, std::vector<ZipEntry>& out) {
    if (!seek_end(f)) return Status{Errc::IoError, "arquivo ilegivel"};
    const u64 total = tell(f);
    if (total < 22) return Status{Errc::UnsupportedFormat, "nao e um pacote"};
    const u64 tail = std::min<u64>(total, 22 + 0xFFFF);
    std::vector<u8> b(static_cast<usize>(tail));
    if (!seek(f, total - tail) || std::fread(b.data(), 1, b.size(), f) != b.size()) return Status{Errc::IoError, "arquivo ilegivel"};
    usize eocd = std::string::npos;
    for (usize i = b.size() - 22 + 1; i-- > 0;) {
        if (get32(&b[i]) == kSigEnd && i + 22 + get16(&b[i + 20]) == b.size()) { eocd = i; break; }
    }
    if (eocd == std::string::npos) return Status{Errc::UnsupportedFormat, "nao e um pacote"};
    const u32 count = get16(&b[eocd + 10]);
    const u32 cdSize = get32(&b[eocd + 12]);
    const u32 cdOffset = get32(&b[eocd + 16]);
    if (get16(&b[eocd + 4]) || get16(&b[eocd + 6]) || get16(&b[eocd + 8]) != count)
        return Status{Errc::UnsupportedFormat, "pacote em varios volumes"};
    if (static_cast<u64>(cdOffset) + cdSize > total - tail + eocd || count > cdSize / 46)
        return Status{Errc::CorruptData, "pacote cortado"};
    // Stream each record: an untrusted directory length must not allocate GiB.
    u64 p = 0, nameBytes = 0;
    constexpr u64 kNameBudget = 8ull << 20;
    std::set<std::string> names;
    for (u32 i = 0; i < count; ++i) {
        u8 h[46];
        if (p + sizeof h > cdSize || !seek(f, cdOffset + p) || std::fread(h, 1, sizeof h, f) != sizeof h || get32(h) != kSigCentral)
            return Status{Errc::CorruptData, "diretorio do pacote"};
        if ((get16(h + 8) & ~static_cast<u16>(kFlagUtf8 | 8)) || get16(h + 34))
            return Status{Errc::UnsupportedFormat, "entrada nao suportada"};
        ZipEntry e;
        e.method = get16(h + 10);
        e.crc = get32(h + 16);
        e.compressed = get32(h + 20);
        e.size = get32(h + 24);
        const u32 nameLen = get16(h + 28);
        const u32 extraLen = get16(h + 30);
        const u32 commentLen = get16(h + 32);
        e.localOffset = get32(h + 42);
        e.dataLimit = cdOffset;
        if (!nameLen || p + 46 + nameLen + extraLen + commentLen > cdSize || e.localOffset >= cdOffset)
            return Status{Errc::CorruptData, "diretorio do pacote"};
        if (nameLen > 4096 || nameLen > kNameBudget - nameBytes)
            return Status{Errc::BudgetExceeded, "diretorio do pacote muito grande"};
        nameBytes += nameLen;
        e.name.resize(nameLen);
        if (std::fread(e.name.data(), 1, nameLen, f) != nameLen || !names.insert(e.name).second)
            return Status{Errc::CorruptData, "diretorio do pacote"};
        p += 46 + nameLen + extraLen + commentLen;
        out.push_back(std::move(e));
    }
    return p == cdSize ? OkStatus : Status{Errc::CorruptData, "diretorio do pacote"};
}

Status seek_entry(std::FILE* f, const ZipEntry& e) {
    if (e.method != 0 || e.compressed != e.size) return Status{Errc::UnsupportedFormat, "entrada comprimida"};
    u8 h[30];
    if (!seek(f, e.localOffset) || std::fread(h, 1, 30, f) != 30 || get32(h) != kSigLocal) return Status{Errc::CorruptData, "entrada do pacote"};
    const u64 data = static_cast<u64>(e.localOffset) + 30 + get16(h + 26) + get16(h + 28);
    if (get16(h + 8) != e.method || (get16(h + 6) & ~static_cast<u16>(kFlagUtf8 | 8)) ||
        data + e.size > e.dataLimit || get16(h + 26) != e.name.size()) return Status{Errc::CorruptData, "entrada do pacote"};
    std::string name(e.name.size(), '\0');
    if (std::fread(name.data(), 1, name.size(), f) != name.size() || name != e.name || !seek(f, data))
        return Status{Errc::CorruptData, "entrada do pacote"};
    return OkStatus;
}

/// Copia a entrada para `dest` conferindo o CRC. Só "stored".
Status extract(std::FILE* f, const ZipEntry& e, const std::string& dest) {
    if (const Status s = seek_entry(f, e); !s.ok()) return s;
    File out(fileio::open_file(dest, "wb"));
    if (!out.f) return Status{Errc::IoError, "destino"};
    std::vector<u8> buf(1u << 20);
    u64 left = e.size;
    u32 crc = 0;
    while (left > 0) {
        const usize want = static_cast<usize>(std::min<u64>(left, buf.size()));
        if (std::fread(buf.data(), 1, want, f) != want) return Status{Errc::CorruptData, "pacote cortado"};
        crc = crc_update(crc, buf.data(), want);
        if (std::fwrite(buf.data(), 1, want, out.f) != want) {
            return errno == ENOSPC ? Status{Errc::StorageFull, "sem espaco"} : Status{Errc::IoError, "falha ao gravar"};
        }
        left -= want;
    }
    if (crc != e.crc) return Status{Errc::ChecksumMismatch, "pacote danificado"};
    std::FILE* raw = out.release();
    const int flushed = std::fflush(raw);
    const int flushError = errno;
    const int closed = std::fclose(raw);
    if (flushed != 0 || closed != 0) {
        return (flushed != 0 ? flushError : errno) == ENOSPC ? Status{Errc::StorageFull, "sem espaco"} : Status{Errc::IoError, "falha ao gravar"};
    }
    return OkStatus;
}

Status read_entry_text(std::FILE* f, const ZipEntry& e, std::string& out) {
    if (e.method != 0 || e.size > (1u << 20)) return Status{Errc::UnsupportedFormat, "manifesto"};
    if (const Status s = seek_entry(f, e); !s.ok()) return s;
    out.resize(e.size);
    if (e.size && std::fread(out.data(), 1, e.size, f) != e.size) return Status{Errc::CorruptData, "manifesto"};
    if (crc_update(0, reinterpret_cast<const u8*>(out.data()), out.size()) != e.crc) return Status{Errc::ChecksumMismatch, "manifesto"};
    return OkStatus;
}

/// Caminho livre em `dir` para `name` ("foto.jpg", "foto-2.jpg", ...).
std::string free_path(const fs::path& dir, const std::string& name) {
    const fs::path base = fs::u8path(name);
    const std::string stem = utf8_of(base.stem());
    const std::string ext = utf8_of(base.extension());
    std::error_code ec;
    fs::path candidate = dir / base;
    for (int i = 2; fs::exists(candidate, ec) && i < 10000; ++i) {
        candidate = dir / fs::u8path(stem + "-" + std::to_string(i) + ext);
    }
    if (fs::exists(candidate, ec) || ec) return {};
    return utf8_of(candidate);
}

u32 distinct_media(const std::string& aureaPath) {
    std::vector<MediaRef> refs;
    if (!list_media(aureaPath, refs).ok()) return 0;
    return static_cast<u32>(refs.size());
}

} // namespace

// =============================================================================

Status list_media(const std::string& aureaPath, std::vector<MediaRef>& out) noexcept {
    Project p;
    if (const Status s = load_project(p, aureaPath); !s.ok()) return s;
    std::set<std::string> seen;
    p.for_each_asset([&](AssetId, const Asset& a) {
        if (!packable(a.kind) || a.sourcePath.empty() || !seen.insert(a.sourcePath).second) return;
        MediaRef r;
        r.kind = a.kind;
        r.stored = a.sourcePath;
        r.resolved = a.sourcePath;
        r.name = a.originalFilename.empty() ? a.name : a.originalFilename;
        out.push_back(std::move(r));
    });
    return OkStatus;
}

Status write_package(const std::string& aureaPath, const std::string& outPath, const std::string& title,
                     const std::string& appVersion, const std::vector<MediaFile>& media,
                     ExportResult* result) noexcept {
    std::lock_guard<std::mutex> writeLock(packageWriteMutex);
    // O projeto precisa abrir aqui: não se manda adiante um arquivo quebrado.
    {
        Project p;
        if (const Status s = load_project(p, aureaPath); !s.ok()) return s;
    }
    File project(fileio::open_file(aureaPath, "rb"));
    if (!project.f) return Status{Errc::IoError, "projeto ilegivel"};

    const std::string tmp = outPath + ".tmp";
    File out(fileio::open_file(tmp, "wb"));
    if (!out.f) return Status{Errc::IoError, "destino"};
    ExportResult r;
    ZipWriter zip(out.f);

    // Mídia primeiro (os nomes das entradas vão no manifesto).
    std::string manifest = "aurea-project " + std::to_string(kPackageVersion) + "\n";
    manifest += "title " + escape(title) + "\n";
    manifest += "app " + escape(appVersion) + "\n";
    std::vector<std::pair<std::string, const MediaFile*>> entries;
    std::set<std::string> seen;
    for (const MediaFile& m : media) {
        if (m.stored.empty() || !seen.insert(m.stored).second) continue;
        char prefix[8];
        std::snprintf(prefix, sizeof prefix, "%03u-", static_cast<unsigned>(entries.size() + 1));
        entries.emplace_back(std::string("media/") + prefix + safe_file_name(m.name.empty() ? m.readable : m.name), &m);
    }
    Status s = OkStatus;
    for (const auto& [entry, m] : entries) {
        File src(fileio::open_file(m->readable, "rb"));
        if (!src.f) { ++r.skipped; continue; }
        s = zip.add_file(entry, src.f);
        if (!s.ok()) break;
        manifest += "media " + escape(entry) + "\t" + escape(m->stored) + "\n";
        ++r.included;
    }
    if (s.ok()) s = zip.add_file(kProjectEntry, project.f);
    if (s.ok()) s = zip.add_bytes(kManifestEntry, manifest);
    if (s.ok()) s = zip.finish();
    r.bytes = tell(out.f);
    std::FILE* raw = out.release();
    if (std::fclose(raw) != 0 && s.ok()) s = Status{Errc::IoError, "falha ao gravar"};
    if (!s.ok()) {
        fileio::remove_file(tmp);
        return s;
    }
    if (const Status committed = fileio::commit_file(tmp, outPath); !committed.ok()) {
        fileio::remove_file(tmp);
        return committed;
    }
    if (result) *result = r;
    return OkStatus;
}

Status relink_media(const std::string& aureaPath,
                    const std::vector<std::pair<std::string, std::string>>& storedToNew,
                    u32* relinked) noexcept {
    Project p;
    if (const Status s = load_project(p, aureaPath); !s.ok()) return s;
    std::map<std::string, std::string> map(storedToNew.begin(), storedToNew.end());
    u32 n = 0;
    p.for_each_asset([&](AssetId, Asset& a) {
        const auto it = map.find(a.sourcePath);
        if (it == map.end()) return;
        a.sourcePath = it->second;
        // Proxy, miniatura e waveform eram do outro aparelho: refeitos aqui.
        a.proxyPath.clear();
        a.proxyWidth = a.proxyHeight = 0;
        a.thumbnailPath.clear();
        a.waveformPath.clear();
        a.waveformBuckets = 0;
        ++n;
    });
    if (n > 0) {
        SaveOptions o;
        o.keepBackup = false;
        if (const Status s = ProjectSerializer::save(p, aureaPath, o); !s.ok()) return s;
    }
    if (relinked) *relinked = n;
    return OkStatus;
}

Status read_package(const std::string& packagePath, const std::string& projectOut, const std::string& mediaDir,
                    ImportResult& out) noexcept {
    std::error_code ec;
    if (fs::exists(fs::u8path(projectOut), ec)) return Status{Errc::AlreadyExists, "o projeto ja existe"};
    out = ImportResult{};

    // Um `.aurea` solto: o próprio projeto, sem mídia.
    {
        FileHeader header;
        std::vector<SectionHeader> sections;
        if (ProjectSerializer::peek(packagePath, header, sections).ok()) {
            if (const Status c = fileio::copy_file(packagePath, projectOut); !c.ok()) return c;
            Project p;
            if (const Status s = load_project(p, projectOut); !s.ok()) {
                fileio::remove_file(projectOut);
                return s.code() == Errc::UnsupportedVersion ? s : Status{Errc::CorruptData, "projeto ilegivel"};
            }
            out.missing = distinct_media(projectOut);
            return OkStatus;
        }
    }

    File f(fileio::open_file(packagePath, "rb"));
    if (!f.f) return Status{Errc::IoError, "arquivo ilegivel"};
    u8 sig[4] = {};
    if (std::fread(sig, 1, 4, f.f) != 4 || get32(sig) != kSigLocal) return Status{Errc::UnsupportedFormat, "nao e um projeto do Aurea"};
    std::vector<ZipEntry> entries;
    if (const Status s = read_directory(f.f, entries); !s.ok()) return s;
    const auto find = [&](const std::string& name) -> const ZipEntry* {
        for (const ZipEntry& e : entries) if (e.name == name) return &e;
        return nullptr;
    };
    const ZipEntry* manifestEntry = find(kManifestEntry);
    const ZipEntry* projectEntry = find(kProjectEntry);
    if (!manifestEntry || !projectEntry) return Status{Errc::UnsupportedFormat, "nao e um projeto do Aurea"};
    std::string manifest;
    if (const Status s = read_entry_text(f.f, *manifestEntry, manifest); !s.ok()) return s;

    // Primeira linha: "aurea-project <versão>".
    std::vector<std::pair<std::string, std::string>> mediaLines;   // entrada → caminho gravado
    {
        usize pos = 0;
        bool first = true;
        while (pos <= manifest.size()) {
            usize nl = manifest.find('\n', pos);
            if (nl == std::string::npos) nl = manifest.size();
            const std::string line = manifest.substr(pos, nl - pos);
            pos = nl + 1;
            if (first) {
                first = false;
                constexpr const char* kHead = "aurea-project ";
                if (line.rfind(kHead, 0) != 0) return Status{Errc::UnsupportedFormat, "nao e um projeto do Aurea"};
                const long v = std::strtol(line.c_str() + std::strlen(kHead), nullptr, 10);
                if (v <= 0) return Status{Errc::UnsupportedFormat, "nao e um projeto do Aurea"};
                if (static_cast<u64>(v) > kPackageVersion) return Status{Errc::UnsupportedVersion, "feito por uma versao mais nova"};
                continue;
            }
            if (line.rfind("title ", 0) == 0) out.title = unescape(line.substr(6));
            else if (line.rfind("app ", 0) == 0) out.appVersion = unescape(line.substr(4));
            else if (line.rfind("media ", 0) == 0) {
                const usize tab = line.find('\t', 6);
                if (tab != std::string::npos) mediaLines.emplace_back(unescape(line.substr(6, tab - 6)), unescape(line.substr(tab + 1)));
            }
        }
    }

    std::vector<std::string> written;
    const auto rollback = [&] {
        for (const std::string& w : written) fileio::remove_file(w);
        fileio::remove_file(projectOut);
        std::error_code e2;
        if (fs::is_empty(fs::u8path(mediaDir), e2)) fs::remove(fs::u8path(mediaDir), e2);
    };

    if (const Status s = extract(f.f, *projectEntry, projectOut); !s.ok()) {
        fileio::remove_file(projectOut);
        return s;
    }
    {
        Project p;
        if (const Status s = load_project(p, projectOut); !s.ok()) {
            fileio::remove_file(projectOut);
            return s.code() == Errc::UnsupportedVersion ? s : Status{Errc::CorruptData, "projeto ilegivel"};
        }
    }

    std::vector<std::pair<std::string, std::string>> relink;
    if (!mediaLines.empty()) {
        fs::create_directories(fs::u8path(mediaDir), ec);
        if (ec) { rollback(); return Status{Errc::IoError, "pasta de midia"}; }
    }
    for (const auto& [entryName, stored] : mediaLines) {
        const ZipEntry* e = find(entryName);
        if (!e || entryName.rfind("media/", 0) != 0) continue;
        std::string name = safe_file_name(entryName.substr(6));
        // "NNN-" do pacote sai: o arquivo volta com o nome que a pessoa conhece.
        if (name.size() > 4 && std::all_of(name.begin(), name.begin() + 3, [](char c) { return c >= '0' && c <= '9'; }) && name[3] == '-') {
            name = name.substr(4);
            if (name.empty()) name = "media";
        }
        const std::string dest = free_path(fs::u8path(mediaDir), name);
        if (dest.empty()) { rollback(); return Status{Errc::AlreadyExists, "nenhum nome de midia disponivel"}; }
        if (const Status s = extract(f.f, *e, dest); !s.ok()) {
            fileio::remove_file(dest);
            rollback();
            return s;
        }
        written.push_back(dest);
        relink.emplace_back(stored, dest);
    }
    u32 relinked = 0;
    if (!relink.empty()) {
        if (const Status s = relink_media(projectOut, relink, &relinked); !s.ok()) { rollback(); return s; }
    }
    out.relinked = relinked;
    const u32 total = distinct_media(projectOut);
    out.missing = total > static_cast<u32>(relink.size()) ? total - static_cast<u32>(relink.size()) : 0;
    return OkStatus;
}

} // namespace aurea::package
