// =============================================================================
//  Aurea / engine / EnginePreRenderRange.cpp
//
//  Pre Render & Pre Export (1.0.2). O painel das duas interfaces escolhe um
//  trecho (Início/Fim/Inteiro/Limpar), uma qualidade (Inteira, 1/2, 1/4, 1/8)
//  e pede:
//    • PreRender: os quadros do trecho "assados" no cache privado
//      (PreRenderCache, RGBA16F), que a prévia toca no lugar do render ao vivo;
//    • pré-export: um MP4 do trecho pelo export de vídeo do motor, guardado em
//      `<cache>/pre-export`, que a reprodução toca no lugar da composição.
//  "Cabe ~Xs" sai de render/PreRenderPlan.hpp com o mesmo teto que o cache
//  aplica. O livro de edições por trecho (EditLedger) diz o que ainda vale
//  depois de uma edição: só os trechos tocados voltam ao vivo.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

namespace aurea {
namespace {
namespace fs = std::filesystem;

constexpr const char* kPreExportDir = "pre-export";
constexpr const char* kPreExportPrefix = "preexport-";

bool preexport_file(const fs::path& path) {
    const std::string name = path.filename().string();
    return name.rfind(kPreExportPrefix, 0) == 0 && path.extension() == ".mp4";
}

/// Livre no volume do cache (0 e `known` falso quando o sistema não diz).
u64 free_disk(const std::string& directory, bool& known) noexcept {
    known = false;
    if (directory.empty()) return 0;
    std::error_code ec;
    const fs::space_info sp = fs::space(fs::u8path(directory), ec);
    if (ec || sp.available == static_cast<std::uintmax_t>(-1)) return 0;
    known = true;
    return static_cast<u64>(sp.available);
}
} // namespace

// --- Livro por trecho ----------------------------------------------------------
void Engine::note_model_edit_locked() noexcept {
    if (!preRenderActive_.load(std::memory_order_acquire)) return;
    bool group = false;
    { std::lock_guard state(preRenderStateMutex_); group = preRenderGroup_; }
    const bool ours = !group && !preRenderSnapshot_.empty() && preRenderSnapshotSession_ == projectSession_
        && preRenderSnapshotGeneration_ == preRenderGeneration_.load(std::memory_order_acquire);
    if (ours && editLedger_.any_clean(preRenderSnapshot_)) {
        // Algum quadro ainda é o do retrato: a chave anda com a revisão e os
        // trechos tocados tocam ao vivo (prerender_frame_clean_locked).
        std::lock_guard state(preRenderStateMutex_);
        preRenderKey_.modelRevision = preview_cache_revision();
        return;
    }
    invalidate_prerender();
}

bool Engine::prerender_frame_clean_locked(i64 frame) const noexcept {
    if (preRenderSnapshot_.empty() || preRenderSnapshotSession_ != projectSession_
        || preRenderSnapshotGeneration_ != preRenderGeneration_.load(std::memory_order_acquire)) return true;
    return editLedger_.frame_clean(preRenderSnapshot_, frame);
}

prerender::RangeChoice Engine::prerender_range_locked(const Composition& comp) noexcept {
    const bool ours = preRangeSession_ == projectSession_ && preRangeComposition_ == comp.id().pack();
    return prerender::resolve_range(ours ? preRangeIn_ : -1, ours ? preRangeOut_ : -1, comp.has_work_area(),
                                    comp.work_in(), comp.work_out(), navigation_end_locked(&comp),
                                    comp.duration().value);
}

// --- Trecho ----------------------------------------------------------------------
bool Engine::prerender_range_action(u32 action) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return false;
    drain_commands_locked();
    Composition* comp = current_composition();
    if (!comp) return false;
    const bool ours = preRangeSession_ == projectSession_ && preRangeComposition_ == comp->id().pack();
    const i64 in = ours ? preRangeIn_ : -1, out = ours ? preRangeOut_ : -1;
    const i64 duration = std::max<i64>(1, comp->duration().value);
    const i64 contentEnd = std::clamp<i64>(navigation_end_locked(comp), 1, duration);
    const i64 playhead = editing_frame_locked().value;
    std::pair<i64, i64> next{-1, -1};
    switch (action) {
        case kRangeStart: next = prerender::range_with_start(playhead, in, out, contentEnd, duration); break;
        case kRangeEnd: next = prerender::range_with_end(playhead, in, out, duration); break;
        case kRangeFull: next = {0, contentEnd}; break;
        case kRangeClear: break;
        default: return false;
    }
    preRangeIn_ = next.first;
    preRangeOut_ = next.second;
    preRangeSession_ = projectSession_;
    preRangeComposition_ = comp->id().pack();
    configure_playback_locked(comp);
    request_render();
    return true;
}

bool Engine::set_prerender_range(i64 in, i64 out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return false;
    drain_commands_locked();
    Composition* comp = current_composition();
    if (!comp) return false;
    const i64 duration = std::max<i64>(1, comp->duration().value);
    if (in < 0 || out <= in) {
        preRangeIn_ = preRangeOut_ = -1;
    } else {
        preRangeIn_ = std::clamp<i64>(in, 0, duration - 1);
        preRangeOut_ = std::clamp<i64>(out, preRangeIn_ + 1, duration);
    }
    preRangeSession_ = projectSession_;
    preRangeComposition_ = comp->id().pack();
    configure_playback_locked(comp);
    request_render();
    return true;
}

PreRenderRangeInfo Engine::query_prerender_range() noexcept {
    PreRenderRangeInfo info;
    std::lock_guard<std::mutex> lock(modelMutex_);
    info.loop = preRangeLoop_;
    info.divisor = preRangeDivisor_;
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return info;
    const bool ours = preRangeSession_ == projectSession_ && preRangeComposition_ == comp->id().pack();
    if (ours && preRangeOut_ > preRangeIn_) { info.in = preRangeIn_; info.out = preRangeOut_; }
    const auto r = prerender_range_locked(*comp);
    info.effectiveIn = r.in;
    info.effectiveOut = r.out;
    info.fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
    return info;
}

void Engine::set_prerender_loop(bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    preRangeLoop_ = on;
    if (Composition* comp = project_ ? current_composition() : nullptr) configure_playback_locked(comp);
    request_render();
}

void Engine::set_prerender_quality(u32 divisor) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    preRangeDivisor_ = prerender::normalize_divisor(divisor);
}

// --- Cabe ~Xs ----------------------------------------------------------------------
PreRenderFitInfo Engine::query_prerender_fit(u32 divisor, u32 mode) noexcept {
    PreRenderFitInfo info;
    info.divisor = prerender::normalize_divisor(divisor);
    info.mode = mode ? 1u : 0u;
    u32 width = 0, height = 0;
    f64 fps = 30.0;
    i64 frames = 0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        const Composition* comp = project_ ? current_composition() : nullptr;
        if (!comp) return info;
        width = comp->width();
        height = comp->height();
        fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        const auto r = prerender_range_locked(*comp);
        frames = r.out - r.in;
    }
    // O MESMO teto do cache (PreRenderCache::begin): 4 GiB no celular, metade
    // do livre acima da reserva. O pré-export guarda um vídeo com o mesmo teto.
    const PreRenderCache::Limits defaults;
    prerender::Budget budget;
    budget.capBytes = std::min(defaults.diskBytes, PreRenderCache::max_disk_bytes());
    budget.reserveBytes = std::max(defaults.reserveFreeBytes, PreRenderCache::min_reserve_bytes());
    budget.reclaimableBytes = info.mode ? preexport_disk_bytes() : preRenderCache_->stats().diskBytes;
    budget.freeDiskBytes = free_disk(config_.cacheDirectory, budget.freeDiskKnown);
    const auto fit = prerender::plan_fit(width, height, fps, info.divisor,
        info.mode ? prerender::Mode::PreExport : prerender::Mode::PreRender, budget, frames,
        preRenderMeasuredBpp_.load(std::memory_order_acquire));
    info.width = fit.width;
    info.height = fit.height;
    info.fps = fit.fps;
    info.fitsSeconds = fit.fitsSeconds;
    info.rangeSeconds = fit.rangeSeconds;
    info.bytesPerSecond = fit.bytesPerSecond;
    info.availableBytes = fit.availableBytes;
    info.rangeBytes = fit.rangeBytes;
    info.fits = fit.fits;
    return info;
}

// --- PreRender do trecho -------------------------------------------------------------
Status Engine::start_prerender_range(u32 divisor) noexcept {
    divisor = prerender::normalize_divisor(divisor);
    if (config_.cacheDirectory.empty()) return Status{Errc::NotSupported, "PreRender precisa de cache privado"};
    PreRenderRangeRequest request;
    u32 shortSide = 0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_) return Errc::InvalidState;
        drain_commands_locked();
        const Composition* comp = current_composition();
        if (!comp) return Errc::NotFound;
        const auto r = prerender_range_locked(*comp);
        request.in = r.in;
        request.out = r.out;
        request.divisor = divisor;
        const auto size = prerender::scaled_size(comp->width(), comp->height(), divisor);
        shortSide = std::min(size.width, size.height);
        preRangeDivisor_ = divisor;
    }
    if (!shortSide || request.out <= request.in) return Errc::InvalidState;
    // Outro trecho/qualidade: o cache anterior (pronto ou andando) sai antes.
    invalidate_prerender();
    bool expected = false;
    if (!preRenderActive_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return Status{Errc::Busy, "PreRender em andamento"};
    preRenderCancel_.store(false, std::memory_order_release);
    preRenderGeneration_.fetch_add(1, std::memory_order_acq_rel);
    preRenderDivisor_.store(divisor, std::memory_order_release);
    {
        std::lock_guard lock(preRenderStateMutex_);
        preRenderProgress_ = {};
        preRenderProgress_.running = true;
        preRenderProgress_.direction = 1;
        preRenderProgress_.divisor = divisor;
        preRenderProgress_.firstFrame = request.in;
        preRenderProgress_.endFrame = request.out;
    }
    ImageExportSettings settings;
    settings.format = ImageExportFormat::PngSequence;
    settings.shortSide = shortSide;
    settings.trimToContent = false;
    const Status result = start_image_export_internal(settings, config_.cacheDirectory.c_str(), 1, 0, &request);
    if (!result.ok()) invalidate_prerender(result.raw());
    else AUREA_LOG_INFO("PreRender do trecho [%lld, %lld) em 1/%u", static_cast<long long>(request.in),
                        static_cast<long long>(request.out), divisor);
    return result;
}

// --- Pré-export ------------------------------------------------------------------------
void Engine::discard_preexport_file(const std::string& path) noexcept {
    std::error_code ec;
    if (!path.empty()) {
        fs::remove(fs::u8path(path), ec);
        return;
    }
    if (config_.cacheDirectory.empty()) return;
    const fs::path dir = fs::u8path(config_.cacheDirectory) / kPreExportDir;
    if (!fs::is_directory(dir, ec)) return;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code one;
        if (it->is_regular_file(one) && preexport_file(it->path())) fs::remove(it->path(), one);
    }
}

u64 Engine::preexport_disk_bytes() const noexcept {
    if (config_.cacheDirectory.empty()) return 0;
    std::error_code ec;
    const fs::path dir = fs::u8path(config_.cacheDirectory) / kPreExportDir;
    if (!fs::is_directory(dir, ec)) return 0;
    u64 total = 0;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code one;
        if (!it->is_regular_file(one)) continue;
        const auto size = it->file_size(one);
        if (!one) total += static_cast<u64>(size);
    }
    return total;
}

Status Engine::start_preexport(u32 divisor) noexcept {
    divisor = prerender::normalize_divisor(divisor);
    if (config_.cacheDirectory.empty()) return Status{Errc::NotSupported, "pre-export precisa de cache privado"};
    if (!config_.exportSinkFactory) return Status{Errc::NotSupported, "sem encoder de video nesta plataforma"};
    poll_preexport();
    {
        std::lock_guard state(preRenderStateMutex_);
        if (preExport_.running) return Status{Errc::Busy, "pre-export em andamento"};
    }
    PreExportRequest request;
    ExportSettings settings;
    u64 session = 0, composition = 0;
    f64 fps = 30.0;
    prerender::ScaledSize size;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_) return Errc::InvalidState;
        drain_commands_locked();
        const Composition* comp = current_composition();
        if (!comp) return Errc::NotFound;
        const auto r = prerender_range_locked(*comp);
        request.in = r.in;
        request.out = r.out;
        size = prerender::scaled_size(comp->width(), comp->height(), divisor);
        fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        session = projectSession_;
        composition = comp->id().pack();
        preRangeDivisor_ = divisor;
    }
    if (!size.width || request.out <= request.in) return Errc::InvalidState;
    // O export pede o LADO MENOR (a largura sai da proporção da composição).
    settings.width = std::max(size.width, size.height);
    settings.height = std::min(size.width, size.height);
    settings.fps = 0.0;
    settings.videoCodec = ExportCodec::H264;
    settings.videoBitrateMbps = 0;
    settings.quality = 2;              // taxa automática "Alta": o vídeo é para conferir a edição
    settings.trimToContent = false;    // o trecho já vem aparado (start_export_checked)
    // Um pré-export por vez no disco: o anterior sai antes do novo.
    std::string previous;
    {
        std::lock_guard state(preRenderStateMutex_);
        previous = preExport_.path;
        preExport_ = PreExportState{};
    }
    if (!previous.empty()) discard_preexport_file(previous);
    discard_preexport_file({});
    std::error_code ec;
    const fs::path dir = fs::u8path(config_.cacheDirectory) / kPreExportDir;
    fs::create_directories(dir, ec);
    if (ec) return Status{Errc::IoError, "nao foi possivel criar a pasta do pre-export"};
    request.id = preExportSerial_.fetch_add(1, std::memory_order_acq_rel) + 1;
    const fs::path file = dir / (std::string(kPreExportPrefix) + std::to_string(session) + "-"
                                 + std::to_string(request.id) + ".mp4");
    const auto utf8 = file.u8string();
    const std::string path(utf8.begin(), utf8.end());
    {
        std::lock_guard state(preRenderStateMutex_);
        preExport_.id = request.id;
        preExport_.running = true;
        preExport_.in = request.in;
        preExport_.out = request.out;
        preExport_.width = size.width;
        preExport_.height = size.height;
        preExport_.divisor = divisor;
        preExport_.fps = fps;
        preExport_.session = session;
        preExport_.composition = composition;
        preExport_.framesTotal = static_cast<u64>(request.out - request.in);
        preExport_.path = path;
    }
    preExportStarting_.store(true, std::memory_order_release);
    const Status started = start_export_checked(settings, path.c_str(), nullptr, nullptr, &request);
    preExportStarting_.store(false, std::memory_order_release);
    if (!started.ok()) {
        {
            std::lock_guard state(preRenderStateMutex_);
            if (preExport_.id == request.id) {
                preExport_.running = false;
                preExport_.error = started.raw();
                preExport_.path.clear();
            }
        }
        discard_preexport_file(path);
        AUREA_LOG_WARN("pre-export: partida recusada (%d)", started.raw());
        return started;
    }
    AUREA_LOG_INFO("pre-export do trecho [%lld, %lld) em %ux%u", static_cast<long long>(request.in),
                   static_cast<long long>(request.out), size.width, size.height);
    return started;
}

PreExportStatus Engine::query_preexport() noexcept {
    poll_preexport();
    PreExportStatus out;
    PreExportState st;
    {
        std::lock_guard state(preRenderStateMutex_);
        st = preExport_;
    }
    out.running = st.running;
    out.complete = st.complete;
    out.framesDone = st.framesDone;
    out.framesTotal = st.framesTotal;
    out.in = st.in;
    out.out = st.out;
    out.width = st.width;
    out.height = st.height;
    out.divisor = st.divisor;
    out.fps = st.fps;
    out.bytes = st.bytes;
    out.error = st.error;
    if (st.complete) {
        const usize n = std::min(st.path.size(), sizeof(out.path) - 1);
        std::memcpy(out.path, st.path.data(), n);
        out.path[n] = '\0';
    }
    if (st.running || st.complete) {
        std::lock_guard<std::mutex> lock(modelMutex_);
        const Composition* comp = project_ ? current_composition() : nullptr;
        const bool same = comp && st.session == projectSession_ && st.composition == comp->id().pack();
        out.dirtySegments = same ? editLedger_.dirty_segments(preExportSnapshot_) : 0u;
        out.stale = !same || out.dirtySegments > 0;
    }
    return out;
}

u32 Engine::query_prerender_dirty(u32 mode, i64* pairs, u32 maxPairs) noexcept {
    std::vector<std::pair<i64, i64>> ranges;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (mode) {
            bool alive = false;
            {
                std::lock_guard state(preRenderStateMutex_);
                alive = (preExport_.running || preExport_.complete) && preExport_.session == projectSession_;
            }
            if (alive) ranges = editLedger_.dirty_ranges(preExportSnapshot_);
        } else if (preRenderActive_.load(std::memory_order_acquire) && !preRenderSnapshot_.empty()
                   && preRenderSnapshotSession_ == projectSession_
                   && preRenderSnapshotGeneration_ == preRenderGeneration_.load(std::memory_order_acquire)) {
            ranges = editLedger_.dirty_ranges(preRenderSnapshot_);
        }
    }
    if (pairs) {
        const u32 n = std::min<u32>(maxPairs, static_cast<u32>(ranges.size()));
        for (u32 i = 0; i < n; ++i) {
            pairs[i * 2] = ranges[i].first;
            pairs[i * 2 + 1] = ranges[i].second;
        }
    }
    return static_cast<u32>(ranges.size());
}

} // namespace aurea
