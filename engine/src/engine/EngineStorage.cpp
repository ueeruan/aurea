// =============================================================================
//  Aurea / engine / EngineStorage.cpp
//
//  Armazenamento (0.5.2): o "apagar projeto" que também libera a mídia que só
//  ele usava, a tela de armazenamento e a limpeza do PreRender. A regra mora
//  em project/MediaStorage.cpp; aqui o motor junta o que só ele sabe: como um
//  caminho gravado vira arquivo deste aparelho, o projeto carregado e se há um
//  export ou PreRender em andamento.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/media/MediaRelink.hpp"
#include "aurea/project/ProjectPackage.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <new>
#include <unordered_set>

namespace aurea {
namespace {

std::string file_name(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/// `file` é o `.aurea` principal `main` ou uma cópia dele (`main.bak`, …)?
bool family_of(const std::string& file, const std::string& main) {
    if (file == main) return true;
    return file.size() > main.size() + 1 && file.compare(0, main.size(), main) == 0 && file[main.size()] == '.'
        && storage::is_project_file(file_name(file));
}

bool family_of_any(const std::string& file, const std::vector<std::string>& mains) {
    return std::any_of(mains.begin(), mains.end(), [&](const std::string& m) { return family_of(file, m); });
}

// 1.0.8 (limpeza nunca leva mídia em uso): uma referência que não resolve para
// arquivo nenhum (caminho de outra instalação, cópia que mudou de pasta) não
// conta como uso na regra do MediaStorage — e o arquivo dela, ainda no app com
// o mesmo nome, viraria "órfão" e sairia. Os nomes dessas referências seguram
// o arquivo homônimo: a abertura do projeto o religa (MediaRelink.hpp).
std::string lower_name(const std::string& path) {
    std::string n = media_relink::file_name_of(path);
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return n;
}

void note_missing_reference(const std::string& stored, const std::string& resolved,
                            std::unordered_set<std::string>& names) {
    if (stored.empty() || stored.find("://") != std::string::npos || stored.rfind("content:", 0) == 0) return;
    const bool docsRef = stored.rfind("docs:", 0) == 0;
    if (!docsRef && !media_relink::is_local_file_path(stored) && !media_relink::is_local_file_path(resolved)) return;
    const std::string& path = resolved.empty() ? stored : resolved;
    std::error_code ec;
    if (!path.empty() && media_relink::is_local_file_path(path) && std::filesystem::exists(std::filesystem::u8path(path), ec) && !ec)
        return;
    if (std::string n = lower_name(stored); !n.empty()) names.insert(std::move(n));
}

u64 bytes_of(const std::string& path, u32& files) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p = fs::u8path(path);
    if (fs::is_regular_file(p, ec)) { ++files; return static_cast<u64>(fs::file_size(p, ec)); }
    u64 total = 0;
    for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e;
        if (it->is_regular_file(e) && !e) { ++files; total += static_cast<u64>(it->file_size(e)); }
    }
    return total;
}

} // namespace

Status Engine::media_release_plan(const MediaStorageRequest& request, storage::ReleasePlan& out) noexcept try {
    out = storage::ReleasePlan{};
    // Export/PreRender em andamento: o retrato congelado que ele lê não está em
    // arquivo nenhum. Nada sai agora (a mídia aparece depois como órfã).
    // Sem a pasta de documentos o "docs:" não resolve: uma referência relativa de
    // um projeto que fica sumiria da conta. Motor não inicializado = nada sai.
    if (config_.documentsDirectory.empty() || exportActive_.load(std::memory_order_acquire)
        || preRenderActive_.load(std::memory_order_acquire)) {
        out.blocked = true;
        return OkStatus;
    }
    std::vector<std::string> mains;
    for (const std::string& d : request.deleted)
        if (std::string c = storage::canonical_file_path(d); !c.empty()) mains.push_back(std::move(c));
    if (mains.empty()) return OkStatus;
    std::vector<std::string> files;
    if (!storage::find_project_files(request.projectsDirectory, files).ok()) {
        out.blocked = true;
        return OkStatus;
    }
    for (const std::string& extra : request.extraProjects)
        if (std::string c = storage::canonical_file_path(extra); !c.empty() && std::find(files.begin(), files.end(), c) == files.end())
            files.push_back(std::move(c));
    storage::ReleaseRequest r;
    r.managedRoots = request.managedRoots;
    r.keepRoots = request.keepRoots;
    std::vector<std::string> skip;
    for (const std::string& p : request.skipProjects)
        if (std::string c = storage::canonical_file_path(p); !c.empty()) skip.push_back(std::move(c));
    for (std::string& f : files) {
        const bool deleted = family_of_any(f, mains);
        if (family_of_any(f, skip)) {
            // Quarentena: nunca é lido. O que fica segura tudo; o apagado não libera nada.
            if (!deleted) ++out.unreadable;
            continue;
        }
        (deleted ? r.deletedProjects : r.remainingProjects).push_back(std::move(f));
    }
    if (out.unreadable) {
        out.blocked = true;
        return OkStatus;
    }
    // Apagado fora da pasta varrida: ainda é lido (as mídias dele são as candidatas).
    for (const std::string& m : mains)
        if (!family_of_any(m, skip) && std::find(r.deletedProjects.begin(), r.deletedProjects.end(), m) == r.deletedProjects.end())
            r.deletedProjects.push_back(m);
    std::vector<package::MediaRef> live;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (project_) {
            const std::string loaded = storage::canonical_file_path(project_->path());
            const bool deletedLoaded = !loaded.empty() && std::find(mains.begin(), mains.end(), loaded) != mains.end();
            if (request.editorOpen || !deletedLoaded) package::collect_media(*project_, live);
        }
    }
    for (const package::MediaRef& m : live)
        if (std::string c = storage::canonical_file_path(resolve_media_for_storage(m.stored)); !c.empty()) {
            r.live.paths.push_back(std::move(c));
            r.live.kinds.push_back(static_cast<u8>(m.kind));
        }
    std::unordered_set<std::string> missingNames;
    const Status s = storage::plan_release(r, [this, &missingNames](const std::string& stored) {
        std::string resolved = resolve_media_for_storage(stored);
        note_missing_reference(stored, resolved, missingNames);
        return resolved;
    }, out);
    if (!missingNames.empty()) {
        const auto kept = std::remove_if(out.paths.begin(), out.paths.end(), [&](const std::string& p) {
            if (missingNames.count(lower_name(p)) == 0) return false;
            u32 files = 0;
            const u64 bytes = bytes_of(p, files);
            out.bytes = out.bytes > bytes ? out.bytes - bytes : 0;
            return true;
        });
        if (kept != out.paths.end())
            AUREA_LOG_INFO("armazenamento: %zu item(ns) mantido(s): o nome bate com uma referencia que ainda nao abre",
                           static_cast<usize>(out.paths.end() - kept));
        out.paths.erase(kept, out.paths.end());
    }
    AUREA_LOG_INFO("armazenamento: apagar %zu projeto(s) libera %zu item(ns), %llu MB%s", mains.size(), out.paths.size(),
                   static_cast<unsigned long long>(out.bytes >> 20), out.blocked ? " (bloqueado: projeto ilegivel)" : "");
    return s;
} catch (const std::bad_alloc&) {
    out = storage::ReleasePlan{};
    out.blocked = true;
    return Status{Errc::OutOfMemory};
} catch (...) {
    out = storage::ReleasePlan{};
    out.blocked = true;
    return Status{Errc::IoError};
}

Status Engine::media_storage_scan(const MediaStorageRequest& request, storage::ScanResult& out) noexcept try {
    out = storage::ScanResult{};
    storage::ScanRequest r;
    r.managedRoots = request.managedRoots;
    r.keepRoots = request.keepRoots;
    r.minAgeSeconds = request.minAgeSeconds;
    r.anyFile = request.anyFile;
    const bool listed = storage::find_project_files(request.projectsDirectory, r.projects).ok();
    std::vector<std::string> skip;
    for (const std::string& p : request.skipProjects)
        if (std::string c = storage::canonical_file_path(p); !c.empty()) skip.push_back(std::move(c));
    const auto skipped = std::remove_if(r.projects.begin(), r.projects.end(), [&](const std::string& f) { return family_of_any(f, skip); });
    const u32 quarantined = static_cast<u32>(r.projects.end() - skipped);
    r.projects.erase(skipped, r.projects.end());
    for (const std::string& extra : request.extraProjects)
        if (std::string c = storage::canonical_file_path(extra); !c.empty()
            && std::find(r.projects.begin(), r.projects.end(), c) == r.projects.end()) r.projects.push_back(std::move(c));
    std::vector<package::MediaRef> live;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (project_) package::collect_media(*project_, live);
    }
    for (const package::MediaRef& m : live)
        if (std::string c = storage::canonical_file_path(resolve_media_for_storage(m.stored)); !c.empty()) {
            r.live.paths.push_back(std::move(c));
            r.live.kinds.push_back(static_cast<u8>(m.kind));
        }
    std::unordered_set<std::string> missingNames;
    const Status s = storage::scan(r, [this, &missingNames](const std::string& stored) {
        std::string resolved = resolve_media_for_storage(stored);
        note_missing_reference(stored, resolved, missingNames);
        return resolved;
    }, out);
    if (!missingNames.empty()) {
        const auto kept = std::remove_if(out.orphans.begin(), out.orphans.end(), [&](const std::string& p) {
            if (missingNames.count(lower_name(p)) == 0) return false;
            u32 files = 0;
            const u64 bytes = bytes_of(p, files);
            out.orphanBytes = out.orphanBytes > bytes ? out.orphanBytes - bytes : 0;
            out.orphanFiles = out.orphanFiles > files ? out.orphanFiles - files : 0;
            out.referencedBytes += bytes;
            out.referencedFiles += files;
            return true;
        });
        out.orphans.erase(kept, out.orphans.end());
    }
    out.unreadable += quarantined;
    // Pasta de projetos que não lista, quarentena ou export em curso: tamanhos sim, órfãos não.
    if (!listed || quarantined || config_.documentsDirectory.empty() || exportActive_.load(std::memory_order_acquire)
        || preRenderActive_.load(std::memory_order_acquire)) {
        out.referencedBytes += out.orphanBytes;
        out.referencedFiles += out.orphanFiles;
        out.orphanBytes = 0;
        out.orphanFiles = 0;
        out.orphans.clear();
        out.blocked = true;
    }
    return s;
} catch (const std::bad_alloc&) {
    out = storage::ScanResult{};
    out.blocked = true;
    return Status{Errc::OutOfMemory};
} catch (...) {
    out = storage::ScanResult{};
    out.blocked = true;
    return Status{Errc::IoError};
}

storage::RemoveResult Engine::remove_managed_media(const std::vector<std::string>& paths,
                                                   const std::vector<std::string>& managedRoots,
                                                   const std::vector<std::string>& keepRoots) noexcept {
    const storage::RemoveResult r = storage::remove_managed(paths, managedRoots, keepRoots);
    AUREA_LOG_INFO("armazenamento: %u arquivo(s) apagado(s), %llu MB; %u recusado(s) fora das pastas do app",
                   r.files, static_cast<unsigned long long>(r.bytes >> 20), r.refused);
    return r;
}

bool Engine::clear_prerender_cache() noexcept {
    // O PreRender grava pelo caminho do export: com ele andando, nada é mexido.
    if (exportActive_.load(std::memory_order_acquire)) return false;
    poll_preexport();
    // 1.0.2: o pré-export guardado sai junto (a mesma linha da tela).
    {
        std::lock_guard state(preRenderStateMutex_);
        if (preExport_.running) return false;
        preExport_ = PreExportState{};
    }
    invalidate_prerender();
    if (!config_.cacheDirectory.empty()) {
        preRenderCache_->sweep_orphans(config_.cacheDirectory);
        discard_preexport_file({});
    }
    return true;
}

} // namespace aurea
