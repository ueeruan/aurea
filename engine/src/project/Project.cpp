#include "aurea/project/Project.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"

#include <cstdio>

namespace aurea {
namespace {

/// O arquivo existe E tem conteúdo?
///
/// As duas condições juntas, num teste só: um journal de zero byte é
/// indistinguível de um ausente pela leitura, e sem esta checagem o usuário
/// veria o aviso de "sessão anterior não foi salva" por causa de um arquivo
/// vazio deixado por uma gravação que falhou antes de escrever.
bool file_has_content(const std::string& path) noexcept {
    if (path.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fclose(f);
    return size > 0;
}

} // namespace

Result<Project> Project::create_new(u32 width, u32 height, f64 fps,
                                    std::string title) {
    Project p;
    p.metadata_.title = std::move(title);
    p.metadata_.createdUnixMs = wall_clock_ms();
    p.metadata_.modifiedUnixMs = p.metadata_.createdUnixMs;
    p.metadata_.appVersionMajor = AUREA_VERSION_MAJOR;
    p.metadata_.appVersionMinor = AUREA_VERSION_MINOR;
    p.metadata_.appVersionPatch = AUREA_VERSION_PATCH;

    const CompositionId root = p.timeline_.create_composition("Composicao principal",
                                                              width, height, fps);
    if (!root.valid()) {
        return Status{Errc::OutOfMemory, "nao foi possivel criar a composicao inicial"};
    }
    p.timeline_.set_root(root);
    p.timeline_.set_current(root);
    p.export_.width = width;
    p.export_.height = height;
    p.export_.fps = fps;

    return p;
}

AssetId Project::add_asset(Asset asset) {
    // Deduplicação por hash de conteúdo: importar o mesmo vídeo duas vezes não
    // deve ocupar duas vezes o cache de decoders nem duplicar o proxy em disco.
    if (asset.contentHash != 0) {
        if (const AssetId existing = find_asset_by_hash(asset.contentHash); existing.valid()) {
            return existing;
        }
    }
    const AssetId id = assets_.create(std::move(asset));
    mark_dirty();
    return id;
}

bool Project::remove_asset(AssetId id) noexcept {
    if (!assets_.contains(id)) return false;
    (void)assets_.destroy(id);
    mark_dirty();
    return true;
}

AssetId Project::find_asset_by_hash(u64 contentHash) const noexcept {
    if (contentHash == 0) return AssetId{};
    AssetId found{};
    assets_.for_each([&](AssetId id, const Asset& a) {
        if (!found.valid() && a.contentHash == contentHash) found = id;
    });
    return found;
}

std::vector<AssetId> Project::unreferenced_assets() const {
    std::vector<AssetId> used;
    used.reserve(64);

    timeline_.for_each_composition([&](CompositionId, const Composition& comp) {
        comp.layers().for_each([&](LayerId, const Layer& l) {
            if (l.source.valid()) used.push_back(l.source);
            if (l.nested.composition.valid()) { /* é composição, não asset */ }
            if (l.model.scene.valid()) used.push_back(l.model.scene);
            if (l.text.font.valid()) { /* fonte é asset também */ }
        });
        if (comp.environment().hdri.valid()) used.push_back(comp.environment().hdri);
    });

    std::vector<AssetId> result;
    assets_.for_each([&](AssetId id, const Asset&) {
        for (AssetId u : used) {
            if (u == id) return;
        }
        result.push_back(id);
    });
    return result;
}

void Project::configure_autosave(std::string journalPath, std::string recoveryPath,
                                 u32 journalIntervalMs, u32 recoveryIntervalMs,
                                 u32 recoveryCommandInterval) noexcept {
    autosave_.journalPath = std::move(journalPath);
    autosave_.recoveryPath = std::move(recoveryPath);
    (void)journalIntervalMs;
    (void)recoveryIntervalMs;
    (void)recoveryCommandInterval;
    // As cadências ficam registradas aqui e são aplicadas pelo Engine, que é
    // quem tem o relógio e o JobSystem. O Project não tem thread própria — não
    // é papel dele decidir quando escrever.
}

bool Project::check_recovery() noexcept {
    autosave_.recoveryCommandCount = 0;

    if (!file_has_content(autosave_.journalPath)) {
        autosave_.recoveryAvailable = false;
        return false;
    }

    // Conta os comandos do journal para a UI poder dizer "recuperar 412 ações
    // da sessão anterior" em vez de um aviso vago.
    std::vector<Command> cmds;
    if (const Status s = ProjectSerializer::read_journal(autosave_.journalPath, cmds); !s.ok()) {
        AUREA_LOG_WARN("journal de recuperacao ilegivel: %s", s.message().data());
        autosave_.recoveryAvailable = false;
        return false;
    }

    autosave_.recoveryCommandCount = static_cast<u32>(cmds.size());
    autosave_.recoveryAvailable = !cmds.empty();
    return autosave_.recoveryAvailable;
}

Status Project::apply_recovery() noexcept {
    if (!autosave_.recoveryAvailable) return Errc::NotFound;

    std::vector<Command> cmds;
    const Status s = ProjectSerializer::read_journal(autosave_.journalPath, cmds);
    if (!s.ok()) return s;

    // O journal é reaplicado pelo Engine (que é quem sabe aplicar comandos).
    // Aqui só ficam guardados os comandos e o Project é marcado como sujo, para
    // que o usuário salve de verdade depois.
    recoveredCommands_ = std::move(cmds);
    autosave_.dirty = true;
    return OkStatus;
}

void Project::discard_recovery() noexcept {
    autosave_.recoveryAvailable = false;
    autosave_.recoveryCommandCount = 0;
    recoveredCommands_.clear();

    if (!autosave_.journalPath.empty()) {
        std::remove(autosave_.journalPath.c_str());
    }
}

Status Project::load(const std::string& path) {
    Project loaded;
    LoadReport report;
    std::string error;

    LoadOptions options;
    options.lazyAssets = true;

    const Status s = ProjectSerializer::load(loaded, path, options, &report, &error);
    if (!s.ok()) {
        AUREA_LOG_ERROR("falha ao abrir '%s': %s", path.c_str(), error.c_str());
        return Status{s.code(), "falha ao abrir o projeto"};
    }

    *this = std::move(loaded);
    path_ = path;

    if (!report.clean()) {
        // A abertura foi degradada. Reportar sem esconder: o usuário precisa
        // saber que algo não veio, senão ele descobre quando exportar.
        AUREA_LOG_WARN("projeto aberto com %llu secoes corrompidas e %llu migradas",
                       static_cast<unsigned long long>(report.sectionsCorrupt.size()),
                       static_cast<unsigned long long>(report.sectionsMigrated.size()));
        return Status{Errc::CorruptData, "projeto aberto parcialmente"};
    }
    return OkStatus;
}

Status Project::save(const std::string& path) {
    SaveOptions options;
    options.compress = true;
    options.incremental = false;
    options.writeThumbnail = true;
    options.fsyncOnComplete = true;

    std::string error;
    const Status s = ProjectSerializer::save(*this, path, options, &error);
    if (!s.ok()) {
        AUREA_LOG_ERROR("falha ao salvar '%s': %s", path.c_str(), error.c_str());
        return s;
    }

    path_ = path;
    metadata_.modifiedUnixMs = wall_clock_ms();
    mark_clean();
    return OkStatus;
}

} // namespace aurea
