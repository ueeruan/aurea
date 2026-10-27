// =============================================================================
//  Aurea / engine / EngineMocap.cpp
//
//  Motion Capture (0.3.6 BETA): a sessão que liga a detecção de pose das
//  plataformas ao solver do motor (rig/Mocap.hpp), à prévia transitória da
//  camada e às keys de osso (Engine::set_bone_keys, uma ação de desfazer).
//
//  Travas: `mocapMutex_` só guarda o ponteiro da sessão; a sessão tem a dela
//  (`work`), segurada durante o solver — nunca junto com `modelMutex_`, que
//  só é pego para ler a camada ou trocar a prévia.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/rig/Mocap.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>

namespace aurea {

struct Engine::MocapSession {
    std::mutex work;
    u64 layer = 0;
    rig::HumanoidRig rig;
    bool ready = false;            ///< rig humanoide usável
    i64 startFrame = 0;            ///< quadro local onde a animação começa
    f64 fps = 30.0;
    std::vector<rig::PoseFrame> frames;
    f32 aspect = 1.0f;
    rig::MocapParams params;
    rig::MocapResult result;
    rig::MocapTimeline timeline;
    bool solved = false;
    bool previewOn = false;
    std::atomic<bool> cancel{false};
    std::atomic<f32> progress{0.0f};
};

namespace {

rig::MocapParams params_from(const f32* p, u32 n, rig::MocapParams base) noexcept {
    auto at = [&](u32 i, f32 def) { return p && i < n && std::isfinite(p[i]) ? p[i] : def; };
    base.intensity = at(0, base.intensity);
    base.smoothing = at(1, base.smoothing);
    base.speed = at(2, base.speed);
    base.yawDeg = at(3, base.yawDeg);
    base.floorOffset = at(4, base.floorOffset);
    base.footLock = at(5, base.footLock ? 1.0f : 0.0f) > 0.5f;
    base.handIk = at(6, base.handIk ? 1.0f : 0.0f) > 0.5f;
    base.rootMotion = at(7, base.rootMotion ? 1.0f : 0.0f) > 0.5f;
    base.footIk = true;
    return base.sanitized();
}

} // namespace

std::shared_ptr<Engine::MocapSession> Engine::mocap_session() noexcept {
    std::lock_guard<std::mutex> lock(mocapMutex_);
    return mocap_;
}

void Engine::set_mocap_preview(u64 layerId, std::shared_ptr<const rig::MocapPreview> preview) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D) return;
    if (!preview && !l->model.mocapPreview) return;
    // Transitório: não passa pelo histórico nem marca o projeto como mudado.
    l->model.mocapPreview = std::move(preview);
    bump_model_revision_locked();
    request_render();
}

i32 Engine::mocap_begin(u64 layerId) noexcept {
    mocap_end();
    i64 start = 0;
    f64 fps = 30.0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!l || l->kind != LayerKind::Model3D) return -1;
        fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        const FrameIndex now = playback_.current();
        // Começa no cabeçote quando ele está dentro da camada; senão no início dela.
        start = l->contains_time(now) ? std::max<i64>(0, static_cast<i64>(l->local_time(now).value)) : 0;
    }
    const i32 status = humanoid_status(layerId);
    auto s = std::make_shared<MocapSession>();
    s->layer = layerId;
    s->startFrame = start;
    s->fps = fps;
    s->ready = status >= 2 && query_humanoid(layerId, s->rig) && s->rig.map.usable();
    std::lock_guard<std::mutex> lock(mocapMutex_);
    mocap_ = std::move(s);
    return status;
}

bool Engine::mocap_push_frames(const f32* data, u32 frames, f32 aspect) noexcept {
    const auto s = mocap_session();
    if (!s || !data || frames == 0) return false;
    std::lock_guard<std::mutex> lock(s->work);
    // Teto de memória: 20 min a 30 qps (≈ 19 MB de pontos).
    constexpr usize kMaxFrames = 36000;
    if (s->frames.size() + frames > kMaxFrames) frames = static_cast<u32>(kMaxFrames - std::min(kMaxFrames, s->frames.size()));
    rig::unpack_frames(data, frames, s->frames);
    if (std::isfinite(aspect) && aspect > 0.0f) s->aspect = aspect;
    s->solved = false;
    return true;
}

bool Engine::mocap_analyze(const f32* params, u32 count) noexcept {
    const auto s = mocap_session();
    if (!s) return false;
    std::lock_guard<std::mutex> lock(s->work);
    s->cancel.store(false);
    s->progress.store(0.0f);
    if (!s->ready || s->frames.size() < 2) return false;
    s->params = params_from(params, count, s->params);
    s->params.imageAspect = s->aspect;
    rig::MocapControl control;
    control.cancel = &s->cancel;
    control.progress = &s->progress;
    rig::MocapResult result;
    if (!rig::solve(s->frames, s->rig.rest, s->params, result, control)) return false;
    s->result = std::move(result);
    rig::resample(s->result, s->params, s->fps, s->startFrame, s->timeline);
    s->solved = !s->timeline.local.empty();
    s->progress.store(1.0f);
    if (s->solved && s->previewOn)
        set_mocap_preview(s->layer, std::make_shared<const rig::MocapPreview>(
            rig::make_preview(s->timeline, s->rig.map, s->params.rootMotion)));
    return s->solved;
}

void Engine::mocap_cancel() noexcept {
    if (const auto s = mocap_session()) s->cancel.store(true);
}

f32 Engine::mocap_progress() noexcept {
    const auto s = mocap_session();
    return s ? s->progress.load() : 0.0f;
}

u32 Engine::mocap_report(f32* out, u32 capacity) noexcept {
    if (!out || capacity < kMocapReportFloats) return kMocapReportFloats;
    std::fill(out, out + kMocapReportFloats, 0.0f);
    const auto s = mocap_session();
    if (!s) return kMocapReportFloats;
    std::lock_guard<std::mutex> lock(s->work);
    if (!s->solved) {
        out[0] = static_cast<f32>(s->frames.size());
        return kMocapReportFloats;
    }
    const rig::MocapReport& r = s->result.report;
    out[0] = static_cast<f32>(r.frames);
    out[1] = static_cast<f32>(r.duration);
    out[2] = r.hasWorld ? 1.0f : 0.0f;
    out[3] = r.lowConfidence ? 1.0f : 0.0f;
    for (usize m = 0; m < rig::kLimbCount; ++m) out[4 + m] = r.lowFraction[m];
    out[10] = r.footLockedFraction;
    out[11] = r.interpolatedFraction;
    return kMocapReportFloats;
}

bool Engine::mocap_preview(bool on) noexcept {
    const auto s = mocap_session();
    if (!s) return false;
    std::lock_guard<std::mutex> lock(s->work);
    s->previewOn = on;
    if (!on || !s->solved) {
        set_mocap_preview(s->layer, nullptr);
        return !on;
    }
    set_mocap_preview(s->layer, std::make_shared<const rig::MocapPreview>(
        rig::make_preview(s->timeline, s->rig.map, s->params.rootMotion)));
    return true;
}

i32 Engine::mocap_apply() noexcept {
    const auto s = mocap_session();
    if (!s) return -1;
    std::lock_guard<std::mutex> lock(s->work);
    if (!s->solved || s->timeline.local.empty()) return -1;
    std::vector<rig::BoneKey> keys;
    rig::bake_keys(s->timeline, s->rig.map, s->rig.rest, s->params.rootMotion, 0.35f, keys);
    if (keys.empty()) return -1;
    set_mocap_preview(s->layer, nullptr);
    s->previewOn = false;
    const i64 from = s->startFrame;
    const i64 to = s->startFrame + static_cast<i64>(s->timeline.local.size()) - 1;
    // Uma ação de desfazer: tirar o clipe do arquivo (a captura parte da T-pose),
    // desligar os IK de mãos/pés (a captura já os resolve) e gravar as keys.
    {
        std::lock_guard<std::mutex> lock2(modelMutex_);
        Composition* comp = project_ ? current_composition() : nullptr;
        Layer* l = comp ? comp->layer(LayerId::unpack(s->layer)) : nullptr;
        if (!l || l->kind != LayerKind::Model3D || l->locked) return -1;
        history_.begin_group("Motion Capture");
        bool ikTracks = false;
        for (u32 i = 0; i < l->tracks.size(); ++i) {
            const Track& t = l->tracks.at(i);
            ikTracks = ikTracks || (t.property == TrackProperty::BonePose && t.effectIndex >= rig::kBoneIkBase);
        }
        if (l->model.animationClip >= 0 || ikTracks) {
            history_.before_mutation(*comp, project_->timeline().current(), "Motion Capture");
            l->model.animationClip = -1;
            const auto& m = s->rig.map;
            const i32 ends[4]{m[rig::HumanoidBone::LeftHand], m[rig::HumanoidBone::RightHand],
                              m[rig::HumanoidBone::LeftFoot], m[rig::HumanoidBone::RightFoot]};
            l->tracks.remove_if([&](const Track& t) {
                if (t.property != TrackProperty::BonePose || t.effectIndex < rig::kBoneIkBase) return false;
                const i32 end = static_cast<i32>(t.effectIndex - rig::kBoneIkBase);
                return std::find(std::begin(ends), std::end(ends), end) != std::end(ends);
            });
            bump_model_revision_locked();
        }
    }
    const Status st = set_bone_keys(s->layer, keys.data(), keys.size(), "Motion Capture", from, to);
    {
        std::lock_guard<std::mutex> lock2(modelMutex_);
        history_.end_group();
    }
    if (!st.ok()) return -1;
    return static_cast<i32>(std::min<usize>(keys.size(), 0x7fffffff));
}

void Engine::mocap_end() noexcept {
    std::shared_ptr<MocapSession> s;
    {
        std::lock_guard<std::mutex> lock(mocapMutex_);
        s = std::move(mocap_);
        mocap_.reset();
    }
    if (!s) return;
    s->cancel.store(true);
    std::lock_guard<std::mutex> lock(s->work);
    set_mocap_preview(s->layer, nullptr);
}

} // namespace aurea
