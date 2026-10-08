// =============================================================================
//  Aurea / engine / EnginePuppet.cpp
//
//  API do FANTOCHE no palco (aurea/effects/Puppet.hpp). Como a Malha de
//  deformação, a UI fala em FRAÇÃO da camada (u, v em 0..1 da caixa dela) e
//  o motor guarda os pinos nos parâmetros ocultos do efeito: tocar = pino
//  novo (repouso = o ponto da imagem sob o dedo na malha já deformada),
//  arrastar = posição atual (keyframe no playhead se a trilha já anima ou com
//  autoKey; o gesto inteiro é um passo de desfazer), segurar = apagar.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Puppet.hpp"
#include "aurea/ai/RotoService.hpp"
#include "aurea/render/Renderer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

EffectInstance* puppet_effect(Layer* l, u32 effectId) noexcept {
    if (!l) return nullptr;
    for (EffectInstance& e : l->effects)
        if (e.id == effectId && e.type == effect_type_id(puppet::kPuppetKey)) return &e;
    return nullptr;
}

/// Valor do parâmetro no instante local (constante ou trilha).
Vec2 pin_value(const Layer& l, const EffectInstance& e, u32 param, FrameIndex local) noexcept {
    if (param >= e.params.size()) return Vec2{0.5f, 0.5f};
    Vec2 v{e.params[param].constant.v[0], e.params[param].constant.v[1]};
    for (u32 c = 0; c < 2; ++c) {
        const Track* t = l.tracks.find(TrackProperty::EffectParam, e.id, param_track_key(param, c));
        if (t) (c == 0 ? v.x : v.y) = t->value_or(local, c == 0 ? v.x : v.y);
    }
    return v;
}

bool pin_on(const EffectInstance& e, u32 i) noexcept {
    const u32 p = puppet::pin_on(i);
    return p < e.params.size() && e.params[p].constant.v[0] > 0.5f;
}

/// Pinos ligados no instante (frações da camada), com o índice de cada um.
std::vector<puppet::Pin> pins_at(const Layer& l, const EffectInstance& e, FrameIndex local, std::vector<u32>* index) {
    std::vector<puppet::Pin> pins;
    for (u32 i = 0; i < puppet::kMaxPins; ++i) {
        if (!pin_on(e, i)) continue;
        pins.push_back(puppet::Pin{pin_value(l, e, puppet::pin_rest(i), local), pin_value(l, e, puppet::pin_pos(i), local)});
        if (index) index->push_back(i);
    }
    return pins;
}

/// Linhas do contorno gravadas no efeito.
void outline_of(const EffectInstance& e, f32* rows) noexcept {
    for (u32 r = 0; r < puppet::kOutline; ++r) {
        const u32 p = puppet::kOutlineFirst + r;
        rows[r] = p < e.params.size() ? e.params[p].constant.v[0] : 0.0f;
    }
}

/// A malha do efeito (a mesma do render: contorno e densidade) e a deformada
/// no instante, em FRAÇÃO da camada (ARAP no tamanho real `w`×`h`).
void mesh_at(const Layer& l, const EffectInstance& e, FrameIndex local, f32 w, f32 h, puppet::Mesh& mesh,
             std::vector<Vec2>& def) {
    f32 rows[puppet::kOutline];
    outline_of(e, rows);
    std::vector<u8> cells;
    const bool outlined = puppet::outline_cells(rows, cells);
    const i32 tris = e.params.size() > puppet::kTriangles ? static_cast<i32>(e.params[puppet::kTriangles].constant.v[0]) : 300;
    const f32 expansion = e.params.size() > puppet::kExpansion ? e.params[puppet::kExpansion].constant.v[0] : 3.0f;
    puppet::build_mesh(w, h, static_cast<u32>(std::max(20, tris)), expansion, mesh, outlined ? cells.data() : nullptr,
                       outlined ? puppet::kOutline : 0u, outlined ? puppet::kOutline : 0u);
    std::vector<puppet::Pin> pins = pins_at(l, e, local, nullptr);
    for (puppet::Pin& p : pins) { p.rest = Vec2{p.rest.x * w, p.rest.y * h}; p.pos = Vec2{p.pos.x * w, p.pos.y * h}; }
    const f32 rigidity = e.params.size() > puppet::kRigidity ? e.params[puppet::kRigidity].constant.v[0] / 100.0f : 0.0f;
    puppet::deform(mesh, pins, rigidity, def);
    for (Vec2& v : mesh.rest) v = Vec2{v.x / w, v.y / h};
    for (Vec2& v : def) v = Vec2{v.x / w, v.y / h};
}

} // namespace

/// Tamanho natural da camada em px (o mesmo que o render dá ao efeito).
void Engine::puppet_layer_size(const Layer& l, f32& w, f32& h) const noexcept {
    w = h = 0.0f;
    const Asset* a = project_ ? project_->asset(l.source) : nullptr;
    if (l.kind == LayerKind::Image) {
        const auto it = images_.find(l.source.pack());
        if (it != images_.end()) { w = static_cast<f32>(it->second.width); h = static_cast<f32>(it->second.height); }
    } else if (l.kind == LayerKind::Video && a) {
        w = static_cast<f32>(a->video.width);
        h = static_cast<f32>(a->video.height);
    } else if (l.kind == LayerKind::Shape) {
        w = l.shape.bounds.w;
        h = l.shape.bounds.h;
    }
    if (!(w > 0.0f) || !(h > 0.0f)) {
        const Composition* comp = project_ ? project_->timeline().composition(project_->timeline().current()) : nullptr;
        w = comp ? static_cast<f32>(comp->width()) : 1.0f;
        h = comp ? static_cast<f32>(comp->height()) : 1.0f;
    }
}

/// Contorno da camada AGORA ("auto masking"): o recorte do Rotobrush da
/// camada, se já calculado; senão o alfa da imagem. false = sem dado (vídeo
/// sem Rotobrush, texto...): a malha cobre a caixa inteira.
bool Engine::puppet_capture_outline(const Composition& comp, const Layer& l, FrameIndex local, f32* rows) noexcept {
    for (u32 r = 0; r < puppet::kOutline; ++r) rows[r] = 0.0f;
    const Asset* a = project_ ? project_->asset(l.source) : nullptr;
    for (const EffectInstance& inst : l.effects) {
        if (!inst.enabled || inst.type != effect_type_id(effect_keys::kRotobrush)) continue;
        i64 frame = 0;
        if (l.kind == LayerKind::Video && a) {
            const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
            const f64 srcFps = a->video.fps > 0.0 ? a->video.fps : fps;
            const f64 src = l.source_frame(l.timeline_time(local));
            frame = std::isfinite(src) ? static_cast<i64>(std::max(0.0, std::floor(src / fps * srcFps + 1e-3))) : 0;
        }
        const auto matte = renderer_.roto_cached_matte(&comp, l, inst, frame, a);
        constexpr u32 k = ai::RotoService::kSize;
        if (matte && matte->size() >= static_cast<usize>(k) * k) {
            puppet::outline_rows(matte->data(), k, k, 0.5f, rows);
            return true;
        }
    }
    if (l.kind != LayerKind::Image) return false;
    const auto it = images_.find(l.source.pack());
    if (it == images_.end()) return false;
    const ImagePixels& px = it->second;
    if (!px.width || !px.height || px.rgba.size() < static_cast<usize>(px.width) * px.height * 4) return false;
    // Cobertura 96×96: o maior alfa de 4×4 amostras por célula.
    constexpr u32 n = 96, sub = 4;
    std::vector<f32> cov(n * n, 0.0f);
    for (u32 y = 0; y < n * sub; ++y) {
        const u32 sy = std::min(px.height - 1, static_cast<u32>((y + 0.5) * px.height / (n * sub)));
        for (u32 x = 0; x < n * sub; ++x) {
            const u32 sx = std::min(px.width - 1, static_cast<u32>((x + 0.5) * px.width / (n * sub)));
            const f32 al = px.rgba[(static_cast<usize>(sy) * px.width + sx) * 4 + 3] / 255.0f;
            f32& c = cov[(y / sub) * n + x / sub];
            c = std::max(c, al);
        }
    }
    puppet::outline_rows(cov.data(), n, n, 0.1f, rows);
    return true;
}

u32 Engine::query_puppet_mesh(u64 layerId, u32 effectId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const EffectInstance* e = puppet_effect(l, effectId);
    if (!e || e->params.size() < puppet::kParamCount) return 0;
    f32 w, h;
    puppet_layer_size(*l, w, h);
    puppet::Mesh mesh;
    std::vector<Vec2> def;
    mesh_at(*l, *e, l->local_time(playback_.current()), w, h, mesh, def);
    const u32 need = static_cast<u32>(mesh.tris.size()) * 2;
    if (!out || capacity < need) return need;
    for (usize i = 0; i < mesh.tris.size(); ++i) {
        out[i * 2] = def[mesh.tris[i]].x;
        out[i * 2 + 1] = def[mesh.tris[i]].y;
    }
    return need;
}

u32 Engine::query_puppet(u64 layerId, u32 effectId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const EffectInstance* e = puppet_effect(l, effectId);
    if (!e) return 0;
    u32 n = 0;
    for (u32 i = 0; i < puppet::kMaxPins; ++i) n += pin_on(*e, i) ? 1u : 0u;
    const u32 need = n * kPuppetPinFloats;
    if (!out || capacity < need) return need;
    const FrameIndex local = l->local_time(playback_.current());
    u32 k = 0;
    for (u32 i = 0; i < puppet::kMaxPins; ++i) {
        if (!pin_on(*e, i)) continue;
        const Vec2 p = pin_value(*l, *e, puppet::pin_pos(i), local);
        const Track* tx = l->tracks.find(TrackProperty::EffectParam, e->id, param_track_key(puppet::pin_pos(i), 0));
        f32* o = out + k * kPuppetPinFloats;
        o[0] = static_cast<f32>(i);
        o[1] = p.x;
        o[2] = p.y;
        o[3] = tx && tx->find_exact(local) != kInvalidIndex ? 1.0f : 0.0f;
        ++k;
    }
    return need;
}

i32 Engine::puppet_add_pin(u64 layerId, u32 effectId, f32 u, f32 v) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = puppet_effect(l, effectId);
    if (!e || !std::isfinite(u) || !std::isfinite(v) || e->params.size() < puppet::kParamCount) return -1;
    i32 slot = -1;
    for (u32 i = 0; i < puppet::kMaxPins && slot < 0; ++i) if (!pin_on(*e, i)) slot = static_cast<i32>(i);
    if (slot < 0) return -1;
    const FrameIndex local = l->local_time(playback_.current());
    // O repouso é o ponto da IMAGEM sob o dedo: com a camada já deformada,
    // a malha atual leva o toque de volta (malha em fração — as baricêntricas
    // não dependem da escala de cada eixo).
    Vec2 rest{u, v};
    const bool first = pins_at(*l, *e, local, nullptr).empty();
    if (!first) {
        f32 w, h;
        puppet_layer_size(*l, w, h);
        puppet::Mesh mesh;
        std::vector<Vec2> def;
        mesh_at(*l, *e, local, w, h, mesh, def);
        (void)puppet::rest_point(mesh, def, Vec2{u, v}, rest);
    }
    // Primeiro pino: o contorno da camada neste instante vira a malha (AE).
    f32 rows[puppet::kOutline];
    const bool outlined = first && puppet_capture_outline(*comp, *l, local, rows);
    history_.before_mutation(*comp, project_->timeline().current(), "pino do fantoche");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    if (first) {
        for (u32 r = 0; r < puppet::kOutline; ++r) e->params[puppet::kOutlineFirst + r].constant.v[0] = outlined ? rows[r] : 0.0f;
    }
    const u32 i = static_cast<u32>(slot);
    e->params[puppet::pin_on(i)].constant.v[0] = 1.0f;
    ParamValue& r = e->params[puppet::pin_rest(i)].constant;
    r.v[0] = rest.x; r.v[1] = rest.y;
    ParamValue& p = e->params[puppet::pin_pos(i)].constant;
    p.v[0] = u; p.v[1] = v;
    // Pino novo numa camada já animada: a trilha dele começa onde foi posto.
    l->tracks.remove_if([&](const Track& t) {
        return t.property == TrackProperty::EffectParam && t.effectIndex == e->id
            && (t.effectParamIndex == param_track_key(puppet::pin_pos(i), 0) || t.effectParamIndex == param_track_key(puppet::pin_pos(i), 1));
    });
    project_->mark_dirty();
    request_render();
    return slot;
}

bool Engine::puppet_move_pin(u64 layerId, u32 effectId, u32 pin, f32 u, f32 v, bool autoKey, bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = puppet_effect(l, effectId);
    if (!e || pin >= puppet::kMaxPins || !pin_on(*e, pin) || !std::isfinite(u) || !std::isfinite(v)) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "mover pino");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const FrameIndex local = l->local_time(playback_.current());
    const u32 param = puppet::pin_pos(pin);
    const f32 value[2] = {u, v};
    for (u32 c = 0; c < 2; ++c) {
        Track* t = l->tracks.find(TrackProperty::EffectParam, e->id, param_track_key(param, c));
        if ((t && !t->keys.empty()) || autoKey) {
            Track& tr = t ? *t : l->tracks.get_or_create(TrackProperty::EffectParam, e->id, param_track_key(param, c));
            (void)tr.set(local, value[c]);
        }
        e->params[param].constant.v[c] = value[c];
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::puppet_remove_pin(u64 layerId, u32 effectId, u32 pin) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = puppet_effect(l, effectId);
    if (!e || pin >= puppet::kMaxPins || !pin_on(*e, pin)) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "apagar pino");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    e->params[puppet::pin_on(pin)].constant.v[0] = 0.0f;
    const u32 rest = puppet::pin_rest(pin), pos = puppet::pin_pos(pin);
    l->tracks.remove_if([&](const Track& t) {
        if (t.property != TrackProperty::EffectParam || t.effectIndex != e->id) return false;
        for (u32 c = 0; c < 2; ++c)
            if (t.effectParamIndex == param_track_key(rest, c) || t.effectParamIndex == param_track_key(pos, c)) return true;
        return false;
    });
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
