// =============================================================================
//  Aurea / engine / EngineAeMasks.cpp
//
//  Lote M (2026-11-12, gap_system itens 12, 23 e 25): máscaras e rig do After
//  Effects. O AE é só a especificação de comportamento; a regra mora aqui e as
//  interfaces só chamam.
//
//  - Chaves da máscara: travada, oculta, RotoBezier (+ tensão por vértice).
//  - Feather X/Y separados e pontos de feather de largura variável
//    (render/MaskRaster.cpp e shaders/mask/raster.frag fazem a conta).
//  - Free Transform Points: afim nos pontos escolhidos da máscara.
//  - Create Nulls from Paths: Points Follow Nulls, Nulls Follow Points e
//    Trace Path (timeline/PathRig.hpp resolve as ligações na avaliação).
//  - Auto-trace: máscaras a partir de um canal da camada (imagem ou vídeo).
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/DefaultNames.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/media/ThumbnailService.hpp"
#include "aurea/media/VideoSource.hpp"
#include "aurea/render/MaskRaster.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/LayerWorld.hpp"
#include "aurea/timeline/PathRig.hpp"
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace aurea {
namespace {

Mask* find_mask(Layer& l, u32 id) noexcept {
    for (Mask& m : l.masks) if (m.id == id) return &m;
    return nullptr;
}

/// Grava a forma: no key do cabeçote se o caminho é animado, senão parada.
void write_shape(Mask& m, i64 local, std::vector<MaskPoint> pts) {
    if (m.pathKeys.empty()) { m.points = std::move(pts); return; }
    for (MaskPathKey& k : m.pathKeys) {
        if (k.frame != local) continue;
        k.points = std::move(pts);
        if (!k.pointMask.empty()) k.pointMask.assign(k.points.size(), 1);
        return;
    }
    MaskPathKey k;
    k.frame = local;
    k.points = std::move(pts);
    const auto it = std::lower_bound(m.pathKeys.begin(), m.pathKeys.end(), local,
                                     [](const MaskPathKey& a, i64 f) { return a.frame < f; });
    m.pathKeys.insert(it, std::move(k));
}

std::string path_label(const Layer& l, u32 pathKind, u32 a, u32 b) {
    if (pathKind == PathFollow::kMask) {
        for (const Mask& m : l.masks) if (m.id == a) return m.name.empty() ? default_name(DefaultName::Mask) : m.name;
        return default_name(DefaultName::Mask);
    }
    std::string name = default_name(DefaultName::Path);
    if (a < l.shape.vector.groups.size()) name = l.shape.vector.groups[a].name + " " + std::to_string(b + 1);
    return name;
}

std::string clip_name(std::string s) {
    constexpr usize kMax = 96;
    if (s.size() <= kMax) return s;
    usize cut = kMax;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0u) == 0x80u) --cut;   // não corta no meio do UTF-8
    s.resize(cut);
    return s;
}

} // namespace

bool Engine::set_mask_flags(u64 layerId, u32 maskId, u32 flags, u32 change) noexcept {
    change &= kMaskLocked | kMaskHidden | kMaskRotoBezier;
    if (change == 0) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return false;
    Mask* m = find_mask(*l, maskId);
    if (!m) return false;
    const u32 now = (m->locked ? kMaskLocked : 0u) | (m->hidden ? kMaskHidden : 0u) | (m->rotoBezier ? kMaskRotoBezier : 0u);
    const u32 next = (now & ~change) | (flags & change);
    if (next == now) return true;
    // Máscara travada só aceita destravar/ocultar; o RotoBezier muda a forma.
    if (((now ^ next) & kMaskRotoBezier) != 0 && (next & kMaskLocked) != 0 && (now & kMaskLocked) != 0) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "chaves da mascara");
    bump_model_revision_locked();
    m->locked = (next & kMaskLocked) != 0;
    m->hidden = (next & kMaskHidden) != 0;
    m->rotoBezier = (next & kMaskRotoBezier) != 0;
    m->cacheKey = 0;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_mask_roto_tension(u64 layerId, u32 maskId, u32 point, f32 tension, bool continuing) noexcept {
    if (!std::isfinite(tension)) return false;
    tension = std::clamp(tension, 0.0f, 1.0f);
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return false;
    Mask* m = find_mask(*l, maskId);
    if (!m || m->locked) return false;
    std::vector<MaskPoint> pts;
    mask::evaluate_path_keys(*m, static_cast<f64>(l->local_time(playback_.current()).value), pts);
    if (pts.empty() || (point != kInvalidIndex && point >= pts.size())) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "tensao do RotoBezier");
    bump_model_revision_locked();
    m->rotoTension.resize(pts.size(), 1.0f);
    if (point == kInvalidIndex) std::fill(m->rotoTension.begin(), m->rotoTension.end(), tension);
    else m->rotoTension[point] = tension;
    m->cacheKey = 0;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_mask_feather_xy(u64 layerId, u32 maskId, f32 x, f32 y, bool continuing) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return false;
    Mask* m = find_mask(*l, maskId);
    if (!m || m->locked) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "feather da mascara");
    bump_model_revision_locked();
    // Com o feather animado, o X é o da trilha (o painel de keyframes manda).
    const Track* tr = l->tracks.find(TrackProperty::MaskParam, m->id, 0);
    if (!tr || !tr->driven()) m->feather = std::clamp(x, 0.0f, 2000.0f);
    m->featherY = y < 0.0f ? -1.0f : std::clamp(y, 0.0f, 2000.0f);
    m->cacheKey = 0;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_mask_feather_points(u64 layerId, u32 maskId, const f32* pts5, u32 count, bool continuing) noexcept {
    if (count > kMaxFeatherPoints || (count > 0 && !pts5)) return false;
    std::vector<MaskFeatherPoint> points(count);
    for (u32 i = 0; i < count; ++i) {
        const f32* p = pts5 + static_cast<usize>(i) * kFeatherPointFloats;
        for (u32 k = 0; k < kFeatherPointFloats; ++k) if (!std::isfinite(p[k])) return false;
        if (p[0] < 0.0f || p[0] > 4096.0f) return false;
        points[i].segment = static_cast<u32>(p[0] + 0.5f);
        points[i].t = std::clamp(p[1], 0.0f, 1.0f);
        points[i].radius = std::clamp(p[2], -2000.0f, 2000.0f);
        points[i].tension = std::clamp(p[3], 0.0f, 1.0f);
        points[i].hold = p[4] > 0.5f;
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return false;
    Mask* m = find_mask(*l, maskId);
    if (!m || m->locked) return false;
    if (m->featherPoints == points) return true;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "pontos de feather");
    bump_model_revision_locked();
    m->featherPoints = std::move(points);
    m->cacheKey = 0;
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::query_mask_extras(u64 layerId, u32 maskId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const Mask* m = l ? find_mask(*l, maskId) : nullptr;
    if (!m) return 0;
    const u32 need = kMaskExtraHeaderFloats + static_cast<u32>(m->featherPoints.size()) * kFeatherPointFloats
                   + static_cast<u32>(m->rotoTension.size());
    if (!out || capacity < need) return need;
    out[0] = static_cast<f32>((m->locked ? kMaskLocked : 0u) | (m->hidden ? kMaskHidden : 0u) | (m->rotoBezier ? kMaskRotoBezier : 0u));
    out[1] = m->featherY;
    out[2] = static_cast<f32>(m->featherPoints.size());
    out[3] = static_cast<f32>(m->drivers.size());
    out[4] = static_cast<f32>(m->rotoTension.size());
    u32 o = kMaskExtraHeaderFloats;
    for (const MaskFeatherPoint& p : m->featherPoints) {
        out[o++] = static_cast<f32>(p.segment);
        out[o++] = p.t;
        out[o++] = p.radius;
        out[o++] = p.tension;
        out[o++] = p.hold ? 1.0f : 0.0f;
    }
    for (const f32 t : m->rotoTension) out[o++] = t;
    return need;
}

bool Engine::transform_mask_points(u64 layerId, u32 maskId, const u32* indices, u32 count, const f32* m6,
                                   bool continuing) noexcept {
    if (!m6 || (count > 0 && !indices)) return false;
    for (u32 i = 0; i < 6; ++i) if (!std::isfinite(m6[i])) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return false;
    Mask* m = find_mask(*l, maskId);
    if (!m || m->locked) return false;
    const i64 local = l->local_time(playback_.current()).value;
    std::vector<MaskPoint> pts;
    mask::evaluate_path_keys(*m, static_cast<f64>(local), pts);
    if (pts.empty()) return false;
    std::vector<u8> chosen(pts.size(), count == 0 ? u8{1} : u8{0});
    for (u32 i = 0; i < count; ++i) {
        if (indices[i] >= pts.size()) return false;
        chosen[indices[i]] = 1;
    }
    const f32 a = m6[0], b = m6[1], c = m6[2], d = m6[3], tx = m6[4], ty = m6[5];
    auto lin = [&](Vec2 v) { return Vec2{a * v.x + c * v.y, b * v.x + d * v.y}; };
    for (usize i = 0; i < pts.size(); ++i) {
        if (!chosen[i]) continue;
        const Vec2 p = lin(pts[i].position);
        pts[i].position = Vec2{p.x + tx, p.y + ty};
        pts[i].inTangent = lin(pts[i].inTangent);
        pts[i].outTangent = lin(pts[i].outTangent);
    }
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "transformar pontos da mascara");
    bump_model_revision_locked();
    write_shape(*m, local, std::move(pts));
    m->cacheKey = 0;
    project_->mark_dirty();
    request_render();
    return true;
}

Result<u32> Engine::create_nulls_from_path(u64 layerId, u32 pathKind, u32 a, u32 b, NullsFromPath mode, bool loop,
                                           std::vector<u64>* created) noexcept {
    if (created) created->clear();
    if (pathKind > PathFollow::kVector || static_cast<u32>(mode) > static_cast<u32>(NullsFromPath::TracePath))
        return Status{Errc::InvalidArgument, "modo invalido"};
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    drain_commands_locked();
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    const LayerId sourceId = LayerId::unpack(layerId);
    const Layer* source = comp->layer(sourceId);
    if (!source) return Status{Errc::NotFound, "camada nao existe"};
    if (source->locked) return Status{Errc::InvalidState, "camada travada"};
    if (pathKind == PathFollow::kVector && source->kind != LayerKind::Shape)
        return Status{Errc::InvalidArgument, "a camada nao tem caminho vetorial"};
    const f64 now = static_cast<f64>(playback_.current().value);
    BezierPath path;
    if (!pathrig::comp_path(*comp, *source, static_cast<u8>(pathKind), a, b, now, path) || path.v.empty())
        return Status{Errc::NotFound, "caminho nao existe"};
    constexpr usize kMaxNulls = 512;
    if (mode != NullsFromPath::TracePath && path.v.size() > kMaxNulls)
        return Status{Errc::OutOfRange, "caminho com pontos demais"};
    const std::string label = path_label(*source, pathKind, a, b);
    const std::string sourceName = source->name;
    const FrameIndex start = source->start, end = source->end;
    const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;

    history_.before_mutation(*comp, project_->timeline().current(), "criar nulos do caminho");
    bump_model_revision_locked();

    auto make_null = [&](const std::string& name, Vec2 at) -> LayerId {
        const LayerId id = comp->add_layer(LayerKind::Null, clip_name(name));
        Layer* n = comp->layer(id);   // `source` e as máscaras podem ter mudado de lugar
        if (!n) return LayerId{};
        n->start = start;
        n->end = end;
        n->transform.anchor = Vec3{50.0f, 50.0f, 0.0f};   // a caixa virtual de 100 px dos nulos
        n->transform.position = Vec3{at.x, at.y, 0.0f};
        return id;
    };

    std::vector<LayerId> ids;
    if (mode == NullsFromPath::TracePath) {
        Vec2 pt = path.v[0].p, tan{1.0f, 0.0f};
        (void)pathrig::point_on_path(path, 0.0, pt, tan);
        const LayerId id = make_null("Trace " + sourceName + ": " + label, pt);
        Layer* n = comp->layer(id);
        if (!n) return Status{Errc::OutOfMemory, "camada nao criada"};
        PathFollow f;
        f.mode = PathFollow::kTrace;
        f.pathKind = static_cast<u8>(pathKind);
        f.source = sourceId;
        f.a = a;
        f.b = b;
        f.loop = loop;
        f.orient = true;
        // O Progresso: um Controle deslizante no nulo, 0 → 100 % em 1 s.
        const EffectTypeId type = effect_type_id(effect_keys::kSliderControl);
        if (const ParameterRegistry* params = effectRegistry_.params(type)) {
            EffectInstance e;
            e.id = n->alloc_effect_id();
            e.type = type;
            initialize_instance(e, *params);
            const u32 effectId = e.id;
            n->effects.push_back(std::move(e));
            Track& tr = n->tracks.get_or_create(TrackProperty::EffectParam, effectId, param_track_key(0, 0));
            const i64 local0 = n->local_time(start).value;
            (void)tr.set(FrameIndex{local0}, 0.0f, Interpolation::Linear);
            (void)tr.set(FrameIndex{local0 + std::max<i64>(1, static_cast<i64>(std::llround(fps)))}, 100.0f, Interpolation::Linear);
            f.progressEffect = effectId;
        }
        n->pathFollow = f;
        ids.push_back(id);
    } else {
        for (usize i = 0; i < path.v.size(); ++i) {
            const LayerId id = make_null(sourceName + ": " + label + " [" + std::to_string(i) + "]", path.v[i].p);
            if (!id.valid()) break;
            ids.push_back(id);
        }
        if (mode == NullsFromPath::NullsFollowPoints) {
            for (usize i = 0; i < ids.size(); ++i) {
                Layer* n = comp->layer(ids[i]);
                if (!n) continue;
                PathFollow f;
                f.mode = PathFollow::kVertex;
                f.pathKind = static_cast<u8>(pathKind);
                f.source = sourceId;
                f.a = a;
                f.b = b;
                f.vertex = static_cast<u32>(i);
                n->pathFollow = f;
            }
        } else {
            Layer* owner = comp->layer(sourceId);
            if (!owner) return Status{Errc::NotFound, "camada sumiu"};
            if (pathKind == PathFollow::kMask) {
                Mask* m = find_mask(*owner, a);
                if (!m) return Status{Errc::NotFound, "mascara sumiu"};
                for (usize i = 0; i < ids.size(); ++i) {
                    const u32 vertex = static_cast<u32>(i);
                    m->drivers.erase(std::remove_if(m->drivers.begin(), m->drivers.end(),
                                                    [vertex](const PathVertexDriver& d) { return d.vertex == vertex; }),
                                     m->drivers.end());
                    m->drivers.push_back(PathVertexDriver{vertex, ids[i]});
                }
                m->cacheKey = 0;
            } else {
                for (usize i = 0; i < ids.size(); ++i) {
                    const u32 vertex = static_cast<u32>(i);
                    owner->vectorDrivers.erase(
                        std::remove_if(owner->vectorDrivers.begin(), owner->vectorDrivers.end(),
                                       [&](const VectorPathDriver& d) { return d.group == a && d.path == b && d.vertex == vertex; }),
                        owner->vectorDrivers.end());
                    owner->vectorDrivers.push_back(VectorPathDriver{a, b, vertex, ids[i]});
                }
                pathrig::note_vector_drivers();
            }
        }
    }
    comp->touch();
    project_->mark_dirty();
    request_render();
    if (created) for (const LayerId id : ids) created->push_back(id.pack());
    return static_cast<u32>(ids.size());
}

bool Engine::clear_path_drivers(u64 layerId, u32 pathKind, u32 a, u32 b) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return false;
    const i64 local = l->local_time(playback_.current()).value;
    if (pathKind == PathFollow::kMask) {
        Mask* m = find_mask(*l, a);
        if (!m || m->drivers.empty()) return false;
        // Os vértices ficam onde os nulos os deixaram no cabeçote.
        std::vector<MaskPoint> stored, driven;
        mask::evaluate_path_keys(*m, static_cast<f64>(local), stored);
        driven = stored;
        const bool moved = pathrig::apply_mask_drivers(*m, static_cast<f64>(local), driven);
        history_.before_mutation(*comp, project_->timeline().current(), "soltar pontos dos nulos");
        bump_model_revision_locked();
        m->drivers.clear();
        if (moved && driven.size() == stored.size()) write_shape(*m, local, std::move(driven));
        m->cacheKey = 0;
    } else {
        const usize before = l->vectorDrivers.size();
        if (before == 0) return false;
        std::vector<VectorPathDriver> kept;
        for (const VectorPathDriver& d : l->vectorDrivers) if (d.group != a || d.path != b) kept.push_back(d);
        if (kept.size() == before) return false;
        history_.before_mutation(*comp, project_->timeline().current(), "soltar pontos dos nulos");
        bump_model_revision_locked();
        l->vectorDrivers = std::move(kept);
    }
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::clear_path_follow(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || l->pathFollow.mode == PathFollow::kNone) return false;
    // A camada fica onde o caminho a deixou no cabeçote (sem keys de posição).
    Vec3 pos = l->transform.position;
    const bool followed = pathrig::follow_position(*l, static_cast<f64>(l->local_time(playback_.current()).value), pos);
    history_.before_mutation(*comp, project_->timeline().current(), "soltar do caminho");
    bump_model_revision_locked();
    l->pathFollow = PathFollow{};
    const Track* tx = l->tracks.find(TrackProperty::PositionX);
    const Track* ty = l->tracks.find(TrackProperty::PositionY);
    if (followed && !(tx && tx->driven()) && !(ty && ty->driven())) l->transform.position = pos;
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::query_path_rig(u64 layerId, f32* out, u32 capacity) noexcept {
    constexpr u32 need = 9;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return 0;
    if (!out || capacity < need) return need;
    const PathFollow& f = l->pathFollow;
    out[0] = static_cast<f32>(f.mode);
    out[1] = static_cast<f32>(f.pathKind);
    out[2] = static_cast<f32>(f.a);
    out[3] = static_cast<f32>(f.b);
    out[4] = static_cast<f32>(f.vertex);
    out[5] = f.progress;
    out[6] = f.loop ? 1.0f : 0.0f;
    out[7] = f.progressEffect == kInvalidIndex ? -1.0f : static_cast<f32>(f.progressEffect);
    out[8] = static_cast<f32>(l->vectorDrivers.size());
    return need;
}

// =============================================================================
// Auto-trace
// =============================================================================
Result<u32> Engine::auto_trace(u64 layerId, const AutoTraceOptions& options, u64* outLayer) noexcept {
    if (outLayer) *outLayer = 0;
    if (options.channel > 4) return Status{Errc::InvalidArgument, "canal invalido"};
    // 1. O que traçar (sob o lock).
    LayerKind kind = LayerKind::Unknown;
    std::shared_ptr<const ImagePixels> image;
    Asset asset;
    std::vector<i64> locals;     // quadro LOCAL de cada quadro traçado
    std::vector<i64> targetUs;   // vídeo: o tempo da fonte de cada um
    f32 layerW = 0.0f, layerH = 0.0f;
    f64 fps = 30.0;
    u32 room = 0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!l) return Status{Errc::NotFound, "camada nao existe"};
        if (l->locked) return Status{Errc::InvalidState, "camada travada"};
        kind = l->kind;
        fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        room = options.newLayer ? kMaxMaskCount : kMaxMaskCount - std::min<u32>(kMaxMaskCount, static_cast<u32>(l->masks.size()));
        if (room == 0) return Status{Errc::OutOfRange, "a camada ja tem o maximo de mascaras"};
        if (kind == LayerKind::Image) {
            const auto it = images_.find(l->source.pack());
            if (it == images_.end() || !it->second) return Status{Errc::InvalidState, "imagem ainda nao carregada"};
            image = it->second;
            layerW = static_cast<f32>(image->width);
            layerH = static_cast<f32>(image->height);
        } else if (kind == LayerKind::Video) {
            const Asset* a = project_->asset(l->source);
            if (!a || !a->has_video()) return Status{Errc::InvalidArgument, "camada sem video"};
            asset = *a;
            asset.sourcePath = resolve_asset_path(asset.sourcePath);
            layerW = static_cast<f32>(a->video.width);
            layerH = static_cast<f32>(a->video.height);
        } else {
            return Status{Errc::NotSupported, "auto-trace precisa de camada de imagem ou de video"};
        }
        const i64 last = std::max<i64>(l->start.value, l->end.value - 1);
        i64 from = std::clamp<i64>(playback_.current().value, l->start.value, last), to = from + 1;
        // Imagem parada: o contorno é o mesmo em todo quadro (um traço só).
        if (options.workArea && kind == LayerKind::Video) {
            from = std::max<i64>(l->start.value, comp->work_in());
            to = std::min<i64>(l->end.value, comp->work_out());
            if (to <= from) return Status{Errc::OutOfRange, "a camada nao aparece na area de trabalho"};
            to = std::min<i64>(to, from + 1200);
        }
        for (i64 f = from; f < to; ++f) {
            locals.push_back(l->local_time(FrameIndex{f}).value);
            if (kind == LayerKind::Video)
                targetUs.push_back(static_cast<i64>(std::llround(std::max(0.0, l->source_frame(FrameIndex{f})) * 1e6 / fps)));
        }
    }
    if (!(layerW > 0.0f) || !(layerH > 0.0f)) return Status{Errc::InvalidState, "camada sem tamanho"};

    // 2. Os contornos de cada quadro (sem o lock: decodifica e ajusta curvas).
    masktrace::Options to;
    to.channel = options.channel;
    to.threshold = options.threshold;
    to.tolerance = options.tolerance;
    to.minArea = options.minArea;
    to.cornerRoundness = options.cornerRoundness;
    to.invert = options.invert;
    std::vector<std::vector<masktrace::Traced>> frames;
    if (kind == LayerKind::Image) {
        if (!image->width || !image->height || image->rgba.size() < static_cast<usize>(image->width) * image->height * 4)
            return Status{Errc::InvalidState, "imagem vazia"};
        frames.emplace_back();
        (void)masktrace::trace(image->rgba.data(), image->width, image->height, to, Vec2{1.0f, 1.0f}, room, frames.back());
        locals.resize(1);
    } else {
        VideoSourceFactory* factory = config_.mediaFactory;
        if (!factory) return Status{Errc::InvalidState, "sem decodificador"};
        auto dec = factory->open_video(asset, MediaPriority::Thumbnail);
        if (!dec) return Status{Errc::IoError, "video ilegivel"};
        const u32 height = std::min<u32>(720u, static_cast<u32>(layerH));
        const i64 halfFrame = static_cast<i64>(5e5 / fps);
        i64 lastPts = std::numeric_limits<i64>::min();
        for (usize i = 0; i < targetUs.size(); ++i) {
            const i64 want = targetUs[i];
            if (i == 0 || want < lastPts - halfFrame) {
                if (!dec->seek_to_keyframe(want).ok()) break;
                lastPts = std::numeric_limits<i64>::min();
            }
            FrameRef frame;
            bool eos = false;
            for (int guard = 0; guard < 600 && !eos; ++guard) {
                FrameRef f;
                i64 pts = 0;
                if (!dec->next_frame(want - halfFrame, f, pts, eos).ok()) { eos = true; break; }
                if (!f) continue;
                lastPts = pts;
                if (pts >= want - halfFrame) { frame = std::move(f); break; }
            }
            if (!frame) break;
            ThumbnailService::Image img;
            if (!frame_to_thumbnail(*frame.get(), height, img) || !img.width || !img.height) break;
            frames.emplace_back();
            (void)masktrace::trace(img.rgba.data(), img.width, img.height, to,
                                   Vec2{layerW / static_cast<f32>(img.width), layerH / static_cast<f32>(img.height)}, room,
                                   frames.back());
        }
        if (frames.empty()) return Status{Errc::IoError, "video ilegivel"};
        locals.resize(frames.size());
    }
    usize maskCount = 0;
    for (const auto& f : frames) maskCount = std::max(maskCount, f.size());
    if (maskCount == 0) return Status{Errc::NotFound, "nada acima do limiar"};

    // 3. As máscaras (um passo de desfazer).
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    const LayerId sourceId = LayerId::unpack(layerId);
    Layer* l = comp ? comp->layer(sourceId) : nullptr;
    if (!l) return Status{Errc::NotFound, "camada sumiu"};
    history_.before_mutation(*comp, project_->timeline().current(), "auto-trace");
    bump_model_revision_locked();
    LayerId targetId = sourceId;
    if (options.newLayer) {
        // Sólido branco do tamanho da camada, FILHO dela com transform neutro:
        // o espaço dele é o da camada, então as máscaras caem no mesmo lugar e
        // acompanham a animação dela.
        const LayerId id = comp->add_layer(LayerKind::Shape, clip_name("Auto-trace " + l->name));
        Layer* n = comp->layer(id);
        l = comp->layer(sourceId);
        if (!n || !l) return Status{Errc::OutOfMemory, "camada nao criada"};
        n->shape.shapeType = 0;
        n->shape.bounds = Rect{0.0f, 0.0f, layerW, layerH};
        n->shape.fillColor = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
        n->shape.filled = true;
        n->start = l->start;
        n->end = l->end;
        n->offset = l->offset;
        n->transform.anchor = Vec3{0.0f, 0.0f, 0.0f};
        n->transform.position = Vec3{0.0f, 0.0f, 0.0f};
        n->parent = sourceId;
        targetId = id;
    }
    Layer* target = comp->layer(targetId);
    if (!target) return Status{Errc::NotFound, "camada sumiu"};
    const usize slots = kMaxMaskCount - std::min<usize>(kMaxMaskCount, target->masks.size());
    maskCount = std::min(maskCount, slots);
    const bool animated = frames.size() > 1;
    u32 made = 0;
    for (usize k = 0; k < maskCount; ++k) {
        Mask m;
        m.id = target->alloc_mask_id();
        m.name = default_name(DefaultName::Mask) + " " + std::to_string(m.id + 1);
        m.closed = true;
        bool first = true;
        for (usize i = 0; i < frames.size(); ++i) {
            const bool has = k < frames[i].size();
            if (has && first) {
                m.operation = frames[i][k].hole ? MaskOperation::Subtract : MaskOperation::Add;
                m.points = frames[i][k].points;
                first = false;
            }
            if (!animated) continue;
            MaskPathKey key;
            key.frame = locals[i];
            key.interp = 0;   // segura: cada quadro tem o contorno dele, sem morph no meio
            if (has) key.points = frames[i][k].points;
            if (!m.pathKeys.empty() && m.pathKeys.back().frame >= key.frame) continue;   // tempo que não anda (congelado)
            m.pathKeys.push_back(std::move(key));
        }
        if (first) continue;
        target->masks.push_back(std::move(m));
        ++made;
    }
    comp->touch();
    project_->mark_dirty();
    request_render();
    if (outLayer) *outLayer = targetId.pack();
    return made;
}

} // namespace aurea
