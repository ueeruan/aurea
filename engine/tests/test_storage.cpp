// =============================================================================
//  Armazenamento (0.5.2): project/MediaStorage.cpp + engine/EngineStorage.cpp.
//
//  Apagar projeto libera SÓ a mídia que nenhum outro arquivo restaurável usa
//  (.aurea, .bak, cópia duplicada, projeto aberto); projeto ilegível = nada
//  sai; fora das pastas do app nunca sai; pasta derivada sai inteira ou não
//  sai; mídia órfã antiga só aparece no scan. PreRender de sessão morta sai na
//  abertura do motor (a viva fica). O mesmo .zip de sequência, o mesmo GIF e o
//  mesmo PSD importados de novo reaproveitam a primeira extração.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/export/ExportCheckpoint.hpp"
#include "aurea/export/ImageEncode.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/MediaStorage.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/render/PreRenderCache.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace aurea;

namespace {

namespace fs = std::filesystem;

std::string s8(const fs::path& p) {
    const auto u = p.generic_u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "aurea_armazenamento_teste" / name;
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

/// Mídia "antiga" (fora da janela de 24 h de importação recente).
void age(const fs::path& p) {
    std::error_code ec;
    fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::hours(72), ec);
}

/// Um `.aurea` de verdade com um asset por caminho gravado (como o app grava).
void save_project(const fs::path& file, const std::vector<std::pair<AssetKind, std::string>>& media) {
    auto created = Project::create_new(64, 36, 30.0, "armazenamento");
    AUREA_CHECK(created.ok());
    if (!created.ok()) return;
    Project p = std::move(*created);
    for (const auto& [kind, stored] : media) {
        Asset a;
        a.kind = kind;
        a.name = "midia";
        a.sourcePath = stored;
        (void)p.add_asset(std::move(a));
    }
    SaveOptions o;
    o.keepBackup = false;
    o.fsyncOnComplete = false;
    o.writeThumbnail = false;
    AUREA_CHECK(ProjectSerializer::save(p, s8(file), o).ok());
}

bool listed(const std::vector<std::string>& paths, const fs::path& p) {
    std::error_code ec;
    const std::string want = s8(fs::weakly_canonical(p, ec));
    return std::find(paths.begin(), paths.end(), want) != paths.end();
}

/// Motor sem GPU com a pasta de documentos do teste (o "docs:" resolve nela).
struct Rig {
    test::SyntheticConfig cfg;
    test::SyntheticFactory factory{cfg};
    Engine e;
    fs::path docs;
    explicit Rig(const fs::path& documents, const fs::path& cache = {}) : docs(documents) {
        EngineConfig ec;
        ec.workerCount = 2;
        ec.memoryBudgetBytes = 64ull << 20;
        ec.disableAutosave = true;
        ec.mediaFactory = &factory;
        ec.documentsDirectory = s8(documents);
        if (!cache.empty()) ec.cacheDirectory = s8(cache);
        AUREA_CHECK(e.initialize(ec).ok());
    }
    ~Rig() { e.shutdown(); }
    Engine::MediaStorageRequest request(std::vector<fs::path> deleted = {}) const {
        Engine::MediaStorageRequest r;
        r.projectsDirectory = s8(docs);
        for (const fs::path& d : deleted) r.deleted.push_back(s8(d));
        r.managedRoots = {s8(docs / "import-sources"), s8(docs / "Media")};
        r.keepRoots = {s8(docs / "Media" / "Modelos")};
        return r;
    }
};

/// O mesmo marcador de dono que o PreRenderCache grava (owner.bin, versão 1).
void owner_marker(const fs::path& directory) {
    std::array<u8, 32> bytes{};
    std::memcpy(bytes.data(), "AURPRO01", 8);
    bytes[8] = 1;
    const u32 crc = crc32_update(0, bytes.data(), 28);
    for (u32 i = 0; i < 4; ++i) bytes[28 + i] = static_cast<u8>(crc >> (8 * i));
    std::ofstream file(directory / "owner.bin", std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace

AUREA_TEST(MediaStorage, MediaOnlyInTheDeletedProjectIsReleasedAndSharedMediaStays) {
    const fs::path docs = scratch("so-do-apagado");
    const fs::path only = docs / "import-sources" / "so-meu.mp4";
    const fs::path shared = docs / "import-sources" / "dividido.mp4";
    const fs::path photo = docs / "Media" / "foto.jpg";
    for (const fs::path& f : {only, shared, photo}) write_file(f, "midia " + s8(f));
    // O apagado guarda relativo ("docs:"); o outro guarda o absoluto do mesmo arquivo.
    save_project(docs / "Apagado.aurea", {{AssetKind::Video, "docs:import-sources/so-meu.mp4"},
                                          {AssetKind::Video, "docs:import-sources/dividido.mp4"},
                                          {AssetKind::Image, "docs:Media/foto.jpg"}});
    save_project(docs / "Fica.aurea", {{AssetKind::Video, s8(shared)}, {AssetKind::Image, s8(photo)}});
    Rig rig(docs);
    storage::ReleasePlan plan;
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Apagado.aurea"}), plan).ok());
    AUREA_CHECK(!plan.blocked);
    AUREA_CHECK_EQ(plan.paths.size(), usize{1});
    AUREA_CHECK(listed(plan.paths, only));
    AUREA_CHECK(!listed(plan.paths, shared));
    AUREA_CHECK(plan.bytes > 0);
    const auto removed = rig.e.remove_managed_media(plan.paths, rig.request().managedRoots, rig.request().keepRoots);
    AUREA_CHECK_EQ(removed.files, 1u);
    AUREA_CHECK(!fs::exists(only));
    AUREA_CHECK(fs::exists(shared));
    AUREA_CHECK(fs::exists(photo));
    // A raiz de mídia nunca sai, nem vazia.
    AUREA_CHECK(fs::is_directory(docs / "import-sources"));
}

AUREA_TEST(MediaStorage, AnUnreadableRemainingProjectReleasesNothing) {
    const fs::path docs = scratch("ilegivel");
    const fs::path only = docs / "import-sources" / "so-meu.mp4";
    write_file(only, "video");
    save_project(docs / "Apagado.aurea", {{AssetKind::Video, "docs:import-sources/so-meu.mp4"}});
    save_project(docs / "Bom.aurea", {});
    write_file(docs / "Quebrado.aurea", "isto nao e um projeto do Aurea");
    Rig rig(docs);
    storage::ReleasePlan plan;
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Apagado.aurea"}), plan).ok());
    AUREA_CHECK(plan.blocked);
    AUREA_CHECK_EQ(plan.unreadable, 1u);
    AUREA_CHECK(plan.paths.empty());
    // A cópia restaurável ilegível também segura tudo (o .tmp de uma gravação cortada).
    fs::remove(docs / "Quebrado.aurea");
    write_file(docs / "Bom.aurea.tmp", "metade de uma gravacao");
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Apagado.aurea"}), plan).ok());
    AUREA_CHECK(plan.blocked);
    AUREA_CHECK(plan.paths.empty());
    // O sidecar da Home não é projeto: não bloqueia.
    fs::remove(docs / "Bom.aurea.tmp");
    write_file(docs / "Bom.aurea.meta.json", "{\"title\":\"Bom\"}");
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Apagado.aurea"}), plan).ok());
    AUREA_CHECK(!plan.blocked);
    AUREA_CHECK(listed(plan.paths, only));
}

AUREA_TEST(MediaStorage, AReferenceOnlyInABackupKeepsTheFile) {
    const fs::path docs = scratch("so-no-bak");
    const fs::path media = docs / "import-sources" / "antigo.mp4";
    write_file(media, "video");
    save_project(docs / "Apagado.aurea", {{AssetKind::Video, "docs:import-sources/antigo.mp4"}});
    // O principal do outro projeto já não usa; o .bak (que a abertura restaura) ainda usa.
    save_project(docs / "Outro.aurea", {});
    save_project(docs / "Outro.aurea.bak", {{AssetKind::Video, "docs:import-sources/antigo.mp4"}});
    Rig rig(docs);
    storage::ReleasePlan plan;
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Apagado.aurea"}), plan).ok());
    AUREA_CHECK(!plan.blocked);
    AUREA_CHECK(plan.paths.empty());
    // A cópia de formato antigo também conta.
    fs::remove(docs / "Outro.aurea.bak");
    save_project(docs / "Outro.aurea.v41.bak", {{AssetKind::Video, "docs:import-sources/antigo.mp4"}});
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Apagado.aurea"}), plan).ok());
    AUREA_CHECK(plan.paths.empty());
    // Sem cópia nenhuma usando: sai.
    fs::remove(docs / "Outro.aurea.v41.bak");
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Apagado.aurea"}), plan).ok());
    AUREA_CHECK(listed(plan.paths, media));
    // As cópias do PRÓPRIO apagado não seguram nada (saem junto com ele).
    save_project(docs / "Apagado.aurea.bak", {{AssetKind::Video, "docs:import-sources/antigo.mp4"}});
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Apagado.aurea"}), plan).ok());
    AUREA_CHECK(listed(plan.paths, media));
}

AUREA_TEST(MediaStorage, ADuplicatedProjectKeepsSharedMediaWhenOneCopyIsDeleted) {
    const fs::path docs = scratch("duplicado");
    const fs::path media = docs / "import-sources" / "clipe.mp4";
    write_file(media, "video");
    save_project(docs / "Original.aurea", {{AssetKind::Video, "docs:import-sources/clipe.mp4"}});
    // "Duplicar" copia o .aurea byte a byte: as duas cópias apontam para o mesmo arquivo.
    fs::copy_file(docs / "Original.aurea", docs / "Original (cópia).aurea");
    Rig rig(docs);
    storage::ReleasePlan plan;
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Original.aurea"}), plan).ok());
    AUREA_CHECK(!plan.blocked);
    AUREA_CHECK(plan.paths.empty());
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Original (cópia).aurea"}), plan).ok());
    AUREA_CHECK(plan.paths.empty());
    // As duas apagadas juntas: aí sim.
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Original.aurea", docs / "Original (cópia).aurea"}), plan).ok());
    AUREA_CHECK(listed(plan.paths, media));
}

AUREA_TEST(MediaStorage, AFileOutsideTheAllowlistIsNeverReturnedOrDeleted) {
    const fs::path base = scratch("fora");
    const fs::path docs = base / "docs";
    const fs::path outside = base / "galeria" / "video-da-pessoa.mp4";
    const fs::path library = docs / "Media" / "Modelos" / "textura.png";
    const fs::path font = docs / "Media" / "fonte.ttf";
    const fs::path loose = docs / "solto.mp4";   // dentro de Documentos, fora das pastas de mídia
    const fs::path sound = docs / "Media" / "som-123-trilha.m4a";   // a biblioteca de sons lista Media/
    for (const fs::path& f : {outside, library, font, loose, sound}) write_file(f, "nao apagar");
    save_project(docs / "Apagado.aurea", {{AssetKind::Video, s8(outside)},
                                          {AssetKind::Image, "docs:Media/Modelos/textura.png"},
                                          {AssetKind::Font, "docs:Media/fonte.ttf"},
                                          {AssetKind::Audio, "docs:Media/som-123-trilha.m4a"},
                                          {AssetKind::Video, "docs:solto.mp4"},
                                          {AssetKind::Video, "content://media/external/video/42"}});
    Rig rig(docs);
    storage::ReleasePlan plan;
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Apagado.aurea"}), plan).ok());
    AUREA_CHECK(!plan.blocked);
    AUREA_CHECK(plan.paths.empty());
    // Nem pedido direto apaga: a porta confere de novo.
    const auto r = rig.request();
    const auto removed = rig.e.remove_managed_media({s8(outside), s8(library), s8(loose), s8(docs / "Media"),
                                                     s8(docs / "Media" / ".." / ".." / "galeria" / "video-da-pessoa.mp4")},
                                                    r.managedRoots, r.keepRoots);
    AUREA_CHECK_EQ(removed.files, 0u);
    AUREA_CHECK_EQ(removed.refused, 5u);
    for (const fs::path& f : {outside, library, font, loose, sound}) AUREA_CHECK(fs::exists(f));
    AUREA_CHECK(fs::is_directory(docs / "Media"));
    // Não órfão também: biblioteca e fonte nunca aparecem para limpar.
    for (const fs::path& f : {library, font}) age(f);
    storage::ScanResult scan;
    AUREA_CHECK(rig.e.media_storage_scan(r, scan).ok());
    AUREA_CHECK(!listed(scan.orphans, library));
    AUREA_CHECK(!listed(scan.orphans, font));
}

AUREA_TEST(MediaStorage, TheOpenProjectCountsAsAReference) {
    const fs::path docs = scratch("aberto");
    const fs::path media = docs / "import-sources" / "no-editor.mp4";
    write_file(media, "video");
    save_project(docs / "Apagado.aurea", {{AssetKind::Video, "docs:import-sources/no-editor.mp4"}});
    Rig rig(docs);
    AUREA_CHECK(rig.e.new_project(64, 36, 30.0, nullptr).ok());
    VideoImport vi;
    vi.sourcePath = s8(media);
    vi.displayName = "no-editor";
    AUREA_CHECK(rig.e.import_video(vi).ok());
    storage::ReleasePlan plan;
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Apagado.aurea"}), plan).ok());
    AUREA_CHECK(plan.paths.empty());
    // O carregado é o próprio apagado e o editor fechou (Home): o arquivo já foi
    // gravado ao sair, então ele não segura nada.
    AUREA_CHECK(rig.e.save_project(s8(docs / "Carregado.aurea").c_str()).ok());
    auto request = rig.request({docs / "Apagado.aurea", docs / "Carregado.aurea"});
    AUREA_CHECK(rig.e.media_release_plan(request, plan).ok());
    AUREA_CHECK(listed(plan.paths, media));
    // Com o editor aberto nele, segura.
    request.editorOpen = true;
    AUREA_CHECK(rig.e.media_release_plan(request, plan).ok());
    AUREA_CHECK(plan.paths.empty());
}

AUREA_TEST(MediaStorage, ADerivedFolderLeavesWholeOrNotAtAll) {
    const fs::path docs = scratch("derivada");
    const fs::path folder = docs / "Media" / "PSD-0123456789abcdef-99";
    write_file(folder / "0.png", "camada 0");
    write_file(folder / "1.png", "camada 1");
    AUREA_CHECK(storage::mark_derived(s8(folder)));
    save_project(docs / "A.aurea", {{AssetKind::Image, "docs:Media/PSD-0123456789abcdef-99/0.png"},
                                    {AssetKind::Image, "docs:Media/PSD-0123456789abcdef-99/1.png"}});
    save_project(docs / "B.aurea", {{AssetKind::Image, "docs:Media/PSD-0123456789abcdef-99/1.png"}});
    Rig rig(docs);
    storage::ReleasePlan plan;
    // B ainda usa uma camada: a pasta fica inteira (nem o 0.png sai).
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "A.aurea"}), plan).ok());
    AUREA_CHECK(plan.paths.empty());
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "A.aurea", docs / "B.aurea"}), plan).ok());
    AUREA_CHECK_EQ(plan.paths.size(), usize{1});
    AUREA_CHECK(listed(plan.paths, folder));
    const auto r = rig.request();
    const auto removed = rig.e.remove_managed_media(plan.paths, r.managedRoots, r.keepRoots);
    AUREA_CHECK_EQ(removed.files, 3u);   // os dois PNGs e o marcador
    AUREA_CHECK(!fs::exists(folder));
    AUREA_CHECK(fs::is_directory(docs / "Media"));
    // Uma pasta comum (sem marcador) nunca sai inteira, mesmo pedida.
    write_file(docs / "Media" / "comum" / "x.png", "x");
    const auto refused = rig.e.remove_managed_media({s8(docs / "Media" / "comum")}, r.managedRoots, r.keepRoots);
    AUREA_CHECK_EQ(refused.refused, 1u);
    AUREA_CHECK(fs::exists(docs / "Media" / "comum" / "x.png"));
}

AUREA_TEST(MediaStorage, OrphanedLegacyMediaIsOnlyListedAndRecentImportsAreNot) {
    const fs::path docs = scratch("orfaos");
    const fs::path used = docs / "import-sources" / "usado.mp4";
    const fs::path orphan = docs / "import-sources" / "esquecido.mp4";
    const fs::path recent = docs / "import-sources" / "agora.mp4";
    const fs::path partial = docs / "import-sources" / "copy-123.tmp";
    for (const fs::path& f : {used, orphan, recent, partial}) write_file(f, "video " + s8(f));
    for (const fs::path& f : {used, orphan, partial}) age(f);
    save_project(docs / "A.aurea", {{AssetKind::Video, "docs:import-sources/usado.mp4"}});
    save_project(docs / "B.aurea", {});
    Rig rig(docs);
    // Apagar um projeto nunca leva a mídia que já era órfã.
    storage::ReleasePlan plan;
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "B.aurea"}), plan).ok());
    AUREA_CHECK(plan.paths.empty());
    AUREA_CHECK(fs::exists(orphan));
    storage::ScanResult scan;
    AUREA_CHECK(rig.e.media_storage_scan(rig.request(), scan).ok());
    AUREA_CHECK(!scan.blocked);
    AUREA_CHECK_EQ(scan.orphans.size(), usize{1});
    AUREA_CHECK(listed(scan.orphans, orphan));
    AUREA_CHECK_EQ(scan.orphanFiles, 1u);
    AUREA_CHECK(scan.referencedFiles >= 3u);   // usado, recente, cópia em andamento
    // Projeto ilegível: nada é órfão.
    write_file(docs / "C.aurea", "lixo");
    AUREA_CHECK(rig.e.media_storage_scan(rig.request(), scan).ok());
    AUREA_CHECK(scan.blocked);
    AUREA_CHECK(scan.orphans.empty());
}

AUREA_TEST(PreRenderCache, OrphanFoldersAreRemovedAtEngineStartAndALiveOneIsKept) {
    const fs::path base = scratch("prerender-abertura");
    const fs::path cache = base / "cache";
    const fs::path docs = base / "docs";
    fs::create_directories(docs);
    // Viva: outro cache com a sessão aberta (lease do sistema segurado).
    PreRenderCache live;
    const auto token = live.begin(s8(cache), PreRenderCache::Key{1, 2, 3, 16, 12, 30, 1, 1}, 1, 1, {});
    AUREA_CHECK(token.ok());
    if (!token.ok()) return;
    fs::path liveDir;
    for (const auto& child : fs::directory_iterator(cache / "pre-render"))
        if (child.is_directory()) liveDir = child.path();
    AUREA_CHECK(!liveDir.empty());
    // Morta: marcada, com um quadro, sem ninguém segurando.
    const fs::path dead = cache / "pre-render" / "owned-0000000000000021-0000000000000022";
    fs::create_directories(dead);
    owner_marker(dead);
    write_file(dead / "frame-00000000000000000000.bin", std::string(4096, 'q'));
    // A queda antes do marcador: só o session.lease vazio.
    const fs::path unmarked = cache / "pre-render" / "owned-0000000000000023-0000000000000024";
    fs::create_directories(unmarked);
    write_file(unmarked / "session.lease", "");
    // Desconhecida (outro conteúdo): fica.
    const fs::path unknown = cache / "pre-render" / "owned-0000000000000025-0000000000000026";
    write_file(unknown / "arquivo-da-pessoa", "fica");
    {
        Rig rig(docs, cache);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((fs::exists(dead) || fs::exists(unmarked)) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    AUREA_CHECK(!fs::exists(dead));
    AUREA_CHECK(!fs::exists(unmarked));
    AUREA_CHECK(fs::exists(liveDir));
    AUREA_CHECK(fs::exists(unknown / "arquivo-da-pessoa"));
    AUREA_CHECK(live.matches(*token, PreRenderCache::Key{1, 2, 3, 16, 12, 30, 1, 1}));
}

AUREA_TEST(PreRenderCache, DiskCapIsFourGigabytesAndKeepsALargerReserve) {
    const PreRenderCache::Limits limits;
    AUREA_CHECK_EQ(limits.diskBytes, 4ull << 30);
    AUREA_CHECK(limits.reserveFreeBytes >= (1ull << 30));
    // Pedir mais que o teto não passa dele (o begin corta; o resto do disco fica livre).
    const fs::path cache = scratch("prerender-teto");
    PreRenderCache c;
    PreRenderCache::Limits big;
    big.diskBytes = 64ull << 30;
    const auto token = c.begin(s8(cache), PreRenderCache::Key{1, 2, 3, 16, 12, 30, 1, 1}, 1, 1, big);
    if (token.ok()) AUREA_CHECK(c.stats().framesTotal == 1);
    else AUREA_CHECK_EQ(token.code(), Errc::StorageFull);   // disco do host com menos de 1 GiB livre
}

AUREA_TEST(ImageSequence, ASecondImportOfTheSameZipReusesTheFirstExtraction) {
    const fs::path base = scratch("zip-de-novo");
    const fs::path docs = base / "docs";
    fs::create_directories(docs);
    const fs::path first = base / "seq-1700000000000.zip";
    {
        StoredZipWriter zip;
        AUREA_CHECK(zip.open(s8(first).c_str()).ok());
        for (u32 n = 1; n <= 4; ++n) {
            std::vector<u8> rgba(8 * 8 * 4, static_cast<u8>(30 * n));
            std::vector<u8> png;
            AUREA_CHECK(encode_png(rgba.data(), 8, 8, false, png).ok());
            char name[32];
            std::snprintf(name, sizeof(name), "frame_%05u.png", n);
            AUREA_CHECK(zip.add(name, png.data(), png.size()).ok());
        }
        AUREA_CHECK(zip.finish().ok());
    }
    // O Android copia o .zip escolhido para um nome novo a cada importação.
    const fs::path second = base / "outra-pasta" / "seq-1700000099999.zip";
    fs::create_directories(second.parent_path());
    fs::copy_file(first, second);
    Rig rig(docs);
    const auto a = rig.e.probe_image_sequence({s8(first)});
    AUREA_CHECK(a.ok());
    if (!a.ok()) return;
    AUREA_CHECK(a->fromArchive);
    const fs::path extracted = fs::u8path(a->directory);
    AUREA_CHECK(storage::is_derived(s8(extracted)));
    // Sentinela: some se a segunda importação extrair de novo (pasta nova ou refeita).
    write_file(extracted / "sentinela.txt", "primeira");
    const auto b = rig.e.probe_image_sequence({s8(second)});
    AUREA_CHECK(b.ok());
    if (!b.ok()) return;
    AUREA_CHECK(b->directory == a->directory);
    AUREA_CHECK(fs::exists(extracted / "sentinela.txt"));
    AUREA_CHECK_EQ(b->frameCount, 4u);
    u32 folders = 0;
    for (const auto& child : fs::directory_iterator(docs / "Media" / "sequences")) if (child.is_directory()) ++folders;
    AUREA_CHECK_EQ(folders, 1u);
}

AUREA_TEST(MediaStorage, TheSameGifImportedTwiceSharesOneExpansionFolder) {
    const fs::path base = scratch("gif-de-novo");
    const fs::path docs = base / "docs";
    fs::create_directories(docs);
    const fs::path gif = base / "import-1.gif";
    {
        GifWriter w;
        AUREA_CHECK(w.open(s8(gif).c_str(), 8, 8, false, GifDither::None).ok());
        for (u32 f = 0; f < 3; ++f) {
            std::vector<u8> rgba(8 * 8 * 4, 255);
            for (usize i = 0; i < rgba.size(); i += 4) rgba[i] = static_cast<u8>(f * 80);
            AUREA_CHECK(w.add_frame(rgba.data(), 10).ok());
        }
        AUREA_CHECK(w.finish().ok());
    }
    const fs::path copy = base / "import-2.gif";
    fs::copy_file(gif, copy);
    Rig rig(docs);
    AUREA_CHECK(rig.e.new_project(64, 36, 30.0, nullptr).ok());
    AUREA_CHECK(rig.e.import_gif(s8(gif), "um").ok());
    AUREA_CHECK(rig.e.import_gif(s8(copy), "dois").ok());
    u32 folders = 0;
    for (const auto& child : fs::directory_iterator(docs / "Media"))
        if (child.is_directory() && s8(child.path().filename()).rfind("GIF-", 0) == 0) {
            ++folders;
            AUREA_CHECK(storage::is_derived(s8(child.path())));
        }
    AUREA_CHECK_EQ(folders, 1u);
}

AUREA_TEST(MediaStorage, ProjectFileNamesAndContentKeys) {
    AUREA_CHECK(storage::is_project_file("Meu.aurea"));
    AUREA_CHECK(storage::is_project_file("Meu.aurea.bak"));
    AUREA_CHECK(storage::is_project_file("Meu.aurea.tmp"));
    AUREA_CHECK(storage::is_project_file("Meu.aurea.v41.bak"));
    AUREA_CHECK(storage::is_project_file("Meu.aurea.corrompido"));
    AUREA_CHECK(storage::is_project_file("Meu.aurea.corrompido3"));
    AUREA_CHECK(storage::is_project_file("Meu.aurea.recusado"));
    AUREA_CHECK(!storage::is_project_file("Meu.aurea.meta.json"));
    AUREA_CHECK(!storage::is_project_file("Meu.aureaproj"));
    AUREA_CHECK(!storage::is_project_file(".aurea"));
    const fs::path dir = scratch("chave");
    write_file(dir / "a.bin", std::string(200000, 'z') + "fim");
    write_file(dir / "b.bin", std::string(200000, 'z') + "fim");
    write_file(dir / "c.bin", std::string(200000, 'z') + "fiM");
    u64 ha = 0, hb = 0, hc = 0, na = 0, nb = 0, nc = 0;
    AUREA_CHECK(storage::file_content_key(s8(dir / "a.bin"), ha, na).ok());
    AUREA_CHECK(storage::file_content_key(s8(dir / "b.bin"), hb, nb).ok());
    AUREA_CHECK(storage::file_content_key(s8(dir / "c.bin"), hc, nc).ok());
    AUREA_CHECK(ha == hb && na == nb);
    AUREA_CHECK(ha != hc);
    const std::string mem = std::string(200000, 'z') + "fim";
    AUREA_CHECK_EQ(storage::content_hash(reinterpret_cast<const u8*>(mem.data()), mem.size()), ha);
}

AUREA_TEST(MediaStorage, NothingIsReleasedWithoutTheDocumentsFolder) {
    // Motor sem pasta de documentos: o "docs:" de um projeto que fica não
    // resolveria e a mídia dele pareceria sem dono. Nada sai.
    const fs::path docs = scratch("sem-documentos");
    write_file(docs / "import-sources" / "a.mp4", "video");
    save_project(docs / "Apagado.aurea", {{AssetKind::Video, s8(docs / "import-sources" / "a.mp4")}});
    save_project(docs / "Fica.aurea", {{AssetKind::Video, "docs:import-sources/a.mp4"}});
    test::SyntheticConfig cfg;
    test::SyntheticFactory factory{cfg};
    Engine e;
    EngineConfig ec;
    ec.workerCount = 2;
    ec.disableAutosave = true;
    ec.mediaFactory = &factory;
    AUREA_CHECK(e.initialize(ec).ok());
    Engine::MediaStorageRequest r;
    r.projectsDirectory = s8(docs);
    r.deleted = {s8(docs / "Apagado.aurea")};
    r.managedRoots = {s8(docs / "import-sources")};
    storage::ReleasePlan plan;
    AUREA_CHECK(e.media_release_plan(r, plan).ok());
    AUREA_CHECK(plan.blocked);
    AUREA_CHECK(plan.paths.empty());
    e.shutdown();
}

AUREA_TEST(MediaStorage, CleaningOrphansRemovesOnlyWhatTheScanListed) {
    const fs::path docs = scratch("limpar-orfaos");
    const fs::path used = docs / "Media" / "Projetos" / "Velho" / "usado.png";
    const fs::path orphan = docs / "Media" / "Projetos" / "Apagado" / "sobrou.png";
    const fs::path model = docs / "Media" / "Modelos" / "carro" / "textura.png";
    for (const fs::path& f : {used, orphan, model}) { write_file(f, "png " + s8(f)); age(f); }
    save_project(docs / "Velho.aurea", {{AssetKind::Image, "docs:Media/Projetos/Velho/usado.png"}});
    Rig rig(docs);
    storage::ScanResult scan;
    AUREA_CHECK(rig.e.media_storage_scan(rig.request(), scan).ok());
    AUREA_CHECK_EQ(scan.orphans.size(), usize{1});
    AUREA_CHECK(listed(scan.orphans, orphan));
    const auto r = rig.request();
    const auto removed = rig.e.remove_managed_media(scan.orphans, r.managedRoots, r.keepRoots);
    AUREA_CHECK_EQ(removed.files, 1u);
    AUREA_CHECK(!fs::exists(orphan));
    AUREA_CHECK(!fs::exists(orphan.parent_path()));   // a pasta do projeto importado ficou vazia: sai
    AUREA_CHECK(fs::exists(used));
    AUREA_CHECK(fs::exists(model));
    AUREA_CHECK(fs::is_directory(docs / "Media" / "Projetos"));
}

AUREA_TEST(MediaStorage, AudioNoProjectUsesIsTheSoundLibraryAndNeverAnOrphan) {
    // A tela de áudio lista as narrações e os sons importados direto das pastas
    // de mídia: limpar "mídia sem projeto" não pode levar a biblioteca de sons.
    const fs::path docs = scratch("orfaos-audio");
    const fs::path voice = docs / "voz" / "narracao-1.m4a";
    const fs::path sound = docs / "import-sources" / "som-9-trilha.MP3";
    const fs::path video = docs / "import-sources" / "esquecido.mp4";
    for (const fs::path& f : {voice, sound, video}) { write_file(f, "midia " + s8(f)); age(f); }
    save_project(docs / "A.aurea", {});
    Rig rig(docs);
    auto request = rig.request();
    request.managedRoots.push_back(s8(docs / "voz"));
    storage::ScanResult scan;
    AUREA_CHECK(rig.e.media_storage_scan(request, scan).ok());
    AUREA_CHECK(!scan.blocked);
    AUREA_CHECK_EQ(scan.orphans.size(), usize{1});
    AUREA_CHECK(listed(scan.orphans, video));
    const auto removed = rig.e.remove_managed_media(scan.orphans, request.managedRoots, request.keepRoots);
    AUREA_CHECK_EQ(removed.files, 1u);
    AUREA_CHECK(fs::exists(voice));
    AUREA_CHECK(fs::exists(sound));
    AUREA_CHECK(!fs::exists(video));
}

AUREA_TEST(MediaStorage, TheTemporarySweepSkipsYoungAndReferencedFiles) {
    const fs::path base = scratch("tmp");
    const fs::path docs = base / "docs";
    const fs::path tmp = base / "tmp";
    const fs::path old = tmp / "HomeImports" / "1B4E28BA-2FA1-11D2-883F-0016D3CCA427" / "velho.mov";
    const fs::path young = tmp / "HomeImports" / "6F9619FF-8B86-D011-B42D-00C04FC964FF" / "agora.mov";
    const fs::path used = tmp / "community-xyz.json";
    for (const fs::path& f : {old, young, used}) write_file(f, "tmp " + s8(f));
    age(old);
    age(used);
    fs::create_directories(docs);
    save_project(docs / "Usa.aurea", {{AssetKind::Image, s8(used)}});
    Rig rig(docs);
    Engine::MediaStorageRequest r;
    r.projectsDirectory = s8(docs);
    r.managedRoots = {s8(tmp)};
    r.anyFile = true;
    storage::ScanResult scan;
    AUREA_CHECK(rig.e.media_storage_scan(r, scan).ok());
    AUREA_CHECK_EQ(scan.orphans.size(), usize{1});
    AUREA_CHECK(listed(scan.orphans, old));
    const auto removed = rig.e.remove_managed_media(scan.orphans, r.managedRoots, {});
    AUREA_CHECK_EQ(removed.files, 1u);
    AUREA_CHECK(!fs::exists(old.parent_path()));
    AUREA_CHECK(fs::exists(young));
    AUREA_CHECK(fs::exists(used));
}

AUREA_TEST(MediaStorage, AQuarantinedProjectIsNeverReadAndHoldsEverything) {
    // Android: o projeto que já derrubou o app ao abrir (ProjectGuard) não é
    // lido. Se fica, nada sai; se é o apagado, a mídia dele só não é liberada.
    const fs::path docs = scratch("quarentena");
    const fs::path media = docs / "import-sources" / "a.mp4";
    write_file(media, "video");
    age(media);
    save_project(docs / "Apagado.aurea", {{AssetKind::Video, "docs:import-sources/a.mp4"}});
    save_project(docs / "Quarentena.aurea", {});
    Rig rig(docs);
    auto request = rig.request({docs / "Apagado.aurea"});
    request.skipProjects = {s8(docs / "Quarentena.aurea")};
    storage::ReleasePlan plan;
    AUREA_CHECK(rig.e.media_release_plan(request, plan).ok());
    AUREA_CHECK(plan.blocked);
    AUREA_CHECK_EQ(plan.unreadable, 1u);
    AUREA_CHECK(plan.paths.empty());
    request.skipProjects = {s8(docs / "Apagado.aurea")};
    AUREA_CHECK(rig.e.media_release_plan(request, plan).ok());
    AUREA_CHECK(!plan.blocked);
    AUREA_CHECK(plan.paths.empty());
    storage::ScanResult scan;
    auto scanRequest = rig.request();
    scanRequest.skipProjects = {s8(docs / "Quarentena.aurea")};
    AUREA_CHECK(rig.e.media_storage_scan(scanRequest, scan).ok());
    AUREA_CHECK(scan.blocked);
    AUREA_CHECK(scan.orphans.empty());
}

// =============================================================================
//  0.5.2 (fechamento): quadros soltos com chave de conteúdo, pastas do formato
//  antigo (PSD-/GIF-/seq-<n>) e temporários que saem no fim da operação.
// =============================================================================
namespace {

/// Envelhece a pasta inteira (arquivos e pastas: a data da pasta também conta).
void age_tree(const fs::path& dir) {
    std::error_code ec;
    std::vector<fs::path> dirs{dir};
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) dirs.push_back(it->path());
        else age(it->path());
    }
    for (auto d = dirs.rbegin(); d != dirs.rend(); ++d) age(*d);
}

/// Quatro PNGs numerados de verdade (o probe lê o cabeçalho do primeiro).
std::vector<fs::path> write_frames(const fs::path& dir, u8 seed) {
    std::vector<fs::path> out;
    for (u32 n = 1; n <= 4; ++n) {
        std::vector<u8> rgba(8 * 8 * 4, static_cast<u8>(seed + 30 * n));
        std::vector<u8> png;
        AUREA_CHECK(encode_png(rgba.data(), 8, 8, false, png).ok());
        char name[32];
        std::snprintf(name, sizeof(name), "render_%04u.png", n);
        const fs::path p = dir / name;
        write_file(p, std::string(png.begin(), png.end()));
        out.push_back(p);
    }
    return out;
}

/// O que as plataformas fazem antes de copiar: a chave de cada quadro.
std::vector<storage::FrameKey> keys_of(const std::vector<fs::path>& frames) {
    std::vector<storage::FrameKey> out;
    for (const fs::path& f : frames) {
        storage::FrameKey k;
        k.name = s8(f.filename());
        AUREA_CHECK(storage::file_content_key(s8(f), k.hash, k.bytes).ok());
        out.push_back(k);
    }
    return out;
}

u32 count_dirs(const fs::path& dir) {
    u32 n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) if (it->is_directory(ec)) ++n;
    return n;
}

u32 count_staging(const fs::path& dir) {
    u32 n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        n += s8(it->path().filename()).find(".parcial") != std::string::npos ? 1u : 0u;
    return n;
}

} // namespace

AUREA_TEST(ImageSequence, ASecondImportOfTheSameLooseFramesCopiesNothing) {
    const fs::path base = scratch("quadros-de-novo");
    const fs::path docs = base / "docs";
    fs::create_directories(docs);
    const auto first = write_frames(base / "galeria", 10);
    Rig rig(docs);
    storage::FrameFolderPlan plan;
    AUREA_CHECK(rig.e.plan_sequence_frames(keys_of(first), plan).ok());
    AUREA_CHECK(!plan.reuse);
    AUREA_CHECK(fs::is_directory(fs::u8path(plan.staging)));
    for (const fs::path& f : first) fs::copy_file(f, fs::u8path(plan.staging) / f.filename());
    std::string directory;
    bool reused = true;
    AUREA_CHECK(rig.e.commit_sequence_frames(plan.staging, directory, reused).ok());
    AUREA_CHECK(!reused);
    AUREA_CHECK(directory == plan.directory);
    AUREA_CHECK(storage::is_derived(directory));
    AUREA_CHECK(!fs::exists(fs::u8path(plan.staging)));
    const auto probe = rig.e.probe_image_sequence({directory});
    AUREA_CHECK(probe.ok());
    if (probe.ok()) AUREA_CHECK_EQ(probe->frameCount, 4u);
    // A cópia de agora fica "antiga": a segunda importação não pode regravar nada.
    age_tree(fs::u8path(directory));
    const auto stamp = fs::last_write_time(fs::u8path(directory) / "render_0001.png");
    // A mesma seleção, de outra cópia do seletor (outra pasta, mesmos bytes).
    fs::create_directories(base / "seletor-2");
    std::vector<fs::path> second;
    for (const fs::path& f : first) { second.push_back(base / "seletor-2" / f.filename()); fs::copy_file(f, second.back()); }
    storage::FrameFolderPlan again;
    AUREA_CHECK(rig.e.plan_sequence_frames(keys_of(second), again).ok());
    AUREA_CHECK(again.reuse);
    AUREA_CHECK(again.directory == directory);
    AUREA_CHECK(again.staging.empty());
    AUREA_CHECK_EQ(count_dirs(docs / "Media" / "sequences"), 1u);   // nenhuma pasta nova, nem .parcial
    AUREA_CHECK(fs::last_write_time(fs::u8path(directory) / "render_0001.png") == stamp);
    // Outro conteúdo com os mesmos nomes: outra pasta.
    const auto other = write_frames(base / "outra", 99);
    storage::FrameFolderPlan different;
    AUREA_CHECK(rig.e.plan_sequence_frames(keys_of(other), different).ok());
    AUREA_CHECK(!different.reuse);
    AUREA_CHECK(different.directory != directory);
    AUREA_CHECK(rig.e.discard_sequence_frames(different.staging));
    // Nome inválido ou repetido: recusado antes de criar qualquer coisa.
    auto bad = keys_of(first);
    bad[1].name = bad[0].name;
    AUREA_CHECK(!rig.e.plan_sequence_frames(bad, different).ok());
    bad = keys_of(first);
    bad[0].name = "../fora.png";
    AUREA_CHECK(!rig.e.plan_sequence_frames(bad, different).ok());
    AUREA_CHECK_EQ(count_dirs(docs / "Media" / "sequences"), 1u);
}

AUREA_TEST(ImageSequence, AHalfCopiedFolderIsNeverReused) {
    const fs::path base = scratch("quadros-pela-metade");
    const fs::path docs = base / "docs";
    fs::create_directories(docs);
    const auto frames = write_frames(base / "galeria", 40);
    const auto keys = keys_of(frames);
    Rig rig(docs);
    // Queda no meio da cópia: dois quadros na .parcial, sem marcador, sem nome final.
    storage::FrameFolderPlan crashed;
    AUREA_CHECK(rig.e.plan_sequence_frames(keys, crashed).ok());
    for (usize i = 0; i < 2; ++i) fs::copy_file(frames[i], fs::u8path(crashed.staging) / frames[i].filename());
    storage::FrameFolderPlan next;
    AUREA_CHECK(rig.e.plan_sequence_frames(keys, next).ok());
    AUREA_CHECK(!next.reuse);
    AUREA_CHECK(next.staging == crashed.staging);
    AUREA_CHECK(fs::is_empty(fs::u8path(next.staging)));   // a sobra saiu: começa do zero
    AUREA_CHECK(rig.e.discard_sequence_frames(next.staging));
    // Pasta com o nome da chave e todos os quadros, mas SEM marcador (cópia
    // de fora, marcador perdido): não é reaproveitada nem apagada.
    const fs::path unmarked = fs::u8path(next.directory);
    fs::create_directories(unmarked);
    for (const fs::path& f : frames) fs::copy_file(f, unmarked / f.filename());
    storage::FrameFolderPlan around;
    AUREA_CHECK(rig.e.plan_sequence_frames(keys, around).ok());
    AUREA_CHECK(!around.reuse);
    AUREA_CHECK(around.directory != s8(unmarked));
    for (const fs::path& f : frames) fs::copy_file(f, fs::u8path(around.staging) / f.filename());
    std::string directory;
    bool reused = true;
    AUREA_CHECK(rig.e.commit_sequence_frames(around.staging, directory, reused).ok());
    AUREA_CHECK(!reused);
    AUREA_CHECK(directory == around.directory);
    AUREA_CHECK(storage::is_derived(directory));
    AUREA_CHECK(!storage::is_derived(s8(unmarked)));
    AUREA_CHECK_EQ(static_cast<u32>(std::distance(fs::directory_iterator(unmarked), fs::directory_iterator())), 4u);
    // Uma pasta marcada com um quadro a menos também não serve.
    fs::remove(fs::u8path(directory) / frames[3].filename());
    storage::FrameFolderPlan shorter;
    AUREA_CHECK(rig.e.plan_sequence_frames(keys, shorter).ok());
    AUREA_CHECK(!shorter.reuse);
    AUREA_CHECK(rig.e.discard_sequence_frames(shorter.staging));
}

AUREA_TEST(ImageSequence, TheCopyStagingLeavesAtTheEndOfEveryOutcome) {
    const fs::path base = scratch("quadros-temporario");
    const fs::path docs = base / "docs";
    fs::create_directories(docs);
    const auto frames = write_frames(base / "galeria", 70);
    Rig rig(docs);
    const fs::path sequences = docs / "Media" / "sequences";
    // Sucesso: a .parcial vira a pasta final.
    storage::FrameFolderPlan ok;
    AUREA_CHECK(rig.e.plan_sequence_frames(keys_of(frames), ok).ok());
    for (const fs::path& f : frames) fs::copy_file(f, fs::u8path(ok.staging) / f.filename());
    std::string directory;
    bool reused = false;
    AUREA_CHECK(rig.e.commit_sequence_frames(ok.staging, directory, reused).ok());
    AUREA_CHECK_EQ(count_staging(sequences), 0u);
    // Erro: algo que não é quadro na cópia (uma pasta) → a cópia inteira sai.
    const auto others = write_frames(base / "outros", 120);
    storage::FrameFolderPlan broken;
    AUREA_CHECK(rig.e.plan_sequence_frames(keys_of(others), broken).ok());
    fs::copy_file(others[0], fs::u8path(broken.staging) / others[0].filename());
    fs::create_directories(fs::u8path(broken.staging) / "sub");
    std::string none;
    AUREA_CHECK(!rig.e.commit_sequence_frames(broken.staging, none, reused).ok());
    AUREA_CHECK(!fs::exists(fs::u8path(broken.staging)));
    AUREA_CHECK_EQ(count_staging(sequences), 0u);
    // Cancelamento: a .parcial sai na hora.
    storage::FrameFolderPlan cancelled;
    AUREA_CHECK(rig.e.plan_sequence_frames(keys_of(others), cancelled).ok());
    fs::copy_file(others[0], fs::u8path(cancelled.staging) / others[0].filename());
    AUREA_CHECK(rig.e.discard_sequence_frames(cancelled.staging));
    AUREA_CHECK_EQ(count_staging(sequences), 0u);
    // A porta só apaga uma .parcial da pasta das sequências.
    AUREA_CHECK(!rig.e.discard_sequence_frames(directory));
    AUREA_CHECK(fs::exists(fs::u8path(directory)));
    write_file(base / "fora.parcial" / "x.png", "x");
    AUREA_CHECK(!rig.e.discard_sequence_frames(s8(base / "fora.parcial")));
    AUREA_CHECK(fs::exists(base / "fora.parcial" / "x.png"));
    // .zip que não extrai: nenhuma .parcial fica.
    write_file(base / "quebrado.zip", "PK isto nao e um zip");
    AUREA_CHECK(!rig.e.probe_image_sequence({s8(base / "quebrado.zip")}).ok());
    AUREA_CHECK_EQ(count_staging(sequences), 0u);
    // A sobra de uma queda (app morto no meio da cópia) aparece inteira como
    // órfã depois de 24 h; nova, conta como em uso.
    storage::FrameFolderPlan dead;
    AUREA_CHECK(rig.e.plan_sequence_frames(keys_of(others), dead).ok());
    fs::copy_file(others[0], fs::u8path(dead.staging) / others[0].filename());
    storage::ScanResult scan;
    AUREA_CHECK(rig.e.media_storage_scan(rig.request(), scan).ok());
    AUREA_CHECK(!listed(scan.orphans, fs::u8path(dead.staging)));
    AUREA_CHECK(!listed(scan.orphans, fs::u8path(dead.staging) / others[0].filename()));
    age_tree(fs::u8path(dead.staging));
    AUREA_CHECK(rig.e.media_storage_scan(rig.request(), scan).ok());
    AUREA_CHECK(listed(scan.orphans, fs::u8path(dead.staging)));
    const auto r = rig.request();
    const auto removed = rig.e.remove_managed_media({dead.staging}, r.managedRoots, r.keepRoots);
    AUREA_CHECK_EQ(removed.files, 1u);
    AUREA_CHECK(!fs::exists(fs::u8path(dead.staging)));
}

AUREA_TEST(MediaStorage, LegacyExpansionFoldersLeaveWholeOnlyWhenNothingPointsInside) {
    const fs::path docs = scratch("pastas-antigas");
    const fs::path psd = docs / "Media" / "PSD-1730000000000000";
    const fs::path gif = docs / "Media" / "GIF-1730000000000001";
    const fs::path seq = docs / "Media" / "sequences" / "seq-1730000000000";
    const fs::path released = docs / "Media" / "PSD-1730000000000002";
    write_file(psd / "0.png", "camada 0");
    write_file(psd / "1.png", "camada 1");
    write_file(gif / "0.png", "quadro 0");
    write_file(gif / "1.png", "quadro 1");
    write_file(seq / "render_0001.png", "q1");
    write_file(seq / "render_0002.png", "q2");
    write_file(released / "0.png", "so do apagado");
    for (const fs::path& d : {psd, gif, seq, released}) age_tree(d);
    AUREA_CHECK(storage::is_legacy_derived(s8(psd)));
    AUREA_CHECK(storage::is_legacy_derived(s8(gif)));
    AUREA_CHECK(storage::is_legacy_derived(s8(seq)));
    AUREA_CHECK(!storage::is_legacy_derived(s8(docs / "Media")));
    AUREA_CHECK(!storage::is_legacy_derived(s8(docs / "Media" / "PSD-0123456789abcdef-99")));
    // Só o .bak de um projeto aponta para UMA camada do PSD antigo.
    save_project(docs / "Usa.aurea.bak", {{AssetKind::Image, "docs:Media/PSD-1730000000000000/1.png"}});
    save_project(docs / "Apagado.aurea", {{AssetKind::Image, "docs:Media/PSD-1730000000000002/0.png"}});
    Rig rig(docs);
    // Apagar o projeto leva a pasta antiga inteira que só ele usava.
    storage::ReleasePlan plan;
    AUREA_CHECK(rig.e.media_release_plan(rig.request({docs / "Apagado.aurea"}), plan).ok());
    AUREA_CHECK_EQ(plan.paths.size(), usize{1});
    AUREA_CHECK(listed(plan.paths, released));
    storage::ScanResult scan;
    AUREA_CHECK(rig.e.media_storage_scan(rig.request(), scan).ok());
    AUREA_CHECK(!scan.blocked);
    // As sem referência aparecem inteiras em "Mídia sem projeto"; a referenciada
    // não aparece, nem a camada que ninguém usa dentro dela.
    AUREA_CHECK(listed(scan.orphans, gif));
    AUREA_CHECK(listed(scan.orphans, seq));
    AUREA_CHECK(!listed(scan.orphans, psd));
    AUREA_CHECK(!listed(scan.orphans, psd / "0.png"));
    AUREA_CHECK(!listed(scan.orphans, gif / "0.png"));
    // E saem pelo mesmo caminho dos órfãos (o pedido da pessoa).
    std::vector<std::string> chosen;
    for (const std::string& o : scan.orphans)
        if (o.find("GIF-") != std::string::npos || o.find("seq-") != std::string::npos) chosen.push_back(o);
    const auto r = rig.request();
    const auto removed = rig.e.remove_managed_media(chosen, r.managedRoots, r.keepRoots);
    AUREA_CHECK_EQ(removed.refused, 0u);
    AUREA_CHECK_EQ(removed.files, 4u);
    AUREA_CHECK(!fs::exists(gif));
    AUREA_CHECK(!fs::exists(seq));
    AUREA_CHECK(fs::exists(psd / "0.png"));
    AUREA_CHECK(fs::exists(psd / "1.png"));
    // Pasta "antiga" com sub-pasta não é do app antigo: nunca sai inteira.
    write_file(docs / "Media" / "GIF-1730000000000009" / "sub" / "0.png", "x");
    AUREA_CHECK(!storage::is_legacy_derived(s8(docs / "Media" / "GIF-1730000000000009")));
    const auto refused = rig.e.remove_managed_media({s8(docs / "Media" / "GIF-1730000000000009")}, r.managedRoots, r.keepRoots);
    AUREA_CHECK_EQ(refused.refused, 1u);
    AUREA_CHECK(fs::exists(docs / "Media" / "GIF-1730000000000009" / "sub" / "0.png"));
}

AUREA_TEST(ExportCheckpoint, StaleCheckpointsLeaveAtEngineStartAndResumableOnesStay) {
    namespace ckpt = export_checkpoint;
    const fs::path base = scratch("checkpoints");
    const fs::path cache = base / "cache";
    const fs::path dir = cache / "export-checkpoints";
    fs::create_directories(dir);
    const auto family = [&](u64 key, int state, u32 failures, bool manifest) {
        const std::string log = ckpt::log_path(s8(dir), key);
        write_file(fs::u8path(log), std::string(4096, 'p'));
        write_file(fs::u8path(ckpt::spill_path(log)), std::string(512, 'a'));
        if (manifest) {
            ckpt::Manifest m;
            m.sessionKey = key;
            m.requestKey = key;
            m.totalFrames = 100;
            m.state = static_cast<ckpt::SessionState>(state);
            m.failureResumes = failures;
            AUREA_CHECK(ckpt::write_manifest(log, m).ok());
        }
        return log;
    };
    const auto old_hours = [&](const std::string& log, int hours) {
        std::error_code ec;
        for (const std::string& f : {log, ckpt::spill_path(log), ckpt::manifest_path(log)})
            fs::last_write_time(fs::u8path(f), fs::file_time_type::clock::now() - std::chrono::hours(hours), ec);
    };
    const std::string noManifest = family(0x11, 0, 0, false);          // queda antes do manifesto
    const std::string exhausted = family(0x22, 2, 4, true);            // continuações esgotadas
    const std::string pausedRecent = family(0x33, 1, 0, true);         // pausa de ontem: fica
    const std::string pausedForgotten = family(0x44, 1, 0, true);      // pausa de 8 dias: sai
    const std::string failedResumable = family(0x55, 2, 1, true);      // falha que ainda continua: fica
    const std::string live = family(0x66, 0, 0, false);                // export gravando agora: fica
    old_hours(noManifest, 2);
    old_hours(exhausted, 2);
    old_hours(pausedRecent, 30);
    old_hours(pausedForgotten, 8 * 24);
    old_hours(failedResumable, 30);
    write_file(dir / "arquivo-da-pessoa.mp4", "fica");
    {
        test::SyntheticConfig cfg;
        test::SyntheticFactory factory{cfg};
        Engine e;
        EngineConfig ec;
        ec.workerCount = 2;
        ec.disableAutosave = true;
        ec.mediaFactory = &factory;
        ec.cacheDirectory = s8(cache);
        ec.enableResumableExport = true;
        AUREA_CHECK(e.initialize(ec).ok());
        e.shutdown();
    }
    AUREA_CHECK(!fs::exists(fs::u8path(noManifest)));
    AUREA_CHECK(!fs::exists(fs::u8path(ckpt::spill_path(noManifest))));
    AUREA_CHECK(!fs::exists(fs::u8path(exhausted)));
    AUREA_CHECK(!fs::exists(fs::u8path(ckpt::manifest_path(exhausted))));
    AUREA_CHECK(!fs::exists(fs::u8path(pausedForgotten)));
    AUREA_CHECK(fs::exists(fs::u8path(pausedRecent)));
    AUREA_CHECK(fs::exists(fs::u8path(ckpt::manifest_path(pausedRecent))));
    AUREA_CHECK(fs::exists(fs::u8path(failedResumable)));
    AUREA_CHECK(fs::exists(fs::u8path(live)));
    AUREA_CHECK(fs::exists(dir / "arquivo-da-pessoa.mp4"));
}
