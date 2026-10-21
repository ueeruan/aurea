#include "aurea/media/PreviewProxy.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/media/ImageSequence.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Log.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace aurea {
namespace {
namespace fs = std::filesystem;
constexpr u64 kMaxFrames = 2'000'000;
constexpr usize kMaxQueue = 64;
/// Espaço livre que o cache de proxies nunca consome (galeria, câmera, export).
constexpr u64 kFreeSpaceReserve = 1ull << 30;
constexpr u32 kTagAvc1 = 0x61766331u, kTagHvc1 = 0x68766331u, kTagHev1 = 0x68657631u,
              kTagAv01 = 0x61763031u, kTagVp09 = 0x76703039u;

std::string fingerprint(const Asset& a, u32 target, const std::string& identity = {}) {
    // "v3" (0.2.7): refaz as mídias otimizadas da 0.2.6 (relatos de vídeo verde;
    // um arquivo ruim no cache era reaproveitado para sempre).
    // "v2" (0.2.5): GOP curto e dizimação acima de 60 fps. Arquivos do formato
    // anterior (GOP de 1 s, tamanho por classe) não são reaproveitados.
    std::string input = a.sourcePath + ':' + std::to_string(a.contentHash) + ':' + std::to_string(a.fileSizeBytes) + ':' + std::to_string(target) + ":v3";
    input += ':' + identity;
    u64 h = 1469598103934665603ull;
    for (unsigned char c : input) { h ^= c; h *= 1099511628211ull; }
    std::ostringstream out; out << std::hex << h;
    return out.str();
}

// Faixa da fonte de cada coordenada de saída num eixo, com pesos de área em
// 1/256 de pixel. Montada uma vez por plano e quadro: o laço por amostra só
// soma inteiros (antes eram f64, mín/máx e arredondamento a cada tap — o 4K
// custava dezenas de ms por quadro na CPU do aparelho).
struct AxisTaps {
    std::vector<u32> first, count, start, total, weight;
    void build(u32 n, f64 origin, f64 extent, bool reverse, f64 scale) {
        first.assign(n, 0); count.assign(n, 0); start.assign(n, 0); total.assign(n, 1); weight.clear();
        for (u32 i = 0; i < n; ++i) {
            f64 u0 = static_cast<f64>(i) / n, u1 = static_cast<f64>(i + 1) / n;
            if (reverse) { const f64 a = 1 - u1; u1 = 1 - u0; u0 = a; }
            const f64 s0 = (origin + u0 * extent) * scale, s1 = (origin + u1 * extent) * scale;
            const u32 lo = static_cast<u32>(s0), hi = static_cast<u32>(std::ceil(s1));
            first[i] = lo; start[i] = static_cast<u32>(weight.size());
            u32 sum = 0;
            for (u32 k = lo; k < hi; ++k) {
                const f64 w = std::min(s1, k + 1.0) - std::max(s0, static_cast<f64>(k));
                const u32 q = static_cast<u32>(std::lround(std::max(0.0, w) * 256.0));
                weight.push_back(q); sum += q;
            }
            count[i] = hi > lo ? hi - lo : 0; total[i] = std::max(1u, sum);
        }
    }
};

template <bool Ten>
inline u32 sample_at(const u8* p) noexcept {
    if constexpr (Ten) return static_cast<u32>(p[0] | (p[1] << 8)) >> 6;
    else return p[0];
}

// Um plano de saída. `swap` (rotação 90/270): a linha da fonte vem do x de
// saída e a coluna do y. Valores de 10 bits voltam a 8 na mesma faixa.
template <bool Ten>
void resample_plane(const u8* base, usize stride, u32 step, u32 component, const AxisTaps& tx, const AxisTaps& ty,
                    bool swap, u32 w, u32 h, bool fullRange, u8* out, u32 outStep) {
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            const AxisTaps& rows = swap ? tx : ty;
            const AxisTaps& cols = swap ? ty : tx;
            const u32 ri = swap ? x : y, ci = swap ? y : x;
            const u32* cw = cols.weight.data() + cols.start[ci];
            const u32* rw = rows.weight.data() + rows.start[ri];
            const u32 cn = cols.count[ci], rn = rows.count[ri];
            const usize col0 = static_cast<usize>(cols.first[ci]) * step + component;
            u64 sum = 0;
            for (u32 r = 0; r < rn; ++r) {
                if (!rw[r]) continue;
                const u8* p = base + static_cast<usize>(rows.first[ri] + r) * stride + col0;
                u64 line = 0;
                for (u32 c = 0; c < cn; ++c) line += static_cast<u64>(cw[c]) * sample_at<Ten>(p + static_cast<usize>(c) * step);
                sum += line * rw[r];
            }
            const u64 total = static_cast<u64>(rows.total[ri]) * cols.total[ci];
            u64 value;
            if constexpr (Ten) value = fullRange ? (sum * 255 + total * 1023 / 2) / (total * 1023) : (sum + total * 2) / (total * 4);
            else value = (sum + total / 2) / total;
            out[(static_cast<usize>(y) * w + x) * outStep] = static_cast<u8>(std::min<u64>(value, 255));
        }
    }
}

class ProxyDecoder final : public VideoDecoderBackend {
    std::unique_ptr<VideoDecoderBackend> decoder_;
    std::shared_ptr<const PreviewProxy> proxy_;
    VideoStreamInfo info_;
public:
    ProxyDecoder(std::unique_ptr<VideoDecoderBackend> d, std::shared_ptr<const PreviewProxy> p)
        : decoder_(std::move(d)), proxy_(std::move(p)), info_(decoder_->info()) {
        info_.durationUs = proxy_->original.durationUs;
        // A grade de prefetch segue os quadros que existem no arquivo (60 numa
        // fonte de 120); o tempo de cada um vem dos carimbos da fonte.
        info_.fps = proxy_->fps > 0 ? proxy_->fps : proxy_->original.fps;
        info_.rotation = 0; info_.color = proxy_->original.color; info_.color.bitDepth = 8;
        info_.preciseFrameTiming = true;
    }
    const VideoStreamInfo& info() const noexcept override { return info_; }
    Status seek_to_keyframe(i64 us) noexcept override { return decoder_->seek_to_keyframe(std::max<i64>(0, us - proxy_->times.front())); }
    Status next_frame(i64 from, FrameRef& out, i64& pts, bool& eos) noexcept override {
        const auto status = decoder_->next_frame(from < 0 ? from : from - proxy_->times.front(), out, pts, eos);
        pts += proxy_->times.front();
        if (!status.ok() || !out) return status;
        auto it = std::lower_bound(proxy_->times.begin(), proxy_->times.end(), pts);
        if (it == proxy_->times.end() || (it != proxy_->times.begin() && pts - *(it - 1) < *it - pts)) --it;
        const usize index = static_cast<usize>(it - proxy_->times.begin());
        out->ptsUs = pts = *it; out->durationUs = proxy_->durations[index];
        const u8 decodedDepth = out->color.bitDepth;
        out->color = info_.color; out->color.bitDepth = decodedDepth;
        if (out->format == PixelFormat::RGBA8 || out->format == PixelFormat::RGBA16F) out->color.fullRange = true;
        out->rotation = 0;
        return status;
    }
    u32 max_live_frames() const noexcept override { return decoder_->max_live_frames(); }
    i64 keyframe_interval_us() const noexcept override { return decoder_->keyframe_interval_us(); }
    void suspend() noexcept override { decoder_->suspend(); }
    Status resume() noexcept override { return decoder_->resume(); }
};

bool write_manifest(const PreviewProxy& p, const std::string& path) {
    std::ofstream out(fs::u8path(path), std::ios::trunc);
    out << "AUREA_PROXY_2\n" << p.width << ' ' << p.height << ' ' << std::setprecision(17) << p.original.fps << ' '
        << p.original.durationUs << ' ' << p.fps << '\n';
    const auto& c = p.original.color;
    out << static_cast<int>(c.matrix) << ' ' << c.fullRange << ' ' << static_cast<int>(c.primaries) << ' ' << static_cast<int>(c.transfer) << '\n';
    out << p.times.size() << '\n';
    for (usize i = 0; i < p.times.size(); ++i) out << p.times[i] << ' ' << p.durations[i] << '\n';
    out.close(); return out.good();
}
std::shared_ptr<PreviewProxy> read_manifest(const std::string& path) {
    std::ifstream in(fs::u8path(path + ".pts"));
    std::string magic; in >> magic;
    const bool v2 = magic == "AUREA_PROXY_2";
    if (!v2 && magic != "AUREA_PROXY_1") return {};
    auto p = std::make_shared<PreviewProxy>(); p->path = path;
    int matrix, full, primaries, transfer; u64 count;
    in >> p->width >> p->height >> p->original.fps >> p->original.durationUs;
    if (v2) in >> p->fps; else p->fps = p->original.fps;
    in >> matrix >> full >> primaries >> transfer >> count;
    if (!in || p->width < 2 || p->height < 2 || p->width > 8192 || p->height > 8192 ||
        !std::isfinite(p->original.fps) || p->original.fps <= 0 || !std::isfinite(p->fps) || p->fps <= 0 ||
        count == 0 || count > kMaxFrames ||
        matrix < 0 || matrix > 2 || primaries < 0 || primaries > 3 || transfer < 0 || transfer > 3) return {};
    p->original.color.matrix = static_cast<YCbCrMatrix>(matrix); p->original.color.fullRange = full != 0;
    p->original.color.primaries = static_cast<ColorPrimaries>(primaries); p->original.color.transfer = static_cast<TransferFunction>(transfer);
    p->original.color.bitDepth = 8;
    for (u64 i = 0; i < count; ++i) {
        i64 time, duration; in >> time >> duration;
        if (!in || duration <= 0 || (i && time <= p->times.back())) return {};
        p->times.push_back(time); p->durations.push_back(duration);
    }
    return p;
}
} // namespace

// =============================================================================
// Política
// =============================================================================
bool proxy_complex_codec(u32 tag) noexcept {
    return tag == kTagHvc1 || tag == kTagHev1 || tag == kTagAv01 || tag == kTagVp09;
}

u32 proxy_codec_tag(const char* mime) noexcept {
    if (!mime || !*mime) return 0;
    if (std::strstr(mime, "hevc") || std::strstr(mime, "hvc1") || std::strstr(mime, "hev1")) return kTagHvc1;
    if (std::strstr(mime, "av01") || std::strstr(mime, "av1")) return kTagAv01;
    if (std::strstr(mime, "vp9") || std::strstr(mime, "vp09")) return kTagVp09;
    if (std::strstr(mime, "avc") || std::strstr(mime, "h264")) return kTagAvc1;
    return 0;
}

u64 proxy_source_bitrate(const Asset& a) noexcept {
    if (!a.fileSizeBytes || !(a.video.fps > 0) || a.video.frameCount.value <= 0) return 0;
    const f64 seconds = static_cast<f64>(a.video.frameCount.value) / a.video.fps;
    // Clipe muito curto: cabeçalho e áudio dominam o arquivo; a média mente.
    if (!(seconds >= 1.0)) return 0;
    return static_cast<u64>(static_cast<f64>(a.fileSizeBytes) * 8.0 / seconds);
}

bool proxy_source_heavy(const Asset& a, const ProxyThresholds& t) noexcept {
    // Sequência de imagens: o proxy é um MP4 (perderia o alfa) e o decoder
    // dela já reduz e antecipa os quadros na CPU.
    if (a.kind != AssetKind::Video || !a.has_video() || is_image_sequence(a.sourcePath)) return false;
    const u64 pixels = static_cast<u64>(a.video.width) * a.video.height;
    if (pixels > t.heavyPixels) return true;
    // 59,94 e 60,0x não contam como "acima de 60".
    if (a.video.fps > t.maxFps + 0.5) return true;
    if (pixels >= t.heavyPixels && (a.profile.bitDepth > 8 || a.profile.hdr || proxy_complex_codec(a.profile.codecTag))) return true;
    const u64 bitrate = proxy_source_bitrate(a);
    return bitrate > t.heavyBitrateBps;
}

bool proxy_wanted(const Asset& a, ProxyMode mode, bool reducedPreview, const ProxyThresholds& t) noexcept {
    if (mode == ProxyMode::Off || a.kind != AssetKind::Video || !a.has_video() || is_image_sequence(a.sourcePath)) return false;
    if (proxy_source_heavy(a, t)) return true;
    const u64 pixels = static_cast<u64>(a.video.width) * a.video.height;
    return (mode == ProxyMode::Always || reducedPreview) && pixels > t.alwaysPixels;
}

f64 proxy_target_fps(f64 sourceFps, f64 maxFps) noexcept {
    if (!std::isfinite(sourceFps) || sourceFps <= 0) return 30.0;
    return sourceFps > maxFps + 0.5 ? maxFps : sourceFps;
}

VideoStreamConfig proxy_stream_config(u32 width, u32 height, f64 fps) noexcept {
    VideoStreamConfig config;
    config.width = width; config.height = height; config.fps = fps;
    config.codec = ExportCodec::H264;
    // Keyframe a cada ~0,5 s: buscar e arrastar decodificam poucos quadros.
    config.keyframeIntervalFrames = static_cast<u32>(std::max<i64>(1, std::lround(fps * kProxyKeyframeSeconds)));
    // ~0,1 bit por pixel: 720p30 ≈ 2,8 Mbps, 720p60 ≈ 5,5 Mbps. É prévia de edição.
    config.bitrateBps = std::clamp(static_cast<u32>(width * static_cast<f64>(height) * std::min(fps, 60.0) * 0.1),
                                   1'500'000u, 8'000'000u);
    config.rateMode = 1;
    return config;
}

ProxyFrameDecimator::ProxyFrameDecimator(f64 sourceFps, f64 targetFps) noexcept {
    decimate_ = std::isfinite(sourceFps) && std::isfinite(targetFps) && targetFps > 0 && sourceFps > targetFps + 0.5;
    if (decimate_) { stepUs_ = 1e6 / targetFps; halfSourceUs_ = 0.5e6 / sourceFps; }
}

bool ProxyFrameDecimator::keep(i64 ptsUs) noexcept {
    if (!decimate_) return true;
    const f64 t = static_cast<f64>(ptsUs);
    if (started_ && t + halfSourceUs_ < nextUs_) return false;
    if (!started_) { started_ = true; nextUs_ = t; }
    // O próximo passo da grade fica além do quadro guardado, mesmo com jitter.
    do nextUs_ += stepUs_; while (nextUs_ <= t + halfSourceUs_);
    return true;
}

bool proxy_frame_nv12(const DecodedFrame& f, u32 width, u32 height, std::vector<u8>& pixels) {
    const bool planar = f.format == PixelFormat::YUV420P, ten = f.format == PixelFormat::P010;
    if (f.format != PixelFormat::NV12 && f.format != PixelFormat::NV21 && !planar && !ten) return false;
    if (!width || !height || (width & 1) || (height & 1) || width > 8192 || height > 8192 ||
        !f.width || !f.height || f.planeCount < (planar ? 3u : 2u) || !f.planes[0] || !f.planes[1] || (planar && !f.planes[2])) return false;
    const u32 vw = f.visibleWidth ? f.visibleWidth : f.width, vh = f.visibleHeight ? f.visibleHeight : f.height;
    if (static_cast<u64>(f.cropLeft) + vw > f.width || static_cast<u64>(f.cropTop) + vh > f.height ||
        f.strides[0] < static_cast<u64>(f.width) * (ten ? 2 : 1) ||
        f.strides[1] < static_cast<u64>((f.width + 1) / 2) * (planar ? 1 : ten ? 4 : 2) ||
        (planar && f.strides[2] < (f.width + 1) / 2)) return false;
    pixels.resize(static_cast<usize>(width) * height * 3 / 2);
    // Eixo de saída → eixo da fonte (antes da rotação do contêiner).
    const bool swap = f.rotation == 90 || f.rotation == 270;
    const bool reverseX = f.rotation == 90 || f.rotation == 180;
    const bool reverseY = f.rotation == 180 || f.rotation == 270;
    const f64 ox = swap ? f.cropTop : f.cropLeft, ex = swap ? vh : vw;
    const f64 oy = swap ? f.cropLeft : f.cropTop, ey = swap ? vw : vh;
    const u32 bytes = ten ? 2 : 1;
    AxisTaps tx, ty;
    for (u32 channel = 0; channel < 3; ++channel) {
        const u32 w = channel ? width / 2 : width, h = channel ? height / 2 : height;
        // Cr usa as mesmas tabelas do Cb (mesmo tamanho e meia escala).
        if (channel < 2) {
            const f64 scale = channel ? 0.5 : 1.0;
            tx.build(w, ox, ex, reverseX, scale);
            ty.build(h, oy, ey, reverseY, scale);
        }
        const u32 plane = channel == 0 ? 0 : planar ? channel : 1;
        const u32 component = channel == 0 || planar ? 0 : (f.format == PixelFormat::NV21 ? 2 - channel : channel - 1);
        const u32 step = (channel == 0 || planar ? 1 : 2) * bytes;
        u8* out = channel ? pixels.data() + static_cast<usize>(width) * height + channel - 1 : pixels.data();
        const u32 outStep = channel ? 2 : 1;
        if (ten) resample_plane<true>(f.planes[plane], f.strides[plane], step, component * bytes, tx, ty, swap, w, h, f.color.fullRange, out, outStep);
        else resample_plane<false>(f.planes[plane], f.strides[plane], step, component * bytes, tx, ty, swap, w, h, f.color.fullRange, out, outStep);
    }
    return true;
}

std::unique_ptr<VideoDecoderBackend> proxy_decoder(std::unique_ptr<VideoDecoderBackend> decoder, std::shared_ptr<const PreviewProxy> proxy) {
    if (!decoder || !proxy || proxy->times.empty() || proxy->times.size() != proxy->durations.size()) return {};
    return std::make_unique<ProxyDecoder>(std::move(decoder), std::move(proxy));
}

// =============================================================================
// Serviço
// =============================================================================
PreviewProxyService::~PreviewProxyService() { stop(); }
void PreviewProxyService::configure(VideoSourceFactory* media, ExportSinkFactory sink, void* context, std::string directory, u64 budget,
                                    std::string (*resolve)(const std::string&, void*), void* resolveContext) {
    stop();
    media_ = media; sink_ = sink; context_ = context; directory_ = std::move(directory); diskBudget_ = budget;
    resolve_ = resolve; resolveContext_ = resolveContext;
    stopping_.store(false);
    paused_.store(false); holding_.store(false);
    pauseReasons_ = 0;
    if (media_ && sink_ && !directory_.empty()) worker_ = std::thread([this] { run(); });
}
void PreviewProxyService::set_policy(u32 shortSide, u32 pacing) noexcept { shortSide_.store(shortSide); pacingMs_.store(pacing); }
void PreviewProxyService::set_thresholds(const ProxyThresholds& thresholds) noexcept {
    std::lock_guard<std::mutex> lock(mutex_); thresholds_ = thresholds;
}
void PreviewProxyService::set_ready_callback(void (*fn)(void*), void* context) noexcept {
    std::lock_guard<std::mutex> lock(mutex_); readyFn_ = fn; readyCtx_ = context;
}
void PreviewProxyService::set_mode(ProxyMode mode) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (mode_.exchange(mode) == mode) return;
        prune_unwanted_locked(true);
    }
    wake_.notify_all();
}
void PreviewProxyService::set_reduced_preview(bool reduced) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (reduced_.exchange(reduced) == reduced) return;
        // Voltar à prévia cheia só tira da fila o que nem começou: o arquivo
        // em curso termina e serve na próxima vez que a escala baixar.
        if (!reduced) prune_unwanted_locked(false);
    }
    wake_.notify_all();
}
void PreviewProxyService::prune_unwanted_locked(bool interruptCurrent) {
    const ProxyMode mode = mode_.load();
    const bool reduced = reduced_.load();
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (proxy_wanted(it->asset, mode, reduced, thresholds_)) { ++it; continue; }
        if (auto e = entries_.find(it->key); e != entries_.end() && e->second.state != ProxyState::Ready) entries_.erase(e);
        it = queue_.erase(it);
    }
    if (!interruptCurrent || current_.empty()) return;
    // O asset em geração já saiu da fila: confere o que está sendo gerado.
    bool still = false;
    if (auto e = entries_.find(current_); e != entries_.end()) still = e->second.state == ProxyState::Generating
        && proxy_wanted(currentAsset_, mode, reduced, thresholds_);
    if (!still) { entries_.erase(current_); cancelCurrent_.store(true); }
}
void PreviewProxyService::set_paused(bool paused) noexcept {
    set_pause_reason(Manual, paused);
}
void PreviewProxyService::set_pause_reason(PauseReason reason, bool paused) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (paused) pauseReasons_ |= reason; else pauseReasons_ &= ~static_cast<u32>(reason);
        holding_.store((pauseReasons_ & Playback) != 0);
        const bool blocked = (pauseReasons_ & ~static_cast<u32>(Playback)) != 0;
        // Interrompe o arquivo em curso (codecs e CPU voltam para quem pediu a
        // pausa); a fila e os prontos ficam, e o interrompido volta à frente.
        if (paused_.exchange(blocked) != blocked && blocked) ++generation_;
    }
    wake_.notify_all();
}
void PreviewProxyService::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_); ++generation_; queue_.clear(); entries_.clear(); wake_.notify_all();
}
void PreviewProxyService::stop() noexcept {
    // Publish with the same mutex as wait_for's predicate. Otherwise stop can
    // notify between its predicate check and sleep, leaving join blocked for
    // the entire pacing interval.
    { std::lock_guard<std::mutex> lock(mutex_); stopping_.store(true); }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    clear();
}
bool PreviewProxyService::cancelled(u64 generation) const noexcept {
    return stopping_.load() || paused_.load() || generation != generation_.load() || cancelCurrent_.load();
}

bool PreviewProxyService::queue_locked(const Asset& asset, const std::string& key, u32 target, bool front, bool force) {
    if (queue_.size() >= kMaxQueue) return false;
    Entry& e = entries_[key];
    e.state = ProxyState::Queued; e.name = asset.name;
    Request r{asset, key, target, thresholds_.maxFps, 0, false, force};
    if (front) queue_.push_front(std::move(r)); else queue_.push_back(std::move(r));
    return true;
}

std::shared_ptr<const PreviewProxy> PreviewProxyService::request(const Asset& asset) {
    const u32 target = shortSide_.load();
    if (!target || !worker_.joinable()) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    if (!proxy_wanted(asset, mode_.load(), reduced_.load(), thresholds_)) return {};
    const std::string key = fingerprint(asset, target);
    if (auto it = entries_.find(key); it != entries_.end()) {
        const Entry& e = it->second;
        if (e.state == ProxyState::Ready) return e.proxy;
        if (e.dismissed || e.state == ProxyState::Queued || e.state == ProxyState::Generating) return {};
        if (e.state == ProxyState::Failed && (e.failures >= kProxyMaxFailures || std::chrono::steady_clock::now() < e.retryAt)) return {};
    }
    if (queue_locked(asset, key, target, false, false)) wake_.notify_one();
    return {};
}

ProxyStatus PreviewProxyService::status(const Asset& asset) {
    ProxyStatus s;
    const u32 target = shortSide_.load();
    if (!target || !worker_.joinable()) return s;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!proxy_wanted(asset, mode_.load(), reduced_.load(), thresholds_)) return s;
    const auto it = entries_.find(fingerprint(asset, target));
    if (it == entries_.end() || it->second.dismissed) return s;
    s.state = it->second.state;
    if (s.state == ProxyState::Generating) s.progress = progress_.load();
    return s;
}

ProxySummary PreviewProxyService::summary() {
    ProxySummary s;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [key, e] : entries_) {
        if (e.dismissed) continue;
        switch (e.state) {
            case ProxyState::Queued: ++s.queued; break;
            case ProxyState::Generating: ++s.generating; s.progress = progress_.load(); s.name = e.name; break;
            case ProxyState::Ready: ++s.ready; break;
            case ProxyState::Failed: ++s.failed; break;
            case ProxyState::None: break;
        }
    }
    return s;
}

bool PreviewProxyService::cancel(const Asset& asset) {
    const u32 target = shortSide_.load();
    if (!target) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string key = fingerprint(asset, target);
        Entry& e = entries_[key];
        if (e.state == ProxyState::Ready) return false;
        e.dismissed = true; e.state = ProxyState::None; e.name = asset.name;
        std::erase_if(queue_, [&](const Request& r) { return r.key == key; });
        if (current_ == key) cancelCurrent_.store(true);
    }
    wake_.notify_all();
    return true;
}

bool PreviewProxyService::regenerate(const Asset& asset) {
    const u32 target = shortSide_.load();
    if (!target || !worker_.joinable()) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!proxy_wanted(asset, mode_.load(), reduced_.load(), thresholds_)) return false;
        const std::string key = fingerprint(asset, target);
        std::erase_if(queue_, [&](const Request& r) { return r.key == key; });
        if (current_ == key) cancelCurrent_.store(true);
        Entry& e = entries_[key];
        e = Entry{};
        // À frente da fila; a fila cheia não recusa um pedido explícito.
        e.state = ProxyState::Queued; e.name = asset.name;
        queue_.push_front(Request{asset, key, target, thresholds_.maxFps, 0, true, true});
    }
    wake_.notify_all();
    return true;
}

u64 PreviewProxyService::cache_bytes() const {
    if (directory_.empty()) return 0;
    std::error_code ec;
    u64 total = 0;
    for (fs::directory_iterator it(fs::u8path(directory_), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileError;
        if (!it->is_regular_file(fileError)) continue;
        const u64 size = it->file_size(fileError);
        if (!fileError) total += size;
    }
    return total;
}

u64 PreviewProxyService::clear_cache() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_; queue_.clear(); entries_.clear();
    }
    wake_.notify_all();
    if (directory_.empty()) return 0;
    // Decoder aberto num proxy segue lendo o arquivo desvinculado (POSIX) até
    // trocar de fonte; nada aqui espera o render.
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(directory_), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileError;
        if (it->is_regular_file(fileError)) files.push_back(it->path());
    }
    u64 freed = 0;
    for (const fs::path& file : files) {
        std::error_code fileError;
        u64 size = fs::file_size(file, fileError);
        if (fileError) { size = 0; fileError.clear(); }
        if (fs::remove(file, fileError) && !fileError) freed += size;
    }
    AUREA_LOG_INFO("proxy: cache limpo, %llu bytes", static_cast<unsigned long long>(freed));
    return freed;
}

bool PreviewProxyService::make_room(u64 bytes, const std::string& preserve) {
    if (bytes > diskBudget_) return false;
    std::error_code ec;
    fs::create_directories(fs::u8path(directory_), ec);
    if (ec) return false;
    struct File { fs::path path; fs::file_time_type time; u64 bytes; };
    std::vector<File> files; u64 used = 0;
    for (fs::directory_iterator it(fs::u8path(directory_), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".mp4") continue;
        if (it->path().generic_string() == preserve) continue;
        u64 size = it->file_size(ec); if (ec) return false;
        const auto metadataBytes = fs::file_size(fs::u8path(it->path().generic_string() + ".pts"), ec);
        if (!ec) size += metadataBytes;
        ec.clear();
        used += size; files.push_back({it->path(), it->last_write_time(ec), size});
    }
    if (ec) return false;
    // O orçamento nunca come o espaço livre do aparelho: sobra sempre 1 GB.
    // O arquivo preservado (o parcial pronto) já saiu do espaço livre.
    u64 budget = diskBudget_;
    if (const auto space = fs::space(fs::u8path(directory_), ec); !ec) {
        u64 available = space.available;
        if (!preserve.empty()) { const u64 own = fs::file_size(fs::u8path(preserve), ec); if (!ec) available += own; }
        const u64 room = available > kFreeSpaceReserve ? available - kFreeSpaceReserve : 0;
        budget = std::min(budget, used + room);
    }
    ec.clear();
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.time < b.time; });
    for (const File& file : files) {
        if (used + bytes <= budget) break;
        const std::string path = file.path.generic_string();
        if (path == preserve) continue;
        bool pinned = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& item : entries_) if (item.second.proxy && item.second.proxy->path == path && item.second.proxy.use_count() > 1) pinned = true;
            if (!pinned) std::erase_if(entries_, [&](const auto& item) { return item.second.proxy && item.second.proxy->path == path; });
        }
        if (pinned) continue;
        if (fs::remove(file.path, ec)) {
            used -= file.bytes;
            fs::remove(fs::u8path(path + ".pts"), ec); ec.clear();
        }
        ec.clear();
    }
    return used + bytes <= budget;
}

void PreviewProxyService::run() {
    set_current_thread_name("aurea-proxy"); set_current_thread_priority(ThreadPriority::Background);
    for (;;) {
        Request request;
        bool lookup = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // Consultar o disco é barato: um proxy já gerado (outra sessão) vale
            // antes de qualquer geração nova, e mesmo durante o play.
            auto unchecked = [&] { return std::find_if(queue_.begin(), queue_.end(), [](const Request& r) { return !r.checked && !r.force; }); };
            wake_.wait(lock, [&] {
                if (stopping_.load()) return true;
                if (paused_.load() || queue_.empty()) return false;
                return !holding_.load() || unchecked() != queue_.end();
            });
            if (stopping_.load()) return;
            auto it = unchecked();
            lookup = it != queue_.end();
            if (!lookup) it = queue_.begin();
            request = std::move(*it); queue_.erase(it);
            request.generation = generation_.load();
            if (!lookup) {
                current_ = request.key; currentAsset_ = request.asset;
                progress_.store(0.0f); cancelCurrent_.store(false);
                if (auto e = entries_.find(request.key); e != entries_.end()) e->second.state = ProxyState::Generating;
            }
        }
        auto proxy = lookup ? cached(request) : generate(request);
        void (*ready)(void*) = nullptr;
        void* readyContext = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const bool interrupted = request.generation != generation_.load() || stopping_.load() || (!lookup && cancelCurrent_.load());
            if (!lookup) { current_.clear(); cancelCurrent_.store(false); }
            auto e = entries_.find(request.key);
            if (e == entries_.end()) {
                // Limpo, podado ou o projeto mudou: nada a publicar.
            } else if (lookup) {
                if (e->second.state == ProxyState::Queued) {
                    if (proxy && !interrupted) { e->second.proxy = std::move(proxy); e->second.state = ProxyState::Ready; ready = readyFn_; readyContext = readyCtx_; }
                    else { request.checked = !interrupted; queue_.push_back(std::move(request)); }
                }
            } else if (e->second.state == ProxyState::Generating) {
                if (interrupted) {
                    // Pausa (fundo, export, calor, captura): recomeça na frente da fila.
                    e->second.state = ProxyState::Queued; request.checked = true;
                    queue_.push_front(std::move(request));
                } else if (proxy) {
                    e->second.proxy = std::move(proxy); e->second.state = ProxyState::Ready; e->second.failures = 0;
                    ready = readyFn_; readyContext = readyCtx_;
                } else {
                    e->second.state = ProxyState::Failed; ++e->second.failures;
                    e->second.retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                }
            }
        }
        // O render troca a fonte no próximo quadro (MediaManager::source_for).
        if (ready) ready(readyContext);
    }
}

std::shared_ptr<PreviewProxy> PreviewProxyService::cached(const Request& request) {
    Asset resolved = request.asset;
    if (resolve_) resolved.sourcePath = resolve_(resolved.sourcePath, resolveContext_);
    const std::string identity = media_->cache_identity(resolved.sourcePath.c_str());
    if (identity.empty()) return {};
    const std::string path = (fs::u8path(directory_) / (fingerprint(resolved, request.target, identity) + ".mp4")).generic_string();
    auto proxy = read_manifest(path);
    if (!proxy) return {};
    std::error_code ec;
    const u64 size = fs::file_size(fs::u8path(path), ec);
    if (ec || size == 0) return {};
    MediaProbe probe;
    if (!media_->probe(path.c_str(), probe) || !probe.hasVideo || probe.video.display_width() != proxy->width
        || probe.video.display_height() != proxy->height) return {};
    // Uso conta como recente: o despejo por idade (make_room) poupa o que serve.
    fs::last_write_time(fs::u8path(path), fs::file_time_type::clock::now(), ec);
    return proxy;
}

std::shared_ptr<PreviewProxy> PreviewProxyService::generate(const Request& request) {
    Asset resolved = request.asset;
    if (resolve_) resolved.sourcePath = resolve_(resolved.sourcePath, resolveContext_);
    const std::string identity = media_->cache_identity(resolved.sourcePath.c_str());
    const std::string session = "session:" + std::to_string(static_cast<i64>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string hash = fingerprint(resolved, request.target, identity.empty() ? session : identity);
    if (hash.empty() || cancelled(request.generation)) return {};
    const std::string path = (fs::u8path(directory_) / (hash + ".mp4")).generic_string();
    std::error_code ec;
    if (request.force) {
        fs::remove(fs::u8path(path), ec); ec.clear();
        fs::remove(fs::u8path(path + ".pts"), ec); ec.clear();
    } else if (!request.checked) {
        if (auto hit = cached(request)) return hit;
    }
    auto decoder = media_->open_video(request.asset, MediaPriority::Thumbnail);
    if (!decoder || !decoder->seek_to_keyframe(0).ok() || cancelled(request.generation)) {
        AUREA_LOG_WARN("proxy: decoder indisponivel, seek falhou ou tarefa cancelada"); return {};
    }
    auto result = std::make_shared<PreviewProxy>(); result->path = path; result->original = decoder->info();
    result->fps = proxy_target_fps(result->original.fps, request.maxFps);
    ProxyFrameDecimator decimator(result->original.fps, result->fps);
    const std::string partial = path + ".partial.mp4";
    std::unique_ptr<ExportSink> sink;
    std::vector<u8> pixels, pendingPixels;
    u32 empty = 0;
    bool eos = false, success = false;
    i64 lastPts = std::numeric_limits<i64>::min(), lastEnd = 0;
    const i64 durationUs = std::max<i64>(1, result->original.durationUs);
    // Quadro sem duração (decoder antigo): um intervalo nominal da fonte.
    const i64 nominalUs = result->original.fps > 0 ? std::max<i64>(1, std::llround(1e6 / result->original.fps)) : 33'333;
    while (!cancelled(request.generation) && result->times.size() < kMaxFrames) {
        const auto started = std::chrono::steady_clock::now();
        auto pace = [&] {
            // Proxy preparation is optional background work. A fixed 1 ms gap
            // lets a costly 4K resample consume a core continuously (iOS CPU
            // exception); allow at most 25% wall-time duty for this worker.
            const auto work = std::chrono::steady_clock::now() - started;
            const auto floor = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::milliseconds(std::max(1u, pacingMs_.load())));
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::max(work * 3, floor), [&] { return cancelled(request.generation); });
            // Reprodução: segura aqui, com decoder, encoder e progresso, até pausar.
            wake_.wait(lock, [&] { return cancelled(request.generation) || !holding_.load(); });
        };
        FrameRef frame; i64 pts = 0;
        if (!decoder->next_frame(-1, frame, pts, eos).ok()) { AUREA_LOG_WARN("proxy: falha ao decodificar quadro"); break; }
        if (!frame) { if (eos) { success = !result->times.empty(); break; } if (++empty > 500) break; pace(); continue; }
        empty = 0;
        if (pts <= lastPts) break;
        lastPts = pts;
        lastEnd = std::max(lastEnd, pts + (frame->durationUs > 0 ? frame->durationUs : nominalUs));
        progress_.store(static_cast<f32>(std::clamp(static_cast<f64>(pts) / static_cast<f64>(durationUs), 0.0, 1.0)));
        // Acima de 60 fps: o quadro fora da grade nem é reamostrado nem codificado.
        if (!decimator.keep(pts)) { frame.reset(); pace(); continue; }
        if (!sink) {
            result->original.color = frame->color;
            const u32 vw = frame->visibleWidth ? frame->visibleWidth : frame->width, vh = frame->visibleHeight ? frame->visibleHeight : frame->height;
            if (!vw || !vh || vw > 65536 || vh > 65536
                || static_cast<u64>(frame->cropLeft) + vw > frame->width
                || static_cast<u64>(frame->cropTop) + vh > frame->height) break;
            const bool rotated = frame->rotation == 90 || frame->rotation == 270;
            const u32 dw = rotated ? vh : vw, dh = rotated ? vw : vh;
            const f64 scale = std::min(1., static_cast<f64>(request.target) / std::min(dw, dh));
            result->width = std::max(2u, static_cast<u32>(dw * scale / 2) * 2);
            result->height = std::max(2u, static_cast<u32>(dh * scale / 2) * 2);
            if (result->width > 4096 || result->height > 4096 || !std::isfinite(result->original.fps) || result->original.fps <= 0) {
                AUREA_LOG_WARN("proxy: dimensoes/taxa invalidas %ux%u %.2f", result->width, result->height, result->original.fps); break;
            }
            VideoStreamConfig config = proxy_stream_config(result->width, result->height, result->fps);
            const auto& color = frame->color;
            config.color.matrix = color.matrix == YCbCrMatrix::BT601 ? 6 : color.matrix == YCbCrMatrix::BT2020 ? 9 : 1;
            config.color.primaries = color.primaries == ColorPrimaries::BT601 ? 6 : color.primaries == ColorPrimaries::BT2020 ? 9 : color.primaries == ColorPrimaries::P3 ? 12 : 1;
            config.color.transfer = color.transfer == TransferFunction::PQ ? 16 : color.transfer == TransferFunction::HLG ? 18 : color.transfer == TransferFunction::Linear ? 8 : 1;
            config.color.fullRange = color.fullRange;
            const u64 expected = static_cast<u64>(std::max<i64>(0, result->original.durationUs) / 1e6 * config.bitrateBps / 8 * 1.25) + (1ull << 20);
            if (!make_room(expected, path)) { AUREA_LOG_WARN("proxy: cache sem espaco ou inacessivel (%llu bytes)", static_cast<unsigned long long>(expected)); break; }
            sink = sink_(context_);
            if (!sink || !sink->open(partial.c_str(), config, nullptr).ok()) break;
            AUREA_LOG_INFO("proxy: gerando %ux%u a %.2f fps (fonte %.2f fps)", result->width, result->height, result->fps, result->original.fps);
        }
        if (frame->color != result->original.color || !proxy_frame_nv12(*frame.get(), result->width, result->height, pixels)) break;
        if (!result->times.empty()) {
            result->durations.back() = pts - result->times.back();
            if (!sink->write_video_timed(pendingPixels.data(), result->width,
                    pendingPixels.data() + static_cast<usize>(result->width) * result->height, result->width,
                    result->times.back() - result->times.front(), result->durations.back()).ok()) break;
        }
        result->times.push_back(pts); result->durations.push_back(frame->durationUs);
        frame.reset();
        pendingPixels.swap(pixels);
        if (result->times.size() % 120 == 0) {
            const u64 size = fs::file_size(fs::u8path(partial), ec);
            if (!ec && size > diskBudget_) break;
            ec.clear();
        }
        pace();
    }
    if (success && sink && !cancelled(request.generation)) {
        // O último guardado cobre também os quadros dizimados depois dele.
        if (lastEnd > result->times.back()) result->durations.back() = std::max(result->durations.back(), lastEnd - result->times.back());
        if (result->durations.back() <= 0) result->durations.back() = result->original.durationUs > result->times.back()
            ? result->original.durationUs - result->times.back() : std::max<i64>(1, static_cast<i64>(1e6 / result->original.fps));
        result->original.durationUs = result->times.back() + result->durations.back();
        success = sink->write_video_timed(pendingPixels.data(), result->width,
            pendingPixels.data() + static_cast<usize>(result->width) * result->height, result->width,
            result->times.back() - result->times.front(), result->durations.back()).ok() && sink->finish().ok();
        const u64 size = fs::file_size(fs::u8path(partial), ec);
        success = success && !ec && size > 0 && write_manifest(*result, path + ".pts.tmp");
        if (success) {
            const u64 metadataBytes = fs::file_size(fs::u8path(path + ".pts.tmp"), ec);
            success = !ec && make_room(size + metadataBytes, partial);
        }
        if (success && !cancelled(request.generation) && (identity.empty() || media_->cache_identity(resolved.sourcePath.c_str()) == identity)) {
            fs::rename(fs::u8path(partial), fs::u8path(path), ec);
            if (!ec) fs::rename(fs::u8path(path + ".pts.tmp"), fs::u8path(path + ".pts"), ec);
            if (!ec) { AUREA_LOG_INFO("proxy pronto: %ux%u, %zu quadros", result->width, result->height, result->times.size()); return result; }
        }
    }
    if (sink) sink->abort();
    fs::remove(fs::u8path(partial), ec); fs::remove(fs::u8path(path + ".pts.tmp"), ec);
    return {};
}
} // namespace aurea
