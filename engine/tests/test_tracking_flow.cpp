// =============================================================================
//  Rastreio de ponto, estabilizador e câmera 3D — o fluxo do app, de ponta a
//  ponta, sobre vídeos sintéticos com movimento CONHECIDO:
//
//    escolher o ponto no cabeçote → análise em segundo plano (progresso,
//    cancelamento) → aplicar (Nulo / camada / estabilizar) → conferir contra a
//    verdade (erro em px, resíduo da estabilização, reprojeção da câmera).
//
//  O "decoder" daqui desenha uma textura de mundo (ruído de valor, cantos por
//  toda parte) vista por uma câmera 2D que treme/anda, mais um detalhe (manchas
//  gaussianas) que anda sozinho. Tudo é suave: a verdade subpixel vale.
//  Também: metadado de rotação (celular em pé), VFR e clipes longos.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/media/ThumbnailService.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/tracking/MotionGeometry.hpp"
#include "aurea/tracking/MotionTrackData.hpp"
#include "aurea/tracking/CameraTrackData.hpp"

#if defined(AUREA_TEST_VULKAN)
#if defined(AUREA_TEST_GLES)
#include "GlesBackend.hpp"
namespace aurea { namespace vk = gles; }
#else
#include "VulkanBackend.hpp"
#endif
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

using namespace aurea;
using namespace aurea::tracking;

namespace {

// -----------------------------------------------------------------------------
// Cena: textura do mundo + câmera 2D + detalhe que anda
// -----------------------------------------------------------------------------
f64 lattice(i32 x, i32 y, u32 seed) {
    u32 h = static_cast<u32>(x) * 374761393u + static_cast<u32>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<f64>((h ^ (h >> 16)) & 0xffffu) / 65535.0;
}
f64 value_noise(f64 x, f64 y, f64 period, u32 seed) {
    const f64 fx = x / period, fy = y / period;
    const i32 x0 = static_cast<i32>(std::floor(fx)), y0 = static_cast<i32>(std::floor(fy));
    f64 tx = fx - x0, ty = fy - y0;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    const f64 a = lattice(x0, y0, seed), b = lattice(x0 + 1, y0, seed), c = lattice(x0, y0 + 1, seed), d = lattice(x0 + 1, y0 + 1, seed);
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}
/// Brilho da parede/chão filmado (0..1): três oitavas, contraste de verdade.
f64 world_luma(f64 x, f64 y) {
    return 0.15 + 0.35 * value_noise(x, y, 24, 1) + 0.3 * value_noise(x, y, 11, 2) + 0.2 * value_noise(x, y, 6, 3);
}

struct Cam2D { f64 tx = 0, ty = 0, angle = 0, scale = 1; };   ///< mundo → exibição, em volta do centro

struct FlowScene {
    u32 width = 640, height = 360;     ///< exibição (já girada)
    u32 frames = 120;
    f64 fps = 30;
    u32 rotation = 0;                  ///< metadado; o quadro codificado sai "deitado"
    bool vfr = false;
    std::function<Cam2D(f64)> camera = [](f64) { return Cam2D{}; };
    /// Centro do detalhe (px da exibição, índice de pixel); false = escondido.
    std::function<bool(f64, f64&, f64&)> object;
    f64 backgroundContrast = 1.0;
    f64 detailScale = 1.0;             ///< tamanho do detalhe (1 = raio ~10 px)
    /// Cena pronta (brilho 0..1 no pixel de exibição, instante t): substitui
    /// fundo + detalhe acima.
    std::function<f64(f64, f64, f64)> lumaFn;

    std::vector<i64> pts() const {
        std::vector<i64> out(frames);
        f64 t = 0;
        for (u32 i = 0; i < frames; ++i) {
            out[i] = static_cast<i64>(std::llround(t * 1e6));
            // VFR de celular: intervalos entre 1/60 e 1/20 s, média ~1/30.
            t += vfr ? (i % 3 == 0 ? 1.0 / 60 : i % 3 == 1 ? 1.0 / 24 : 1.0 / 30) : 1.0 / fps;
        }
        return out;
    }
    [[nodiscard]] Vec2 world_to_display(f64 t, Vec2 w) const {
        const Cam2D c = camera(t);
        const f64 cx = 0.5 * (width - 1), cy = 0.5 * (height - 1);
        const f64 co = std::cos(c.angle) * c.scale, si = std::sin(c.angle) * c.scale;
        const f64 x = w.x - cx, y = w.y - cy;
        return {static_cast<f32>(co * x - si * y + cx + c.tx), static_cast<f32>(si * x + co * y + cy + c.ty)};
    }
    /// Brilho no pixel de exibição (x, y) no instante t.
    [[nodiscard]] f64 luma(f64 t, f64 x, f64 y) const {
        if (lumaFn) return lumaFn(t, x, y);
        const Cam2D c = camera(t);
        const f64 cx = 0.5 * (width - 1), cy = 0.5 * (height - 1);
        const f64 co = std::cos(c.angle) / c.scale, si = std::sin(c.angle) / c.scale;
        const f64 dx = x - cx - c.tx, dy = y - cy - c.ty;
        f64 l = 0.5 + (world_luma(co * dx + si * dy + cx, -si * dx + co * dy + cy) - 0.5) * backgroundContrast;
        f64 ox, oy;
        if (object && object(t, ox, oy)) {
            const f64 rx = (x - ox) / detailScale, ry = (y - oy) / detailScale, r2 = rx * rx + ry * ry;
            if (r2 < 400) {
                auto g = [&](f64 px, f64 py, f64 s) { const f64 a = rx - px, b = ry - py; return std::exp(-(a * a + b * b) / (2 * s * s)); };
                const f64 mask = std::exp(-std::pow(r2 / 100.0, 2.0));
                const f64 blobs = 0.1 + 0.85 * g(0, 0, 2.2) + 0.6 * g(5, -3, 1.7) + 0.7 * g(-4, 4, 1.9) + 0.5 * g(-3, -5, 1.5);
                l = (1 - mask) * l + mask * blobs;
            }
        }
        return std::clamp(l, 0.0, 1.0);
    }
};

class FlowFrame final : public DecodedFrame {
public:
    std::vector<u8> y, uv;
};

/// "H.264" com GOP de 30 sobre a cena: seek cai no keyframe, pts reais (VFR).
class FlowDecoder final : public VideoDecoderBackend {
public:
    explicit FlowDecoder(const FlowScene& s) : scene_(s), pts_(s.pts()) {
        const bool sideways = s.rotation == 90 || s.rotation == 270;
        info_.codedWidth = sideways ? s.height : s.width;
        info_.codedHeight = sideways ? s.width : s.height;
        info_.fps = s.fps;
        info_.rotation = s.rotation;
        info_.durationUs = pts_.back() + static_cast<i64>(1e6 / s.fps);
        std::snprintf(info_.codec, sizeof(info_.codec), "%s", "video/flow");
        std::snprintf(info_.decoderName, sizeof(info_.decoderName), "%s", "flow");
    }
    const VideoStreamInfo& info() const noexcept override { return info_; }
    Status seek_to_keyframe(i64 targetUs) noexcept override {
        u32 i = 0;
        while (i + 1 < pts_.size() && pts_[i + 1] <= targetUs) ++i;
        pos_ = (i / 30) * 30;
        return OkStatus;
    }
    Status next_frame(i64 deliverFromUs, FrameRef& out, i64& outPts, bool& eos) noexcept override {
        eos = false;
        if (pos_ >= pts_.size()) { eos = true; outPts = pts_.back(); return OkStatus; }
        const u32 index = pos_++;
        outPts = pts_[index];
        eos = pos_ >= pts_.size();
        if (outPts < deliverFromUs) return OkStatus;
        out = FrameRef::adopt(make(index));
        return OkStatus;
    }
    i64 keyframe_interval_us() const noexcept override { return 1'000'000; }

private:
    DecodedFrame* make(u32 index) const {
        auto* f = new FlowFrame();
        const u32 w = info_.codedWidth, h = info_.codedHeight;
        f->ptsUs = pts_[index];
        f->durationUs = (index + 1 < pts_.size() ? pts_[index + 1] : info_.durationUs) - pts_[index];
        f->width = f->visibleWidth = w;
        f->height = f->visibleHeight = h;
        f->rotation = scene_.rotation;
        f->format = PixelFormat::NV12;
        f->bufferId = index;
        f->y.assign(static_cast<usize>(w) * h, 16);
        f->uv.assign(static_cast<usize>((w + 1) / 2) * ((h + 1) / 2) * 2, 128);
        const f64 t = static_cast<f64>(pts_[index]) * 1e-6;
        for (u32 sy = 0; sy < h; ++sy)
            for (u32 sx = 0; sx < w; ++sx) {
                // Codificado → exibição (a mesma convenção do shader e da miniatura).
                f64 x = sx, y = sy;
                if (scene_.rotation == 90) { x = scene_.width - 1.0 - sy; y = sx; }
                else if (scene_.rotation == 270) { x = sy; y = scene_.height - 1.0 - sx; }
                else if (scene_.rotation == 180) { x = scene_.width - 1.0 - sx; y = scene_.height - 1.0 - sy; }
                f->y[static_cast<usize>(sy) * w + sx] = static_cast<u8>(std::lround(16 + 219 * scene_.luma(t, x, y)));
            }
        f->planes[0] = f->y.data();
        f->planes[1] = f->uv.data();
        f->strides[0] = w;
        f->strides[1] = ((w + 1) / 2) * 2;
        f->planeCount = 2;
        return f;
    }
    FlowScene scene_;
    std::vector<i64> pts_;
    VideoStreamInfo info_;
    u32 pos_ = 0;
};

class FlowFactory final : public VideoSourceFactory {
public:
    explicit FlowFactory(const FlowScene& s) : scene(s) {}
    bool probe(const char*, MediaProbe& out) override {
        FlowDecoder d(scene);
        out.video = d.info();
        out.video.color.fromStream = true;
        out.hasVideo = true;
        return true;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        ++opened;
        return std::make_unique<FlowDecoder>(scene);
    }
    FlowScene scene;
    std::atomic<u32> opened{0};
};

// -----------------------------------------------------------------------------
// Motor
// -----------------------------------------------------------------------------
struct Rig {
    Engine e;
    u64 video = 0;
    bool ok = false;
    Rig(VideoSourceFactory& factory, u32 compW, u32 compH, f64 fps, bool gpu = false) {
        EngineConfig ec;
        ec.workerCount = 2;
        ec.memoryBudgetBytes = 256ull << 20;
        ec.disableAutosave = true;
        ec.mediaFactory = &factory;
#if defined(AUREA_TEST_VULKAN)
        if (gpu) ec.backend = new vk::Backend();
#else
        (void)gpu;
#endif
        if (!e.initialize(ec).ok() || !e.new_project(compW, compH, fps, "rastreio").ok()) return;
        VideoImport imp;
        imp.sourcePath = "flow";
        imp.displayName = "flow";
        auto layer = e.import_video(imp);
        if (!layer.ok()) return;
        video = *layer;
        ok = true;
    }
    ~Rig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* layer(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    void seek(i64 frame) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{frame}, comp()->fps());
        (void)e.apply_command(c);
    }
    Engine::MotionTrackStatus wait_motion(f64 limitSeconds = 120) {
        const auto t0 = std::chrono::steady_clock::now();
        while (e.motion_track_status().state == 1 &&
               std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count() < limitSeconds)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return e.motion_track_status();
    }
    Engine::CameraTrackStatus wait_camera(f64 limitSeconds = 180) {
        const auto t0 = std::chrono::steady_clock::now();
        while (e.camera_track_status().state == 1 &&
               std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count() < limitSeconds)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return e.camera_track_status();
    }
};

/// O quadro que o PREVIEW mostra no instante (DecodedFrameCache::find): o que
/// cobre o instante — os quadros daqui têm duração (até o próximo pts), como
/// os do MediaCodec/AVFoundation. O rastreio tem de analisar esse mesmo quadro.
f64 shown_time(const std::vector<i64>& pts, i64 wantUs) {
    i64 shown = pts.front();
    for (i64 p : pts) if (p <= wantUs) shown = p;
    return static_cast<f64>(shown) * 1e-6;
}

Vec2 to_comp(const Mat4& m, Vec2 p) {
    const Vec4 q = m * Vec4{p.x, p.y, 0, 1};
    return {q.x / q.w, q.y / q.w};
}

/// Corner Pin gravado na camada (estabilizar): origem px → saída px, no quadro.
bool corner_pin_at(const Layer& l, FrameIndex compFrame, f32 w, f32 h, Homography& out) {
    const EffectInstance* pin = nullptr;
    for (const auto& fx : l.effects) if (fx.id == l.motionTrackEffect) pin = &fx;
    if (!pin) return false;
    const FrameIndex local = l.local_time(compFrame);
    std::array<Vec2, 4> corners{};
    for (u32 p = 0; p < 4; ++p) {
        const Track* x = l.tracks.find(TrackProperty::EffectParam, pin->id, param_track_key(p * 2, 0));
        const Track* y = l.tracks.find(TrackProperty::EffectParam, pin->id, param_track_key(p * 2 + 1, 0));
        if (!x || !y || x->keys.empty()) return false;
        corners[p] = {x->sample_keys(local) * w / 100.0f, y->sample_keys(local) * h / 100.0f};
    }
    Homography unit;
    if (!quad_map(corners, unit)) return false;
    Homography toUnit;
    toUnit.m = {1.0 / w, 0, 0, 0, 1.0 / h, 0, 0, 0, 1};
    out = unit * toUnit;
    return true;
}

constexpr f64 kPiF = 3.14159265358979323846;

// Detalhe andando numa curva (até ~3,6 px/quadro), fundo parado, sempre no quadro.
bool walking_detail(f64 t, f64& x, f64& y) {
    x = 160 + 70 * t + 18 * std::sin(t * 2.1);
    y = 180 + 70 * std::sin(t * 1.3);
    return true;
}

} // namespace

// =============================================================================
// Ponto: escolhido no cabeçote, seguido até o fim, vira Nulo e alvo
// =============================================================================
AUREA_TEST(TrackingFlow, PointTrackerFollowsDetailIntoNullAndTarget) {
    FlowScene scene;
    scene.frames = 150;
    scene.object = walking_detail;
    scene.backgroundContrast = 0.35;
    FlowFactory factory(scene);
    // Composição 2× o vídeo: o Nulo tem de sair em px da COMPOSIÇÃO.
    Rig rig(factory, 1280, 720, 30);
    AUREA_CHECK(rig.ok);
    if (!rig.ok) return;
    const i64 startFrame = 12;
    rig.seek(startFrame);
    const auto pts = scene.pts();
    f64 sx = 0, sy = 0;
    (void)walking_detail(shown_time(pts, static_cast<i64>(std::llround(startFrame * 1e6 / 30))), sx, sy);
    const f32 seed[2] = {static_cast<f32>(sx), static_cast<f32>(sy)};
    AUREA_CHECK(rig.e.start_motion_track(rig.video, 0, 0, false, seed, 1));
    const auto st = rig.wait_motion();
    std::printf("    ponto: estado %u, %u/%u quadros, perdidos %u, confianca %.2f %s\n", st.state, st.validFrames, st.frames, st.lost,
                st.confidence, st.message.c_str());
    AUREA_CHECK_EQ(st.state, 2u);
    const Layer* video = rig.layer(rig.video);
    AUREA_CHECK(video && video->motionTrack);
    if (!video || !video->motionTrack) return;
    const auto& data = *video->motionTrack;
    AUREA_CHECK_EQ(data.localFrames.front(), startFrame - video->start.value);
    f64 worst = 0, sum = 0;
    for (usize k = 0; k < data.points.size(); ++k) {
        f64 tx, ty;
        (void)walking_detail(shown_time(pts, data.sourceUs[k]), tx, ty);
        const f64 err = std::hypot(data.points[k][0].x - tx, data.points[k][0].y - ty);
        worst = std::max(worst, err);
        sum += err;
    }
    std::printf("    ponto: erro medio %.3f px, pior %.3f px (px do video) em %zu quadros\n", sum / data.points.size(), worst, data.points.size());
    AUREA_CHECK(worst < 1.0);
    AUREA_CHECK_EQ(data.points.size(), static_cast<usize>(scene.frames - startFrame));
    // Nulo: posição = o ponto em px da composição, quadro a quadro.
    auto nul = rig.e.apply_motion_track(rig.video, 0);
    AUREA_CHECK(nul.ok());
    if (!nul.ok()) return;
    const Layer* n = rig.layer(*nul);
    f64 nullWorst = 0;
    for (usize k = 0; k < data.points.size(); ++k) {
        const FrameIndex f{video->start.value + data.localFrames[k]};
        const Vec2 truth = to_comp(layer_comp_matrix(*rig.comp(), *video, f), data.points[k][0]);
        const FrameIndex local = n->local_time(f);
        const f32 px = n->tracks.find(TrackProperty::PositionX)->sample_keys(local);
        const f32 py = n->tracks.find(TrackProperty::PositionY)->sample_keys(local);
        nullWorst = std::max(nullWorst, static_cast<f64>(std::hypot(px - truth.x, py - truth.y)));
    }
    std::printf("    Nulo: pior desvio %.4f px da composicao\n", nullWorst);
    AUREA_CHECK(nullWorst < 0.01);
}

// Oclusão curta (o detalhe some 6 quadros e volta): o rastreio reencontra e a
// análise continua APLICÁVEL — os quadros sem medida ficam sem key (a curva
// interpola), em vez de recusar o clipe inteiro.
AUREA_TEST(TrackingFlow, PointTrackerBridgesShortOcclusion) {
    FlowScene scene;
    scene.frames = 120;
    scene.backgroundContrast = 0.35;
    scene.object = [](f64 t, f64& x, f64& y) {
        (void)walking_detail(t, x, y);
        const i64 f = std::llround(t * 30);
        return !(f >= 50 && f < 56);
    };
    FlowFactory factory(scene);
    Rig rig(factory, 640, 360, 30);
    AUREA_CHECK(rig.ok);
    if (!rig.ok) return;
    rig.seek(0);
    f64 sx, sy;
    (void)walking_detail(0, sx, sy);
    const f32 seed[2] = {static_cast<f32>(sx), static_cast<f32>(sy)};
    AUREA_CHECK(rig.e.start_motion_track(rig.video, 0, 0, false, seed, 1));
    const auto st = rig.wait_motion();
    std::printf("    oclusao: estado %u, %u/%u validos, perdidos %u, reencontrado %u; %s\n", st.state, st.validFrames, st.frames, st.lost,
                st.reacquired, st.message.c_str());
    AUREA_CHECK_EQ(st.state, 2u);
    const Layer* video = rig.layer(rig.video);
    if (!video || !video->motionTrack) { AUREA_CHECK(false); return; }
    const auto& data = *video->motionTrack;
    // Depois de voltar, o ponto está de novo no detalhe.
    f64 after = 0;
    for (usize k = 60; k < data.points.size(); ++k) {
        f64 tx, ty;
        (void)walking_detail(static_cast<f64>(data.sourceUs[k]) * 1e-6, tx, ty);
        after = std::max(after, static_cast<f64>(std::hypot(data.points[k][0].x - tx, data.points[k][0].y - ty)));
    }
    std::printf("    oclusao: pior erro depois de reencontrar %.3f px\n", after);
    AUREA_CHECK(after < 1.0);
    auto nul = rig.e.apply_motion_track(rig.video, 0);
    std::printf("    oclusao: aplicar Nulo -> %s\n", nul.ok() ? "ok" : std::string(nul.status().detail()).c_str());
    AUREA_CHECK(nul.ok());
    if (!nul.ok()) return;
    const Layer* n = rig.layer(*nul);
    const Track* x = n->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(x != nullptr);
    if (!x) return;
    // Sem key nos quadros perdidos; keys em todos os medidos.
    AUREA_CHECK_EQ(x->keys.size(), static_cast<usize>(st.validFrames));
    // A curva atravessa a lacuna perto da verdade (movimento suave).
    f64 gap = 0;
    for (i64 f = 50; f < 56; ++f) {
        f64 tx, ty;
        (void)walking_detail(f / 30.0, tx, ty);
        gap = std::max(gap, std::abs(static_cast<f64>(x->sample_keys(n->local_time(FrameIndex{f}))) - tx));
    }
    std::printf("    oclusao: lacuna interpolada, desvio %.2f px\n", gap);
    AUREA_CHECK(gap < 3.0);
}

// =============================================================================
// Estabilizador: câmera na mão (tremor + giro + panorâmica lenta)
// =============================================================================
namespace {
Cam2D shaky(f64 t) {
    Cam2D c;
    c.tx = 25 * t + 6 * std::sin(t * 37) + 4 * std::sin(t * 23 + 1);
    c.ty = 5 * std::sin(t * 29 + 2) + 3 * std::sin(t * 41);
    c.angle = 0.012 * std::sin(t * 31);
    c.scale = 1.0 + 0.004 * std::sin(t * 19);
    return c;
}
/// Resíduo: RMS (sobre uma grade de pontos do mundo) da distância entre a
/// posição de saída e (a) a do 1º quadro — travar — ou (b) a média móvel da
/// própria trajetória — tremor que sobrou.
struct Residual { f64 locked = 0, jitter = 0; };
Residual residual(const FlowScene& s, const std::vector<f64>& times, const std::function<Vec2(usize, Vec2)>& out) {
    std::vector<Vec2> world;
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 7; ++x) world.push_back({130.f + x * 60, 90.f + y * 45});
    Residual r;
    f64 lockedSum = 0, jitterSum = 0;
    u32 n = 0, m = 0;
    for (Vec2 w : world) {
        std::vector<Vec2> path(times.size());
        for (usize i = 0; i < times.size(); ++i) path[i] = out(i, s.world_to_display(times[i], w));
        for (usize i = 0; i < times.size(); ++i) {
            lockedSum += std::pow(path[i].x - path[0].x, 2) + std::pow(path[i].y - path[0].y, 2);
            ++n;
            if (i >= 3 && i + 3 < times.size()) {
                f64 ax = 0, ay = 0;
                for (usize j = i - 3; j <= i + 3; ++j) { ax += path[j].x; ay += path[j].y; }
                jitterSum += std::pow(path[i].x - ax / 7, 2) + std::pow(path[i].y - ay / 7, 2);
                ++m;
            }
        }
    }
    r.locked = std::sqrt(lockedSum / std::max(1u, n));
    r.jitter = std::sqrt(jitterSum / std::max(1u, m));
    return r;
}
} // namespace

AUREA_TEST(TrackingFlow, StabilizerLocksAndSmoothsHandheldShot) {
    FlowScene scene;
    scene.frames = 120;
    scene.camera = [](f64 t) { Cam2D c = shaky(t); c.tx -= 25 * t; return c; };   // tremor sem panorâmica
    FlowFactory factory(scene);
    Rig rig(factory, 640, 360, 30, true);
    AUREA_CHECK(rig.ok);
    if (!rig.ok) return;
    AUREA_CHECK(rig.e.start_motion_track(rig.video, 4, 0, false, nullptr, 0));
    const auto st = rig.wait_motion();
    std::printf("    estabilizador: estado %u, %u/%u validos, perdidos %u, rms %.3f px %s\n", st.state, st.validFrames, st.frames, st.lost,
                st.errorPx, st.message.c_str());
    AUREA_CHECK_EQ(st.state, 2u);
    std::vector<f64> times;
    for (i64 us : scene.pts()) times.push_back(static_cast<f64>(us) * 1e-6);
    const Residual before = residual(scene, times, [](usize, Vec2 p) { return p; });
    for (bool lock : {true, false}) {
        auto applied = rig.e.apply_motion_track(0, 3, lock, 0.5f, 1.3f, 1);
        AUREA_CHECK(applied.ok());
        if (!applied.ok()) return;
        const Layer* v = rig.layer(rig.video);
        const Residual after = residual(scene, times, [&](usize i, Vec2 p) {
            Homography h;
            if (!corner_pin_at(*v, FrameIndex{static_cast<i64>(i)}, 640, 360, h)) return Vec2{NAN, NAN};
            return h.project(p);
        });
        std::printf("    estabilizar (%s): parado %.2f -> %.3f px, tremor %.2f -> %.3f px, zoom %.1f%%\n", lock ? "travar" : "suavizar",
                    before.locked, after.locked, before.jitter, after.jitter, rig.e.motion_track_status().cropPercent);
        if (lock) AUREA_CHECK(after.locked < 0.35);
        AUREA_CHECK(after.jitter < before.jitter * 0.1);
    }
    // Travar numa panorâmica maior que o zoom máximo: o alvo desliza para o
    // caminho suave — o tremor continua fora (antes a força caía quadro a
    // quadro e devolvia o tremor: 7,5 -> 3,3 px).
    {
        FlowScene pan;
        pan.frames = 120;
        pan.camera = shaky;
        FlowFactory panFactory(pan);
        Rig panRig(panFactory, 640, 360, 30);
        AUREA_CHECK(panRig.ok);
        if (panRig.ok && panRig.e.start_motion_track(panRig.video, 4, 0, false, nullptr, 0) && panRig.wait_motion().state == 2) {
            std::vector<f64> panTimes;
            for (i64 us : pan.pts()) panTimes.push_back(static_cast<f64>(us) * 1e-6);
            const Residual raw = residual(pan, panTimes, [](usize, Vec2 p) { return p; });
            AUREA_CHECK(panRig.e.apply_motion_track(0, 3, true, 0.5f, 1.15f, 1).ok());
            const Layer* v = panRig.layer(panRig.video);
            const Residual locked = residual(pan, panTimes, [&](usize i, Vec2 p) {
                Homography h;
                if (!corner_pin_at(*v, FrameIndex{static_cast<i64>(i)}, 640, 360, h)) return Vec2{NAN, NAN};
                return h.project(p);
            });
            std::printf("    travar numa panoramica (zoom 15%%): tremor %.2f -> %.3f px\n", raw.jitter, locked.jitter);
            AUREA_CHECK(locked.jitter < raw.jitter * 0.1);
        } else AUREA_CHECK(false);
    }
#if defined(AUREA_TEST_VULKAN)
    // O efeito renderiza de verdade: com "travar", quadros distantes saem iguais.
    if (!rig.e.gpu()) { std::printf("    (sem GPU: render pulado)\n"); return; }
    AUREA_CHECK(rig.e.apply_motion_track(0, 3, true, 0.5f, 1.3f, 1).ok());
    TextureDesc d;
    d.width = 640;
    d.height = 360;
    d.format = SurfaceFormat::RGBA8;
    d.renderTarget = true;
    d.transferSrc = true;
    auto target = rig.e.gpu()->create_texture(d);
    AUREA_CHECK(target.ok());
    if (!target.ok()) return;
    auto grab = [&](i64 frame, std::vector<u8>& px) {
        rig.seek(frame);
        px.assign(640 * 360 * 4, 0);
        return rig.e.render_offscreen(*target, 640, 360).ok() && rig.e.gpu()->read_texture(*target, px.data(), 640 * 4).ok();
    };
    auto mad = [](const std::vector<u8>& a, const std::vector<u8>& b) {
        f64 s = 0;
        u32 n = 0;
        for (u32 y = 90; y < 270; ++y) for (u32 x = 160; x < 480; ++x) { s += std::abs(int(a[(y * 640 + x) * 4 + 1]) - int(b[(y * 640 + x) * 4 + 1])); ++n; }
        return s / n;
    };
    std::vector<u8> first, other;
    AUREA_CHECK(grab(0, first));
    f64 stabilized = 0;
    for (i64 f : {20, 47, 83, 119}) { AUREA_CHECK(grab(f, other)); stabilized = std::max(stabilized, mad(first, other)); }
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(rig.e.apply_command(undo).ok());
    std::vector<u8> raw0, rawN;
    AUREA_CHECK(grab(0, raw0));
    f64 shaking = 0;
    for (i64 f : {20, 47, 83, 119}) { AUREA_CHECK(grab(f, rawN)); shaking = std::max(shaking, mad(raw0, rawN)); }
    std::printf("    render (travar): diferenca media entre quadros %.2f -> %.2f niveis (0..255)\n", shaking, stabilized);
    AUREA_CHECK(stabilized < shaking * 0.25);
    AUREA_CHECK(stabilized < 6.0);
    rig.e.gpu()->destroy_texture(*target);
#endif
}

// "Estabilizar pelo ponto" (menu da camada): o vídeo anda o contrário do
// detalhe e o ponto fica parado na tela.
AUREA_TEST(TrackingFlow, StabilizeByPointKeepsDetailStill) {
    FlowScene scene;
    scene.frames = 90;
    scene.camera = shaky;
    FlowFactory factory(scene);
    Rig rig(factory, 640, 360, 30);
    AUREA_CHECK(rig.ok);
    if (!rig.ok) return;
    const Vec2 world{300, 170};
    const Vec2 p0 = scene.world_to_display(0, world);
    const f32 seed[2] = {p0.x, p0.y};
    rig.seek(0);
    AUREA_CHECK(rig.e.start_motion_track(rig.video, 0, 0, false, seed, 1));
    const auto st = rig.wait_motion();
    AUREA_CHECK_EQ(st.state, 2u);
    auto applied = rig.e.apply_motion_track(0, 3, true, 0.5f, 1.0f, 0);
    std::printf("    pelo ponto: aplicar -> %s\n", applied.ok() ? "ok" : std::string(applied.status().detail()).c_str());
    AUREA_CHECK(applied.ok());
    if (!applied.ok()) return;
    const Layer* v = rig.layer(rig.video);
    f64 worst = 0, raw = 0;
    const auto pts = scene.pts();
    for (u32 i = 0; i < scene.frames; ++i) {
        Homography h;
        if (!corner_pin_at(*v, FrameIndex{i}, 640, 360, h)) { worst = 1e9; break; }
        const Vec2 p = scene.world_to_display(static_cast<f64>(pts[i]) * 1e-6, world);
        const Vec2 q = h.project(p);
        worst = std::max(worst, static_cast<f64>(std::hypot(q.x - p0.x, q.y - p0.y)));
        raw = std::max(raw, static_cast<f64>(std::hypot(p.x - p0.x, p.y - p0.y)));
    }
    std::printf("    pelo ponto: o detalhe andava ate %.1f px; estabilizado fica a %.3f px do lugar\n", raw, worst);
    AUREA_CHECK(worst < 0.6);
}

// =============================================================================
// Celular em pé (rotação 90° no container), VFR e clipe longo
// =============================================================================
AUREA_TEST(TrackingFlow, RotatedPhoneVideoTracksInDisplaySpace) {
    for (u32 rotation : {0u, 90u, 270u}) {
        FlowScene scene;
        scene.width = 360;
        scene.height = 640;
        scene.frames = 90;
        scene.rotation = rotation;   // 0 = o mesmo vídeo em pé sem metadado (referência)
        scene.backgroundContrast = 0.35;
        scene.object = [](f64 t, f64& x, f64& y) { x = 100 + 50 * t; y = 150 + 110 * t + 20 * std::sin(t * 3); return true; };
        FlowFactory factory(scene);
        Rig rig(factory, 1080, 1920, 30);
        AUREA_CHECK(rig.ok);
        if (!rig.ok) return;
        const auto* asset = rig.e.project()->asset(rig.layer(rig.video)->source);
        AUREA_CHECK(asset->video.width == 360 && asset->video.height == 640);
        rig.seek(0);
        f64 sx, sy;
        (void)scene.object(0, sx, sy);
        const f32 seed[2] = {static_cast<f32>(sx), static_cast<f32>(sy)};
        AUREA_CHECK(rig.e.start_motion_track(rig.video, 0, 0, false, seed, 1));
        const auto st = rig.wait_motion();
        const Layer* video = rig.layer(rig.video);
        f64 worst = video && video->motionTrack ? 0 : 1e9;
        if (video && video->motionTrack) {
            const auto& d = *video->motionTrack;
            for (usize k = 0; k < d.points.size(); ++k) {
                f64 tx, ty;
                (void)scene.object(static_cast<f64>(d.sourceUs[k]) * 1e-6, tx, ty);
                worst = std::max(worst, static_cast<f64>(std::hypot(d.points[k][0].x - tx, d.points[k][0].y - ty)));
            }
        }
        std::printf("    rotacao %u: estado %u, %u/%u quadros, pior erro %.3f px\n", rotation, st.state, st.validFrames, st.frames, worst);
        AUREA_CHECK_EQ(st.state, 2u);
        AUREA_CHECK(worst < 1.0);
    }
}

AUREA_TEST(TrackingFlow, VariableFrameRateFollowsRealTimestamps) {
    FlowScene scene;
    scene.frames = 110;
    scene.vfr = true;
    scene.object = walking_detail;
    scene.backgroundContrast = 0.35;
    FlowFactory factory(scene);
    Rig rig(factory, 640, 360, 30);
    AUREA_CHECK(rig.ok);
    if (!rig.ok) return;
    rig.seek(0);
    f64 sx, sy;
    (void)walking_detail(0, sx, sy);
    const f32 seed[2] = {static_cast<f32>(sx), static_cast<f32>(sy)};
    AUREA_CHECK(rig.e.start_motion_track(rig.video, 0, 0, false, seed, 1));
    const auto st = rig.wait_motion();
    const auto pts = scene.pts();
    const Layer* video = rig.layer(rig.video);
    f64 worst = video && video->motionTrack ? 0 : 1e9;
    if (video && video->motionTrack) {
        const auto& d = *video->motionTrack;
        for (usize k = 0; k < d.points.size(); ++k) {
            f64 tx, ty;
            (void)walking_detail(shown_time(pts, d.sourceUs[k]), tx, ty);
            worst = std::max(worst, static_cast<f64>(std::hypot(d.points[k][0].x - tx, d.points[k][0].y - ty)));
        }
    }
    u32 mismatched = 0;
    if (video && video->motionTrack)
        for (i64 want : video->motionTrack->sourceUs) {
            // Regra antiga do rastreio: primeiro pts >= alvo - meio quadro.
            i64 old = pts.back();
            for (i64 p : pts) if (p >= want - static_cast<i64>(5e5 / 30)) { old = p; break; }
            mismatched += std::abs(static_cast<f64>(old) * 1e-6 - shown_time(pts, want)) > 1e-9 ? 1u : 0u;
        }
    std::printf("    VFR: estado %u, %u/%u quadros, pior erro %.3f px (a regra antiga analisava %u quadros diferentes do preview)\n",
                st.state, st.validFrames, st.frames, worst, mismatched);
    AUREA_CHECK_EQ(st.state, 2u);
    AUREA_CHECK(worst < 1.0);
}

// Clipe de 70 s (2100 quadros): o ponto e o estabilizador analisam o clipe
// inteiro; a câmera 3D analisa o trecho permitido e diz isso.
AUREA_TEST(TrackingFlow, LongClipIsAnalysedInsteadOfRefused) {
    FlowScene scene;
    scene.width = 320;
    scene.height = 180;
    scene.frames = 2100;
    scene.camera = [](f64 t) { Cam2D c; c.tx = 3 * std::sin(t * 23); c.ty = 2 * std::sin(t * 29 + 1); return c; };
    scene.object = [](f64 t, f64& x, f64& y) { x = 160 + 90 * std::sin(t * 0.7); y = 90 + 50 * std::sin(t * 0.45); return true; };
    FlowFactory factory(scene);
    Rig rig(factory, 320, 180, 30);
    AUREA_CHECK(rig.ok);
    if (!rig.ok) return;
    rig.seek(0);
    f64 sx, sy;
    (void)scene.object(0, sx, sy);
    const f32 seed[2] = {static_cast<f32>(sx), static_cast<f32>(sy)};
    const auto t0 = std::chrono::steady_clock::now();
    const bool started = rig.e.start_motion_track(rig.video, 0, 0, false, seed, 1);
    std::printf("    longo (ponto): iniciar -> %s\n", started ? "ok" : "RECUSADO");
    AUREA_CHECK(started);
    if (started) {
        const auto st = rig.wait_motion(300);
        const f64 secs = std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
        const Layer* video = rig.layer(rig.video);
        f64 worst = 0;
        if (video && video->motionTrack)
            for (usize k = 0; k < video->motionTrack->points.size(); ++k) {
                f64 tx, ty;
                (void)scene.object(static_cast<f64>(video->motionTrack->sourceUs[k]) * 1e-6, tx, ty);
                worst = std::max(worst, static_cast<f64>(std::hypot(video->motionTrack->points[k][0].x - tx, video->motionTrack->points[k][0].y - ty)));
            }
        std::printf("    longo (ponto): estado %u, %u/%u quadros em %.1f s, pior erro %.3f px\n", st.state, st.validFrames, st.frames, secs, worst);
        AUREA_CHECK_EQ(st.state, 2u);
        AUREA_CHECK_EQ(st.frames, 2100u);
        AUREA_CHECK(worst < 1.0);
    }
    const bool stab = rig.e.start_motion_track(rig.video, 4, 0, false, nullptr, 0);
    std::printf("    longo (estabilizador): iniciar -> %s\n", stab ? "ok" : "RECUSADO");
    AUREA_CHECK(stab);
    if (stab) {
        const auto st = rig.wait_motion(300);
        std::printf("    longo (estabilizador): estado %u, %u/%u quadros; %s\n", st.state, st.validFrames, st.frames, st.message.c_str());
        AUREA_CHECK_EQ(st.state, 2u);
        AUREA_CHECK_EQ(st.frames, 2100u);
        AUREA_CHECK(rig.e.apply_motion_track(0, 3).ok());
    }
    // Câmera 3D (limite do solve: 1800 quadros): a UI recebe o PORQUÊ e o que
    // fazer, em vez de "precisa de 10 quadros".
    const bool cam = rig.e.start_camera_track(rig.video, 0);
    const auto cst = rig.wait_camera();
    std::printf("    longo (camera): iniciar -> %s, estado %u: %s\n", cam ? "ok" : "RECUSADO", cst.state, cst.message.c_str());
    AUREA_CHECK(cam);
    AUREA_CHECK_EQ(cst.state, 3u);
    AUREA_CHECK(cst.message.find("1800") != std::string::npos);
}

// Cancelar no meio: volta rápido, o projeto não muda, dá para recomeçar.
AUREA_TEST(TrackingFlow, CancelStopsQuicklyAndLeavesProjectUntouched) {
    FlowScene scene;
    scene.frames = 900;
    scene.camera = shaky;
    FlowFactory factory(scene);
    Rig rig(factory, 640, 360, 30);
    AUREA_CHECK(rig.ok);
    if (!rig.ok) return;
    AUREA_CHECK(rig.e.start_motion_track(rig.video, 4, 0, false, nullptr, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const f32 midway = rig.e.motion_track_status().progress;
    const auto t0 = std::chrono::steady_clock::now();
    rig.e.cancel_motion_track();
    const auto st = rig.wait_motion(10);
    // Recomeçar (outra análise) não pode esperar a anterior terminar o clipe.
    const f32 seed[2] = {320, 180};
    const bool again = rig.e.start_motion_track(rig.video, 0, 0, false, seed, 1);
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    cancelar: progresso %.0f%% -> estado %u, recomecar %s em %.0f ms\n", midway * 100, st.state, again ? "ok" : "recusado", ms);
    AUREA_CHECK_EQ(st.state, 4u);
    AUREA_CHECK(rig.layer(rig.video)->motionTrack == nullptr);
    AUREA_CHECK(rig.layer(rig.video)->effects.empty());
    AUREA_CHECK(again);
    AUREA_CHECK(ms < 2000);
    rig.e.cancel_motion_track();
    (void)rig.wait_motion(10);
}

// =============================================================================
// Câmera 3D num vídeo de celular em pé (rotação 90°) numa composição vertical
// =============================================================================
AUREA_TEST(TrackingFlow, CameraTrackerSolvesRotatedPhoneVideo) {
    using namespace aurea::test;
    SyntheticConfig cfg;
    cfg.width = 640;          // codificado deitado; exibição 360×640
    cfg.height = 360;
    cfg.frameCount = 90;
    cfg.rotation = 90;
    cfg.pattern = SyntheticPattern::Scene3D;
    SyntheticFactory factory(cfg);
    Rig rig(factory, 1080, 1920, 30);
    AUREA_CHECK(rig.ok);
    if (!rig.ok) return;
    AUREA_CHECK(rig.e.start_camera_track(rig.video, 1));
    const auto st = rig.wait_camera();
    std::printf("    camera (90 graus): estado %u, %u/%u quadros, erro %.3f px, FOV %.1f; %s\n", st.state, st.framesSolved, st.frames,
                st.rmsError, st.fovDeg, st.message.c_str());
    AUREA_CHECK_EQ(st.state, 2u);
    auto cam = rig.e.apply_camera_track(-1, {}, 0);
    AUREA_CHECK(cam.ok());
    if (!cam.ok()) return;
    // Reprojeção na COMPOSIÇÃO: ponto 3D resolvido pela câmera criada × a
    // posição verdadeira da mancha no vídeo mostrado (exibição girada).
    const std::vector<Vec3> pts = rig.e.camera_track_points();
    const Composition* comp = rig.comp();
    const Layer* video = rig.layer(rig.video);
    const auto& truth = scene3d_points();
    std::vector<f64> errs;
    for (const Vec3& q : pts) {
        auto proj = [&](i64 f, f64& u, f64& v) {
            const Vec4 c = comp_view_projection(*comp, FrameIndex{f}) * Vec4{q.x, q.y, q.z, 1};
            if (c.w <= 0) return false;
            u = c.x / c.w;
            v = c.y / c.w;
            return true;
        };
        auto truthComp = [&](const std::array<f64, 3>& X, i64 f, f64& u, f64& v) {
            f64 cu, cv;   // px do quadro CODIFICADO (640×360)
            if (!scene3d_project(X, static_cast<u32>(f), 90, 640, 360, cu, cv)) return false;
            // Codificado → exibição (90°): x = H − 1 − y, y = x (índices de pixel).
            const Vec2 display{static_cast<f32>(360 - 1 - cv), static_cast<f32>(cu)};
            const Vec2 c = to_comp(layer_comp_matrix(*comp, *video, FrameIndex{f}), display);
            u = c.x;
            v = c.y;
            return true;
        };
        f64 u0, v0;
        if (!proj(0, u0, v0)) continue;
        usize best = truth.size();
        f64 bd = 5.0;
        for (usize k = 0; k < truth.size(); ++k) {
            f64 tu, tv;
            if (!truthComp(truth[k], 0, tu, tv)) continue;
            const f64 d = std::hypot(tu - u0, tv - v0);
            if (d < bd) { bd = d; best = k; }
        }
        if (best == truth.size()) continue;
        f64 worst = bd;
        for (i64 f : {45, 89}) {
            f64 u, v, tu, tv;
            if (!proj(f, u, v) || !truthComp(truth[best], f, tu, tv)) { worst = 1e9; break; }
            worst = std::max(worst, std::hypot(u - tu, v - tv));
        }
        errs.push_back(worst);
    }
    std::sort(errs.begin(), errs.end());
    const f64 med = errs.empty() ? 1e9 : errs[errs.size() / 2];
    // Composição 3× o vídeo: 1 px do vídeo = 3 px aqui.
    std::printf("    camera (90 graus): reprojecao na composicao mediana %.2f px (%.2f px do video) em %zu pontos\n", med, med / 3, errs.size());
    AUREA_CHECK(errs.size() >= 40);
    AUREA_CHECK(med < 4.5);
    // "Criar câmera + sólido" (painel e menu do palco): pontos escolhidos → um
    // sólido 3D preso na cena, na mesma câmera, cobrindo o clipe.
    const auto cache = rig.layer(rig.video)->cameraTrack;
    std::vector<u32> ids;
    for (u32 t = 0; cache && t < cache->solution.trackSolved.size() && ids.size() < 3; ++t)
        if (cache->solution.trackSolved[t]) ids.push_back(t);
    AUREA_CHECK_EQ(rig.e.select_camera_track_points(ids.data(), static_cast<u32>(ids.size())), 3u);
    const u32 before = rig.comp()->layers().count();
    const FrameIndex clipStart = rig.layer(rig.video)->start, clipEnd = rig.layer(rig.video)->end;
    auto solid = rig.e.apply_camera_track(-1, {}, 4);
    AUREA_CHECK(solid.ok());
    AUREA_CHECK_EQ(rig.comp()->layers().count(), before + 1);
    u32 cameras = 0, solids = 0;
    rig.comp()->layers().for_each([&](LayerId, const Layer& l) {
        if (l.kind == LayerKind::Camera) ++cameras;
        if (l.kind == LayerKind::Shape && l.threeD && l.start == clipStart && l.end == clipEnd && l.shape.bounds.w > 0) ++solids;
    });
    std::printf("    camera (90 graus): %u camera, %u solido 3D criado\n", cameras, solids);
    AUREA_CHECK_EQ(cameras, 1u);
    AUREA_CHECK_EQ(solids, 1u);
}

// Vídeo 1080p analisado a 360 linhas (o caso do celular): a luma de análise
// por área (frame_to_gray) mantém o registro subpixel na fonte. A miniatura
// antiga (vizinho mais próximo) entra só como referência: aqui as duas ficam
// iguais — a precisão do rastreio NÃO vinha da conversão.
AUREA_TEST(TrackingFlow, AnalysisAt360LinesKeepsSubpixelPrecisionOn1080p) {
    FlowScene scene;
    scene.width = 1920;
    scene.height = 1080;
    scene.frames = 60;
    scene.detailScale = 3;
    scene.backgroundContrast = 0.35;
    scene.object = [](f64 t, f64& x, f64& y) { x = 600 + 210 * t + 54 * std::sin(t * 2.1); y = 540 + 150 * std::sin(t * 1.3); return true; };
    FlowDecoder dec(scene);
    AUREA_CHECK(dec.seek_to_keyframe(0).ok());
    std::vector<FrameRef> frames;
    for (bool eos = false; !eos && frames.size() < scene.frames;) {
        FrameRef f;
        i64 pts = 0;
        if (!dec.next_frame(0, f, pts, eos).ok()) break;
        if (f) frames.push_back(f);
    }
    AUREA_CHECK_EQ(frames.size(), static_cast<usize>(scene.frames));
    // Registro de cada quadro contra o bloco do 1º (o detalhe não muda de
    // aparência): sem acúmulo de passo a passo, só a conversão aparece.
    auto run = [&](bool area, f64& mean, bool chained = false) {
        tracking::Gray first;
        Vec2 seed{}, p{};
        f32 k = 1;
        f64 worst = 0, sum = 0;
        for (usize i = 0; i < frames.size(); ++i) {
            tracking::Gray g;
            if (area) (void)tracking::frame_to_gray(*frames[i].get(), 360, g);
            else {
                ThumbnailService::Image img;
                (void)frame_to_thumbnail(*frames[i].get(), 360, img);
                g = tracking::to_gray(img.rgba.data(), img.width, img.height);
            }
            f64 tx, ty;
            (void)scene.object(static_cast<f64>(frames[i]->ptsUs) * 1e-6, tx, ty);
            if (i == 0) {
                k = 1920.0f / static_cast<f32>(g.width);
                seed = area ? Vec2{static_cast<f32>((tx + .5) / k - .5), static_cast<f32>((ty + .5) / k - .5)}
                            : Vec2{static_cast<f32>(tx / k), static_cast<f32>(ty / k)};
                p = seed;
                first = g;
            } else if (chained) {
                // O rastreio antigo do motor: bloco do quadro ANTERIOR, sem âncora.
                p = tracking::track_step(first, g, p, 4, 24).pos;
            } else p = tracking::track_step(first, g, seed, 4, 24, p, true).pos;
            if (chained) first = g;
            const Vec2 source = area ? Vec2{(p.x + .5f) * k - .5f, (p.y + .5f) * k - .5f} : Vec2{p.x * k, p.y * k};
            const f64 err = std::hypot(source.x - tx, source.y - ty);
            worst = std::max(worst, err);
            sum += err;
        }
        mean = sum / frames.size();
        return worst;
    };
    f64 nearestMean = 0, areaMean = 0, chainMean = 0;
    const f64 nearest = run(false, nearestMean), area = run(true, areaMean);
    const f64 chain = run(false, chainMean, true);
    std::printf("    1080p, algoritmo antigo (miniatura + passo a passo, sem ancora): medio %.2f / pior %.2f px\n", chainMean, chain);
    std::printf("    1080p -> 360 linhas: vizinho mais proximo medio %.2f / pior %.2f px; area medio %.2f / pior %.2f px (px da fonte)\n",
                nearestMean, nearest, areaMean, area);
    AUREA_CHECK(area < 1.5);
    AUREA_CHECK(areaMean < 1.0);
}

// O mesmo 1080p pelo motor inteiro (raio padrão do painel = bloco pequeno na
// análise): o passo a passo sozinho derivava ~5 px em 2 s; a âncora no bloco
// escolhido segura o ponto no detalhe.
AUREA_TEST(TrackingFlow, PointTrackerHoldsOn1080pPhoneClip) {
    FlowScene scene;
    scene.width = 1920;
    scene.height = 1080;
    scene.frames = 60;
    scene.detailScale = 3;
    scene.backgroundContrast = 0.35;
    scene.object = [](f64 t, f64& x, f64& y) { x = 600 + 210 * t + 54 * std::sin(t * 2.1); y = 540 + 150 * std::sin(t * 1.3); return true; };
    FlowFactory factory(scene);
    Rig rig(factory, 1920, 1080, 30);
    AUREA_CHECK(rig.ok);
    if (!rig.ok) return;
    rig.seek(0);
    f64 sx, sy;
    (void)scene.object(0, sx, sy);
    const f32 seed[2] = {static_cast<f32>(sx), static_cast<f32>(sy)};
    AUREA_CHECK(rig.e.start_motion_track(rig.video, 0, 0, false, seed, 1));
    const auto st = rig.wait_motion();
    const Layer* video = rig.layer(rig.video);
    f64 worst = video && video->motionTrack ? 0 : 1e9, sum = 0;
    if (video && video->motionTrack)
        for (usize k = 0; k < video->motionTrack->points.size(); ++k) {
            f64 tx, ty;
            (void)scene.object(static_cast<f64>(video->motionTrack->sourceUs[k]) * 1e-6, tx, ty);
            const f64 err = std::hypot(video->motionTrack->points[k][0].x - tx, video->motionTrack->points[k][0].y - ty);
            worst = std::max(worst, err);
            sum += err;
        }
    std::printf("    1080p pelo motor: estado %u, %u/%u quadros, erro medio %.2f, pior %.2f px da fonte\n", st.state, st.validFrames, st.frames,
                sum / std::max<usize>(1, st.frames), worst);
    AUREA_CHECK_EQ(st.state, 2u);
    AUREA_CHECK(worst < 1.5);
}


// =============================================================================
// O caso do aparelho: vídeo de celular EM PÉ (720×1280) numa composição
// deitada (1920×1080), fundo de blocos coloridos com tremor de mão e um alvo
// pequeno (anel branco com cruz vermelha) andando por conta própria. O mesmo
// desenho do vídeo gerado para o emulador (make_track_video.py), em luma
// BT.601: a cruz vermelha tem quase o brilho médio do fundo — só o anel se
// destaca. Toque no centro do anel no quadro 77, raios PADRÃO do painel.
// Antes: o bloco de 25 px só via cruz + fundo e seguia o FUNDO (erro de 127 px
// no vídeo; no aparelho o anel andava ~117 px na tela "estabilizada").
// =============================================================================
namespace {
struct Shake { i32 x = 0, y = 0; };
Shake ring_shake(f64 t) {
    return {static_cast<i32>(std::lround(9 * std::sin(2 * kPiF * 1.7 * t) + 5 * std::sin(2 * kPiF * 4.3 * t + 1))),
            static_cast<i32>(std::lround(8 * std::sin(2 * kPiF * 2.1 * t + 0.5) + 4 * std::sin(2 * kPiF * 5.2 * t)))};
}
Vec2 ring_center(f64 t) {
    const Shake s = ring_shake(t);
    return {static_cast<f32>(200 + 300 * t / 5 + s.x), static_cast<f32>(500 + 150 * std::sin(2 * kPiF * t / 5) + s.y)};
}
f64 ring_scene_luma(f64 t, f64 x, f64 y) {
    const Vec2 c = ring_center(t);
    const f64 dx = x - c.x, dy = y - c.y, r = std::hypot(dx, dy);
    if (std::abs(r - 28) < 5) return 1.0;                                             // anel branco
    if ((std::abs(dx) < 3 && std::abs(dy) < 22) || (std::abs(dy) < 3 && std::abs(dx) < 22))
        return (0.299 * 255 + 0.587 * 40 + 0.114 * 40) / 255.0;                        // cruz vermelha
    // Cenário maior que o quadro, recortado com o tremor: blocos de 8 px,
    // cor (v, 0,7v, (255 − v)/2).
    const Shake s = ring_shake(t);
    const i32 bx = static_cast<i32>(std::floor((x + 100 + s.x) / 8)), by = static_cast<i32>(std::floor((y + 100 + s.y) / 8));
    const f64 v = 255.0 * lattice(bx, by, 7);
    return (0.299 * v + 0.587 * 0.7 * v + 0.114 * (255 - v) / 2) / 255.0;
}
FlowScene ring_scene() {
    FlowScene scene;
    scene.width = 720;
    scene.height = 1280;
    scene.frames = 150;
    scene.lumaFn = ring_scene_luma;
    return scene;
}
/// Posição na composição de um ponto do vídeo, depois do Corner Pin de estabilizar.
Vec2 shown_in_comp(Rig& rig, const Layer& v, i64 frame, Vec2 videoPx) {
    Homography h;
    Vec2 p = videoPx;
    if (corner_pin_at(v, FrameIndex{frame}, 720, 1280, h)) p = h.project(p);
    return to_comp(layer_comp_matrix(*rig.comp(), v, FrameIndex{frame}), p);
}
constexpr f64 kScreenPerVideo = 0.472;   // emulador 1080×2340: px de tela por px do vídeo
} // namespace

AUREA_TEST(TrackingFlow, SmallTargetOnBusyBackgroundPortraitInLandscape) {
    FlowFactory factory(ring_scene());
    Rig rig(factory, 1920, 1080, 30);
    AUREA_CHECK(rig.ok);
    if (!rig.ok) return;
    rig.seek(77);
    const Vec2 truth0 = ring_center(77 / 30.0);
    const f32 seed[2] = {truth0.x - 1.8f, truth0.y - 2.0f};   // o toque no aparelho caiu ~2 px fora do centro
    AUREA_CHECK(rig.e.start_motion_track(rig.video, 0, 0, false, seed, 1));   // raios padrão (12 / 48 px)
    const auto st = rig.wait_motion();
    const Layer* video = rig.layer(rig.video);
    AUREA_CHECK(st.state == 2 && video && video->motionTrack);
    if (st.state != 2 || !video || !video->motionTrack) return;
    const auto& d = *video->motionTrack;
    f64 trackWorst = 0;
    for (usize k = 0; k < d.points.size(); ++k) {
        const Vec2 truth = ring_center(static_cast<f64>(d.sourceUs[k]) * 1e-6);
        trackWorst = std::max(trackWorst, static_cast<f64>(std::hypot((d.points[k][0].x - seed[0]) - (truth.x - truth0.x), (d.points[k][0].y - seed[1]) - (truth.y - truth0.y))));
    }
    std::printf("    alvo pequeno: %u/%u quadros, deslocamento do ponto x verdade: pior %.2f px do video\n", st.validFrames, st.frames, trackWorst);
    AUREA_CHECK_EQ(st.validFrames, 73u);
    AUREA_CHECK(trackWorst < 2.0);

    // Estabilizar pelo ponto (o que o menu e o painel aplicam): o anel fica parado.
    AUREA_CHECK(rig.e.apply_motion_track(0, 3, true, .5f, 1.f, 0).ok());
    video = rig.layer(rig.video);
    const Vec2 anchor = shown_in_comp(rig, *video, 77, truth0);
    f64 held = 0, raw = 0;
    for (i64 f = 77; f < 150; ++f) {
        const Vec2 truth = ring_center(f / 30.0);
        const Vec2 q = shown_in_comp(rig, *video, f, truth);
        held = std::max(held, static_cast<f64>(std::hypot(q.x - anchor.x, q.y - anchor.y)));
        raw = std::max(raw, static_cast<f64>(std::hypot(truth.x - truth0.x, truth.y - truth0.y)));
    }
    const f64 compPerVideo = 1080.0 / 1280.0;
    std::printf("    estabilizar pelo ponto: o anel andava ate %.1f px do video; fica a %.2f px da composicao (%.2f px de tela do emulador)\n",
                raw, held, held / compPerVideo * kScreenPerVideo);
    AUREA_CHECK(held / compPerVideo * kScreenPerVideo < 1.0);

    // Aplicar numa camada escolhida: um sólido pequeno segue o anel.
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(rig.e.apply_command(undo).ok());
    const LayerId solidId = rig.comp()->add_layer(LayerKind::Shape, "Solido");
    Layer* solid = rig.comp()->layer(solidId);
    solid->start = FrameIndex{0};
    solid->end = FrameIndex{150};
    solid->shape.bounds = Rect{0, 0, 40, 40};
    solid->shape.filled = true;
    solid->transform.anchor = Vec3{20, 20, 0};
    solid->transform.position = Vec3{300, 200, 0};
    AUREA_CHECK(rig.e.apply_motion_track(solidId.pack(), 1).ok());
    solid = rig.comp()->layer(solidId);
    video = rig.layer(rig.video);
    f64 follow = 0;
    const Track* sx = solid->tracks.find(TrackProperty::PositionX);
    const Track* sy = solid->tracks.find(TrackProperty::PositionY);
    AUREA_CHECK(sx && sy);
    if (!sx || !sy) return;
    const Vec2 ring77 = to_comp(layer_comp_matrix(*rig.comp(), *video, FrameIndex{77}), truth0);
    for (i64 f = 77; f < 150; ++f) {
        const Vec2 ring = to_comp(layer_comp_matrix(*rig.comp(), *video, FrameIndex{f}), ring_center(f / 30.0));
        const FrameIndex local = solid->local_time(FrameIndex{f});
        follow = std::max(follow, static_cast<f64>(std::hypot((sx->sample_keys(local) - 300) - (ring.x - ring77.x), (sy->sample_keys(local) - 200) - (ring.y - ring77.y))));
    }
    std::printf("    aplicar na camada: o solido segue o anel com desvio maximo %.2f px da composicao\n", follow);
    AUREA_CHECK(follow < 2.0 * compPerVideo);

    // Estabilizador global no mesmo vídeo: o FUNDO (tremor da câmera) para.
    AUREA_CHECK(rig.e.start_motion_track(rig.video, 4, 0, false, nullptr, 0));
    const auto global = rig.wait_motion();
    AUREA_CHECK_EQ(global.state, 2u);
    AUREA_CHECK(rig.e.apply_motion_track(0, 3, true, .5f, 1.15f, 1).ok());
    video = rig.layer(rig.video);
    f64 bgHeld = 0, bgRaw = 0;
    for (Vec2 world : {Vec2{150, 900}, Vec2{600, 200}, Vec2{500, 1100}}) {
        auto shown = [&](i64 f) { const Shake s = ring_shake(f / 30.0); return Vec2{world.x - s.x, world.y - s.y}; };
        const Vec2 a = shown_in_comp(rig, *video, 0, shown(0));
        for (i64 f = 0; f < 150; ++f) {
            const Vec2 q = shown_in_comp(rig, *video, f, shown(f));
            bgHeld = std::max(bgHeld, static_cast<f64>(std::hypot(q.x - a.x, q.y - a.y)));
            bgRaw = std::max(bgRaw, static_cast<f64>(std::hypot(shown(f).x - shown(0).x, shown(f).y - shown(0).y)) * compPerVideo);
        }
    }
    std::printf("    estabilizador global: o fundo tremia ate %.1f px da composicao; parado a %.2f px\n", bgRaw, bgHeld);
    AUREA_CHECK(bgHeld < 1.5);
}

