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
#include "aurea/project/ProjectPackage.hpp"

#include <algorithm>
#include <filesystem>
#include <new>

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
    const Status s = storage::plan_release(r, [this](const std::string& stored) { return resolve_media_for_storage(stored); }, out);
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
    const Status s = storage::scan(r, [this](const std::string& stored) { return resolve_media_for_storage(stored); }, out);
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
    invalidate_prerender();
    if (!config_.cacheDirectory.empty()) preRenderCache_->sweep_orphans(config_.cacheDirectory);
    return true;
}

} // namespace aurea
