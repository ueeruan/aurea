#include "aurea/export/ExportRecovery.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/scene3d/Shape3D.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/text/FontManager.hpp"
#include "cgltf.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <new>
#include <unordered_set>

namespace aurea::export_recovery {
namespace {
constexpr u32 kMagic = 0x43455241; // AREC, little endian.
constexpr u32 kVersion = 1;
constexpr u32 kMaxDependencies = 16384;
constexpr usize kMaxPathBytes = 32768;
constexpr usize kMaxGltfJsonBytes = 8ull << 20;

u64 hash_bytes(const u8* p, usize n, u64 h = 1469598103934665603ull) noexcept {
    for (usize i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
std::string utf8(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
bool unsupported_uri(const std::string& p) noexcept {
    const auto colon = p.find(':');
    return colon != std::string::npos && !(colon == 1 && p.size() > 2 &&
        ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')));
}
Status add_path(Package& package, std::string& path, const PathResolver& resolve) {
    if (path.empty()) return OkStatus;
    std::string real = resolve ? resolve(path) : path;
    if (real.rfind("file://", 0) == 0) {
        real.erase(0, 7);
        if (real.rfind("localhost/", 0) == 0) real.erase(0, 9);
        if (real.empty() || real.front() != '/') return Errc::NotSupported;
        std::string decoded;
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (usize i = 0; i < real.size(); ++i) {
            if (real[i] != '%') { decoded.push_back(real[i]); continue; }
            if (i + 2 >= real.size()) return Errc::InvalidArgument;
            const int a = hex(real[i + 1]), b = hex(real[i + 2]);
            if (a < 0 || b < 0 || !(a * 16 + b)) return Errc::InvalidArgument;
            decoded.push_back(static_cast<char>(a * 16 + b)); i += 2;
        }
#if defined(_WIN32)
        if (decoded.size() > 3 && decoded[0] == '/' && decoded[2] == ':') decoded.erase(0, 1);
#endif
        real = std::move(decoded);
    }
    if (real.empty() || unsupported_uri(real))
        return Status{Errc::NotSupported, "recuperacao exige fontes em arquivos persistentes"};
    std::error_code ec;
    auto absolute = std::filesystem::absolute(std::filesystem::u8path(real), ec).lexically_normal();
    if (ec) return Errc::IoError;
    path = utf8(absolute);
    if (path.size() > kMaxPathBytes || package.dependencies.size() >= kMaxDependencies)
        return Errc::BudgetExceeded;
    if (std::none_of(package.dependencies.begin(), package.dependencies.end(), [&](const Dependency& d) { return d.path == path; }))
        package.dependencies.push_back({path, 0, 0});
    return OkStatus;
}
Status fingerprint(const std::string& path, Dependency& out) {
    std::error_code ec;
    const auto file = std::filesystem::u8path(path);
    if (!std::filesystem::is_regular_file(file, ec)) return Errc::MediaSourceMissing;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) return Errc::IoError;
    const auto time = std::filesystem::last_write_time(file, ec);
    if (ec) return Errc::IoError;
    std::unique_ptr<std::FILE, decltype(&std::fclose)> f(fileio::open_file(path, "rb"), &std::fclose);
    if (!f) return Errc::MediaSourceMissing;
    std::array<u8, 65536> bytes{};
    u64 total = 0, h = 1469598103934665603ull;
    for (;;) {
        const usize n = std::fread(bytes.data(), 1, bytes.size(), f.get());
        total += n; h = hash_bytes(bytes.data(), n, h);
        if (n != bytes.size()) {
            if (std::ferror(f.get())) return Errc::IoError;
            break;
        }
    }
    if (total != size || std::filesystem::file_size(file, ec) != size || ec ||
        std::filesystem::last_write_time(file, ec) != time || ec)
        return Status{Errc::InvalidState, "fonte alterada durante a verificacao da recuperacao"};
    out = {path, total, h};
    return OkStatus;
}

// cgltf parses only the bounded JSON chunk here, never the model's binary
// payload. External image and buffer URIs must be fingerprinted too.
Status add_gltf_dependencies(Package& package, const std::string& source) {
    const auto file = std::filesystem::u8path(source);
    std::string ext = utf8(file.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".gltf" && ext != ".glb")
        return Status{Errc::NotSupported, "recuperacao de modelos requer glTF ou GLB com dependencias verificaveis"};
    std::vector<u8> json;
    if (ext == ".gltf") {
        if (!fileio::read_all(source, json, kMaxGltfJsonBytes)) return Errc::MediaSourceMissing;
    } else {
        std::unique_ptr<std::FILE, decltype(&std::fclose)> f(fileio::open_file(source, "rb"), &std::fclose);
        if (!f) return Errc::MediaSourceMissing;
        std::array<u8, 20> h{};
        if (std::fread(h.data(), 1, h.size(), f.get()) != h.size()) return Errc::CorruptData;
        auto u32at = [&](usize i) { return u32(h[i]) | (u32(h[i+1]) << 8) | (u32(h[i+2]) << 16) | (u32(h[i+3]) << 24); };
        const u32 n = u32at(12);
        if (u32at(0) != 0x46546c67 || u32at(4) != 2 || u32at(16) != 0x4e4f534a || n == 0 || n > kMaxGltfJsonBytes)
            return Errc::CorruptData;
        json.resize(n);
        if (std::fread(json.data(), 1, n, f.get()) != n) return Errc::CorruptData;
    }
    cgltf_options options{};
    options.type = cgltf_file_type_gltf;
    cgltf_data* raw = nullptr;
    if (cgltf_parse(&options, json.data(), json.size(), &raw) != cgltf_result_success)
        return Errc::CorruptData;
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, &cgltf_free);
    auto addUri = [&](const char* uri) -> Status {
        if (!uri || std::strncmp(uri, "data:", 5) == 0) return OkStatus;
        std::string decoded(uri);
        decoded.resize(cgltf_decode_uri(decoded.data()));
        if (decoded.empty() || unsupported_uri(decoded)) return Errc::NotSupported;
        auto path = utf8(file.parent_path() / std::filesystem::u8path(decoded));
        return add_path(package, path, {});
    };
    for (usize i = 0; i < data->buffers_count; ++i) if (const Status s = addUri(data->buffers[i].uri); !s.ok()) return s;
    for (usize i = 0; i < data->images_count; ++i) if (const Status s = addUri(data->images[i].uri); !s.ok()) return s;
    return OkStatus;
}

Status clone(const std::vector<u8>& bytes, std::unique_ptr<Project>& out) {
    auto project = std::make_unique<Project>();
    LoadReport report;
    const Status s = ProjectSerializer::load_bytes(*project, bytes.data(), bytes.size(), LoadOptions{}, &report);
    if (!s.ok()) return s;
    if (report.partial || !report.sectionsSkipped.empty() ||
        std::count(report.sectionsRead.begin(), report.sectionsRead.end(), SectionKind::Project) != 1 ||
        std::count(report.sectionsRead.begin(), report.sectionsRead.end(), SectionKind::Timeline) != 1 ||
        std::count(report.sectionsRead.begin(), report.sectionsRead.end(), SectionKind::Assets) != 1)
        return Errc::ProjectCorrupted;
    out = std::move(project);
    return OkStatus;
}
struct Writer {
    std::vector<u8> bytes;
    void u32v(u32 x) { for (u32 i = 0; i < 4; ++i) bytes.push_back(static_cast<u8>(x >> (i*8))); }
    void u64v(u64 x) { for (u32 i = 0; i < 8; ++i) bytes.push_back(static_cast<u8>(x >> (i*8))); }
    void string(const std::string& s) { u32v(static_cast<u32>(s.size())); bytes.insert(bytes.end(), s.begin(), s.end()); }
};
struct Reader {
    const u8* p; usize n; usize at = 0; bool ok = true;
    u64 integer(usize size) { if (at > n || size > n - at) { ok = false; return 0; } u64 x = 0; for (usize i = 0; i < size; ++i) x |= u64(p[at++]) << (8*i); return x; }
    u32 u32v() { return static_cast<u32>(integer(4)); }
    u64 u64v() { return integer(8); }
    std::string string() { const u32 size = u32v(); if (!ok || size > kMaxPathBytes || at > n || size > n - at) { ok = false; return {}; } std::string s(reinterpret_cast<const char*>(p + at), size); at += size; if (s.find('\0') != std::string::npos) ok = false; return s; }
};
void settings_write(Writer& w, const ExportSettings& s) {
    w.u32v(s.width); w.u32v(s.height); w.u64v(std::bit_cast<u64>(s.fps));
    w.u32v(static_cast<u32>(s.videoCodec)); w.u32v(s.videoBitrateMbps); w.u32v(s.rateMode); w.u32v(s.keyframeIntervalFrames);
    w.u32v(static_cast<u32>(s.audioCodec)); w.u32v(s.audioBitrateKbps); w.u32v(s.audioSampleRate); w.u32v(s.audioChannels);
    w.u32v(s.container); w.u32v(static_cast<u32>(s.outputColorSpace)); w.u32v(s.toneMapToSdr);
    w.u32v(s.parallelSegments); w.u32v(s.motionBlurSamples); w.u32v(s.opticalFlowQuality); w.u32v(std::bit_cast<u32>(s.scale));
    w.u32v(s.dither); w.u32v(s.aiUpscale); w.u32v(s.trimToContent); w.u32v(s.quality); w.u32v(s.safeMode);
}
ExportSettings settings_read(Reader& r) {
    ExportSettings s;
    s.width = r.u32v(); s.height = r.u32v(); s.fps = std::bit_cast<f64>(r.u64v());
    s.videoCodec = static_cast<ExportCodec>(r.u32v()); s.videoBitrateMbps = r.u32v(); s.rateMode = r.u32v(); s.keyframeIntervalFrames = r.u32v();
    s.audioCodec = static_cast<AudioCodec>(r.u32v()); s.audioBitrateKbps = r.u32v(); s.audioSampleRate = r.u32v(); s.audioChannels = r.u32v();
    s.container = r.u32v(); s.outputColorSpace = static_cast<ColorSpace>(r.u32v()); s.toneMapToSdr = r.u32v() != 0;
    s.parallelSegments = r.u32v(); s.motionBlurSamples = r.u32v(); s.opticalFlowQuality = r.u32v(); s.scale = std::bit_cast<f32>(r.u32v());
    s.dither = r.u32v() != 0; s.aiUpscale = r.u32v(); s.trimToContent = r.u32v() != 0; s.quality = r.u32v(); s.safeMode = r.u32v();
    return s;
}
bool valid_settings(const ExportSettings& s) noexcept {
    // The platform APIs use width=0 (derive aspect ratio), height=0 (native
    // raster), fps=0 (composition rate). These are valid restart settings.
    return std::isfinite(s.fps) && s.fps >= 0 && std::isfinite(s.scale) && s.scale > 0 &&
        static_cast<u32>(s.videoCodec) <= static_cast<u32>(ExportCodec::HEVC) &&
        (s.aiUpscale == 0 || s.aiUpscale == 2 || s.aiUpscale == 4);
}
} // namespace

Status capture(const Project& project, CompositionId composition, const ExportSettings& settings,
               const PathResolver& resolve, std::unique_ptr<Project>& frozen, Package& package) noexcept {
    try {
        Package candidate; candidate.settings = settings; candidate.composition = composition;
        SaveOptions save; save.writeThumbnail = false;
        std::vector<u8> initial;
        if (const Status s = ProjectSerializer::encode(project, save, initial); !s.ok()) return s;
        if (initial.size() > kMaxProjectBytes) return Errc::BudgetExceeded;
        std::unique_ptr<Project> copy;
        if (const Status s = clone(initial, copy); !s.ok()) return s;
        initial.clear(); initial.shrink_to_fit();
        if (!copy->timeline().composition(composition)) return Errc::NotFound;
        copy->timeline().set_current(composition);
        Status status;
        copy->for_each_asset([&](AssetId, Asset& asset) {
            if (!status.ok()) return;
            scene3d::Text3DSpec text;
            scene3d::Shape3DSpec shape;
            if (scene3d::decode_text3d(asset.sourcePath, text)) {
                if (const auto font = scene3d::text3d_font(text)) {
                    text.fontPath = font->source_path();
                    TextData data; data.content = text.content;
                    for (auto path : aurea::text::source_dependencies(*font, data)) {
                        status = add_path(candidate, path, resolve); if (!status.ok()) return;
                    }
                } else { status = Errc::MediaSourceMissing; return; }
                status = add_path(candidate, text.fontPath, resolve);
                if (status.ok()) status = add_path(candidate, text.texturePath, resolve);
                asset.sourcePath = scene3d::encode_text3d(text);
            } else if (scene3d::decode_shape3d(asset.sourcePath, shape)) {
                for (auto& part : shape.parts) { status = add_path(candidate, part.image, resolve); if (!status.ok()) break; }
                asset.sourcePath = scene3d::encode_shape3d(shape);
            } else if (!asset.sourcePath.empty()) {
                status = add_path(candidate, asset.sourcePath, resolve);
                if (status.ok() && asset.kind == AssetKind::Model3D) status = add_gltf_dependencies(candidate, asset.sourcePath);
            } else if (asset.kind == AssetKind::Image || asset.kind == AssetKind::Video || asset.kind == AssetKind::Audio || asset.kind == AssetKind::Model3D) {
                status = Status{Errc::NotSupported, "fonte apenas em memoria nao pode ser recuperada apos fechar o app"};
            }
            asset.proxyPath.clear(); // Final export always resolves the original.
        });
        if (!status.ok()) return status;
        copy->timeline().for_each_composition([&](CompositionId, Composition& comp) {
            comp.layers().for_each([&](LayerId, Layer& layer) {
                if (!status.ok() || layer.kind != LayerKind::Text) return;
                if (const auto font = text::FontManager::instance().font_for(layer.text)) {
                    layer.text.fontPath = font->source_path();
                    for (auto path : text::source_dependencies(*font, layer.text)) {
                        status = add_path(candidate, path, resolve); if (!status.ok()) return;
                    }
                } else { status = Errc::MediaSourceMissing; return; }
                status = add_path(candidate, layer.text.fontPath, resolve);
            });
        });
        if (!status.ok()) return status;
        if (const Status s = ProjectSerializer::encode(*copy, save, candidate.projectBytes); !s.ok()) return s;
        if (candidate.projectBytes.size() > kMaxProjectBytes) return Errc::BudgetExceeded;
        frozen = std::move(copy); package = std::move(candidate);
        return OkStatus;
    } catch (const std::bad_alloc&) { return Errc::OutOfMemory; }
      catch (...) { return Errc::IoError; }
}
Status fingerprint_sources(Package& package) noexcept {
    try {
        auto dependencies = package.dependencies;
        for (auto& d : dependencies) if (const Status s = fingerprint(d.path, d); !s.ok()) return s;
        package.dependencies = std::move(dependencies); return OkStatus;
    } catch (const std::bad_alloc&) { return Errc::OutOfMemory; } catch (...) { return Errc::IoError; }
}
Status validate_sources(const Package& package) noexcept {
    try {
        for (const auto& d : package.dependencies) {
            Dependency actual;
            if (const Status s = fingerprint(d.path, actual); !s.ok()) return s;
            if (d.bytes != actual.bytes || d.contentHash != actual.contentHash)
                return Status{Errc::ChecksumMismatch, "fonte de export alterada desde o snapshot"};
        }
        return OkStatus;
    } catch (const std::bad_alloc&) { return Errc::OutOfMemory; } catch (...) { return Errc::IoError; }
}
Status write(const std::string& path, const Package& package) noexcept {
    try {
        if (!valid_settings(package.settings) || package.projectBytes.empty() || package.projectBytes.size() > kMaxProjectBytes ||
            package.dependencies.size() > kMaxDependencies || package.outputPath.size() > kMaxPathBytes) return Errc::InvalidArgument;
        Writer payload;
        payload.u32v(static_cast<u32>(package.state)); payload.u32v(static_cast<u32>(package.result));
        payload.u32v(package.acceptedFrames); payload.u64v(package.composition.pack());
        settings_write(payload, package.settings); payload.string(package.outputPath);
        payload.u32v(static_cast<u32>(package.dependencies.size()));
        for (const auto& d : package.dependencies) {
            if (d.path.size() > kMaxPathBytes) return Errc::InvalidArgument;
            payload.string(d.path); payload.u64v(d.bytes); payload.u64v(d.contentHash);
            if (payload.bytes.size() > (4ull << 20)) return Errc::BudgetExceeded;
        }
        payload.u64v(package.projectBytes.size());
        payload.bytes.insert(payload.bytes.end(), package.projectBytes.begin(), package.projectBytes.end());
        if (payload.bytes.size() + 24 > kMaxPackageBytes) return Errc::BudgetExceeded;
        Writer file; file.bytes.reserve(payload.bytes.size() + 24);
        file.u32v(kMagic); file.u32v(kVersion); file.u64v(payload.bytes.size()); file.u64v(hash_bytes(payload.bytes.data(), payload.bytes.size()));
        file.bytes.insert(file.bytes.end(), payload.bytes.begin(), payload.bytes.end());
        return fileio::write_atomic(path, file.bytes.data(), file.bytes.size(), {true, false});
    } catch (const std::bad_alloc&) { return Errc::OutOfMemory; } catch (...) { return Errc::IoError; }
}
Status read(const std::string& path, Package& package, std::unique_ptr<Project>& frozen) noexcept {
    try {
        std::vector<u8> bytes;
        if (!fileio::read_all(path, bytes, kMaxPackageBytes)) return fileio::exists(path) ? Errc::CorruptData : Errc::NotFound;
        Reader header{bytes.data(), bytes.size()};
        if (header.u32v() != kMagic) return Errc::UnsupportedFormat;
        if (header.u32v() != kVersion) return Errc::UnsupportedVersion;
        const u64 size = header.u64v(), expected = header.u64v();
        if (!header.ok || size != bytes.size() - header.at) return Errc::CorruptData;
        if (hash_bytes(bytes.data() + header.at, static_cast<usize>(size)) != expected) return Errc::ChecksumMismatch;
        Reader r{bytes.data() + header.at, static_cast<usize>(size)};
        Package candidate;
        candidate.state = static_cast<State>(r.u32v()); candidate.result = static_cast<Errc>(r.u32v());
        candidate.acceptedFrames = r.u32v(); candidate.composition = CompositionId::unpack(r.u64v());
        candidate.settings = settings_read(r); candidate.outputPath = r.string();
        const u32 count = r.u32v();
        if (!r.ok || count > kMaxDependencies || !valid_settings(candidate.settings) ||
            static_cast<u32>(candidate.state) > 2 || static_cast<u32>(candidate.result) > static_cast<u32>(Errc::EncoderUnavailable)) return Errc::CorruptData;
        candidate.dependencies.reserve(count);
        std::unordered_set<std::string> seen;
        for (u32 i = 0; i < count; ++i) {
            Dependency d; d.path = r.string(); d.bytes = r.u64v(); d.contentHash = r.u64v();
            if (!r.ok || d.path.empty() || unsupported_uri(d.path) || !std::filesystem::u8path(d.path).is_absolute() || !seen.insert(d.path).second) return Errc::CorruptData;
            candidate.dependencies.push_back(std::move(d));
        }
        const u64 projectSize = r.u64v();
        if (!r.ok || projectSize == 0 || projectSize > kMaxProjectBytes || projectSize != r.n - r.at) return Errc::CorruptData;
        candidate.projectBytes.assign(r.p + r.at, r.p + r.n);
        std::unique_ptr<Project> copy;
        if (const Status s = clone(candidate.projectBytes, copy); !s.ok()) return s;
        if (!copy->timeline().composition(candidate.composition)) return Errc::ProjectCorrupted;
        if (const Status s = validate_sources(candidate); !s.ok()) return s;
        copy->timeline().set_current(candidate.composition);
        package = std::move(candidate); frozen = std::move(copy); return OkStatus;
    } catch (const std::bad_alloc&) { return Errc::OutOfMemory; } catch (...) { return Errc::CorruptData; }
}
Status write_project(const std::string& path, const Package& package) noexcept {
    return fileio::write_atomic(path, package.projectBytes.data(), package.projectBytes.size(), {true, false});
}
} // namespace aurea::export_recovery
