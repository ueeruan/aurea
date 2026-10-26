#include "TestFramework.hpp"
#include "aurea/export/ExportRecovery.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/text/FontManager.hpp"

#include <filesystem>
#include <algorithm>
#include <limits>
#include <atomic>
#include <cstdio>
#include <thread>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace aurea;
namespace recovery = aurea::export_recovery;
namespace {
struct Files {
    std::vector<std::string> paths;
    ~Files() { fileio::clear_fault_injection(); for (const auto& p : paths) { fileio::remove_file(p); fileio::remove_file(p + ".tmp"); } }
    std::string add(const char* name) { auto p = std::string("aurea_test_recovery_") + name; paths.push_back(p); return p; }
};
Project project() { auto made = Project::create_new(96, 64, 30, "frozen"); return std::move(*made); }
bool put(const std::string& p, const std::string& text) { return fileio::write_atomic(p, text.data(), text.size()).ok(); }
// 0.2.4: o snapshot so resolve assets que a composicao usa. Os testes que
// conferem dependencias colocam o asset numa camada da raiz.
AssetId use(Project& p, AssetId id, CompositionId in = {}) {
    auto* comp = p.timeline().composition(in.valid() ? in : p.timeline().root());
    const Asset* a = p.asset(id);
    const bool model = a && a->kind == AssetKind::Model3D;
    const auto layer = comp->add_layer(model ? LayerKind::Model3D : (a && a->kind == AssetKind::Image ? LayerKind::Image : LayerKind::Video), "uses asset");
    if (model) comp->layer(layer)->model.scene = id; else comp->layer(layer)->source = id;
    return id;
}
}

AUREA_TEST(ExportRecovery, AtomicPackageRestoresNestedSnapshotAndSessionSettings) {
    Files files; const auto path = files.add("roundtrip.arec");
    auto p = project();
    const auto child = p.timeline().create_composition("nested", 48, 32, 60);
    p.timeline().set_current(child);
    auto* comp = p.timeline().composition(child);
    const auto layer = comp->add_layer(LayerKind::Shape, "immutable");
    comp->layer(layer)->transform.opacity = .375f;
    comp->motion_blur().enabled = true; comp->motion_blur().samples = 12; comp->motion_blur().shutterAngle = 270;
    ExportSettings settings; settings.fps = 29.97; settings.dither = false;
    settings.aiUpscale = 2; settings.safeMode = 1; settings.trimToContent = true; settings.quality = 2;
    settings.motionBlurSamples = 19; settings.opticalFlowQuality = 2; settings.videoBitrateMbps = 13;
    settings.scale = .5f; settings.parallelSegments = 3;
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, child, settings, {}, frozen, package).ok());
    if (!frozen) return;
    comp->layer(layer)->transform.opacity = 0; comp->motion_blur().samples = 1;
    p.metadata().title = "changed";
    AUREA_CHECK_EQ(frozen->metadata().title, std::string("frozen"));
    AUREA_CHECK_EQ(frozen->timeline().composition(child)->layer(layer)->transform.opacity, .375f);
    package.state = recovery::State::Failed; package.result = Errc::EncodeFailed; package.acceptedFrames = 7;
    package.outputPath = "failed.mp4";
    AUREA_CHECK(recovery::fingerprint_sources(package).ok());
    AUREA_CHECK(recovery::write(path, package).ok());
    frozen.reset();
    recovery::Package restored;
    AUREA_CHECK(recovery::read(path, restored, frozen).ok());
    if (!frozen) return;
    AUREA_CHECK_EQ(restored.state, recovery::State::Failed);
    AUREA_CHECK_EQ(restored.result, Errc::EncodeFailed);
    AUREA_CHECK_EQ(restored.acceptedFrames, 7u); // Diagnostic, replay still begins at zero.
    AUREA_CHECK_EQ(restored.composition, child);
    AUREA_CHECK_EQ(frozen->timeline().current(), child);
    AUREA_CHECK_EQ(frozen->timeline().composition(child)->layer(layer)->transform.opacity, .375f);
    AUREA_CHECK_EQ(frozen->timeline().composition(child)->motion_blur().samples, 12u);
    AUREA_CHECK_EQ(restored.settings.fps, settings.fps);
    AUREA_CHECK(!restored.settings.dither && restored.settings.trimToContent);
    AUREA_CHECK_EQ(restored.settings.aiUpscale, 2u); AUREA_CHECK_EQ(restored.settings.safeMode, 1u);
    AUREA_CHECK_EQ(restored.settings.quality, 2u); AUREA_CHECK_EQ(restored.settings.motionBlurSamples, 19u);
    AUREA_CHECK_EQ(restored.settings.opticalFlowQuality, 2u); AUREA_CHECK_EQ(restored.settings.videoBitrateMbps, 13u);
    AUREA_CHECK_EQ(restored.settings.parallelSegments, 3u); AUREA_CHECK_EQ(restored.settings.scale, .5f);
}

AUREA_TEST(ExportRecovery, PlatformAutomaticDimensionsAndRateSurviveRestart) {
    Files files; const auto path = files.add("automatic.arec");
    auto p = project(); ExportSettings settings;
    settings.width = 0; settings.height = 0; settings.fps = 0;
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().current(), settings, {}, frozen, package).ok());
    AUREA_CHECK(recovery::write(path, package).ok());
    recovery::Package restored; frozen.reset();
    AUREA_CHECK(recovery::read(path, restored, frozen).ok());
    AUREA_CHECK_EQ(restored.settings.width, 0u);
    AUREA_CHECK_EQ(restored.settings.height, 0u);
    AUREA_CHECK_EQ(restored.settings.fps, 0.0);
}

AUREA_TEST(ExportRecovery, WholeSourceHashRejectsSameSizeChangeAndMissingSource) {
    Files files; const auto source = files.add("source.bin"), path = files.add("source.arec");
    AUREA_CHECK(put(source, "original source bytes"));
    auto p = project(); Asset a; a.kind = AssetKind::Video; a.sourcePath = source; (void)use(p, p.add_asset(a));
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).ok());
    AUREA_CHECK_EQ(package.dependencies.size(), usize{1});
    AUREA_CHECK(recovery::fingerprint_sources(package).ok()); AUREA_CHECK(recovery::write(path, package).ok());
    std::error_code ec; const auto stamp = std::filesystem::last_write_time(std::filesystem::u8path(source), ec);
    AUREA_CHECK(!ec); AUREA_CHECK(put(source, "modified source bytes"));
    std::filesystem::last_write_time(std::filesystem::u8path(source), stamp, ec); AUREA_CHECK(!ec);
    recovery::Package untouched; untouched.outputPath = "not-published";
    std::unique_ptr<Project> result;
    AUREA_CHECK_EQ(recovery::read(path, untouched, result).code(), Errc::ChecksumMismatch);
    AUREA_CHECK(!result); AUREA_CHECK_EQ(untouched.outputPath, std::string("not-published"));
    AUREA_CHECK(fileio::remove_file(source));
    AUREA_CHECK_EQ(recovery::read(path, untouched, result).code(), Errc::MediaSourceMissing);
}

AUREA_TEST(ExportRecovery, LocalFileUriKeepsPersistentVideoSourceAndEncodedSpaces) {
    Files files; const auto source = files.add("URI space.bin");
    AUREA_CHECK(put(source, "persistent media bytes"));
    const auto absolute = std::filesystem::absolute(source).generic_string();
    std::string uri = "file:///";
    if (absolute.front() == '/') uri = "file://";
    for (const char c : absolute) uri += c == ' ' ? "%20" : std::string(1, c);
    auto p = project(); Asset a; a.kind = AssetKind::Video; a.sourcePath = uri; const auto id = use(p, p.add_asset(a));
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).ok());
    AUREA_CHECK_EQ(package.dependencies.size(), usize{1});
    AUREA_CHECK(recovery::fingerprint_sources(package).ok());
    AUREA_CHECK_EQ(p.asset(id)->sourcePath, uri); // The original project is untouched.
}

AUREA_TEST(ExportRecovery, InterruptedCheckpointWritePreservesPreparedSnapshot) {
    Files files; const auto path = files.add("atomic.arec"); auto p = project();
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).ok());
    AUREA_CHECK(recovery::write(path, package).ok());
    for (const auto fault : {fileio::Fault::DiskFullAfter, fileio::Fault::FlushFails, fileio::Fault::RenameFails}) {
        fileio::FaultInjection injection; injection.kind = fault; injection.afterBytes = 32; injection.pathContains = path;
        fileio::set_fault_injection(injection);
        auto update = package; update.state = recovery::State::Complete; update.acceptedFrames = 99;
        AUREA_CHECK(!recovery::write(path, update).ok()); AUREA_CHECK_EQ(fileio::injected_failures(), 1u);
        fileio::clear_fault_injection();
        recovery::Package preserved; std::unique_ptr<Project> restored;
        AUREA_CHECK(recovery::read(path, preserved, restored).ok());
        AUREA_CHECK_EQ(preserved.state, recovery::State::Prepared); AUREA_CHECK_EQ(preserved.acceptedFrames, 0u);
        AUREA_CHECK_EQ(preserved.projectBytes, package.projectBytes);
    }
}

AUREA_TEST(ExportRecovery, TruncationVersionAndCorruptionNeverPublishPartialDocument) {
    Files files; const auto path = files.add("invalid.arec"); auto p = project();
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).ok());
    AUREA_CHECK(recovery::write(path, package).ok());
    std::vector<u8> valid; AUREA_CHECK(fileio::read_all(path, valid, recovery::kMaxPackageBytes));
    if (valid.size() < 24) return;
    recovery::Package result; std::unique_ptr<Project> loaded;
    for (usize n : {usize{0}, usize{7}, usize{23}, valid.size()-1}) {
        AUREA_CHECK(fileio::write_atomic(path, valid.data(), n).ok());
        AUREA_CHECK(!recovery::read(path, result, loaded).ok()); AUREA_CHECK(!loaded);
    }
    auto corrupt = valid; corrupt.back() ^= 1;
    AUREA_CHECK(fileio::write_atomic(path, corrupt.data(), corrupt.size()).ok());
    AUREA_CHECK_EQ(recovery::read(path, result, loaded).code(), Errc::ChecksumMismatch);
    corrupt = valid; corrupt[4] = 2;
    AUREA_CHECK(fileio::write_atomic(path, corrupt.data(), corrupt.size()).ok());
    AUREA_CHECK_EQ(recovery::read(path, result, loaded).code(), Errc::UnsupportedVersion);
    AUREA_CHECK(!loaded);
}

AUREA_TEST(ExportRecovery, GltfExternalBuffersAndImagesAreDependencyChecked) {
    Files files; const auto source = files.add("model.gltf"), binary = files.add("buffer.bin"), image = files.add("image.png");
    AUREA_CHECK(put(binary, "0123456789ab")); AUREA_CHECK(put(image, "image payload"));
    const std::string json = "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"" + binary + "\",\"byteLength\":12}],\"images\":[{\"uri\":\"" + image + "\"},{\"uri\":\"data:image/png;base64,AA==\"}]}";
    AUREA_CHECK(put(source, json));
    auto p = project(); Asset a; a.kind = AssetKind::Model3D; a.sourcePath = source; (void)use(p, p.add_asset(a));
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).ok());
    AUREA_CHECK_EQ(package.dependencies.size(), usize{3});
    AUREA_CHECK(recovery::fingerprint_sources(package).ok()); AUREA_CHECK(recovery::validate_sources(package).ok());
    AUREA_CHECK(put(binary, "0123456789ac"));
    AUREA_CHECK_EQ(recovery::validate_sources(package).code(), Errc::ChecksumMismatch);
}

AUREA_TEST(ExportRecovery, FbxAndObjModelsFreezeByTheirOwnFile) {
    // Beta 0.1.x: um modelo FBX/OBJ na timeline fazia o export V2 recusar na
    // largada (NotSupported). O arquivo do modelo e a dependencia verificada.
    Files files; const auto fbx = files.add("personagem.fbx"), obj = files.add("cubo.obj");
    AUREA_CHECK(put(fbx, "Kaydara FBX Binary  ")); AUREA_CHECK(put(obj, "v 0 0 0 v 1 0 0 v 0 1 0 f 1 2 3"));
    auto p = project();
    Asset a; a.kind = AssetKind::Model3D; a.sourcePath = fbx; (void)use(p, p.add_asset(a));
    Asset b; b.kind = AssetKind::Model3D; b.sourcePath = obj; (void)use(p, p.add_asset(b));
    recovery::Package package; std::unique_ptr<Project> frozen;
    const Status captured = recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package);
    AUREA_CHECK(captured.ok());
    AUREA_CHECK_EQ(package.dependencies.size(), usize{2});
    AUREA_CHECK(recovery::fingerprint_sources(package).ok()); AUREA_CHECK(recovery::validate_sources(package).ok());
    AUREA_CHECK(put(fbx, "Kaydara FBX Binary  x"));
    AUREA_CHECK_EQ(recovery::validate_sources(package).code(), Errc::ChecksumMismatch);
}

AUREA_TEST(ExportRecovery, UnsupportedEphemeralSourcesFailWithoutChangingEditor) {
    auto p = project(); Asset a; a.kind = AssetKind::Video; a.sourcePath = "fd:123"; const auto id = use(p, p.add_asset(a));
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK_EQ(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).code(), Errc::NotSupported);
    AUREA_CHECK(!frozen); AUREA_CHECK_EQ(p.asset(id)->sourcePath, std::string("fd:123"));
}

AUREA_TEST(ExportRecovery, ColonInLocalFilenameIsNotAnEphemeralUriScheme) {
    Files files; auto p = project(); Asset a; a.kind = AssetKind::Video;
    a.sourcePath = std::filesystem::absolute(files.add("clip:source.mp4")).string(); const auto id = use(p, p.add_asset(a));
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).ok());
    AUREA_CHECK(frozen != nullptr); AUREA_CHECK_EQ(package.dependencies.size(), usize{1});
    AUREA_CHECK_EQ(p.asset(id)->sourcePath, a.sourcePath);
}

AUREA_TEST(ExportRecovery, EffectiveSystemAndFallbackFontsBecomeVerifiedDependencies) {
    auto font = text::default_font();
    if (!font) { std::printf("(sem fonte da plataforma: pulado) "); return; }
    auto p = project(); auto* comp = p.timeline().composition(p.timeline().root());
    const auto id = comp->add_layer(LayerKind::Text, "font dependency");
    auto& data = comp->layer(id)->text; data.content = "Aurea 日本 العربية";
    const auto actual = text::FontManager::instance().font_for(data);
    AUREA_CHECK(actual != nullptr); if (!actual) return;
    const auto dependencies = text::source_dependencies(*actual, data);
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).ok());
    if (!frozen) return;
    AUREA_CHECK(data.fontPath.empty());
    AUREA_CHECK(!frozen->timeline().composition(p.timeline().root())->layer(id)->text.fontPath.empty());
    for (const auto& path : dependencies) {
        const auto real = std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal();
        const auto u = real.u8string(); const std::string expected(reinterpret_cast<const char*>(u.data()), u.size());
        AUREA_CHECK(std::any_of(package.dependencies.begin(), package.dependencies.end(), [&](const recovery::Dependency& d) { return d.path == expected; }));
    }
    AUREA_CHECK(recovery::fingerprint_sources(package).ok());
    AUREA_CHECK(recovery::validate_sources(package).ok());
}

// 0.2.4 (C1): uma URI morta num asset que a composicao nao usa (clipe
// apagado, midia substituida) derrubava todo export com erro 18.
AUREA_TEST(ExportRecovery, DeadUnusedAssetIsNeverResolvedAndDoesNotBlockExport) {
    Files files; const auto live = files.add("live.bin");
    AUREA_CHECK(put(live, "live media"));
    auto p = project();
    Asset used; used.kind = AssetKind::Video; used.sourcePath = live; (void)use(p, p.add_asset(used));
    Asset dead; dead.kind = AssetKind::Video; dead.sourcePath = "content://media/expired/1";
    const auto deadId = p.add_asset(dead);
    std::vector<std::string> asked;
    const recovery::PathResolver resolver = [&](const std::string& path) {
        asked.push_back(path);
        return path.rfind("content://", 0) == 0 ? std::string{} : path;  // URI morta: falha
    };
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, resolver, frozen, package, false).ok());
    AUREA_CHECK_EQ(asked.size(), usize{1});
    AUREA_CHECK(std::none_of(asked.begin(), asked.end(), [](const std::string& a) { return a.rfind("content://", 0) == 0; }));
    AUREA_CHECK_EQ(package.dependencies.size(), usize{1});
    if (frozen) AUREA_CHECK_EQ(frozen->asset(deadId)->sourcePath, std::string("content://media/expired/1"));
    // Usado de verdade, o mesmo asset morto continua falhando (nada mascarado).
    (void)use(p, deadId);
    recovery::Package failing; std::unique_ptr<Project> none;
    AUREA_CHECK(!recovery::capture(p, p.timeline().root(), ExportSettings{}, resolver, none, failing, false).ok());
}

AUREA_TEST(ExportRecovery, ReplacedMediaDoesNotAskForTheOldSource) {
    Files files; const auto fresh = files.add("fresh.bin");
    AUREA_CHECK(put(fresh, "new media"));
    auto p = project(); auto* comp = p.timeline().composition(p.timeline().root());
    Asset old; old.kind = AssetKind::Video; old.sourcePath = "content://media/old/7";
    const auto oldId = p.add_asset(old);
    const auto layer = comp->add_layer(LayerKind::Video, "clip");
    comp->layer(layer)->source = oldId;
    Asset next; next.kind = AssetKind::Video; next.sourcePath = fresh;
    comp->layer(layer)->source = p.add_asset(next);  // como "Substituir midia" antiga
    u32 oldAsked = 0;
    const recovery::PathResolver resolver = [&](const std::string& path) {
        if (path.rfind("content://", 0) == 0) { ++oldAsked; return std::string{}; }
        return path;
    };
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, resolver, frozen, package, false).ok());
    AUREA_CHECK_EQ(oldAsked, 0u);
}

AUREA_TEST(ExportRecovery, NestedCompositionAssetsAreReachableOthersAreNot) {
    Files files; const auto inner = files.add("inner.bin"), other = files.add("other.bin");
    AUREA_CHECK(put(inner, "inner")); AUREA_CHECK(put(other, "other"));
    auto p = project();
    const auto child = p.timeline().create_composition("child", 48, 32, 30);
    const auto unrelated = p.timeline().create_composition("unrelated", 48, 32, 30);
    Asset a; a.kind = AssetKind::Video; a.sourcePath = inner; (void)use(p, p.add_asset(a), child);
    Asset b; b.kind = AssetKind::Video; b.sourcePath = other; (void)use(p, p.add_asset(b), unrelated);
    auto* root = p.timeline().composition(p.timeline().root());
    const auto group = root->add_layer(LayerKind::Composition, "group");
    root->layer(group)->nested.composition = child;
    const auto reach = p.assets_reachable_from(p.timeline().root());
    AUREA_CHECK_EQ(reach.size(), usize{1});
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).ok());
    AUREA_CHECK_EQ(package.dependencies.size(), usize{1});
    const auto u = std::filesystem::absolute(std::filesystem::u8path(inner)).lexically_normal().u8string();
    if (package.dependencies.size() == 1)
        AUREA_CHECK_EQ(package.dependencies[0].path, std::string(reinterpret_cast<const char*>(u.data()), u.size()));
    // Sem a camada de grupo, a filha nao e alcancavel.
    root->remove_layer(group);
    AUREA_CHECK(p.assets_reachable_from(p.timeline().root()).empty());
}

AUREA_TEST(ExportRecovery, RetainedImagePixelsSkipTheResolverWithoutRecovery) {
    auto p = project();
    Asset img; img.kind = AssetKind::Image; img.sourcePath = "content://media/photo/3";
    const auto id = use(p, p.add_asset(img));
    u32 asked = 0;
    const recovery::PathResolver resolver = [&](const std::string&) { ++asked; return std::string{}; };
    const std::vector<u64> retained{id.pack()};
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, resolver, frozen, package, false, &retained).ok());
    AUREA_CHECK_EQ(asked, 0u);
    // Sem pixels retidos, a imagem passa pelo resolvedor (e a URI morta falha).
    recovery::Package second; std::unique_ptr<Project> none;
    AUREA_CHECK(!recovery::capture(p, p.timeline().root(), ExportSettings{}, resolver, none, second, false).ok());
    AUREA_CHECK_EQ(asked, 1u);
}

AUREA_TEST(ExportPreparation, OnlyOwnedOrphanOutputsAreRemovedAndRecoveryRemains) {
    namespace fs = std::filesystem;
    const fs::path root = fs::absolute("aurea_test_export_cleanup");
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
    std::error_code ec;
    fs::create_directories(root / "nested", ec); AUREA_CHECK(!ec);
    const auto file = [&](const std::string& name) { return (root / fs::u8path(name)).string(); };
    const auto pid =
#if defined(_WIN32)
        _getpid();
#else
        getpid();
#endif
    const std::string live = "writer.mp4.aurea-startup-" + std::to_string(pid) + "-1-1.mp4";
    const std::string stale = "writer.mp4.aurea-startup-0-1-1.mp4";
    for (const auto& name : {std::string("old.mp4"), std::string("old.png"), std::string("old.gif"),
            std::string("old.zip"), std::string("current.mp4"), std::string("recover.mp4"),
            std::string("recover.mp4.aurea-export"), std::string("project.aurea"), std::string("original.jpg"),
            std::string("nested/kept.mp4"), live, stale}) AUREA_CHECK(put(file(name), "owned test bytes"));
    const auto result = recovery::cleanup_temporary_outputs(root.string(), file("current.mp4"));
    AUREA_CHECK(result.ok()); if (result.ok()) AUREA_CHECK_EQ(*result, 5u);
    for (const auto& name : {"old.mp4", "old.png", "old.gif", "old.zip"}) AUREA_CHECK(!fs::exists(file(name)));
    AUREA_CHECK(!fs::exists(file(stale)));
    for (const auto* name : {"current.mp4", "recover.mp4", "recover.mp4.aurea-export", "project.aurea",
                            "original.jpg", "nested/kept.mp4"}) AUREA_CHECK(fs::exists(file(name)));
    AUREA_CHECK(fs::exists(file(live)));
    AUREA_CHECK(put(file("cancelled.png"), "keep on cancel"));
    std::atomic<bool> cancel{true};
    AUREA_CHECK_EQ(recovery::cleanup_temporary_outputs(root.string(), file("current.mp4"), &cancel).status().code(), Errc::Cancelled);
    AUREA_CHECK(fs::exists(file("cancelled.png")));
    // Direct symlinks and redirected directory ancestors must not traverse to
    // media outside the owned cache. Hosts lacking symlink privilege skip only
    // this OS setup; all regular file scope assertions above still execute.
    fs::create_symlink(root / "original.jpg", root / "alias.mp4", ec);
    if (!ec) {
        AUREA_CHECK(recovery::cleanup_temporary_outputs(root.string(), file("current.mp4")).ok());
        AUREA_CHECK(fs::exists(file("original.jpg")));
        AUREA_CHECK(fs::is_symlink(root / "alias.mp4"));
    }
}

// 0.3.2 (CheckpointManager): o hash das fontes do "Preparando" é cancelável a
// cada bloco de 64 KiB, mostra progresso e batida, e a memória não cresce com
// o tamanho da fonte (buffer fixo). Cancelado, o pacote fica como estava.
AUREA_TEST(ExportRecovery, SourceFingerprintIsCancellableWithProgressAndHeartbeat) {
    Files files; const auto source = files.add("big-source.bin");
    {
        std::vector<u8> block(1u << 20);
        for (usize i = 0; i < block.size(); ++i) block[i] = static_cast<u8>(i * 31u + 7u);
        std::FILE* f = fileio::open_file(source, "wb");
        AUREA_CHECK(f != nullptr); if (!f) return;
        for (int i = 0; i < 64; ++i) AUREA_CHECK_EQ(std::fwrite(block.data(), 1, block.size(), f), block.size());
        std::fclose(f);
    }
    auto p = project(); Asset a; a.kind = AssetKind::Video; a.sourcePath = source; (void)use(p, p.add_asset(a));
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).ok());
    AUREA_CHECK_EQ(package.dependencies.size(), usize{1});
    std::atomic<u64> done{0}, total{0}, beat{0};
    const recovery::FingerprintProgress progress{&done, &total, &beat};
    // Já cancelado: nada é lido.
    std::atomic<bool> cancel{true};
    AUREA_CHECK_EQ(recovery::fingerprint_sources(package, &cancel, &progress).code(), Errc::Cancelled);
    AUREA_CHECK_EQ(done.load(), u64{0});
    AUREA_CHECK_EQ(total.load(), u64{64} << 20);
    AUREA_CHECK_EQ(package.dependencies[0].contentHash, u64{0});
    // Cancelado no meio: volta dentro de um bloco depois do pedido.
    cancel = false; done = 0;
    std::thread canceller([&] {
        while (done.load() < (1u << 20)) std::this_thread::yield();
        cancel = true;
    });
    const Status interrupted = recovery::fingerprint_sources(package, &cancel, &progress);
    canceller.join();
    AUREA_CHECK_EQ(interrupted.code(), Errc::Cancelled);
    AUREA_CHECK(done.load() >= (1u << 20) && done.load() < (u64{64} << 20));
    AUREA_CHECK(beat.load() != 0);
    AUREA_CHECK_EQ(package.dependencies[0].contentHash, u64{0});
    AUREA_CHECK_EQ(package.dependencies[0].bytes, u64{0});
    // Inteiro: o mesmo conteúdo dá o mesmo hash; um byte diferente, outro.
    cancel = false; done = 0;
    AUREA_CHECK(recovery::fingerprint_sources(package, &cancel, &progress).ok());
    AUREA_CHECK_EQ(done.load(), u64{64} << 20);
    const u64 first = package.dependencies[0].contentHash;
    AUREA_CHECK(first != 0);
    AUREA_CHECK_EQ(package.dependencies[0].bytes, u64{64} << 20);
    AUREA_CHECK(recovery::fingerprint_sources(package).ok());
    AUREA_CHECK_EQ(package.dependencies[0].contentHash, first);
    {
        std::FILE* f = fileio::open_file(source, "rb+");
        AUREA_CHECK(f != nullptr); if (!f) return;
        std::fseek(f, (33 << 20) + 5, SEEK_SET);
        std::fputc(0xA5, f);
        std::fclose(f);
    }
    AUREA_CHECK(recovery::fingerprint_sources(package).ok());
    AUREA_CHECK(package.dependencies[0].contentHash != first);
}
