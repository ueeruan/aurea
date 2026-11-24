// =============================================================================
//  Aurea / media / MediaRelink.cpp — ver MediaRelink.hpp.
// =============================================================================
#include "aurea/media/MediaRelink.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <new>
#include <string_view>

namespace aurea::media_relink {
namespace {

namespace fs = std::filesystem;

std::string slashes(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string utf8(const fs::path& p) {
    const auto u = p.u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

bool exists_any(const std::string& path) {
    std::error_code ec;
    return fs::exists(fs::u8path(path), ec) && !ec;
}

bool regular(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::u8path(path), ec) && !ec;
}

/// Segmentos de pasta que nunca guardam mídia de projeto durável.
bool skipped_folder(const std::string& name) {
    static constexpr std::string_view kSkip[] = {
        "tmp", "caches", "cache", "code_cache", "export-sources", "preview-proxies", "prerender",
        ".trash", "snapshots", "splashboard", "no_backup", "shared_prefs", "databases", "app_webview",
        // Quadros de sequência e miniaturas: milhares de arquivos que nunca são a
        // mídia de uma camada (a sequência religa pela receita, não por arquivo).
        "sequences", "thumbs", "thumbnails",
    };
    const std::string n = lower(name);
    if (!n.empty() && n.front() == '.') return true;
    for (std::string_view s : kSkip) if (n == s) return true;
    // Cópia em andamento (EngineImageSequence: `.parcial`).
    return n.size() > 8 && n.compare(n.size() - 8, 8, ".parcial") == 0;
}

bool skipped_file(const std::string& name) {
    const std::string n = lower(name);
    return n.empty() || n.front() == '.' || (n.size() > 4 && n.compare(n.size() - 4, 4, ".tmp") == 0);
}

/// iOS: `…/Data/Application/<UUID>` (a raiz) e o resto (`/…`) de `path`.
bool split_ios(const std::string& path, std::string& root, std::string& rest) {
    constexpr std::string_view kApp = "/Data/Application/";
    const usize at = path.find(kApp);
    if (at == std::string::npos) return false;
    const usize uuid = at + kApp.size();
    const usize end = path.find('/', uuid);
    if (end == uuid) return false;
    root = path.substr(0, end == std::string::npos ? path.size() : end);
    rest = end == std::string::npos ? std::string{} : path.substr(end);
    return true;
}

/// Android: `/data/user/<n>/<pacote>` ou `/data/data/<pacote>`, o pacote e o resto.
bool split_android(const std::string& path, std::string& root, std::string& package, std::string& rest) {
    usize start = 0;
    if (path.rfind("/data/user/", 0) == 0) {
        const usize user = path.find('/', 11);
        if (user == std::string::npos || user == 11) return false;
        for (usize i = 11; i < user; ++i) if (!std::isdigit(static_cast<unsigned char>(path[i]))) return false;
        start = user + 1;
    } else if (path.rfind("/data/data/", 0) == 0) {
        start = 11;
    } else {
        return false;
    }
    const usize end = path.find('/', start);
    package = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (package.empty() || package.find('.') == std::string::npos) return false;
    root = path.substr(0, end == std::string::npos ? path.size() : end);
    rest = end == std::string::npos ? std::string{} : path.substr(end);
    return true;
}

} // namespace

bool is_local_file_path(const std::string& stored) noexcept {
    if (stored.empty() || stored.find("://") != std::string::npos) return false;
    if (stored.rfind("docs:", 0) == 0 || stored.rfind("content:", 0) == 0 || stored.rfind("file:", 0) == 0) return false;
    if (stored[0] == '/') return true;
    if (stored.size() >= 2 && stored[0] == '\\' && stored[1] == '\\') return true;
    return stored.size() >= 3 && std::isalpha(static_cast<unsigned char>(stored[0])) && stored[1] == ':'
        && (stored[2] == '/' || stored[2] == '\\');
}

std::string file_name_of(const std::string& path) noexcept {
    const usize slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (name.rfind("docs:", 0) == 0) name = name.substr(5);
    return name;
}

std::string app_container_of(const std::string& documents) noexcept try {
    const std::string docs = slashes(documents);
    std::string root, rest, package;
    if (split_ios(docs, root, rest)) return root;
    if (split_android(docs, root, package, rest)) return root;
    return {};
} catch (...) {
    return {};
}

std::string rebase_into_container(const std::string& stored, const std::string& documents) noexcept try {
    if (!is_local_file_path(stored) || documents.empty()) return {};
    const std::string path = slashes(stored);
    const std::string docs = slashes(documents);
    if (path.find("/../") != std::string::npos || path.find("/./") != std::string::npos) return {};
    std::string oldRoot, rest, oldPackage;
    std::string nowRoot, nowRest, nowPackage;
    if (split_ios(path, oldRoot, rest)) {
        if (!split_ios(docs, nowRoot, nowRest)) return {};
    } else if (split_android(path, oldRoot, oldPackage, rest)) {
        // Só o MESMO pacote: outro app (ou outra variante) nunca é lido.
        if (!split_android(docs, nowRoot, nowPackage, nowRest) || nowPackage != oldPackage) return {};
    } else {
        return {};
    }
    if (rest.size() < 2 || oldRoot == nowRoot) return {};
    if (exists_any(stored)) return {};
    const std::string candidate = nowRoot + rest;
    if (!regular(candidate)) return {};
    return candidate;
} catch (...) {
    return {};
}

std::vector<std::string> search_roots(const std::string& documents) noexcept try {
    std::vector<std::string> roots;
    if (documents.empty()) return roots;
    roots.push_back(documents);
    const fs::path docs = fs::u8path(documents).lexically_normal();
    const std::string name = utf8(docs.filename());
    const fs::path parent = docs.parent_path();
    if (name == "projetos") {
        // Android: files/projetos; o Aurea AI e o Hyper guardam ao lado.
        for (const char* sibling : {"aurea-ai", "hyper"}) roots.push_back(utf8(parent / sibling));
    } else if (name == "Documents") {
        // iOS: Library/Application Support (fontes do Hyper, cópias antigas).
        roots.push_back(utf8(parent / "Library" / "Application Support"));
    }
    return roots;
} catch (...) {
    return {};
}

void NameIndex::build(const std::vector<std::string>& roots, u32 maxEntries, u32 maxDepth) noexcept {
    byName_.clear();
    files_ = 0;
    try {
        u32 visited = 0;
        for (const std::string& rootText : roots) {
            std::error_code ec;
            const fs::path root = fs::u8path(rootText);
            if (rootText.empty() || !fs::is_directory(root, ec) || ec) continue;
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
            for (const fs::recursive_directory_iterator end{}; !ec && it != end; it.increment(ec)) {
                if (++visited > maxEntries) return;
                const fs::directory_entry& entry = *it;
                const std::string name = utf8(entry.path().filename());
                std::error_code typeError;
                const auto status = entry.symlink_status(typeError);
                if (typeError || fs::is_symlink(status)) {
                    if (fs::is_directory(status)) it.disable_recursion_pending();
                    continue;
                }
                if (fs::is_directory(status)) {
                    if (skipped_folder(name) || static_cast<u32>(it.depth()) + 1 >= maxDepth) it.disable_recursion_pending();
                    continue;
                }
                if (!fs::is_regular_file(status) || skipped_file(name)) continue;
                std::error_code sizeError;
                const u64 bytes = static_cast<u64>(entry.file_size(sizeError));
                byName_[lower(name)].push_back(Entry{utf8(entry.path().lexically_normal()), sizeError ? 0 : bytes});
                ++files_;
            }
        }
    } catch (...) {
        // Índice parcial: o que já entrou continua valendo (achar menos, nunca errado).
    }
}

std::string NameIndex::find(const std::vector<std::string>& names, u64 bytes) const noexcept try {
    std::vector<const Entry*> found;
    for (const std::string& raw : names) {
        if (raw.empty()) continue;
        const auto hit = byName_.find(lower(raw));
        if (hit == byName_.end()) continue;
        for (const Entry& e : hit->second) {
            if (bytes > 0 && e.bytes != bytes) continue;
            if (std::none_of(found.begin(), found.end(), [&](const Entry* f) { return f->path == e.path; })) found.push_back(&e);
        }
        // O nome do arquivo gravado vale mais que o nome original: achou, para.
        if (!found.empty()) break;
    }
    if (found.empty()) return {};
    std::sort(found.begin(), found.end(), [](const Entry* a, const Entry* b) { return a->path < b->path; });
    if (bytes == 0) {
        // Sem tamanho no projeto: dois arquivos de tamanhos diferentes com o
        // mesmo nome são mídias diferentes. Não escolhe.
        const u64 first = found.front()->bytes;
        if (std::any_of(found.begin(), found.end(), [&](const Entry* e) { return e->bytes != first; })) return {};
    }
    return found.front()->path;
} catch (...) {
    return {};
}

std::string relocate(const std::string& stored, const std::string& documents, u64 bytes,
                     const std::string& originalName, const NameIndex* index) noexcept try {
    if (std::string moved = rebase_into_container(stored, documents); !moved.empty()) return moved;
    if (!index || index->empty()) return {};
    if (!is_local_file_path(stored) && stored.rfind("docs:", 0) != 0) return {};
    std::vector<std::string> names{file_name_of(stored)};
    // O nome original só ajuda se tiver extensão (o nome "Imagem" da camada não é um arquivo).
    if (const std::string original = file_name_of(originalName); original.find('.') != std::string::npos && original != names.front())
        names.push_back(original);
    std::string found = index->find(names, bytes);
    if (found.empty() || slashes(found) == slashes(stored)) return {};
    return found;
} catch (...) {
    return {};
}

} // namespace aurea::media_relink
