// =============================================================================
//  Aurea / engine / EngineImageSequence.cpp
//
//  Importar SEQUÊNCIA DE IMAGENS (render do Blender, PNG com alfa, EXR) como um
//  clipe de vídeo. O padrão, a receita e o decoder estão em ImageSequence.hpp;
//  aqui ficam a entrada (arquivos, pasta, um quadro só ou .zip) e a criação do
//  asset e da camada, pela mesma regra do `import_video`.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/project/FileIO.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <new>

namespace aurea {
namespace {

namespace fs = std::filesystem;

/// Teto do que um .zip de sequência pode extrair (o ZIP sem ZIP64 já para em 4 GB).
constexpr u64 kSequenceArchiveDiskLimit = kAddressSpace32 ? (2ull << 30) : (4ull << 30);
constexpr usize kProbeMissingList = 256;

std::string utf8_of(const fs::path& p) {
    const auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

std::string lower_extension(const fs::path& p) {
    std::string ext = utf8_of(p.extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

void list_files(const fs::path& dir, bool recursive, std::vector<std::string>& out) {
    std::error_code ec;
    if (recursive) {
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec)) out.push_back(utf8_of(it->path()));
        }
    } else {
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec)) out.push_back(utf8_of(it->path()));
        }
    }
}

/// Pasta do .zip extraído: a mesma para o mesmo arquivo (sondar e depois
/// importar não extrai duas vezes; reimportar reaproveita).
std::string archive_directory(const std::string& documents, const fs::path& archive) {
    std::error_code ec;
    const auto size = fs::file_size(archive, ec);
    const auto time = fs::last_write_time(archive, ec);
    u64 h = 1469598103934665603ull;
    const std::string key = utf8_of(fs::weakly_canonical(archive, ec)) + "|" + std::to_string(size) + "|"
        + std::to_string(static_cast<long long>(time.time_since_epoch().count()));
    for (unsigned char c : key) { h ^= c; h *= 1099511628211ull; }
    char name[24];
    std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(h));
    if (documents.empty()) return utf8_of(fs::u8path(utf8_of(archive) + ".aurea-seq"));
    return utf8_of(fs::u8path(documents) / "Media" / "sequences" / name);
}

} // namespace

Status Engine::collect_sequence_inputs(const std::vector<std::string>& paths, std::vector<std::string>& files,
                                       std::string& preferred, bool& fromArchive) noexcept try {
    files.clear();
    preferred.clear();
    fromArchive = false;
    bool sawMissing = false;
    for (std::string raw : paths) {
        if (raw.rfind("file://", 0) == 0) raw.erase(0, 7);
        if (raw.empty()) continue;
        const fs::path p = fs::u8path(raw);
        std::error_code ec;
        const auto status = fs::status(p, ec);
        if (ec || !fs::exists(status)) { sawMissing = true; continue; }
        if (fs::is_directory(status)) { list_files(p, false, files); continue; }
        if (!fs::is_regular_file(status)) continue;
        if (lower_extension(p) == ".zip") {
            const std::string dir = archive_directory(config_.documentsDirectory, p);
            if (!fs::is_directory(fs::u8path(dir), ec)) {
                // Extrai numa pasta temporária e só então dá o nome final: o app
                // morto no meio não deixa uma sequência pela metade para a
                // próxima importação reaproveitar.
                fs::create_directories(fs::u8path(dir).parent_path(), ec);
                const std::string staging = dir + ".parcial";
                fs::remove_all(fs::u8path(staging), ec);   // sobra de uma extração interrompida (nossa)
                std::vector<std::string> extracted;
                if (const Status s = package::extract_model_archive(raw, staging, extracted, kSequenceArchiveDiskLimit); !s.ok())
                    return s;
                fs::rename(fs::u8path(staging), fs::u8path(dir), ec);
                if (ec) {
                    fs::remove_all(fs::u8path(staging), ec);
                    return Status{Errc::IoError, "pasta da sequencia extraida"};
                }
            }
            list_files(fs::u8path(dir), true, files);
            fromArchive = true;
            continue;
        }
        if (paths.size() == 1) {
            // UM quadro: os irmãos dele na mesma pasta formam a sequência.
            preferred = raw;
            list_files(p.parent_path(), false, files);
        } else {
            files.push_back(raw);
        }
    }
    if (files.empty()) return sawMissing ? Status{Errc::MediaSourceMissing, "arquivos da sequencia nao encontrados"}
                                         : Status{Errc::UnsupportedFormat, "nenhuma imagem na selecao"};
    return OkStatus;
} catch (const std::bad_alloc&) {
    return Status{Errc::OutOfMemory};
} catch (...) {
    return Status{Errc::IoError};
}

Result<ImageSequenceProbe> Engine::probe_sequence_files(const std::vector<std::string>& paths,
                                                        ImageSequenceSpec& spec) noexcept try {
    std::vector<std::string> files;
    std::string preferred;
    bool fromArchive = false;
    if (const Status s = collect_sequence_inputs(paths, files, preferred, fromArchive); !s.ok()) return s;
    u32 candidates = 0;
    if (!detect_image_sequence(files, preferred, spec, &candidates))
        return Status{Errc::UnsupportedFormat, "nenhuma sequencia numerada de imagens (png, jpg, tga, bmp, exr)"};
    ImageSequenceProbe probe;
    probe.directory = spec.directory;
    probe.prefix = spec.prefix;
    probe.suffix = spec.suffix;
    probe.pattern = spec.display_pattern();
    probe.padding = spec.padding;
    probe.first = spec.first;
    probe.last = spec.last;
    probe.frameCount = static_cast<u32>(spec.frame_count());
    probe.presentCount = static_cast<u32>(spec.present_count());
    probe.missingCount = probe.frameCount - probe.presentCount;
    probe.candidates = candidates;
    probe.fromArchive = fromArchive;
    {
        const usize dot = spec.suffix.find_last_of('.');
        probe.format = dot == std::string::npos ? std::string{} : spec.suffix.substr(dot + 1);
        for (char& c : probe.format) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (probe.format == "jpeg") probe.format = "jpg";
    }
    if (!spec.present.empty()) {
        for (usize r = 0; r + 1 < spec.present.size() && probe.missing.size() < kProbeMissingList; ++r)
            for (i64 n = spec.present[r].second + 1; n < spec.present[r + 1].first && probe.missing.size() < kProbeMissingList; ++n)
                probe.missing.push_back(n);
    }
    const fs::path dir = fs::u8path(spec.directory);
    probe.firstFile = utf8_of(dir / fs::u8path(spec.file_name(spec.first)));
    SequenceFrameHeader header;
    if (const Status s = read_sequence_frame_header(probe.firstFile, header); !s.ok()) return s;
    probe.width = header.width;
    probe.height = header.height;
    probe.channels = header.channels;
    probe.bitDepth = header.bitDepth;
    probe.hasAlpha = header.hasAlpha;
    probe.linear = header.exr;
    probe.suggestedFps = header.fps > 0.0 && header.fps <= 240.0 ? header.fps : 24.0;
    probe.suggestedAlpha = header.exr ? SequenceAlphaMode::Premultiplied
                         : header.hasAlpha ? SequenceAlphaMode::Straight : SequenceAlphaMode::Ignore;
    probe.suggestedColor = header.exr ? SequenceColorMode::Linear : SequenceColorMode::Srgb;
    std::error_code ec;
    for (i64 n : spec.present_numbers()) {
        const auto size = fs::file_size(dir / fs::u8path(spec.file_name(n)), ec);
        if (!ec) probe.totalBytes += size;
    }
    return probe;
} catch (const std::bad_alloc&) {
    return Status{Errc::OutOfMemory};
} catch (...) {
    return Status{Errc::IoError};
}

Result<ImageSequenceProbe> Engine::probe_image_sequence(const std::vector<std::string>& paths) noexcept {
    ImageSequenceSpec spec;
    return probe_sequence_files(paths, spec);
}

Result<u64> Engine::import_image_sequence(const ImageSequenceImport& request) noexcept try {
    if (config_.mediaFactory != &sequenceFactory_)
        return Status{Errc::NotSupported, "sem decodificador de midia nesta plataforma"};
    u64 session = 0;
    i64 insertionFrame = 0;
    CompositionId composition;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_ || !current_composition()) return Status{Errc::InvalidState, "nenhum projeto aberto"};
        drain_commands_locked();
        insertionFrame = std::max<i64>(0, playback_.current().value);
        session = projectSession_;
        composition = project_->timeline().current();
    }

    // Sondagem FORA do lock: listar a pasta, extrair o .zip e ler o cabeçalho
    // do primeiro quadro podem levar muito mais que um quadro da UI.
    ImageSequenceSpec spec;
    auto probed = probe_sequence_files(request.paths, spec);
    if (!probed.ok()) return probed.status();
    const ImageSequenceProbe& probe = *probed;
    if (!probe.width || !probe.height) return Status{Errc::AssetCorrupted, "sequencia sem dimensoes"};
    const f64 fps = request.fps > 0.0 && std::isfinite(request.fps) ? std::clamp(request.fps, 0.1, 1000.0) : probe.suggestedFps;
    spec.fps = fps;
    spec.alpha = request.alpha == SequenceAlphaMode::Auto ? probe.suggestedAlpha : request.alpha;
    spec.color = request.color == SequenceColorMode::Auto ? probe.suggestedColor : request.color;
    // A pasta como `store_asset_path` (docs:… dentro de Documentos): abre de
    // novo depois da troca do contêiner do app e dentro do pacote do projeto.
    spec.directory = store_asset_path(probe.directory);
    const std::string recipe = encode_image_sequence(spec);
    const i64 frames = spec.frame_count();
    const f64 durationUs = static_cast<f64>(frames) * 1e6 / fps;

    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_ || session != projectSession_ || composition != project_->timeline().current()
        || state_ == EngineState::ShuttingDown)
        return Status{Errc::Cancelled, "projeto alterado durante a importacao"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};

    history_.before_mutation(*comp, project_->timeline().current(), "importar sequencia de imagens");
    bump_model_revision_locked();

    const std::string name = request.displayName.empty() ? probe.pattern : request.displayName;
    Asset asset;
    asset.kind = AssetKind::Video;
    asset.name = name;
    asset.sourcePath = recipe;
    asset.originalFilename = probe.pattern;
    asset.video.width = probe.width;
    asset.video.height = probe.height;
    asset.video.fps = fps;
    asset.timebaseFps = fps;
    asset.video.frameCount = FrameIndex{frames};
    asset.duration = asset.video.frameCount;
    asset.profile.bitDepth = static_cast<u8>(std::min<u32>(probe.bitDepth, 16));
    asset.profile.hdr = false;
    asset.profile.transfer = spec.color == SequenceColorMode::Linear ? ColorSpace::Unknown : ColorSpace::SRGB;
    asset.profile.primaries = ColorSpace::Rec709;
    asset.fileSizeBytes = probe.totalBytes;
    asset.audio.sampleRate = 0;   // sem som: nada de mixer, waveform ou "Extrair o áudio"
    asset.audio.channels = 0;
    const AssetId assetId = project_->add_asset(std::move(asset));

    const f64 compFps = comp->fps();
    const bool first = comp->layers().count() == 0;
    const i64 length = std::max<i64>(1, static_cast<i64>(std::ceil(durationUs * compFps / 1e6 - 1e-6)));
    const i64 endFrame = insertionFrame + length;
    if ((first && insertionFrame == 0) || endFrame > comp->duration().value) comp->set_duration(FrameIndex{endFrame});

    const LayerId lid = comp->add_layer(LayerKind::Video, name);
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    l->source = assetId;
    l->start = FrameIndex{insertionFrame};
    l->end = FrameIndex{endFrame};
    // Modo Edição: na linha magnética o trecho entra no fim dela, não no cabeçote.
    if (place_on_magnetic_line_locked(*comp, lid, length)) l = comp->layer(lid);
    const f32 w = static_cast<f32>(probe.width), h = static_cast<f32>(probe.height);
    l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    l->transform.position = Vec3{static_cast<f32>(comp->width()) * 0.5f, static_cast<f32>(comp->height()) * 0.5f, 0.0f};
    const f32 fit = std::min(static_cast<f32>(comp->width()) / w, static_cast<f32>(comp->height()) / h);
    l->transform.scale = Vec3{fit, fit, 1.0f};

    adapt().configure(comp->width(), comp->height(), static_cast<f32>(comp->fps()));
    configure_playback_locked(comp);
    project_->mark_dirty();
    request_render();
    AUREA_LOG_INFO("sequencia importada: %s, %lld quadros (%u ausentes), %ux%u, %u bits, %.3f fps, alfa %u, cor %u",
                   probe.pattern.c_str(), static_cast<long long>(frames), probe.missingCount, probe.width, probe.height,
                   probe.bitDepth, fps, static_cast<u32>(spec.alpha), static_cast<u32>(spec.color));
    return lid.pack();
} catch (const std::bad_alloc&) {
    return Status{Errc::OutOfMemory};
} catch (...) {
    return Status{Errc::IoError};
}

} // namespace aurea
