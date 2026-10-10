#include "TestFramework.hpp"
#include "aurea/export/ExportRecovery.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/text/FontManager.hpp"

#include <filesystem>
#include <algorithm>
#include <limits>

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
    auto p = project(); Asset a; a.kind = AssetKind::Video; a.sourcePath = source; (void)p.add_asset(a);
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
    auto p = project(); Asset a; a.kind = AssetKind::Video; a.sourcePath = uri; const auto id = p.add_asset(a);
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
    auto p = project(); Asset a; a.kind = AssetKind::Model3D; a.sourcePath = source; (void)p.add_asset(a);
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).ok());
    AUREA_CHECK_EQ(package.dependencies.size(), usize{3});
    AUREA_CHECK(recovery::fingerprint_sources(package).ok()); AUREA_CHECK(recovery::validate_sources(package).ok());
    AUREA_CHECK(put(binary, "0123456789ac"));
    AUREA_CHECK_EQ(recovery::validate_sources(package).code(), Errc::ChecksumMismatch);
}

AUREA_TEST(ExportRecovery, UnsupportedEphemeralSourcesFailWithoutChangingEditor) {
    auto p = project(); Asset a; a.kind = AssetKind::Video; a.sourcePath = "fd:123"; const auto id = p.add_asset(a);
    recovery::Package package; std::unique_ptr<Project> frozen;
    AUREA_CHECK_EQ(recovery::capture(p, p.timeline().root(), ExportSettings{}, {}, frozen, package).code(), Errc::NotSupported);
    AUREA_CHECK(!frozen); AUREA_CHECK_EQ(p.asset(id)->sourcePath, std::string("fd:123"));
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
