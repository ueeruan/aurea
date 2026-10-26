// =============================================================================
//  Aurea / engine / EngineOmegaTimeline.cpp
//
//  Omega 0.3.2, área 03 (timeline): regras de edição que a timeline da
//  referência pede e o motor ainda não tinha. A UI (Android e iOS) só chama;
//  a conta mora aqui, igual para as duas plataformas.
//
//  P1-8 · alças de velocidade: esticar a camada muda a velocidade e mantém o
//  mesmo trecho da fonte.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aurea {

namespace {

constexpr f32 kMinStretchSpeed = 0.05f;
constexpr f32 kMaxStretchSpeed = 16.0f;

/// Tempo LOCAL reescalado em volta do ponto de entrada (`anchor`).
i64 stretch_time(i64 t, i64 anchor, f64 scale) noexcept {
    return anchor + static_cast<i64>(std::llround(static_cast<f64>(t - anchor) * scale));
}

/// Keys com os tempos novos; dois que caem no mesmo quadro depois de
/// arredondar viram um (fica o último, como no retime da composição).
void stretch_track(Track& t, i64 anchor, f64 scale, f64 valueScale) {
    std::vector<Keyframe> out;
    out.reserve(t.keys.size());
    for (Keyframe k : t.keys) {
        k.time = FrameIndex{stretch_time(k.time.value, anchor, scale)};
        if (valueScale != 1.0) k.value = static_cast<f32>(k.value * valueScale);
        if (!out.empty() && out.back().time == k.time) out.back() = k;
        else out.push_back(k);
    }
    t.keys = std::move(out);
    t.lastIndex = 0;
}

template <typename Keys>
void stretch_frames(Keys& keys, i64 anchor, f64 scale) {
    for (auto& key : keys) key.frame = stretch_time(key.frame, anchor, scale);
    keys.erase(std::unique(keys.begin(), keys.end(),
                           [](const auto& a, const auto& b) { return a.frame == b.frame; }), keys.end());
}

} // namespace

bool Engine::stretch_layer_span(u64 layerId, i64 newStart, i64 newEnd) noexcept {
    constexpr i64 limit = i64{1} << 31;
    if (newStart < 0 || newEnd <= 0 || newStart >= limit || newEnd >= limit) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->locked) return false;
    const i64 s0 = layer->start.value, e0 = layer->end.value, d0 = e0 - s0;
    if (d0 <= 0) return false;
    // Qual borda andou: a outra fica. Linha magnética (ou modo Edição): o começo
    // fica sempre e o fim leva os vizinhos da linha junto.
    const bool startMoved = newStart != s0;
    const bool magnetic = layer->magneticTrack || comp->edit_mode();
    i64 d1 = std::max<i64>(1, newEnd - newStart);
    if (d1 == d0 && !startMoved) return true;

    const bool remap = layer->timeRemapEnabled && !layer->timeRemap.keys.empty();
    Track* speedTrack = remap ? nullptr : layer->tracks.find(TrackProperty::Speed);
    if (speedTrack && speedTrack->keys.empty()) speedTrack = nullptr;
    if (!remap) {
        // A velocidade nova fica em 0,05…16×: fora dela a duração para no limite.
        f64 lo = 0.0, hi = 0.0;
        if (speedTrack) {
            f64 vmin = 1e30, vmax = 0.0;
            for (const Keyframe& k : speedTrack->keys) {
                vmin = std::min<f64>(vmin, std::fabs(k.value));
                vmax = std::max<f64>(vmax, std::fabs(k.value));
            }
            // d1 = d0 × s; cada chave vira v / s.
            lo = vmax > 0.0 ? vmax / kMaxStretchSpeed : 0.0;
            hi = vmin > 0.0 ? vmin / kMinStretchSpeed : 1e30;
        } else if (layer->speed > 0.0f) {
            lo = layer->speed / kMaxStretchSpeed;
            hi = layer->speed / kMinStretchSpeed;
        }
        if (hi > 0.0) {
            if (hi < lo) hi = lo;   // chaves já fora da faixa: ao menos não aperta mais
            const f64 s = std::clamp(static_cast<f64>(d1) / static_cast<f64>(d0), lo, hi);
            d1 = std::max<i64>(1, static_cast<i64>(std::llround(static_cast<f64>(d0) * s)));
        }
    }
    i64 ns = magnetic ? s0 : (startMoved ? newEnd - d1 : s0);
    if (ns < 0) ns = 0;
    const i64 ne = ns + d1;
    if (ne >= limit) return false;
    if (ns == s0 && ne == e0) return true;
    const i64 ripple = magnetic ? ne - e0 : 0;
    const u32 track = layer->trackId;
    const bool trackScoped = layer->magneticTrack;
    if (ripple != 0) {
        // Um trecho travado no caminho do ripple recusa tudo (como no aparar).
        bool blocked = false;
        comp->layers().for_each([&](LayerId id, const Layer& item) {
            if (id.pack() == layerId || !item.locked || item.start.value < e0) return;
            if (trackScoped && item.trackId != track) return;
            blocked = true;
        });
        if (blocked) return false;
    }

    const f64 scale = static_cast<f64>(d1) / static_cast<f64>(d0);
    history_.before_mutation(*comp, project_->timeline().current(), "esticar velocidade");
    const i64 anchor = layer->offset.value;
    for (u32 i = 0; i < layer->tracks.size(); ++i) {
        Track& t = layer->tracks.at(i);
        // A curva de velocidade guarda a velocidade nos VALORES: a integral (o
        // trecho da fonte) continua a mesma com o tempo × s e o valor ÷ s.
        stretch_track(t, anchor, scale, &t == speedTrack ? 1.0 / scale : 1.0);
    }
    if (speedTrack) {
        for (Keyframe& k : speedTrack->keys)
            k.value = std::clamp(k.value, -kMaxStretchSpeed, kMaxStretchSpeed);
    }
    // O remapeamento guarda o quadro da FONTE nos valores: só o tempo estica.
    stretch_track(layer->timeRemap, anchor, scale, 1.0);
    for (auto& legacy : layer->timeRemapLegacyTracks) stretch_track(legacy, anchor, scale, 1.0);
    for (auto& mask : layer->masks) { stretch_frames(mask.pathKeys, anchor, scale); mask.cacheKey = 0; }
    for (auto& group : layer->shape.vector.groups) for (auto& path : group.paths) stretch_frames(path.keys, anchor, scale);
    for (auto& effect : layer->effects) for (auto& mesh : effect.meshes) stretch_frames(mesh.keys, anchor, scale);
    if (!remap && !speedTrack && layer->speed > 0.0f) {
        layer->speed = std::clamp(static_cast<f32>(layer->speed / scale), kMinStretchSpeed, kMaxStretchSpeed);
    }
    layer->start = FrameIndex{ns};
    layer->end = FrameIndex{ne};
    if (ripple != 0) {
        if (trackScoped) comp->shift_track(FrameIndex{e0}, ripple, track, LayerId::unpack(layerId));
        else comp->shift_from(FrameIndex{e0}, ripple, LayerId::unpack(layerId));
    }
    i64 duration = comp->duration().value;
    comp->layers().for_each([&](LayerId, const Layer& item) { duration = std::max(duration, item.end.value); });
    if (duration != comp->duration().value) comp->set_duration(FrameIndex{duration});
    configure_playback_locked(comp);
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
