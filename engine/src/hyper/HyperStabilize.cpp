// =============================================================================
//  Aurea / hyper / HyperStabilize.cpp — análise da fonte e keyframes.
// =============================================================================
#include "HyperStabilize.hpp"

#include "aurea/timeline/Layer.hpp"
#include "tracking/AnalysisDecode.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::hyper {

Status analyze_stabilization(const StabAnalysis& rq, StabPlan& out) {
    out = StabPlan{};
    if (!rq.factory || !rq.asset || rq.frames == 0) return Status{Errc::InvalidArgument, "estabilizacao sem fonte"};
    static const std::atomic<bool> kNever{false};
    const std::atomic<bool>& cancel = rq.cancel ? *rq.cancel : kNever;
    auto decoder = rq.factory->open_video(*rq.asset, MediaPriority::Thumbnail);
    if (!decoder) return Status{Errc::DecodeFailed, "video ilegivel para a estabilizacao"};
    const f64 fps = rq.fps > 0.0 ? rq.fps : 30.0;
    tracking::AnalysisFramePicker picker(*decoder, fps, cancel);
    const u32 srcW = rq.asset->video.width, srcH = rq.asset->video.height;
    if (!srcW || !srcH) return Status{Errc::InvalidArgument, "fonte sem dimensoes"};
    // ¼ da resolução (no mínimo 144 linhas, no máximo 360).
    const u32 analysisH = std::clamp<u32>(srcH / 4, std::min<u32>(144, srcH), 360);
    std::vector<Motion2D> steps;
    steps.reserve(static_cast<usize>(rq.frames));
    tracking::Gray prev, cur;
    for (u64 i = 0; i < rq.frames; ++i) {
        if (cancel.load(std::memory_order_relaxed)) return Errc::Cancelled;
        FrameRef frame;
        const i64 us = static_cast<i64>(std::llround(static_cast<f64>(i) * 1e6 / fps));
        const auto picked = picker.pick(us, frame);
        if (picked == tracking::AnalysisFramePicker::Result::Cancelled) return Errc::Cancelled;
        if (picked != tracking::AnalysisFramePicker::Result::Ok || !frame) {
            // Fim antes do previsto (duração do container arredondada): o resto parado.
            steps.resize(static_cast<usize>(rq.frames), Motion2D{});
            break;
        }
        if (!tracking::frame_to_gray(*frame.get(), analysisH, cur)) return Status{Errc::DecodeFailed, "quadro sem planos na CPU"};
        frame = {};
        steps.push_back(i == 0 ? Motion2D{} : estimate_motion(prev, cur));
        std::swap(prev, cur);
        if (rq.progress) rq.progress(rq.progressContext, static_cast<f32>(i + 1) / static_cast<f32>(rq.frames));
    }
    const f64 scale = prev.height ? static_cast<f64>(srcH) / prev.height : 1.0;
    out = smooth_trajectory(steps, rq.strength, srcW, srcH, scale, rq.crop);
    return OkStatus;
}

void apply_stabilization(Layer& layer, const StabPlan& plan, f32 layerScale, f64 compFramesPerSourceFrame, bool hold) {
    if (plan.frames.empty()) return;
    const f32 z = std::max(1.0f, plan.zoom);
    Track& px = layer.tracks.get_or_create(TrackProperty::PositionX);
    Track& py = layer.tracks.get_or_create(TrackProperty::PositionY);
    Track& rz = layer.tracks.get_or_create(TrackProperty::RotationZ);
    Track& sx = layer.tracks.get_or_create(TrackProperty::ScaleX);
    Track& sy = layer.tracks.get_or_create(TrackProperty::ScaleY);
    for (Track* t : {&px, &py, &rz, &sx, &sy}) t->keys.clear();
    const Vec3 center = layer.transform.position;
    // p_estável = s·R(θ)·(p − c) + c + t ; na composição: posição = centro + L·z·t,
    // rotação = θ, escala = L·z·s (âncora no centro da fonte).
    i64 last = -1;
    for (usize k = 0; k < plan.frames.size(); ++k) {
        const StabCorrection& c = plan.frames[k];
        const i64 t = static_cast<i64>(std::llround(static_cast<f64>(k) * compFramesPerSourceFrame));
        if (t <= last) continue;
        last = t;
        auto key = [&](Track& tr, f32 v) {
            Keyframe kf;
            kf.time = FrameIndex{t};
            kf.value = v;
            kf.interp = hold ? Interpolation::Hold : Interpolation::Linear;
            tr.keys.push_back(kf);
        };
        key(px, center.x + layerScale * z * c.dx);
        key(py, center.y + layerScale * z * c.dy);
        key(rz, c.degrees);
        key(sx, layerScale * z * c.scale);
        key(sy, layerScale * z * c.scale);
    }
}

} // namespace aurea::hyper
