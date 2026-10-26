// =============================================================================
//  Aurea / engine / EngineOmegaEffects.cpp
//
//  Omega 0.3.2, área 07 (efeitos), P0-1. Duas coisas que o painel de efeitos
//  da referência pede e o motor ainda não tinha:
//
//  1. APRESENTAÇÃO do parâmetro (aurea/effects/Parameter.hpp): qual controle a
//     linha usa (slider, régua, ponto, XYZ, orientação, disco de matiz), quanto
//     vale um risco da régua, de onde o slider preenche, se a escolha é
//     segmentada ou suspensa e se o parâmetro está habilitado agora
//     (habilitação cruzada + interruptor da seção). Regra única, resolvida
//     aqui e entregue pronta na linha da ponte (`EffectParamRow` +136..+159):
//     Android e iOS só desenham.
//  2. As operações do menu ⋮ do efeito que faltavam: duplicar e colar as
//     configurações de um efeito do mesmo tipo.
//
//  Nada aqui muda o formato do projeto.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace aurea {

static_assert(kParamRowEnabled == bridge::kEffectRowEnabled, "bits da linha: motor e ponte iguais");
static_assert(kParamRowHueDisc == bridge::kEffectRowHueDisc, "bits da linha: motor e ponte iguais");

// =============================================================================
// Regras de apresentação
// =============================================================================
namespace {

/// Letras (não bytes) de um texto UTF-8: "Médio" tem 5.
u32 utf8_letters(const char* s) noexcept {
    if (!s) return 0;
    u32 n = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
        if ((*p & 0xC0u) != 0x80u) ++n;
    }
    return n;
}

/// 1·10ⁿ, 2·10ⁿ ou 5·10ⁿ mais próximo de `x` (> 0) na escala logarítmica.
f32 nice_step(f64 x) noexcept {
    if (!(x > 0.0) || !std::isfinite(x)) return 0.01f;
    const f64 e = std::floor(std::log10(x));
    const f64 base = std::pow(10.0, e);
    const f64 m = x / base;   // [1, 10)
    // Pontos médios geométricos: √2, √10, √50.
    const f64 pick = m < 1.4142135623730951 ? 1.0 : m < 3.1622776601683795 ? 2.0 : m < 7.0710678118654755 ? 5.0 : 10.0;
    return static_cast<f32>(pick * base);
}

/// Valor de um componente agora: trilha (keyframe/expressão) ou constante; sem
/// instância, o padrão declarado.
f32 current_value(const EffectInstance* effect, const ParamSpec& spec, u32 index, const TrackSet* tracks,
                  FrameIndex local) noexcept {
    if (!effect) return spec.defaultValue.v[0];
    if (tracks) return evaluate_param(*tracks, *effect, index, spec, local).v[0];
    if (index < effect->params.size()) return effect->params[index].constant.v[0];
    return (spec.flags & kParamLegacyZero) != 0 ? 0.0f : spec.defaultValue.v[0];
}

/// O controlador `ctrl` (Bool ou Enum) libera o parâmetro com esta máscara?
bool controller_allows(const ParamSpec& ctrl, f32 value, u32 mask) noexcept {
    if (ctrl.type == ParamType::Bool) {
        const bool on = value >= 0.5f;
        return on ? (mask & 1u) != 0 : (mask & 2u) != 0;
    }
    if (ctrl.type == ParamType::Enum) {
        const u32 option = value <= 0.0f ? 0u : static_cast<u32>(value + 0.5f);
        return option < 32 && ((mask >> option) & 1u) != 0;
    }
    return true;
}

/// Profundidade máxima da cadeia de controladores (protege contra ciclo).
constexpr u32 kMaxEnableDepth = 8;

bool enabled_impl(const EffectInstance* effect, const ParameterRegistry& params, u32 index, const TrackSet* tracks,
                  FrameIndex local, u32 depth) noexcept {
    if (index >= params.count()) return false;
    if (depth > kMaxEnableDepth) return false;   // ciclo declarado: desliga em vez de travar
    const ParamSpec& spec = params.at(index);
    // Interruptor da seção: os membros só valem com ele ligado.
    if (spec.section > 0) {
        if (const ParamSection* sec = params.section(spec.section);
            sec && sec->toggle != kInvalidIndex && sec->toggle != index && sec->toggle < params.count()) {
            const ParamSpec& t = params.at(sec->toggle);
            if (current_value(effect, t, sec->toggle, tracks, local) < 0.5f) return false;
            if (!enabled_impl(effect, params, sec->toggle, tracks, local, depth + 1)) return false;
        }
    }
    // Habilitação cruzada declarada.
    if (spec.enabledBy != kInvalidIndex && spec.enabledBy < params.count() && spec.enabledBy != index) {
        const ParamSpec& ctrl = params.at(spec.enabledBy);
        if (!controller_allows(ctrl, current_value(effect, ctrl, spec.enabledBy, tracks, local), spec.enabledMask)) {
            return false;
        }
        if (!enabled_impl(effect, params, spec.enabledBy, tracks, local, depth + 1)) return false;
    }
    return true;
}

} // namespace

ParamControl resolve_control(const ParamSpec& spec) noexcept {
    switch (spec.type) {
        case ParamType::Point2D:
            return spec.control == ParamControl::HueDisc ? ParamControl::HueDisc : ParamControl::Point;
        case ParamType::Point3D:
            return spec.control == ParamControl::Orientation ? ParamControl::Orientation : ParamControl::Xyz;
        case ParamType::Angle:
            return spec.control == ParamControl::Slider ? ParamControl::Slider : ParamControl::Ruler;
        case ParamType::Float:
        case ParamType::Int: {
            if (spec.control == ParamControl::Slider || spec.control == ParamControl::Ruler) return spec.control;
            const f32 lo = spec.minValue, hi = spec.maxValue;
            if (!std::isfinite(lo) || !std::isfinite(hi) || !(hi > lo)) return ParamControl::Ruler;
            const f32 range = hi - lo;
            const f32 typed = spec.typed_max() - spec.typed_min();
            return std::isfinite(typed) && typed <= range * 4.0f + 1e-6f ? ParamControl::Slider : ParamControl::Ruler;
        }
        default:
            return ParamControl::Auto;
    }
}

f32 resolve_step(const ParamSpec& spec) noexcept {
    if (std::isfinite(spec.step) && spec.step > 0.0f) return spec.step;
    switch (spec.type) {
        case ParamType::Int:
        case ParamType::Enum:
        case ParamType::Bool:
            return 1.0f;
        case ParamType::Angle:
            return 1.0f;
        case ParamType::Float:
        case ParamType::Point2D:
        case ParamType::Point3D: {
            const f64 range = static_cast<f64>(spec.maxValue) - static_cast<f64>(spec.minValue);
            if (!std::isfinite(range) || !(range > 0.0)) return 0.01f;
            return nice_step(range / 200.0);
        }
        default:
            return 0.0f;
    }
}

f32 resolve_origin(const ParamSpec& spec) noexcept {
    if (std::isfinite(spec.origin)) return spec.origin;
    const f32 lo = spec.minValue, hi = spec.maxValue;
    if (!std::isfinite(lo) || !std::isfinite(hi)) return 0.0f;
    if (lo < 0.0f && hi > 0.0f) return 0.0f;
    return std::min(lo, hi);
}

EnumStyle resolve_enum_style(const ParamSpec& spec) noexcept {
    if (spec.type != ParamType::Enum) return EnumStyle::Auto;
    if (spec.enumStyle != EnumStyle::Auto) return spec.enumStyle;
    if (spec.enumCount == 0 || spec.enumCount > 4 || !spec.enumLabels) return EnumStyle::Dropdown;
    for (u32 k = 0; k < spec.enumCount; ++k) {
        if (utf8_letters(spec.enumLabels[k]) > 10) return EnumStyle::Dropdown;
    }
    return EnumStyle::Segmented;
}

bool param_enabled(const EffectInstance& effect, const ParameterRegistry& params, u32 index, const TrackSet* tracks,
                   FrameIndex localTime) noexcept {
    return enabled_impl(&effect, params, index, tracks, localTime, 0);
}

bool effect_at_defaults(const EffectInstance& effect, const ParameterRegistry& params, const TrackSet* tracks) noexcept {
    for (u32 i = 0; i < params.count(); ++i) {
        const ParamSpec& spec = params.at(i);
        const u32 comps = component_count(spec.type);
        if (comps == 0) continue;
        if (i < effect.params.size()) {
            const ParamSlot& slot = effect.params[i];
            if (slot.source == ParamSource::Expression) return false;
            for (u32 c = 0; c < comps; ++c) {
                if (std::fabs(slot.constant.v[c] - spec.defaultValue.v[c]) > 1e-6f) return false;
            }
        }
        if (tracks) {
            for (u32 c = 0; c < comps; ++c) {
                const Track* t = tracks->find(TrackProperty::EffectParam, effect.id, param_track_key(i, c));
                if (t && (!t->keys.empty() || t->has_expression())) return false;
            }
        }
    }
    return true;
}

void apply_edit_first_sections(EffectInstance& instance, const ParameterRegistry& params) noexcept {
    const u32 n = std::min(params.section_count(), kMaxParamSections);
    for (u32 ordinal = 1; ordinal <= n; ++ordinal) {
        const ParamSection* sec = params.section(ordinal);
        if (sec && sec->editFirst && !sec->open) {
            instance.sectionsFlipped = static_cast<u8>(instance.sectionsFlipped | (1u << (ordinal - 1)));
        }
    }
}

/// Preenche a apresentação de uma linha da ponte (+136..+159). Chamado pelo
/// `query_effect_params` (com instância) e pelo `query_effect_specs` (sem:
/// valores = padrões). Declarado localmente no Engine.cpp.
void omega_fill_param_presentation(const ParameterRegistry& params, u32 index, const EffectInstance* effect,
                                   const TrackSet* tracks, FrameIndex local, bridge::EffectParamRow& row) noexcept {
    if (index >= params.count()) return;
    const ParamSpec& spec = params.at(index);
    const ParamControl control = resolve_control(spec);
    row.control = static_cast<u32>(control);
    row.step = control == ParamControl::Auto ? 0.0f : resolve_step(spec);
    row.origin = control == ParamControl::Auto ? 0.0f : resolve_origin(spec);
    row.enumStyle = static_cast<u32>(resolve_enum_style(spec));
    u32 flags = 0;
    if (enabled_impl(effect, params, index, tracks, local, 0)) flags |= bridge::kEffectRowEnabled;
    if (control == ParamControl::HueDisc) flags |= bridge::kEffectRowHueDisc;
    // Passo DECLARADO pelo efeito (não a regra): a UI passa a contar as casas
    // pelo passo (formato da referência) só nesse caso.
    if (std::isfinite(spec.step) && spec.step > 0.0f) flags |= bridge::kEffectRowStepDeclared;
    row.rowFlags = flags;
    row.reservedOmega = 0;
    // Seção "edite primeiro" de um efeito ainda todo no padrão: aberta, ainda
    // que a instância seja antiga (criada antes de a seção ganhar a marca).
    if (effect && spec.section > 0 && (row.sectionFlags & bridge::kEffectSectionOpen) == 0) {
        const ParamSection* sec = params.section(spec.section);
        const bool flipped = spec.section <= kMaxParamSections &&
                             ((effect->sectionsFlipped >> (spec.section - 1)) & 1u) != 0;
        if (sec && sec->editFirst && !sec->open && !flipped && effect_at_defaults(*effect, params, tracks)) {
            // Bit 0 = "como declarado" e o efeito intocado: a pessoa ainda não
            // recolheu (recolher grava o bit) — vale o "edite primeiro".
            row.sectionFlags |= bridge::kEffectSectionOpen;
        }
    }
}

// =============================================================================
// Duplicar e colar configurações (menu ⋮ do efeito)
// =============================================================================
namespace {

/// Tipos que não se repetem na camada: o remapear tempo lê a curva da camada,
/// os layouts 3D são únicos por camada, o transformar de texto é único e o
/// rotobrush guarda os traços fora da instância.
bool duplicable_type(EffectTypeId type) noexcept {
    return type != effect_type_id(effect_keys::kTimeRemap) &&
           type != effect_type_id(effect_keys::kText3DLayout) &&
           type != effect_type_id(effect_keys::kShape3DLayout) &&
           type != effect_type_id(effect_keys::kRotobrush) &&
           type != effect_type_id("aurea.text.transform");
}

/// Trilha que pertence ao efeito de id `effectId` (parâmetros e malha).
bool effect_owned_track(const Track& t, u32 effectId) noexcept {
    return t.effectIndex == effectId &&
           (t.property == TrackProperty::EffectParam || t.property == TrackProperty::MeshWarp);
}

/// Leva as chaves do instante-base de origem para o de destino; com `scale`
/// ≠ 1 (pré-comp com outro fps) mantém a distância em SEGUNDOS. Duas chaves no
/// mesmo quadro depois de arredondar viram uma (fica a última).
void place_keys(Track& t, i64 srcBase, i64 dstBase, f64 scale) noexcept {
    std::vector<Keyframe> out;
    out.reserve(t.keys.size());
    for (Keyframe k : t.keys) {
        const f64 rel = static_cast<f64>(k.time.value - srcBase) * scale;
        k.time.value = dstBase + static_cast<i64>(std::llround(rel));
        if (!out.empty() && out.back().time.value == k.time.value) out.back() = k;
        else out.push_back(k);
    }
    t.keys = std::move(out);
    t.lastIndex = 0;
}

} // namespace

u32 Engine::duplicate_effect(u64 layerId, u32 effectId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    // Um ajuste ainda na fila (a régua acabou de soltar) entra na cópia.
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return kInvalidIndex;
    const u32 at = l->effect_index(EffectId{effectId, 0});
    if (at == kInvalidIndex) return kInvalidIndex;
    if (!duplicable_type(l->effects[at].type) || l->effects[at].id == l->motionTrackEffect) return kInvalidIndex;

    history_.before_mutation(*comp, project_->timeline().current(), "duplicar efeito");
    bump_model_revision_locked();
    // Id novo depois do maior da camada e do contador (um efeito apagado deixa
    // a trilha órfã para trás; reusar o id faria `find` achar a órfã).
    u32 next = l->nextEffectId;
    for (const EffectInstance& e : l->effects) {
        if (e.id != kInvalidIndex) next = std::max(next, e.id + 1);
    }
    EffectInstance copy = l->effects[at];   // cópia ANTES do push_back (realoca)
    copy.id = next;
    l->tracks.remove_if([&](const Track& t) { return effect_owned_track(t, copy.id); });
    std::vector<Track> fresh;
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        const Track& t = l->tracks.at(i);
        if (!effect_owned_track(t, effectId)) continue;
        Track c = t;   // a expressão compilada é imutável e compartilhada
        c.effectIndex = copy.id;
        c.lastIndex = 0;
        fresh.push_back(std::move(c));
    }
    for (Track& t : fresh) l->tracks.add(std::move(t));
    l->effects.push_back(std::move(copy));
    l->nextEffectId = std::max(l->nextEffectId, next + 1);
    project_->mark_dirty();
    request_render();
    return next;
}

bool Engine::paste_effect_settings(u64 layerId, u32 effectId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || clipboard_.effects.size() != 1) return false;
    EffectInstance* target = l->find_effect(EffectId{effectId, 0});
    if (!target || target->type != clipboard_.effects.front().type) return false;

    // As mídias do efeito copiado (LUT, imagem) entram neste projeto; colar
    // noutro projeto zera as referências a camada (as camadas são outras).
    Layer mediaHolder;
    mediaHolder.effects = clipboard_.effects;
    std::vector<Layer> holders{mediaHolder};
    restore_clipboard_media_locked(clipboard_.effectMedia, holders);
    EffectInstance source = holders.front().effects.front();
    if (clipboard_.effectMedia.session != projectSession_) {
        if (const ParameterRegistry* specs = effectRegistry_.params(source.type)) {
            const u32 n = std::min(specs->count(), static_cast<u32>(source.params.size()));
            for (u32 p = 0; p < n; ++p) {
                if (specs->at(p).type == ParamType::LayerReference) source.params[p].constant.ref = 0;
            }
        }
    }
    const f64 srcFps = clipboard_.effectsFps, dstFps = comp->fps();
    const f64 scale = srcFps > 0.0 && dstFps > 0.0 && std::fabs(srcFps - dstFps) > 1e-6 ? dstFps / srcFps : 1.0;
    const u32 sourceId = clipboard_.effects.front().id;

    history_.before_mutation(*comp, project_->timeline().current(), "colar configurações do efeito");
    bump_model_revision_locked();
    target = l->find_effect(EffectId{effectId, 0});
    if (!target) return false;
    // Configurações = valores, curvas, degradês e malha. Ficam: id (a chave
    // das trilhas), posição, ligado/desligado, máscara e o estado do painel.
    target->params = source.params;
    target->curves = source.curves;
    target->gradients = source.gradients;
    target->meshes = source.meshes;
    l->tracks.remove_if([&](const Track& t) { return t.property == TrackProperty::EffectParam && t.effectIndex == effectId; });
    for (const Track& t : clipboard_.effectTracks) {
        if (t.effectIndex != sourceId || t.property != TrackProperty::EffectParam) continue;
        Track nt = t;
        nt.effectIndex = effectId;
        place_keys(nt, clipboard_.effectsBase, l->offset.value, scale);
        l->tracks.add(std::move(nt));
    }
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::clipboard_effect_type() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return clipboard_.effects.size() == 1 ? clipboard_.effects.front().type : 0u;
}

} // namespace aurea
