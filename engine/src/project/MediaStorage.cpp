// =============================================================================
//  Aurea / project / MediaStorage.cpp — ver MediaStorage.hpp.
//
//  Toda decisão de apagar passa por três portas, nesta ordem: (1) referência
//  em algum arquivo de projeto restaurável ou no projeto aberto; (2) caminho
//  canônico dentro de uma pasta de mídia do app e fora das de biblioteca;
//  (3) arquivo comum (ou pasta derivada sem link nenhum dentro). Na dúvida
//  (projeto ilegível, pasta que não lista), nada sai.
// =============================================================================
#include "aurea/project/MediaStorage.hpp"

#include "aurea/project/Asset.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/project/ProjectPackage.hpp"
#include "aurea/project/Serialization.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>
#include <string_view>

namespace aurea::storage {
namespace {

namespace fs = std::filesystem;

std::string utf8(const fs::path& p) {
    const auto s = p.generic_u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool digits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

/// O que vem depois de ".aurea" num arquivo de projeto restaurável.
bool project_suffix(std::string_view rest) {
    if (rest.empty() || rest == ".bak" || rest == ".tmp" || rest == ".recusado") return true;
    if (rest.rfind(".corrompido", 0) == 0) {
        const std::string_view n = rest.substr(11);
        return n.empty() || digits(n);
    }
    // Cópia do formato antigo: ".v12.bak".
    if (rest.size() > 6 && rest.rfind(".v", 0) == 0 && rest.substr(rest.size() - 4) == ".bak")
        return digits(rest.substr(2, rest.size() - 6));
    return false;
}

/// Só diretório ou arquivo comum: link simbólico, junção do Windows ou
/// qualquer outro tipo fica de fora (nunca seguido, nunca apagado).
bool plain_type(const fs::file_status& st, bool directory) {
    return directory ? st.type() == fs::file_type::directory : st.type() == fs::file_type::regular;
}

bool under(const std::string& root, const std::string& path) {
    if (root.empty() || path.size() <= root.size() || path.compare(0, root.size(), root) != 0) return false;
    return root.back() == '/' || path[root.size()] == '/';
}

struct Roots {
    std::vector<std::string> managed, keep;
    /// A raiz de mídia que contém `path` (fora das de biblioteca), ou vazio.
    const std::string* owner(const std::string& path) const {
        for (const std::string& k : keep) if (k == path || under(k, path)) return nullptr;
        for (const std::string& r : managed) if (under(r, path)) return &r;
        return nullptr;
    }
};

Roots canonical_roots(const std::vector<std::string>& managed, const std::vector<std::string>& keep) {
    Roots out;
    for (const std::string& r : managed) {
        const std::string c = canonical_file_path(r);
        std::error_code ec;
        // A raiz precisa ser uma pasta de verdade e funda o bastante: "/" ou
        // "C:/" nunca é pasta de mídia do app.
        if (c.empty() || std::count(c.begin(), c.end(), '/') < 2) continue;
        if (!plain_type(fs::symlink_status(fs::u8path(c), ec), true) || ec) continue;
        out.managed.push_back(c);
    }
    // Raiz dentro de outra conta uma vez só.
    std::sort(out.managed.begin(), out.managed.end());
    std::vector<std::string> unique;
    for (const std::string& r : out.managed)
        if (std::none_of(unique.begin(), unique.end(), [&](const std::string& u) { return u == r || under(u, r); }))
            unique.push_back(r);
    out.managed = std::move(unique);
    for (const std::string& k : keep)
        if (std::string c = canonical_file_path(k); !c.empty()) out.keep.push_back(std::move(c));
    return out;
}

/// Bytes que o arquivo ocupa SÓ por este nome: um link físico (a importação
/// repetida do iOS) divide o tamanho entre os nomes.
u64 share_of(const fs::path& p) {
    std::error_code ec;
    const u64 size = fs::file_size(p, ec);
    if (ec) return 0;
    const auto links = fs::hard_link_count(p, ec);
    return ec || links <= 1 ? size : size / static_cast<u64>(links);
}

/// Candidato a "mídia sem projeto". Áudio fica de fora de propósito: dentro
/// das pastas de mídia ele é a biblioteca de sons do app (a tela de áudio lista
/// narrações e sons importados mesmo sem projeto nenhum usá-los), do mesmo
/// jeito que `plan_release` não o libera.
bool media_name(const std::string& name) {
    static const std::set<std::string> kMedia = {
        // vídeo
        "mp4", "mov", "m4v", "3gp", "3g2", "mkv", "webm", "avi", "mts", "m2ts", "ts", "hevc",
        // imagem
        "jpg", "jpeg", "png", "heic", "heif", "webp", "gif", "bmp", "tga", "tif", "tiff", "exr", "psd", "dng", "avif",
    };
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return true;   // a cópia sem extensão do Android
    return kMedia.count(lower(name.substr(dot + 1))) != 0;
}

/// Temporário de algo em andamento (cópia, extração, gravação atômica).
bool transient_name(const std::string& name) {
    if (name.empty() || name[0] == '.') return true;
    const std::string l = lower(name);
    for (const char* s : {".tmp", ".part", ".parcial"})
        if (l.size() >= std::strlen(s) && l.compare(l.size() - std::strlen(s), std::string::npos, s) == 0) return true;
    return l.rfind("importando", 0) == 0 || l.rfind("copy-", 0) == 0;
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

constexpr std::string_view kStagingSuffix = ".parcial";

/// Cópia/extração interrompida (`<pasta>.parcial`, só o motor cria): nunca é
/// referência de projeto; velha, sai inteira como órfã.
bool staging_dir(const std::string& dir) {
    return ends_with(utf8(fs::u8path(dir).filename()), kStagingSuffix);
}

/// Pasta que só sai inteira: derivada, do formato antigo ou cópia interrompida.
bool whole_unit(const std::string& dir) {
    return is_derived(dir) || is_legacy_derived(dir) || staging_dir(dir);
}

/// A pasta inteira (derivada ou antiga) que contém `path`, abaixo de `root`, ou vazio.
std::string derived_dir_of(const std::string& path, const std::string& root) {
    std::string dir = utf8(fs::u8path(path).parent_path());
    while (under(root, dir)) {
        if (is_derived(dir) || is_legacy_derived(dir)) return dir;
        dir = utf8(fs::u8path(dir).parent_path());
    }
    return {};
}

bool any_under(const std::set<std::string>& refs, const std::string& dir) {
    const auto it = refs.lower_bound(dir + "/");
    return it != refs.end() && under(dir, *it);
}

/// Bytes, arquivos e o mais novo de uma pasta derivada; false = tem link ou
/// algo que não é arquivo/pasta comum (não pode sair inteira).
bool measure_dir(const std::string& dir, u64& bytes, u32& files, fs::file_time_type& newest) {
    bytes = 0; files = 0; newest = fs::file_time_type::min();
    std::error_code ec;
    // A data da própria pasta (e das de dentro) conta: a cópia que preserva a
    // data do original (FileManager do iOS) ainda muda a da pasta que recebe.
    if (const auto t = fs::last_write_time(fs::u8path(dir), ec); !ec) newest = t;
    ec.clear();
    fs::recursive_directory_iterator it(fs::u8path(dir), fs::directory_options::none, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::file_status st = it->symlink_status(ec);
        if (ec) return false;
        if (plain_type(st, true)) {
            if (const auto t = it->last_write_time(ec); !ec) newest = std::max(newest, t);
            ec.clear();
            continue;
        }
        if (!plain_type(st, false)) return false;
        bytes += share_of(it->path());
        ++files;
        const auto t = it->last_write_time(ec);
        if (!ec) newest = std::max(newest, t);
        ec.clear();
    }
    return !ec;
}

using RefSet = std::set<std::string>;

/// Junta as referências dos arquivos de projeto. Devolve quantos não abriram.
u32 collect(const std::vector<std::string>& projects, const Resolver& resolve, RefSet& refs) {
    u32 unreadable = 0;
    for (const std::string& file : projects) {
        MediaRefs r;
        if (!read_project_refs(file, resolve, r).ok()) { ++unreadable; continue; }
        refs.insert(r.paths.begin(), r.paths.end());
    }
    return unreadable;
}

/// Só vídeo e imagem saem com o projeto. O áudio importado também é a
/// biblioteca de sons do app (o navegador de áudio lista Media/ no iOS e
/// import-sources/voz no Android): fica, e aparece como "sem projeto".
bool deletable_kind(u8 kind) {
    const auto k = static_cast<AssetKind>(kind);
    return k == AssetKind::Video || k == AssetKind::Image;
}

/// Quatro faixas FNV-1a sobre palavras de 64 bits e a cauda byte a byte (a
/// mesma ideia do hash de fontes do export): ~8x o FNV por byte.
struct Hasher {
    std::array<u64, 4> lane{1469598103934665603ull, 0x84222325cbf29ce4ull, 0x9e3779b97f4a7c15ull, 0xc2b2ae3d27d4eb4full};
    u64 tail = 1469598103934665603ull;
    static u64 bytes(const u8* p, usize n, u64 h) {
        for (usize i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }
    void add(const u8* p, usize n) {
        usize i = 0;
        for (; i + 32 <= n; i += 32)
            for (usize k = 0; k < 4; ++k) {
                u64 word;
                std::memcpy(&word, p + i + 8 * k, 8);
                lane[k] = (lane[k] ^ word) * 1099511628211ull;
            }
        tail = bytes(p + i, n - i, tail);
    }
    u64 value(u64 total) const {
        const u64 h = bytes(reinterpret_cast<const u8*>(lane.data()), sizeof(lane), tail);
        return bytes(reinterpret_cast<const u8*>(&total), sizeof(total), h);
    }
};

} // namespace

bool is_project_file(const std::string& fileName) noexcept try {
    for (usize at = fileName.find(".aurea"); at != std::string::npos; at = fileName.find(".aurea", at + 1)) {
        if (at == 0) continue;
        if (project_suffix(std::string_view(fileName).substr(at + 6))) return true;
    }
    return false;
} catch (...) {
    return false;
}

std::string canonical_file_path(const std::string& path) noexcept try {
    std::string raw = path;
    if (raw.rfind("file://", 0) == 0) raw.erase(0, 7);
    if (raw.empty()) return {};
    const fs::path p = fs::u8path(raw);
    if (!p.is_absolute()) return {};
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    if (ec || c.empty()) c = p.lexically_normal();
    return utf8(c);
} catch (...) {
    return {};
}

Status find_project_files(const std::string& directory, std::vector<std::string>& out) noexcept try {
    std::error_code ec;
    const fs::path root = fs::u8path(directory);
    if (!plain_type(fs::symlink_status(root, ec), true) || ec) return Status{Errc::NotFound, "pasta de projetos"};
    fs::recursive_directory_iterator it(root, fs::directory_options::none, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::file_status st = it->symlink_status(ec);
        if (ec) break;
        const std::string name = utf8(it->path().filename());
        if (plain_type(st, true)) {
            // Miniaturas e outras pastas ocultas não guardam projeto.
            if (!name.empty() && name[0] == '.') it.disable_recursion_pending();
            continue;
        }
        if (plain_type(st, false) && is_project_file(name)) {
            const std::string c = canonical_file_path(utf8(it->path()));
            if (!c.empty()) out.push_back(c);
        }
    }
    // Pasta que não lista inteira: quem chama não sabe o que sobra (nada sai).
    return ec ? Status{Errc::IoError, "pasta de projetos ilegivel"} : OkStatus;
} catch (...) {
    return Status{Errc::IoError};
}

Status read_project_refs(const std::string& projectFile, const Resolver& resolve, MediaRefs& out) noexcept try {
    Project project;
    LoadOptions options;
    options.lazyAssets = true;
    options.tolerateCorruptSections = false;
    LoadReport report;
    if (const Status s = ProjectSerializer::load(project, projectFile, options, &report, nullptr); !s.ok()) return s;
    // Seção faltando = referências desconhecidas: tão ilegível quanto o resto.
    if (report.partial) return Status{Errc::CorruptData, "secoes faltando"};
    std::vector<package::MediaRef> refs;
    package::collect_media(project, refs);
    for (const package::MediaRef& m : refs) {
        const std::string c = canonical_file_path(resolve ? resolve(m.stored) : m.stored);
        if (c.empty()) continue;
        out.paths.push_back(c);
        out.kinds.push_back(static_cast<u8>(m.kind));
    }
    return OkStatus;
} catch (const std::bad_alloc&) {
    return Status{Errc::OutOfMemory};
} catch (...) {
    return Status{Errc::IoError};
}

Status plan_release(const ReleaseRequest& request, const Resolver& resolve, ReleasePlan& out) noexcept try {
    out = ReleasePlan{};
    const Roots roots = canonical_roots(request.managedRoots, request.keepRoots);
    if (roots.managed.empty()) return OkStatus;
    RefSet kept(request.live.paths.begin(), request.live.paths.end());
    out.unreadable = collect(request.remainingProjects, resolve, kept);
    if (out.unreadable) { out.blocked = true; return OkStatus; }

    RefSet chosen;
    for (const std::string& file : request.deletedProjects) {
        MediaRefs refs;
        // O apagado que não abre só não libera nada: o que ele usava fica.
        if (!read_project_refs(file, resolve, refs).ok()) continue;
        for (usize i = 0; i < refs.paths.size(); ++i) {
            const std::string& path = refs.paths[i];
            if (!deletable_kind(refs.kinds[i]) || kept.count(path)) continue;
            const std::string* root = roots.owner(path);
            if (!root) continue;
            std::error_code ec;
            if (!plain_type(fs::symlink_status(fs::u8path(path), ec), false) || ec) continue;
            if (const std::string dir = derived_dir_of(path, *root); !dir.empty()) {
                // Pasta derivada: inteira, e só se nada que fica aponta para dentro dela.
                if (!any_under(kept, dir)) chosen.insert(dir);
                continue;
            }
            chosen.insert(path);
        }
    }
    for (const std::string& path : chosen) {
        std::error_code ec;
        const fs::path p = fs::u8path(path);
        if (plain_type(fs::symlink_status(p, ec), true)) {
            u64 bytes = 0; u32 files = 0; fs::file_time_type newest;
            if (!measure_dir(path, bytes, files, newest)) continue;
            out.bytes += bytes;
        } else {
            out.bytes += share_of(p);
        }
        out.paths.push_back(path);
    }
    return OkStatus;
} catch (const std::bad_alloc&) {
    out = ReleasePlan{};
    out.blocked = true;
    return Status{Errc::OutOfMemory};
} catch (...) {
    out = ReleasePlan{};
    out.blocked = true;
    return Status{Errc::IoError};
}

Status scan(const ScanRequest& request, const Resolver& resolve, ScanResult& out) noexcept try {
    out = ScanResult{};
    const Roots roots = canonical_roots(request.managedRoots, request.keepRoots);
    RefSet refs(request.live.paths.begin(), request.live.paths.end());
    out.unreadable = collect(request.projects, resolve, refs);
    out.blocked = out.unreadable != 0;
    const auto limit = fs::file_time_type::clock::now() - std::chrono::seconds(static_cast<i64>(request.minAgeSeconds));
    for (const std::string& root : roots.managed) {
        std::error_code ec;
        fs::recursive_directory_iterator it(fs::u8path(root), fs::directory_options::none, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            const fs::file_status st = it->symlink_status(ec);
            if (ec) break;
            const std::string path = utf8(it->path());
            if (plain_type(st, true)) {
                if (!roots.owner(path + "/x")) { it.disable_recursion_pending(); continue; }  // biblioteca
                if (!whole_unit(path)) continue;
                it.disable_recursion_pending();
                u64 bytes = 0; u32 files = 0; fs::file_time_type newest;
                const bool whole = measure_dir(path, bytes, files, newest);
                if (!out.blocked && whole && !any_under(refs, path) && newest <= limit) {
                    out.orphans.push_back(path);
                    out.orphanBytes += bytes;
                    out.orphanFiles += files;
                } else {
                    out.referencedBytes += bytes;
                    out.referencedFiles += files;
                }
                continue;
            }
            if (!plain_type(st, false)) continue;
            const u64 bytes = share_of(it->path());
            const std::string name = utf8(it->path().filename());
            const bool candidate = !transient_name(name) && (request.anyFile || media_name(name));
            const auto time = it->last_write_time(ec);
            const bool old = !ec && time <= limit;
            ec.clear();
            if (!out.blocked && candidate && old && !refs.count(path)) {
                out.orphans.push_back(path);
                out.orphanBytes += bytes;
                ++out.orphanFiles;
            } else {
                out.referencedBytes += bytes;
                ++out.referencedFiles;
            }
        }
        // Pasta de mídia que não lista inteira: nada dela é dado como órfão.
        if (ec) { out.orphans.clear(); out.orphanBytes = 0; out.orphanFiles = 0; out.blocked = true; }
    }
    return OkStatus;
} catch (const std::bad_alloc&) {
    out = ScanResult{};
    out.blocked = true;
    return Status{Errc::OutOfMemory};
} catch (...) {
    out = ScanResult{};
    out.blocked = true;
    return Status{Errc::IoError};
}

RemoveResult remove_managed(const std::vector<std::string>& paths, const std::vector<std::string>& managedRoots,
                            const std::vector<std::string>& keepRoots) noexcept {
    RemoveResult result;
    try {
        const Roots roots = canonical_roots(managedRoots, keepRoots);
        for (const std::string& raw : paths) {
            const std::string path = canonical_file_path(raw);
            const std::string* root = path.empty() ? nullptr : roots.owner(path);
            if (!root) { ++result.refused; continue; }
            std::error_code ec;
            const fs::path p = fs::u8path(path);
            const fs::file_status st = fs::symlink_status(p, ec);
            if (ec) continue;   // já não existe
            if (plain_type(st, false)) {
                // Só o último nome libera espaço (link físico da importação do iOS).
                const u64 links = fs::hard_link_count(p, ec);
                const u64 size = fs::file_size(p, ec);
                if (!fs::remove(p, ec) || ec) continue;
                ++result.files;
                if (links <= 1) result.bytes += size;
            } else if (plain_type(st, true) && whole_unit(path)) {
                u64 bytes = 0; u32 files = 0; fs::file_time_type newest;
                if (!measure_dir(path, bytes, files, newest)) { ++result.refused; continue; }
                fs::remove_all(p, ec);
                if (ec) continue;
                result.files += files;
                result.bytes += bytes;
            } else {
                ++result.refused;
                continue;
            }
            // Pastas que ficaram vazias saem (Media/Projetos/<nome>, seq-…), a raiz não.
            for (fs::path dir = p.parent_path(); under(*root, utf8(dir)); dir = dir.parent_path()) {
                std::error_code e;
                if (!fs::is_empty(dir, e) || e || !fs::remove(dir, e) || e) break;
            }
        }
    } catch (...) {
    }
    return result;
}

Status stream_content_key(std::FILE* f, u64& hash, u64& bytes) noexcept try {
    if (!f) return Status{Errc::InvalidArgument};
    // Bloco múltiplo de 32: a divisão em faixas não depende de onde o fread parou.
    std::vector<u8> buffer(1u << 16);
    Hasher h;
    u64 total = 0;
    for (;;) {
        usize n = 0;
        while (n < buffer.size()) {
            const usize got = std::fread(buffer.data() + n, 1, buffer.size() - n, f);
            if (!got) break;
            n += got;
        }
        h.add(buffer.data(), n);
        total += n;
        if (n != buffer.size()) {
            if (std::ferror(f)) return Status{Errc::IoError};
            break;
        }
    }
    hash = h.value(total);
    bytes = total;
    return OkStatus;
} catch (const std::bad_alloc&) {
    return Status{Errc::OutOfMemory};
} catch (...) {
    return Status{Errc::IoError};
}

Status file_content_key(const std::string& path, u64& hash, u64& bytes) noexcept try {
    std::unique_ptr<std::FILE, decltype(&std::fclose)> f(fileio::open_file(path, "rb"), &std::fclose);
    if (!f) return Status{Errc::NotFound};
    return stream_content_key(f.get(), hash, bytes);
} catch (...) {
    return Status{Errc::IoError};
}

u64 content_hash(const u8* data, usize size) noexcept {
    Hasher h;
    // Mesmos blocos de 64 KiB da leitura em fluxo: a chave da memória e a do
    // arquivo são a mesma.
    for (usize at = 0; at < size; at += (1u << 16)) h.add(data + at, std::min<usize>(1u << 16, size - at));
    return h.value(size);
}

bool mark_derived(const std::string& directory) noexcept try {
    const std::string marker = utf8(fs::u8path(directory) / kDerivedMarker);
    std::FILE* f = fileio::open_file(marker, "wb");
    if (!f) return false;
    const char text[] = "aurea\n";
    const bool ok = std::fwrite(text, 1, sizeof(text) - 1, f) == sizeof(text) - 1;
    return std::fclose(f) == 0 && ok;
} catch (...) {
    return false;
}

bool is_derived(const std::string& directory) noexcept try {
    std::error_code ec;
    return plain_type(fs::symlink_status(fs::u8path(directory) / kDerivedMarker, ec), false) && !ec;
} catch (...) {
    return false;
}

bool is_legacy_derived(const std::string& directory) noexcept try {
    const fs::path dir = fs::u8path(directory);
    const std::string name = utf8(dir.filename());
    const std::string parent = utf8(dir.parent_path().filename());
    const auto numbered = [&](std::string_view prefix) {
        return name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0
            && digits(std::string_view(name).substr(prefix.size()));
    };
    const bool expansion = parent == "Media" && (numbered("PSD-") || numbered("GIF-"));
    const bool frames = parent == "sequences" && utf8(dir.parent_path().parent_path().filename()) == "Media" && numbered("seq-");
    if (!expansion && !frames) return false;
    std::error_code ec;
    if (!plain_type(fs::symlink_status(dir, ec), true) || ec) return false;
    // Só arquivos comuns, como o app antigo gravava: sub-pasta ou link = não é dele.
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::file_status st = it->symlink_status(ec);
        if (ec || !plain_type(st, false)) return false;
    }
    return !ec;
} catch (...) {
    return false;
}

namespace {

bool frame_name_ok(const std::string& name) {
    return !name.empty() && name.size() <= 255 && name[0] != '.' && name.find_first_of("/\\") == std::string::npos
        && name.find('\0') == std::string::npos;
}

/// A pasta pronta tem exatamente estes quadros (marcador, nomes e tamanhos)?
bool frame_folder_ready(const fs::path& dir, const std::vector<FrameKey>& frames) {
    if (!is_derived(utf8(dir))) return false;
    std::error_code ec;
    if (!plain_type(fs::symlink_status(dir, ec), true) || ec) return false;
    usize count = 0;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (utf8(it->path().filename()) == kDerivedMarker) continue;
        if (!plain_type(it->symlink_status(ec), false) || ec) return false;
        ++count;
    }
    if (ec || count != frames.size()) return false;
    for (const FrameKey& f : frames) {
        const fs::path p = dir / fs::u8path(f.name);
        if (!plain_type(fs::symlink_status(p, ec), false) || ec) return false;
        if (fs::file_size(p, ec) != f.bytes || ec) return false;
    }
    return true;
}

/// O nome livre (ou a pasta pronta) para este conjunto. Uma pasta com o nome
/// mas outro conteúdo ou sem marcador nunca é tocada: o nome seguinte serve.
Status frame_folder_target(const fs::path& root, const std::vector<FrameKey>& frames, fs::path& target, bool& ready) {
    const std::string name = frame_folder_name(frames);
    if (name.empty()) return Status{Errc::InvalidArgument, "nomes dos quadros"};
    ready = false;
    for (u32 k = 1; k <= 16; ++k) {
        const fs::path dir = root / fs::u8path(k == 1 ? name : name + "-" + std::to_string(k));
        std::error_code ec;
        const fs::file_status st = fs::symlink_status(dir, ec);
        if (st.type() == fs::file_type::not_found) { target = dir; return OkStatus; }
        if (ec) return Status{Errc::IoError, "pasta da sequencia"};
        if (frame_folder_ready(dir, frames)) { target = dir; ready = true; return OkStatus; }
    }
    return Status{Errc::AlreadyExists, "pasta da sequencia"};
}

/// `staging` é uma `.parcial` direto em `root` (o que a plataforma recebeu do plano)?
bool staging_of(const std::string& root, const std::string& staging, fs::path& out) {
    const std::string r = canonical_file_path(root), c = canonical_file_path(staging);
    if (r.empty() || c.empty()) return false;
    const fs::path p = fs::u8path(c);
    if (utf8(p.parent_path()) != r || !staging_dir(c) || utf8(p.filename()).size() <= kStagingSuffix.size()) return false;
    std::error_code ec;
    if (!plain_type(fs::symlink_status(p, ec), true) || ec) return false;
    out = p;
    return true;
}

} // namespace

std::string frame_folder_name(const std::vector<FrameKey>& frames) {
    try {
        if (frames.empty()) return {};
        std::vector<const FrameKey*> sorted;
        sorted.reserve(frames.size());
        for (const FrameKey& f : frames) {
            if (!frame_name_ok(f.name) || f.name == kDerivedMarker) return {};
            sorted.push_back(&f);
        }
        std::sort(sorted.begin(), sorted.end(), [](const FrameKey* a, const FrameKey* b) { return a->name < b->name; });
        std::string blob;
        u64 total = 0;
        for (usize i = 0; i < sorted.size(); ++i) {
            if (i && sorted[i]->name == sorted[i - 1]->name) return {};
            blob += sorted[i]->name;
            blob.push_back('\0');
            for (const u64 v : {sorted[i]->hash, sorted[i]->bytes})
                for (u32 b = 0; b < 8; ++b) blob.push_back(static_cast<char>((v >> (8 * b)) & 0xff));
            total += sorted[i]->bytes;
        }
        char text[64];
        std::snprintf(text, sizeof(text), "seq-%016llx-%llu",
                      static_cast<unsigned long long>(content_hash(reinterpret_cast<const u8*>(blob.data()), blob.size())),
                      static_cast<unsigned long long>(total));
        return text;
    } catch (...) {
        return {};
    }
}

Status plan_frame_folder(const std::string& sequencesRoot, const std::vector<FrameKey>& frames,
                         FrameFolderPlan& out) noexcept try {
    out = FrameFolderPlan{};
    if (sequencesRoot.empty()) return Status{Errc::InvalidArgument, "pasta das sequencias"};
    const fs::path root = fs::u8path(sequencesRoot);
    std::error_code ec;
    fs::create_directories(root, ec);
    if (!plain_type(fs::symlink_status(root, ec), true) || ec) return Status{Errc::IoError, "pasta das sequencias"};
    fs::path target;
    bool ready = false;
    if (const Status s = frame_folder_target(root, frames, target, ready); !s.ok()) return s;
    out.directory = utf8(target);
    if (ready) { out.reuse = true; return OkStatus; }
    // A sobra de uma cópia interrompida deste mesmo conjunto (nossa) sai antes.
    const fs::path staging = fs::u8path(out.directory + std::string(kStagingSuffix));
    fs::remove_all(staging, ec);
    if (!fs::create_directory(staging, ec) || ec) {
        out = FrameFolderPlan{};
        return Status{Errc::IoError, "pasta da copia"};
    }
    out.staging = utf8(staging);
    return OkStatus;
} catch (const std::bad_alloc&) {
    out = FrameFolderPlan{};
    return Status{Errc::OutOfMemory};
} catch (...) {
    out = FrameFolderPlan{};
    return Status{Errc::IoError};
}

Status commit_frame_folder(const std::string& sequencesRoot, const std::string& staging, std::string& directory,
                           bool& reused) noexcept try {
    directory.clear();
    reused = false;
    fs::path dir;
    if (!staging_of(sequencesRoot, staging, dir)) return Status{Errc::InvalidArgument, "pasta da copia"};
    std::error_code ec;
    const auto fail = [&](Status s) {
        std::error_code e;
        fs::remove_all(dir, e);
        return s;
    };
    // A chave do que foi COPIADO, não a da fonte: os bytes na pasta mandam.
    std::vector<FrameKey> frames;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::file_status st = it->symlink_status(ec);
        if (ec) break;
        FrameKey f;
        f.name = utf8(it->path().filename());
        if (!plain_type(st, false) || !frame_name_ok(f.name)) return fail(Status{Errc::InvalidArgument, "copia da sequencia"});
        if (const Status s = file_content_key(utf8(it->path()), f.hash, f.bytes); !s.ok()) return fail(s);
        frames.push_back(std::move(f));
    }
    if (ec) return fail(Status{Errc::IoError, "copia da sequencia"});
    if (frames.empty()) return fail(Status{Errc::UnsupportedFormat, "nenhuma imagem na selecao"});
    fs::path target;
    bool ready = false;
    if (const Status s = frame_folder_target(dir.parent_path(), frames, target, ready); !s.ok()) return fail(s);
    if (ready) {
        (void)fail(OkStatus);
        directory = utf8(target);
        reused = true;
        return OkStatus;
    }
    // Marcador depois do último quadro conferido; só então o nome final.
    if (!mark_derived(utf8(dir))) return fail(Status{Errc::IoError, "pasta da sequencia"});
    fs::rename(dir, target, ec);
    if (ec) return fail(Status{Errc::IoError, "pasta da sequencia"});
    directory = utf8(target);
    return OkStatus;
} catch (const std::bad_alloc&) {
    return Status{Errc::OutOfMemory};
} catch (...) {
    return Status{Errc::IoError};
}

bool discard_frame_folder(const std::string& sequencesRoot, const std::string& staging) noexcept try {
    fs::path dir;
    if (!staging_of(sequencesRoot, staging, dir)) return false;
    std::error_code ec;
    fs::remove_all(dir, ec);
    return !ec;
} catch (...) {
    return false;
}

} // namespace aurea::storage
