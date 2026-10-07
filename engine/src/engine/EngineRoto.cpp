// =============================================================================
//  Aurea / engine / EngineRoto.cpp
//
//  O Roto Brush do Rotobrush IA (referência: Roto Brush do After Effects):
//  traços de objeto/fundo pintados no palco, guardados na instância do efeito
//  (parâmetro de curva oculto, só acrescentado), e o "Propagar clipe". As
//  interfaces mandam pontos em px da composição; a conta para a camada e o
//  quadro da fonte é daqui (a mesma do render).
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/ai/RotoMatte.hpp"
#include "aurea/ai/RotoService.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/render/Renderer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

EffectInstance* roto_instance(Composition* comp, u64 layerId, u32 effectId, Layer*& layer) noexcept {
    layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || (layer->kind != LayerKind::Image && layer->kind != LayerKind::Video)) return nullptr;
    EffectInstance* e = layer->find_effect(EffectId{effectId, 0});
    if (!e || e->type != effect_type_id(effect_keys::kRotobrush)) return nullptr;
    return e;
}

/// O slot dos traços, criado (com os padrões) numa instância antiga.
CurveData* roto_curve(EffectInstance& e, const ParameterRegistry& specs) {
    if (specs.count() <= ai::kRotoStrokesParam) return nullptr;
    if (e.params.size() < specs.count()) {
        EffectInstance defaults;
        initialize_instance(defaults, specs);
        for (u32 p = static_cast<u32>(e.params.size()); p < specs.count(); ++p) {
            auto slot = defaults.params[p];
            if (specs.at(p).type == ParamType::Curve) {
                e.curves.push_back(defaults.curves[slot.constant.ref]);
                slot.constant.ref = e.curves.size() - 1;
            } else if (specs.at(p).type == ParamType::Gradient) {
                e.gradients.push_back(defaults.gradients[slot.constant.ref]);
                slot.constant.ref = e.gradients.size() - 1;
            }
            e.params.push_back(slot);
        }
    }
    const u64 ref = e.params[ai::kRotoStrokesParam].constant.ref;
    return ref < e.curves.size() ? &e.curves[ref] : nullptr;
}

/// O quadro da FONTE que a camada mostra no instante (a conta do render).
i64 roto_source_frame(const Project& project, const Composition& comp, const Layer& l, FrameIndex t) noexcept {
    if (l.kind != LayerKind::Video) return 0;
    const Asset* asset = project.asset(l.source);
    if (!asset) return 0;
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    const f64 srcFps = asset->video.fps > 0.0 ? asset->video.fps : fps;
    const f64 srcFrame = l.source_frame(l.timeline_time(l.local_time(t)));
    if (!std::isfinite(srcFrame)) return 0;
    f64 idx = std::floor(srcFrame / fps * srcFps + 1e-3);
    if (asset->video.frameCount.value > 0) idx = std::min(idx, static_cast<f64>(asset->video.frameCount.value - 1));
    return static_cast<i64>(std::max(idx, 0.0));
}

} // namespace

bool Engine::roto_add_stroke(u64 layerId, u32 effectId, bool background, f32 radius, const f32* xy, u32 count) noexcept try {
    if (!xy || count == 0 || !(radius > 0.0f) || !std::isfinite(radius)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = nullptr;
    EffectInstance* e = roto_instance(comp, layerId, effectId, l);
    const ParameterRegistry* specs = e ? effectRegistry_.params(e->type) : nullptr;
    if (!e || !specs) return false;
    bridge::LayerDetailPOD pod{};
    if (!fill_layer_detail_locked(layerId, pod) || pod.sourceWidth == 0 || pod.sourceHeight == 0) return false;
    const FrameIndex t = playback_.current();
    // Composição → camada: o inverso da matriz 2D que o render usa.
    const Mat4 w = layer_comp_matrix(*comp, *l, t);
    const f32 a = w.col[0].x, b = w.col[0].y, c = w.col[1].x, d = w.col[1].y, tx = w.col[3].x, ty = w.col[3].y;
    const f32 det = a * d - b * c;
    if (!(std::fabs(det) > 1e-12f)) return false;
    const f32 sw = static_cast<f32>(pod.sourceWidth), sh = static_cast<f32>(pod.sourceHeight);
    const f32 scale = std::sqrt(std::fabs(det));
    ai::RotoStroke stroke;
    stroke.background = background;
    stroke.frame = roto_source_frame(*project_, *comp, *l, t);
    stroke.radius = std::clamp(radius / scale / std::min(sw, sh), 0.002f, 0.5f);
    // Pontos com um passo mínimo de ¼ do raio: o traço cabe no teto do canal.
    const f32 minStep = 0.25f * stroke.radius * std::min(sw, sh);
    Vec2 last{-1e9f, -1e9f};
    for (u32 i = 0; i < count; ++i) {
        const f32 cx = xy[i * 2] - tx, cy = xy[i * 2 + 1] - ty;
        if (!std::isfinite(cx) || !std::isfinite(cy)) continue;
        const f32 lx = (d * cx - c * cy) / det, ly = (-b * cx + a * cy) / det;
        const bool end = i + 1 == count;
        if (!end && std::hypot(lx - last.x, ly - last.y) < minStep && !stroke.points.empty()) continue;
        last = Vec2{lx, ly};
        stroke.points.push_back(Vec2{lx / sw, ly / sh});
    }
    if (stroke.points.empty()) return false;
    if (stroke.points.size() > ai::kRotoMaxStrokePoints) {
        std::vector<Vec2> thin;
        const f32 k = static_cast<f32>(stroke.points.size() - 1) / static_cast<f32>(ai::kRotoMaxStrokePoints - 1);
        for (u32 i = 0; i < ai::kRotoMaxStrokePoints; ++i) thin.push_back(stroke.points[static_cast<usize>(std::lround(i * k))]);
        stroke.points = std::move(thin);
    }
    CurveData* curve = roto_curve(*e, *specs);
    if (!curve) return false;
    ai::RotoStrokes strokes;
    (void)ai::roto_decode(*curve, strokes);
    usize total = stroke.points.size();
    for (const auto& s : strokes) total += s.points.size();
    if (total > ai::kRotoMaxPoints || strokes.size() >= 4000) return false;   // teto do formato
    history_.before_mutation(*comp, project_->timeline().current(), background ? "traço de fundo" : "traço de objeto");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    strokes.push_back(std::move(stroke));
    ai::roto_encode(strokes, *curve);
    project_->mark_dirty();
    // "Atualizar outros quadros": depois de uma propagação, o traço novo refaz
    // os quadros que dependem dele.
    if (auto* roto = renderer_.roto_service(); roto && roto->progress().total > 0 && l->kind == LayerKind::Video)
        (void)renderer_.roto_propagate(*project_, *comp, *l, *e, &media_);
    request_render();
    return true;
} catch (...) {
    return false;
}

bool Engine::roto_undo_stroke(u64 layerId, u32 effectId) noexcept try {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = nullptr;
    EffectInstance* e = roto_instance(comp, layerId, effectId, l);
    const ParameterRegistry* specs = e ? effectRegistry_.params(e->type) : nullptr;
    if (!e || !specs) return false;
    CurveData* curve = roto_curve(*e, *specs);
    ai::RotoStrokes strokes;
    if (!curve || !ai::roto_decode(*curve, strokes) || strokes.empty()) return false;
    const i64 frame = roto_source_frame(*project_, *comp, *l, playback_.current());
    usize at = strokes.size() - 1;
    for (usize i = strokes.size(); i-- > 0;) if (strokes[i].frame == frame) { at = i; break; }
    history_.before_mutation(*comp, project_->timeline().current(), "desfazer traço");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    strokes.erase(strokes.begin() + static_cast<std::ptrdiff_t>(at));
    ai::roto_encode(strokes, *curve);
    project_->mark_dirty();
    if (auto* roto = renderer_.roto_service(); roto && roto->progress().total > 0 && l->kind == LayerKind::Video && !strokes.empty())
        (void)renderer_.roto_propagate(*project_, *comp, *l, *e, &media_);
    request_render();
    return true;
} catch (...) {
    return false;
}

bool Engine::roto_stroke_info(u64 layerId, u32 effectId, u32* out3) noexcept {
    if (!out3) return false;
    out3[0] = out3[1] = out3[2] = 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = nullptr;
    const EffectInstance* e = roto_instance(comp, layerId, effectId, l);
    if (!e) return false;
    ai::RotoStrokes strokes;
    if (!ai::roto_instance_strokes(*e, strokes)) return true;
    const i64 frame = roto_source_frame(*project_, *comp, *l, playback_.current());
    for (const auto& s : strokes) if (s.frame == frame) ++out3[0];
    out3[1] = static_cast<u32>(strokes.size());
    out3[2] = static_cast<u32>(ai::roto_bases(strokes).size());
    return true;
}

bool Engine::roto_propagate(u64 layerId, u32 effectId) noexcept {
    // Ordem dos locks do render (render → modelo): o serviço é do renderer.
    std::lock_guard<std::mutex> rl(renderMutex_);
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = nullptr;
    const EffectInstance* e = roto_instance(comp, layerId, effectId, l);
    if (!e) return false;
    const bool ok = renderer_.roto_propagate(*project_, *comp, *l, *e, &media_);
    request_render();
    return ok;
}

bool Engine::roto_progress(i64* out4) noexcept {
    if (!out4) return false;
    out4[0] = out4[1] = out4[2] = out4[3] = 0;
    auto* roto = renderer_.roto_service();
    if (!roto) return false;
    const auto p = roto->progress();
    out4[0] = p.done; out4[1] = p.total; out4[2] = p.running ? 1 : 0; out4[3] = p.failed ? 1 : 0;
    return p.total > 0;
}

void Engine::roto_cancel() noexcept {
    if (auto* roto = renderer_.roto_service()) roto->cancel();
    request_render();
}

bool Engine::roto_set_view(u64 layerId, u32 effectId, u32 mode) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = nullptr;
    EffectInstance* e = roto_instance(comp, layerId, effectId, l);
    const ParameterRegistry* specs = e ? effectRegistry_.params(e->type) : nullptr;
    if (!e || !specs || !roto_curve(*e, *specs)) return false;
    constexpr u32 kView = 10;
    e->params[kView].constant = ParamValue::scalar(static_cast<f32>(std::min(mode, 2u)));
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    request_render();
    return true;
}

} // namespace aurea
