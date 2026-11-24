// =============================================================================
//  1.0.8 — "A mídia original não está mais no aparelho" (erro 18).
//
//  media/MediaRelink + Engine::load_project + EngineStorage: antes de qualquer
//  aviso, a mídia que não abre é procurada no contêiner atual (qualquer pasta
//  do app, não só Documents) e pelo nome/tamanho nas pastas do app; o caminho
//  novo fica gravado. A limpeza nunca trata como órfão o arquivo que um
//  projeto ainda referencia por um caminho antigo. E o crash nativo no export
//  (driver GLES na Surface do encoder, 1.0.7.1) sai do caminho da Surface.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/export/ExportAttempts.hpp"
#include "aurea/media/MediaRelink.hpp"
#include "aurea/project/MediaStorage.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/project/Serialization.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace aurea;

namespace {

namespace fs = std::filesystem;

std::string s8(const fs::path& p) {
    const auto u = p.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "aurea_relink_teste" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return fs::weakly_canonical(dir, ec);
}

void write_file(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f << text;
}

void age(const fs::path& p) {
    std::error_code ec;
    fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::hours(72), ec);
}

bool same_file(const std::string& a, const fs::path& b) {
    std::error_code ec;
    return !a.empty() && fs::equivalent(fs::u8path(a), b, ec) && !ec;
}

struct Media { AssetKind kind; std::string stored; u64 bytes = 0; std::string original; };

void save_project(const fs::path& file, const std::vector<Media>& media) {
    auto created = Project::create_new(64, 36, 30.0, "religar");
    AUREA_CHECK(created.ok());
    if (!created.ok()) return;
    Project p = std::move(*created);
    for (const Media& m : media) {
        Asset a;
        a.kind = m.kind;
        a.name = "midia";
        a.sourcePath = m.stored;
        a.fileSizeBytes = m.bytes;
        a.originalFilename = m.original;
        (void)p.add_asset(std::move(a));
    }
    SaveOptions o;
    o.keepBackup = false;
    o.fsyncOnComplete = false;
    o.writeThumbnail = false;
    AUREA_CHECK(ProjectSerializer::save(p, s8(file), o).ok());
}

struct Rig {
    test::SyntheticConfig cfg;
    test::SyntheticFactory factory{cfg};
    Engine e;
    fs::path docs;
    explicit Rig(const fs::path& documents) : docs(documents) {
        EngineConfig ec;
        ec.workerCount = 2;
        ec.memoryBudgetBytes = 64ull << 20;
        ec.disableAutosave = true;
        ec.mediaFactory = &factory;
        ec.documentsDirectory = s8(documents);
        AUREA_CHECK(e.initialize(ec).ok());
    }
    ~Rig() { e.shutdown(); }
    Engine::MediaStorageRequest request(std::vector<fs::path> deleted = {}) const {
        Engine::MediaStorageRequest r;
        r.projectsDirectory = s8(docs);
        for (const fs::path& d : deleted) r.deleted.push_back(s8(d));
        r.managedRoots = {s8(docs / "import-sources"), s8(docs / "Media")};
        return r;
    }
};

std::vector<std::string> asset_paths(Engine& e) {
    std::vector<std::string> out;
    if (!e.project()) return out;
    e.project()->for_each_asset([&](AssetId, const Asset& a) { out.push_back(a.sourcePath); });
    return out;
}

bool listed(const std::vector<std::string>& paths, const fs::path& p) {
    return std::any_of(paths.begin(), paths.end(), [&](const std::string& x) { return same_file(x, p); });
}

} // namespace

AUREA_TEST(MediaRelink, ContainerRootsAreRecognizedOnBothPlatforms) {
    AUREA_CHECK_EQ(media_relink::app_container_of(
        "/var/mobile/Containers/Data/Application/AAAA-1111/Documents"),
        std::string("/var/mobile/Containers/Data/Application/AAAA-1111"));
    AUREA_CHECK_EQ(media_relink::app_container_of("/data/user/0/com.aurea.editor/files/projetos"),
                   std::string("/data/user/0/com.aurea.editor"));
    AUREA_CHECK_EQ(media_relink::app_container_of("/data/data/com.aurea.editor/files/projetos"),
                   std::string("/data/data/com.aurea.editor"));
    AUREA_CHECK(media_relink::app_container_of("/home/qualquer/pasta").empty());
    AUREA_CHECK(media_relink::is_local_file_path("/data/user/0/x/files/a.mp4"));
    AUREA_CHECK(media_relink::is_local_file_path("C:/Users/x/a.mp4"));
    AUREA_CHECK(!media_relink::is_local_file_path("content://media/external/video/1"));
    AUREA_CHECK(!media_relink::is_local_file_path("docs:Media/a.mp4"));
    AUREA_CHECK(!media_relink::is_local_file_path("relativo.mp4"));
}

// iOS: a atualização muda o UUID do contêiner. Mídia fora de Documents (as
// fontes do Hyper em Library/Application Support) também volta.
AUREA_TEST(MediaRelink, AnyFolderOfThePreviousIosContainerIsRebased) {
    const fs::path root = scratch("ios");
    const fs::path docs = root / "Containers" / "Data" / "Application" / "NOVO-2222" / "Documents";
    const fs::path file = root / "Containers" / "Data" / "Application" / "NOVO-2222" / "Library" / "Application Support"
                          / "hyper" / "fontes" / "clip.mov";
    write_file(file, "video");
    std::error_code ec;
    fs::create_directories(docs, ec);
    const std::string stored =
        "/private/var/mobile/Containers/Data/Application/VELHO-1111/Library/Application Support/hyper/fontes/clip.mov";
    AUREA_CHECK(same_file(media_relink::rebase_into_container(stored, s8(docs)), file));
    // Sem o arquivo no contêiner atual: nada muda (continua ausente, nunca outro arquivo).
    AUREA_CHECK(media_relink::rebase_into_container(
        "/private/var/mobile/Containers/Data/Application/VELHO-1111/Library/x/nao-existe.mov", s8(docs)).empty());
    // O gravado ainda existe: fica como está.
    AUREA_CHECK(media_relink::rebase_into_container(s8(file), s8(docs)).empty());
    // Android: outro pacote nunca é lido.
    AUREA_CHECK(media_relink::rebase_into_container("/data/user/0/com.outro.app/files/projetos/a.mp4",
                                                    "/data/user/0/com.aurea.editor/files/projetos").empty());
}

AUREA_TEST(MediaRelink, TheNameIndexFindsOneCopyAndRefusesAGuess) {
    const fs::path docs = scratch("indice");
    write_file(docs / "import-sources" / "a1b2c3-11.mp4", "onze bytes!");
    write_file(docs / "Media" / "IMG_0001.JPG", "foto um");
    write_file(docs / "Media" / "antigo" / "IMG_0001.JPG", "foto dois maior");
    write_file(docs / "tmp" / "temporario.mov", "fica de fora");
    write_file(docs / "Media" / "copy-99.tmp", "parcial");
    media_relink::NameIndex index;
    index.build(media_relink::search_roots(s8(docs)));
    // Nome + tamanho: acha.
    AUREA_CHECK(same_file(index.find({"a1b2c3-11.mp4"}, 11), docs / "import-sources" / "a1b2c3-11.mp4"));
    // Tamanho diferente: não é a mesma mídia.
    AUREA_CHECK(index.find({"a1b2c3-11.mp4"}, 12).empty());
    // Mesmo nome, conteúdos diferentes, sem tamanho no projeto: não escolhe.
    AUREA_CHECK(index.find({"img_0001.jpg"}, 0).empty());
    // Com o tamanho, escolhe o certo.
    AUREA_CHECK(same_file(index.find({"IMG_0001.JPG"}, 15), docs / "Media" / "antigo" / "IMG_0001.JPG"));
    // Temporários e cópias parciais nunca entram.
    AUREA_CHECK(index.find({"temporario.mov"}, 0).empty());
    AUREA_CHECK(index.find({"copy-99.tmp"}, 0).empty());
}

// A mídia que mudou de lugar dentro do app volta sozinha na abertura, e o
// caminho novo fica gravado (o projeto sai sujo para o autosave).
AUREA_TEST(MediaRelink, OpeningAProjectRelinksMovedMediaBeforeAnyWarning) {
    const fs::path docs = scratch("abrir");
    const fs::path moved = docs / "import-sources" / "f00d-11.mp4";
    write_file(moved, "onze bytes!");
    const fs::path photo = docs / "Media" / "ferias.jpg";
    write_file(photo, "foto");
    const fs::path project = docs / "Religar.aurea";
    save_project(project, {
        {AssetKind::Video, "/sumiu/pasta/antiga/f00d-11.mp4", 11, "Video da galeria.mp4"},
        {AssetKind::Image, "/outra/pasta/IMG_9.jpg", 0, "ferias.jpg"},
        {AssetKind::Video, "/sumiu/de/vez/nada-igual.mp4", 99, ""},
        {AssetKind::Video, "content://media/external/video/media/42", 0, ""},
    });
    Rig rig(docs);
    AUREA_CHECK(rig.e.load_project(s8(project).c_str()).ok());
    const auto paths = asset_paths(rig.e);
    AUREA_CHECK(listed(paths, moved));
    AUREA_CHECK(listed(paths, photo));   // pelo nome original da importação
    AUREA_CHECK(std::find(paths.begin(), paths.end(), "/sumiu/de/vez/nada-igual.mp4") != paths.end());
    AUREA_CHECK(std::find(paths.begin(), paths.end(), "content://media/external/video/media/42") != paths.end());
    AUREA_CHECK(rig.e.project() && rig.e.project()->dirty());
    // Gravado e reaberto: já aponta para o arquivo de agora.
    AUREA_CHECK(rig.e.save_project(s8(project).c_str()).ok());
    AUREA_CHECK(rig.e.load_project(s8(project).c_str()).ok());
    AUREA_CHECK(listed(asset_paths(rig.e), moved));
}

// Limpeza: o arquivo que um projeto ainda referencia por um caminho antigo
// (outra instalação, outra pasta) nunca é "órfão" — nem no scan, nem no apagar.
AUREA_TEST(MediaStorage, MediaReferencedByAStalePathIsNeverAnOrphan) {
    const fs::path docs = scratch("limpeza");
    const fs::path kept = docs / "import-sources" / "beef-5.mp4";
    const fs::path orphan = docs / "import-sources" / "lixo-3.mp4";
    write_file(kept, "cinco");
    write_file(orphan, "tres");
    age(kept);
    age(orphan);
    save_project(docs / "A.aurea", {{AssetKind::Video, "/instalacao/antiga/import-sources/beef-5.mp4", 5, ""}});
    save_project(docs / "B.aurea", {});
    Rig rig(docs);
    storage::ScanResult scan;
    AUREA_CHECK(rig.e.media_storage_scan(rig.request(), scan).ok());
    AUREA_CHECK(!scan.blocked);
    AUREA_CHECK(listed(scan.orphans, orphan));
    AUREA_CHECK(!listed(scan.orphans, kept));
    AUREA_CHECK_EQ(scan.orphanFiles, 1u);
    // Apagar o projeto que NÃO usa a mídia não a leva.
    storage::ReleasePlan plan;
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "B.aurea"}), plan).ok());
    AUREA_CHECK(!listed(plan.paths, kept));
    AUREA_CHECK(fs::exists(kept));
}

// 1.0.7.1 (POCO X6 Pro, Mali): o driver GLES caía no glTexSubImage2D da
// Surface do encoder. Depois de um crash nativo no export, a escada vai para o
// caminho sem a Surface GLES (mesmo perfil e taxa), em vez de repetir o degrau.
AUREA_TEST(ExportAttempts, ANativeCrashInTheEncoderSurfaceMovesToTheBufferRoute) {
    const ExportRequestShape shape{1080, 30.0, false, false};
    const u32 next = export_next_attempt(0, export_retry_kind(static_cast<i32>(Errc::EncodeFailed),
        static_cast<u32>(ExportFailure::Encoder), false, false), shape);
    AUREA_CHECK(next >= 1 && next <= kExportAttemptMax);
    AUREA_CHECK_EQ(export_attempt_plan(next).safeMode, 1u);
    AUREA_CHECK_EQ(export_attempt_plan(next).shortSideCap, 0u);   // a qualidade pedida fica
}
