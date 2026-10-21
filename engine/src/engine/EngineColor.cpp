// =============================================================================
//  Aurea / engine / EngineColor.cpp
//
//  Correção de cor no modo de edição (0.3.0): o que os painéis Filtros e
//  Ajustar pedem ao motor — a "Correção de cor" de cada papel na camada, os
//  looks (presets do efeito) aplicados de um passo só, as miniaturas dos looks
//  sobre o quadro do cabeçote e os scopes (histograma, forma de onda, parade,
//  vetorscópio) da prévia.
//
//  As regras ficam aqui (motor compartilhado); Android e iOS só chamam.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

// ColorGradeEffect.cpp: índices fixos (static_assert lá).
constexpr u32 kGradeMix = 76, kGradeRole = 77, kGradeLook = 78;

bool keeps_on_reset(u32 param) noexcept { return param == kGradeRole || param == kGradeMix; }

} // namespace

u32 Engine::ensure_color_grade(u64 layerId, u32 role) noexcept {
    if (role < 1 || role > 2) return kInvalidIndex;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const EffectTypeId type = effect_type_id(effect_keys::kColorGrade);
    const ParameterRegistry* params = effectRegistry_.params(type);
    if (!l || l->locked || !params || params->count() <= kGradeLook) return kInvalidIndex;
    for (const EffectInstance& e : l->effects) {
        if (e.type == type && e.params.size() > kGradeRole
            && std::lround(e.params[kGradeRole].constant.v[0]) == static_cast<long>(role))
            return e.id;
    }
    if (l->effects.size() >= kMaxEffectCount) return kInvalidIndex;
    history_.before_mutation(*comp, project_->timeline().current(), role == 1 ? "filtro" : "ajustar cor");
    bump_model_revision_locked();
    EffectInstance e;
    e.id = l->alloc_effect_id();
    e.type = type;
    initialize_instance(e, *params);
    e.params[kGradeRole].constant = ParamValue::scalar(static_cast<f32>(role));
    // O Filtro vem antes do Ajustar: o ajuste fino age sobre o look.
    const u32 id = e.id;
    if (role == 1) {
        auto at = std::find_if(l->effects.begin(), l->effects.end(), [&](const EffectInstance& x) {
            return x.type == type && x.params.size() > kGradeRole && std::lround(x.params[kGradeRole].constant.v[0]) == 2;
        });
        l->effects.insert(at, std::move(e));
    } else {
        l->effects.push_back(std::move(e));
    }
    project_->mark_dirty();
    request_render();
    return id;
}

namespace {
/// Volta os parâmetros (e as chaves) da Correção de cor ao padrão, menos papel e intensidade.
void reset_grade_locked(Layer& l, EffectInstance& e, const ParameterRegistry& params) noexcept {
    EffectInstance fresh;
    fresh.id = e.id;
    fresh.type = e.type;
    initialize_instance(fresh, params);
    for (u32 i = 0; i < e.params.size() && i < fresh.params.size(); ++i) {
        if (keeps_on_reset(i)) continue;
        e.params[i] = fresh.params[i];
    }
    e.curves = fresh.curves;
    for (u32 i = 0; i < e.params.size(); ++i) {
        // As curvas novas ficam nos mesmos índices da instância limpa.
        if (i < fresh.params.size() && params.at(i).type == ParamType::Curve) e.params[i].constant.ref = fresh.params[i].constant.ref;
    }
    const u32 id = e.id;
    l.tracks.remove_if([&](const Track& t) {
        return t.property == TrackProperty::EffectParam && t.effectIndex == id && !keeps_on_reset(t.effectParamIndex / 4);
    });
}
} // namespace

bool Engine::apply_color_look(u64 layerId, u32 effectId, i32 look) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = l ? l->find_effect(EffectId{effectId, 0}) : nullptr;
    const EffectTypeId type = effect_type_id(effect_keys::kColorGrade);
    const Effect* fx = e && e->type == type ? effectRegistry_.find(type) : nullptr;
    const ParameterRegistry* specs = fx ? effectRegistry_.params(type) : nullptr;
    if (!l || l->locked || !fx || !specs || e->params.size() <= kGradeLook) return false;
    const std::span<const EffectPreset> all = fx->presets();
    if (look >= static_cast<i32>(all.size())) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "filtro");
    bump_model_revision_locked();
    reset_grade_locked(*l, *e, *specs);
    if (look >= 0) {
        for (const EffectPresetValue& v : all[static_cast<usize>(look)].values) {
            if (v.param >= e->params.size() || v.param >= specs->count() || !std::isfinite(v.value)) continue;
            const ParamSpec& spec = specs->at(v.param);
            ParamValue& dst = e->params[v.param].constant;
            switch (spec.type) {
                case ParamType::Bool: dst = ParamValue::boolean(v.value >= 0.5f); break;
                case ParamType::Int: case ParamType::Enum: dst.v[0] = std::round(v.value); break;
                default: dst.v[std::min<u32>(v.component, component_count(spec.type) > 0 ? component_count(spec.type) - 1 : 0)] = v.value; break;
            }
        }
    }
    e->params[kGradeLook].constant = ParamValue::scalar(static_cast<f32>(look));
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::reset_color_grade(u64 layerId, u32 effectId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = l ? l->find_effect(EffectId{effectId, 0}) : nullptr;
    const EffectTypeId type = effect_type_id(effect_keys::kColorGrade);
    const ParameterRegistry* specs = e && e->type == type ? effectRegistry_.params(type) : nullptr;
    if (!l || l->locked || !specs || e->params.size() <= kGradeLook) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "redefinir cor");
    bump_model_revision_locked();
    reset_grade_locked(*l, *e, *specs);
    project_->mark_dirty();
    request_render();
    return true;
}

Status Engine::render_color_looks(u32 maxDim, std::vector<u8>& out, u32& width, u32& height, u32& count) noexcept {
    out.clear();
    width = height = count = 0;
    std::vector<u8> frame;
    u32 w = 0, h = 0;
    // O quadro do cabeçote, como a prévia mostra (a captura cuida do lock).
    if (const Status s = capture_preview_frame_rgba(std::clamp<u32>(maxDim, 32, 512), frame, w, h); !s.ok()) return s;
    if (!w || !h || frame.size() < static_cast<usize>(w) * h * 4) return Status{Errc::InvalidState, "quadro vazio"};
    if (livePreviewGraphActive_.load(std::memory_order_acquire) || offscreenReaders_.load(std::memory_order_acquire))
        return Status{Errc::Busy, "looks aguardam o quadro do preview"};
    if (!gpu_ || !renderer_.ready()) return Status{Errc::InvalidState, "sem GPU"};
    const EffectTypeId type = effect_type_id(effect_keys::kColorGrade);
    const Effect* fx = effectRegistry_.find(type);
    if (!fx) return Status{Errc::NotFound, "correcao de cor ausente"};
    const std::span<const EffectPreset> looks = fx->presets();
    const usize bytes = static_cast<usize>(w) * h * 4;
    out.reserve(bytes * (looks.size() + 1));
    out.insert(out.end(), frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(bytes));
    std::lock_guard<std::mutex> rl(renderMutex_);
    if (renderer_.preview_graph_pending() || offscreenReaders_.load(std::memory_order_acquire))
        return Status{Errc::Busy, "looks aguardam o quadro do preview"};
    if (const Status ready = poll_export_gpu_locked(); !ready.ok()) return ready;
    std::vector<u8> one;
    for (const EffectPreset& look : looks) {
        one.clear();
        const Status s = renderer_.render_effect_preset_preview(effectRegistry_, type, w, h, frame.data(), &look, one);
        if (!s.ok() || one.size() != bytes) return s.ok() ? Status{Errc::InvalidState, "miniatura sem tamanho"} : s;
        out.insert(out.end(), one.begin(), one.end());
    }
    width = w;
    height = h;
    count = static_cast<u32>(looks.size() + 1);
    return OkStatus;
}

Status Engine::compute_scopes(u32 maxDim, Scopes& out) noexcept {
    std::vector<u8> frame;
    u32 w = 0, h = 0;
    if (const Status s = capture_preview_frame_rgba(std::clamp<u32>(maxDim, 32, 512), frame, w, h); !s.ok()) return s;
    if (!w || !h || frame.size() < static_cast<usize>(w) * h * 4) return Status{Errc::InvalidState, "quadro vazio"};
    out = Scopes{};
    out.width = w;
    out.height = h;
    out.columns = std::min<u32>(w, 256);
    out.histogram.assign(4 * 256, 0);
    out.waveform.assign(static_cast<usize>(out.columns) * 128, 0);
    out.parade.assign(static_cast<usize>(out.columns) * 128 * 3, 0);
    out.vectorscope.assign(128 * 128, 0);
    auto bump = [](u16& c) { if (c < 0xFFFF) ++c; };
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            const u8* p = &frame[(static_cast<usize>(y) * w + x) * 4];
            if (p[3] == 0) continue;
            const f32 r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
            // Valores de tela, como os scopes de um corretor (luz BT.709).
            const f32 Y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            const u32 yi = std::min<u32>(255, static_cast<u32>(Y * 255.0f + 0.5f));
            ++out.histogram[p[0]];
            ++out.histogram[256 + p[1]];
            ++out.histogram[512 + p[2]];
            ++out.histogram[768 + yi];
            const u32 col = x * out.columns / w;
            const u32 row = std::min<u32>(127, static_cast<u32>(Y * 127.0f + 0.5f));
            bump(out.waveform[static_cast<usize>(col) * 128 + row]);
            const f32 rgb[3] = {r, g, b};
            for (u32 c = 0; c < 3; ++c) {
                const u32 rr = std::min<u32>(127, static_cast<u32>(rgb[c] * 127.0f + 0.5f));
                bump(out.parade[(static_cast<usize>(c) * out.columns + col) * 128 + rr]);
            }
            // Cb/Cr (BT.709) de −0,5..0,5 para 0..127.
            const f32 cb = (b - Y) / 1.8556f, cr = (r - Y) / 1.5748f;
            const u32 vx = std::min<u32>(127, static_cast<u32>(std::clamp(cb + 0.5f, 0.0f, 1.0f) * 127.0f + 0.5f));
            const u32 vy = std::min<u32>(127, static_cast<u32>(std::clamp(0.5f - cr, 0.0f, 1.0f) * 127.0f + 0.5f));
            bump(out.vectorscope[static_cast<usize>(vy) * 128 + vx]);
        }
    }
    return OkStatus;
}

} // namespace aurea
