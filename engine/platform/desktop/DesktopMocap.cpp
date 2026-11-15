// =============================================================================
//  Aurea / platform / desktop / DesktopMocap.cpp
//
//  Aurea PC: a análise de pose do Motion Capture (com.aurea.aurea.desktop.
//  DesktopMocap). No celular quem faz isto é o MocapDetector.kt (ML Kit) e o
//  MocapDetector.swift (Vision); no PC o vídeo é decodificado pelo MESMO
//  Media Foundation do palco (windows::MediaFactory) e a pose sai do BlazePose
//  do motor (aurea/ai/PoseEstimator.hpp). Só entra na aurea.dll do PC (e no
//  aurea_pc_mocap_bench).
//
//  A UI puxa os quadros (sem callback da JNI): abre a sessão, pede lotes até o
//  fim (progresso e cancelamento entre um lote e outro) e fecha. Cada quadro
//  é o rig::kMocapFrameFloats do motor — o mesmo que o Android manda — e, no
//  PC, seguido do quadro companheiro com os pontos de MUNDO do BlazePose GHUM
//  3D (rig::kMocapWorldFlag), que o solver usa no lugar do 3D adivinhado.
//
//  Qualidade (13/11, medida com aurea_pc_mocap_bench nos vídeos do dono):
//  vídeo na resolução própria (o recorte de 256 px sai da pirâmide, sem
//  serrilhado), modelo heavy com refino pelo heatmap, ROI pelos pontos
//  auxiliares filtrados, detector de novo quando a ROI escapa e trocas de
//  lado desfeitas (mocap::VideoPoseTracker). Options::legacy() refaz o de 12/11.
// =============================================================================
#include "DesktopMocap.hpp"

#include "WindowsMedia.hpp"
#include "aurea/ai/PoseEstimator.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/media/ThumbnailService.hpp"
#include "aurea/project/Asset.hpp"

#include <jni.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace aurea::desktop_mocap {

Options Options::defaults(bool reduced) noexcept {
    Options o;
    if (reduced) {
        // Modo leve: o full, metade dos quadros e a imagem até 960 px.
        o.fps = 15.0;
        o.maxSide = 960;
        o.heavy = false;
        o.refine = false;
    }
    return o;
}

Options Options::legacy(bool reduced) noexcept {
    Options o;
    o.fps = reduced ? 15.0 : 30.0;
    o.maxSide = reduced ? 256 : 384;
    o.legacyImage = true;
    o.heavy = false;
    o.antialias = false;
    o.refine = false;
    o.world = false;
    o.videoTracker = false;
    o.filterRoi = false;
    o.fixSwaps = false;
    o.borderRedetect = false;
    o.recheckEvery = 0;
    return o;
}

struct Session {
    windows::MediaFactory factory;
    std::unique_ptr<VideoDecoderBackend> decoder;
    mocap::PoseEstimator estimator;
    mocap::PoseTracker tracker{estimator};
    std::unique_ptr<mocap::VideoPoseTracker> video;
    Options options;
    i64 durationUs = 0;
    i64 firstPts = std::numeric_limits<i64>::min();
    i64 halfSourceUs = 16'666;
    f64 stepUs = 1e6 / 30.0;
    f64 nextSampleUs = 0.0;      ///< relativo ao primeiro quadro
    f64 lastT = -1.0;
    bool done = false;
    f32 progress = 0.0f;
    f32 aspect = 1.0f;
    f64 netMs = 0.0;
    f64 imageMs = 0.0;
    u32 frames = 0;
    u32 withPerson = 0;
    ThumbnailService::Image image;
    mocap::PoseImage pyramid;
};

namespace {

/// Média k×k em YUV → RGBA8 sRGB, já girado como a camada mostra. Linhas em
/// paralelo. false = formato que esta conta não lê (o chamador usa o caminho comum).
bool convert_box(const DecodedFrame& f, u32 k, ThumbnailService::Image& out) {
    const bool threePlane = f.format == PixelFormat::YUV420P;
    const bool tenBit = f.format == PixelFormat::P010;
    const bool nv21 = f.format == PixelFormat::NV21;
    if (!threePlane && !tenBit && !nv21 && f.format != PixelFormat::NV12) return false;
    if (f.planeCount < 2 || !f.planes[0] || !f.planes[1] || (threePlane && (f.planeCount < 3 || !f.planes[2]))) return false;
    const u32 vw = f.visibleWidth ? f.visibleWidth : f.width;
    const u32 vh = f.visibleHeight ? f.visibleHeight : f.height;
    if (!vw || !vh || static_cast<u64>(f.cropLeft) + vw > f.width || static_cast<u64>(f.cropTop) + vh > f.height) return false;
    const u64 chromaWidth = (static_cast<u64>(f.width) + 1) / 2;
    if (f.strides[0] < static_cast<u64>(f.width) * (tenBit ? 2 : 1)
        || f.strides[1] < chromaWidth * (threePlane ? 1 : tenBit ? 4 : 2) || (threePlane && f.strides[2] < chromaWidth)) return false;
    const u32 rot = f.rotation % 360;
    const bool sideways = rot == 90 || rot == 270;
    const u32 dispW = sideways ? vh : vw, dispH = sideways ? vw : vh;
    k = std::max(1u, k);
    const u32 ow = dispW / k, oh = dispH / k;
    if (!ow || !oh) return false;
    out.width = ow;
    out.height = oh;
    out.rgba.resize(static_cast<usize>(ow) * oh * 4);
    f32 kr = 0.2126f, kb = 0.0722f;
    f.color.coefficients(kr, kb);
    const f32 kg = 1.0f - kr - kb;
    const bool full = f.color.fullRange;
    auto luma = [&](u32 x, u32 y) -> f32 {
        if (tenBit) {
            const u8* p = f.planes[0] + static_cast<usize>(y) * f.strides[0] + static_cast<usize>(x) * 2;
            return static_cast<f32>(static_cast<u16>(p[0] | (p[1] << 8)) >> 6) * (1.0f / 1023.0f);
        }
        return static_cast<f32>(f.planes[0][static_cast<usize>(y) * f.strides[0] + x]) * (1.0f / 255.0f);
    };
    auto chroma = [&](u32 x, u32 y, f32& cb, f32& cr) {
        const u32 cx = x / 2, cy = y / 2;
        if (threePlane) {
            cb = f.planes[1][static_cast<usize>(cy) * f.strides[1] + cx] * (1.0f / 255.0f);
            cr = f.planes[2][static_cast<usize>(cy) * f.strides[2] + cx] * (1.0f / 255.0f);
        } else if (tenBit) {
            const u8* p = f.planes[1] + static_cast<usize>(cy) * f.strides[1] + static_cast<usize>(cx) * 4;
            cb = static_cast<f32>(static_cast<u16>(p[0] | (p[1] << 8)) >> 6) * (1.0f / 1023.0f);
            cr = static_cast<f32>(static_cast<u16>(p[2] | (p[3] << 8)) >> 6) * (1.0f / 1023.0f);
        } else {
            const u8* p = f.planes[1] + static_cast<usize>(cy) * f.strides[1] + static_cast<usize>(cx) * 2;
            cb = p[nv21 ? 1 : 0] * (1.0f / 255.0f);
            cr = p[nv21 ? 0 : 1] * (1.0f / 255.0f);
        }
    };
    // Pixel de exibição (dx, dy) → pixel codificado.
    auto source = [&](u32 dx, u32 dy, u32& sx, u32& sy) {
        switch (rot) {
            case 90:  sx = dy;            sy = vh - 1 - dx; break;
            case 180: sx = vw - 1 - dx;   sy = vh - 1 - dy; break;
            case 270: sx = vw - 1 - dy;   sy = dx;          break;
            default:  sx = dx;            sy = dy;          break;
        }
        sx += f.cropLeft;
        sy += f.cropTop;
    };
    const f32 inv = 1.0f / static_cast<f32>(k * k);
    auto rows = [&](u32 y0, u32 y1) {
        for (u32 oy = y0; oy < y1; ++oy) {
            u8* px = out.rgba.data() + static_cast<usize>(oy) * ow * 4;
            for (u32 ox = 0; ox < ow; ++ox, px += 4) {
                f32 yy = 0.0f, cb = 0.0f, cr = 0.0f;
                for (u32 j = 0; j < k; ++j)
                    for (u32 i = 0; i < k; ++i) {
                        u32 sx, sy;
                        source(ox * k + i, oy * k + j, sx, sy);
                        yy += luma(sx, sy);
                        f32 b, r;
                        chroma(sx, sy, b, r);
                        cb += b;
                        cr += r;
                    }
                yy *= inv;
                f32 pb = cb * inv - 0.5f, pr = cr * inv - 0.5f;
                if (!full) {
                    yy = (yy - 16.0f / 255.0f) * (255.0f / 219.0f);
                    pb *= 255.0f / 224.0f;
                    pr *= 255.0f / 224.0f;
                }
                const f32 r = yy + 2.0f * (1.0f - kr) * pr;
                const f32 b = yy + 2.0f * (1.0f - kb) * pb;
                const f32 g = (yy - kr * r - kb * b) / kg;
                auto u8c = [](f32 v) { return static_cast<u8>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f)); };
                px[0] = u8c(r);
                px[1] = u8c(g);
                px[2] = u8c(b);
                px[3] = 255;
            }
        }
    };
    const u32 hw = std::max(1u, std::thread::hardware_concurrency());
    const u32 parts = std::clamp(hw / 2, 1u, 6u);
    if (parts <= 1 || oh < 64) {
        rows(0, oh);
        return true;
    }
    std::vector<std::thread> pool;
    pool.reserve(parts - 1);
    const u32 band = (oh + parts - 1) / parts;
    for (u32 p = 1; p < parts; ++p) {
        const u32 a = p * band, b = std::min(oh, a + band);
        if (a < b) pool.emplace_back(rows, a, b);
    }
    rows(0, std::min(oh, band));
    for (std::thread& t : pool) t.join();
    return true;
}

/// Quadro decodificado → RGBA8 no tamanho da análise (girado como a camada mostra).
bool frame_rgba(Session& s, DecodedFrame& f) {
    if (!f.prepare_cpu_planes()) return false;
    const u32 vw = f.visibleWidth ? f.visibleWidth : f.width;
    const u32 vh = f.visibleHeight ? f.visibleHeight : f.height;
    const bool sideways = f.rotation == 90 || f.rotation == 270;
    const f64 dispW = sideways ? vh : vw, dispH = sideways ? vw : vh;
    if (dispW <= 0 || dispH <= 0) return false;
    const f64 longest = std::max(dispW, dispH);
    if (s.options.legacyImage) {
        const f64 k = std::min(1.0, static_cast<f64>(std::max(16u, s.options.maxSide)) / longest);
        const u32 height = std::max<u32>(1, static_cast<u32>(std::lround(dispH * k)));
        return frame_to_thumbnail(f, height, s.image) && s.image.width && s.image.height;
    }
    // Redução inteira por média (sem serrilhado); 0 = a resolução do vídeo.
    const u32 k = s.options.maxSide ? std::max(1u, static_cast<u32>(std::ceil(longest / s.options.maxSide - 1e-6))) : 1u;
    if (convert_box(f, k, s.image)) return true;
    const u32 height = std::max<u32>(1, static_cast<u32>(std::lround(dispH / k)));
    return frame_to_thumbnail(f, std::min(height, 4096u), s.image) && s.image.width && s.image.height;
}

} // namespace

Session* open(const std::string& modelDir, const std::string& videoPath, bool reduced, f32 info[kInfoFloats]) {
    return open(modelDir, videoPath, Options::defaults(reduced), info);
}

Session* open(const std::string& modelDir, const std::string& videoPath, const Options& options, f32 info[kInfoFloats]) {
    std::fill(info, info + kInfoFloats, 0.0f);
    std::unique_ptr<Session> s(new (std::nothrow) Session);
    if (!s) return nullptr;
    s->options = options;
    const Status loaded = s->estimator.load(modelDir, mocap::PoseEstimator::Backend::Auto, 0,
                                            options.heavy ? mocap::LandmarkModel::Heavy : mocap::LandmarkModel::Full);
    if (!loaded.ok()) {
        AUREA_LOG_WARN("mocap: modelos de pose nao carregaram (%s) em %s", std::string(loaded.message()).c_str(), modelDir.c_str());
        info[0] = 1.0f;
        return nullptr;
    }
    s->estimator.set_refine(options.refine, options.antialias);
    if (options.videoTracker) {
        mocap::VideoTrackerOptions vt;
        vt.filterRoi = options.filterRoi;
        vt.fixSwaps = options.fixSwaps;
        vt.borderRedetect = options.borderRedetect;
        vt.recheckEvery = options.recheckEvery;
        s->video = std::make_unique<mocap::VideoPoseTracker>(s->estimator, vt);
    }
    Asset asset;
    asset.sourcePath = videoPath;
    s->decoder = s->factory.open_video(asset, MediaPriority::Thumbnail);
    if (!s->decoder) {
        AUREA_LOG_WARN("mocap: video nao abriu: %s", videoPath.c_str());
        info[0] = 2.0f;
        return nullptr;
    }
    const VideoStreamInfo& vi = s->decoder->info();
    s->durationUs = std::max<i64>(0, vi.durationUs);
    const f64 fps = vi.fps > 1.0 && vi.fps < 1000.0 ? vi.fps : 30.0;
    s->halfSourceUs = std::max<i64>(1, static_cast<i64>(5e5 / fps));
    s->stepUs = 1e6 / std::clamp(options.fps, 1.0, 240.0);
    const bool vulkan = s->estimator.backend() == mocap::PoseEstimator::Backend::Vulkan;
    AUREA_LOG_INFO("mocap: %ux%u %.2f fps %.1f s, pose %s em %s, imagem ate %u px (0 = inteira)", vi.display_width(),
                   vi.display_height(), fps, static_cast<f64>(s->durationUs) * 1e-6,
                   s->estimator.model() == mocap::LandmarkModel::Heavy ? "heavy" : "full", vulkan ? "Vulkan" : "CPU",
                   options.maxSide);
    info[1] = static_cast<f32>(static_cast<f64>(s->durationUs) * 1e-6);
    info[2] = vulkan ? 1.0f : 0.0f;
    info[3] = static_cast<f32>(vi.display_width());
    info[4] = static_cast<f32>(vi.display_height());
    info[5] = static_cast<f32>(fps);
    return s.release();
}

i32 next(Session* s, f32* out, i32 maxFrames, f32 state[kStateFloats]) {
    if (!s || !out || maxFrames <= 0) return -1;
    const i32 slots = s->options.world ? 2 : 1;
    i32 count = 0;
    u32 retries = 0, empty = 0;
    bool failed = false;
    while (count + slots <= maxFrames && !s->done) {
        // Os quadros antes da próxima amostra são decodificados e descartados sem imagem.
        const i64 deliverFrom = s->firstPts == std::numeric_limits<i64>::min()
            ? std::numeric_limits<i64>::min() / 4
            : s->firstPts + static_cast<i64>(s->nextSampleUs) - s->halfSourceUs;
        FrameRef frame;
        i64 pts = 0;
        bool eos = false;
        const Status st = s->decoder->next_frame(deliverFrom, frame, pts, eos);
        if (!st.ok()) {
            if (st.code() == Errc::BudgetExceeded && ++retries < 200) continue;
            AUREA_LOG_WARN("mocap: decoder parou (%s) depois de %u quadros", std::string(st.message()).c_str(), s->frames);
            failed = s->frames == 0;
            s->done = true;
            break;
        }
        if (!frame) {
            if (eos || ++empty > 100000) s->done = true;
            continue;
        }
        if (s->firstPts == std::numeric_limits<i64>::min()) s->firstPts = pts;
        const i64 t = std::max<i64>(0, pts - s->firstPts);
        if (s->durationUs > 0)
            s->progress = std::clamp(static_cast<f32>(static_cast<f64>(pts) / static_cast<f64>(s->durationUs)), 0.0f, 1.0f);
        mocap::PoseOutput pose{};
        const f64 tSec = static_cast<f64>(t) * 1e-6;
        const u64 i0 = monotonic_ns();
        const bool haveImage = frame_rgba(*s, *frame.get());
        if (haveImage) {
            s->aspect = static_cast<f32>(s->image.width) / static_cast<f32>(s->image.height);
            if (s->video) {
                s->pyramid.build(s->image.rgba.data(), s->image.width, s->image.height, s->image.width * 4, 4);
                s->imageMs += static_cast<f64>(monotonic_ns() - i0) * 1e-6;
                s->estimator.reset_ms();
                const f32 dt = s->lastT >= 0.0 ? static_cast<f32>(tSec - s->lastT) : 1.0f / 30.0f;
                if (s->video->track(s->pyramid, std::max(dt, 1e-3f), pose)) ++s->withPerson;
            } else {
                s->imageMs += static_cast<f64>(monotonic_ns() - i0) * 1e-6;
                if (s->tracker.track(s->image.rgba.data(), s->image.width, s->image.height, s->image.width * 4, pose.image)) {
                    ++s->withPerson;
                    pose.presence = 1.0f;
                }
            }
            s->netMs += s->estimator.last_ms();
            s->lastT = tSec;
        }
        // Sem imagem legível ou sem pessoa: o quadro vai com confiança 0 (o motor interpola).
        f32* dst = out + static_cast<usize>(count) * rig::kMocapFrameFloats;
        mocap::pack_frame(tSec, pose.image, dst);
        ++count;
        if (s->options.world) {
            mocap::pack_world_frame(tSec, pose, dst + rig::kMocapFrameFloats);
            ++count;
        }
        ++s->frames;
        do s->nextSampleUs += s->stepUs; while (s->nextSampleUs <= static_cast<f64>(t));
        if (eos) s->done = true;
    }
    if (s->done) s->progress = 1.0f;
    state[0] = s->progress;
    state[1] = s->aspect;
    state[2] = s->done ? 1.0f : 0.0f;
    state[3] = s->frames ? static_cast<f32>(s->netMs / s->frames) : 0.0f;
    state[4] = static_cast<f32>(s->video ? s->video->detector_runs() : s->tracker.detector_runs());
    state[5] = static_cast<f32>(s->withPerson);
    return failed ? -1 : count;
}

FrameView last_image(const Session* s) {
    if (!s || s->image.rgba.empty()) return {};
    return FrameView{s->image.rgba.data(), s->image.width, s->image.height};
}

u32 swaps_fixed(const Session* s) { return s && s->video ? s->video->swaps_fixed() : 0u; }
f64 image_ms(const Session* s) { return s ? s->imageMs : 0.0; }

void close(Session* s) {
    if (!s) return;
    AUREA_LOG_INFO("mocap: %u quadros, %u com pessoa, detector %u vezes, %.1f ms de rede e %.1f ms de imagem por quadro, %u trocas de lado desfeitas",
                   s->frames, s->withPerson, s->video ? s->video->detector_runs() : s->tracker.detector_runs(),
                   s->frames ? s->netMs / s->frames : 0.0, s->frames ? s->imageMs / s->frames : 0.0, swaps_fixed(s));
    delete s;
}

} // namespace aurea::desktop_mocap

// ---- JNI (com.aurea.aurea.desktop.DesktopMocap) ---------------------------------

namespace {

std::string utf8_of(JNIEnv* env, jstring s) {
    if (!s) return {};
    const jsize n = env->GetStringLength(s);
    const jchar* chars = env->GetStringChars(s, nullptr);
    if (!chars) return {};
    const std::wstring wide(reinterpret_cast<const wchar_t*>(chars), static_cast<size_t>(n));
    env->ReleaseStringChars(s, chars);
    return aurea::windows::utf8(wide);
}

void put(JNIEnv* env, jfloatArray array, const aurea::f32* values, jsize count) {
    if (!array) return;
    env->SetFloatArrayRegion(array, 0, std::min(env->GetArrayLength(array), count), values);
}

} // namespace

#define AUREA_MOCAP_FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_aurea_aurea_desktop_DesktopMocap_##name

AUREA_MOCAP_FN(jlong, nativeOpen)(JNIEnv* env, jclass, jstring modelDir, jstring videoPath, jboolean reduced, jfloatArray info) {
    aurea::f32 values[aurea::desktop_mocap::kInfoFloats]{};
    auto* s = aurea::desktop_mocap::open(utf8_of(env, modelDir), utf8_of(env, videoPath), reduced == JNI_TRUE, values);
    put(env, info, values, aurea::desktop_mocap::kInfoFloats);
    return reinterpret_cast<jlong>(s);
}

AUREA_MOCAP_FN(jint, nativeNext)(JNIEnv* env, jclass, jlong handle, jfloatArray out, jint maxFrames, jfloatArray state) {
    auto* s = reinterpret_cast<aurea::desktop_mocap::Session*>(handle);
    if (!s || !out) return -1;
    const jint capacity = env->GetArrayLength(out) / static_cast<jint>(aurea::rig::kMocapFrameFloats);
    const jint limit = std::min(maxFrames, capacity);
    if (limit <= 0) return -1;
    std::vector<aurea::f32> buffer(static_cast<size_t>(limit) * aurea::rig::kMocapFrameFloats);
    aurea::f32 values[aurea::desktop_mocap::kStateFloats]{};
    const aurea::i32 n = aurea::desktop_mocap::next(s, buffer.data(), limit, values);
    if (n > 0) env->SetFloatArrayRegion(out, 0, n * static_cast<jsize>(aurea::rig::kMocapFrameFloats), buffer.data());
    put(env, state, values, aurea::desktop_mocap::kStateFloats);
    return n;
}

AUREA_MOCAP_FN(void, nativeClose)(JNIEnv*, jclass, jlong handle) {
    aurea::desktop_mocap::close(reinterpret_cast<aurea::desktop_mocap::Session*>(handle));
}
