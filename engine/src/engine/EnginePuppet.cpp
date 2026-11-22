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
#include "AutoRigParts.hpp"

#include <algorithm>
#include <array>
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

/// Grava a posição do pino no instante: keyframe se a trilha já anima ou com
/// `autoKey`; sempre também a constante (o arrasto e o giro de pinos).
void write_pin_pos(Layer& l, EffectInstance& e, u32 pin, Vec2 uv, FrameIndex local, bool autoKey) {
    const u32 param = puppet::pin_pos(pin);
    const f32 value[2] = {uv.x, uv.y};
    for (u32 c = 0; c < 2; ++c) {
        Track* t = l.tracks.find(TrackProperty::EffectParam, e.id, param_track_key(param, c));
        if ((t && !t->keys.empty()) || autoKey) {
            Track& tr = t ? *t : l.tracks.get_or_create(TrackProperty::EffectParam, e.id, param_track_key(param, c));
            // A first drag after seeking must keep the pin's earlier pose.
            // One key at the destination alone samples that pose at every frame.
            if (tr.keys.empty()) {
                const f32 initial = e.params[param].constant.v[c];
                tr.staticValue = initial;
                const FrameIndex start = l.local_time(l.start);
                if (local.value > start.value) (void)tr.set(start, initial, Interpolation::Linear);
            }
            (void)tr.set(local, value[c]);
        }
        e.params[param].constant.v[c] = value[c];
    }
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
        if (it != images_.end() && it->second) { w = static_cast<f32>(it->second->width); h = static_cast<f32>(it->second->height); }
    } else if (l.kind == LayerKind::Video && a) {
        w = static_cast<f32>(a->video.width);
        h = static_cast<f32>(a->video.height);
    } else if (l.kind == LayerKind::Shape) {
        w = l.shape.bounds.w;
        h = l.shape.bounds.h;
    }
    if (!(w > 0.0f) || !(h > 0.0f)) {
        const Composition* comp = project_ ? current_composition() : nullptr;
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
    if (it == images_.end() || !it->second) return false;
    const ImagePixels& px = *it->second;
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
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const EffectInstance* e = puppet_effect(l, effectId);
    if (!e || e->params.size() < puppet::kParamCount) return 0;
    f32 w, h;
    puppet_layer_size(*l, w, h);
    puppet::Mesh mesh;
    std::vector<Vec2> def;
    mesh_at(*l, *e, l->local_time(editing_frame_locked()), w, h, mesh, def);
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
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const EffectInstance* e = puppet_effect(l, effectId);
    if (!e) return 0;
    u32 n = 0;
    for (u32 i = 0; i < puppet::kMaxPins; ++i) n += pin_on(*e, i) ? 1u : 0u;
    const u32 need = n * kPuppetPinFloats;
    if (!out || capacity < need) return need;
    const FrameIndex local = l->local_time(editing_frame_locked());
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
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = puppet_effect(l, effectId);
    if (!e || !std::isfinite(u) || !std::isfinite(v) || e->params.size() < puppet::kParamCount) return -1;
    i32 slot = -1;
    for (u32 i = 0; i < puppet::kMaxPins && slot < 0; ++i) if (!pin_on(*e, i)) slot = static_cast<i32>(i);
    if (slot < 0) return -1;
    const FrameIndex local = l->local_time(editing_frame_locked());
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
    bump_model_revision_locked();
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

i32 Engine::puppet_auto_rig_image(u64 layerId, u32 effectId, const f32* pose, u32 count) noexcept {
    if (!pose || count != 2 + 33 * 4) return -1;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* effect = puppet_effect(layer, effectId);
    if (!effect || !layer || layer->locked || layer->kind != LayerKind::Image
        || layer->duration().value < 2 || effect->params.size() < puppet::kParamCount) return -1;
    for (u32 slot = 0; slot < puppet::kMaxPins; ++slot)
        if (pin_on(*effect, slot)) return -2;

    const auto source = images_.find(layer->source.pack());
    if (source == images_.end() || !source->second) return -1;
    const auto& image = *source->second;
    if (image.width == 0 || image.height == 0
        || image.rgba.size() / 4 < static_cast<usize>(image.width) * image.height) return -1;
    const f32 w = static_cast<f32>(image.width), h = static_cast<f32>(image.height);
    enum Joint : u32 { Pelvis, Head, LeftShoulder, RightShoulder, LeftElbow, RightElbow,
        LeftWrist, RightWrist, LeftHip, RightHip, LeftKnee, RightKnee, LeftAnkle, RightAnkle, Chest, JointCount };
    static_assert(JointCount <= puppet::kMaxPins);
    constexpr std::array<u32, JointCount> detector = {0, 0, 11, 12, 13, 14, 15, 16, 23, 24, 25, 26, 27, 28, 0};
    std::array<Vec2, JointCount> rest{};
    std::array<bool, JointCount> detected{};
    for (u32 joint = Head; joint < Chest; ++joint) {
        const f32* point = pose + 2 + detector[joint] * 4;
        if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[3])
            || point[3] < 0.45f || point[0] < 0 || point[0] >= 1 || point[1] < 0 || point[1] >= 1) continue;
        const u32 x = std::min(image.width - 1, static_cast<u32>(point[0] * w));
        const u32 y = std::min(image.height - 1, static_cast<u32>(point[1] * h));
        if (image.rgba[(static_cast<usize>(y) * image.width + x) * 4 + 3] < 26) continue;
        rest[joint] = {point[0] * w, point[1] * h};
        detected[joint] = true;
    }
    const bool shoulders = detected[LeftShoulder] && detected[RightShoulder];
    const bool hips = detected[LeftHip] && detected[RightHip];
    const bool wholeTorso = shoulders && hips;
    const bool leftArm = detected[LeftShoulder] && detected[LeftElbow];
    const bool rightArm = detected[RightShoulder] && detected[RightElbow];
    // Crops can omit the hips or hide a shoulder. Require a visible head plus
    // a shoulder and elbow, or the two shoulders and a visible head or arm.
    if (!wholeTorso && !(shoulders ? detected[Head] || leftArm || rightArm
                                 : detected[Head] && (leftArm || rightArm))) return -1;
    rest[Chest] = shoulders ? (rest[LeftShoulder] + rest[RightShoulder]) * .5f
                            : rest[leftArm ? LeftShoulder : RightShoulder];
    detected[Chest] = shoulders;
    if (wholeTorso) {
        rest[Pelvis] = (rest[LeftHip] + rest[RightHip]) * .5f;
        detected[Pelvis] = true;
    } else {
        // No lower-body chain without an observed pelvis to anchor it.
        for (Joint joint : {LeftHip, RightHip, LeftKnee, RightKnee, LeftAnkle, RightAnkle}) detected[joint] = false;
    }
    const Vec2 pivot = rest[wholeTorso ? Pelvis : Chest];
    const f32 torso = wholeTorso ? (rest[Chest] - pivot).length()
        : shoulders ? (rest[LeftShoulder] - rest[RightShoulder]).length()
                    : (rest[Head] - rest[Chest]).length();
    if (!std::isfinite(torso) || torso < 5.0f) return -1;
    detected[LeftElbow] = leftArm;
    detected[RightElbow] = rightArm;
    detected[LeftWrist] = detected[LeftWrist] && detected[LeftElbow];
    detected[RightWrist] = detected[RightWrist] && detected[RightElbow];
    detected[LeftAnkle] = detected[LeftAnkle] && detected[LeftKnee];
    detected[RightAnkle] = detected[RightAnkle] && detected[RightKnee];

    const FrameIndex start = layer->local_time(layer->start);
    const i64 length = layer->duration().value - 1;
    const f64 fps = std::isfinite(comp->fps()) ? std::clamp(comp->fps(), 1.0, 240.0) : 30.0;
    const i64 period = std::max<i64>(8, static_cast<i64>(std::llround(fps * 2.4)));
    const i64 step = std::max<i64>(1, period / 16);
    // Keep key density bounded on long stills. After the final key the idle holds.
    const i64 animationLength = std::min(length, period * 32);
    auto rotate = [](Vec2 delta, f32 angle) {
        const f32 c = std::cos(angle), s = std::sin(angle);
        return Vec2{c * delta.x - s * delta.y, s * delta.x + c * delta.y};
    };
    f32 outline[puppet::kOutline];
    if (!puppet_capture_outline(*comp, *layer, start, outline)) return -1;
    history_.before_mutation(*comp, project_->timeline().current(), "AutoRig 2D");
    for (u32 row = 0; row < puppet::kOutline; ++row)
        effect->params[puppet::kOutlineFirst + row].constant.v[0] = outline[row];

    auto normalized = [w, h](Vec2 p) { return Vec2{p.x / w, p.y / h}; };
    i32 pins = 0;
    for (u32 joint = 0; joint < JointCount; ++joint) {
        if (!detected[joint]) continue;
        const auto p = normalized(rest[joint]);
        effect->params[puppet::pin_on(joint)].constant.v[0] = 1.0f;
        auto& bind = effect->params[puppet::pin_rest(joint)].constant;
        bind.v[0] = p.x;
        bind.v[1] = p.y;
        write_pin_pos(*layer, *effect, joint, p, start, true);
        ++pins;
    }

    for (i64 frame = step; ; frame = std::min(animationLength, frame + step)) {
        frame = std::min(animationLength, frame);
        const f32 phase = static_cast<f32>((frame % period) * 2.0 * kPi / period);
        const f32 wave = std::sin(phase), breath = 0.5f - 0.5f * std::cos(phase);
        const Vec2 rootShift{.025f * torso * wave, .012f * torso * breath};
        const Vec2 upperShift{rootShift.x, rootShift.y - .018f * torso * breath};
        const f32 sway = .045f * wave;
        auto now = rest;
        auto upper = [&](Joint j) { return pivot + upperShift + rotate(rest[j] - pivot, sway); };
        now[Pelvis] = rest[Pelvis] + rootShift;
        for (Joint j : {Head, Chest, LeftShoulder, RightShoulder}) now[j] = upper(j);
        if (detected[Head]) {
            now[Head] = now[Chest] + rotate(rest[Head] - rest[Chest], sway * .35f);
        }
        auto arm = [&](Joint shoulder, Joint elbow, Joint wrist, f32 direction) {
            if (!detected[elbow]) return;
            const f32 swing = direction * .055f * wave;
            now[elbow] = now[shoulder] + rotate(rest[elbow] - rest[shoulder], sway + swing);
            if (detected[wrist]) now[wrist] = now[elbow] + rotate(rest[wrist] - rest[elbow], sway + swing * 1.4f);
        };
        arm(LeftShoulder, LeftElbow, LeftWrist, 1);
        arm(RightShoulder, RightElbow, RightWrist, -1);
        auto leg = [&](Joint hip, Joint knee, Joint ankle) {
            now[hip] = rest[hip] + rootShift;
            if (!detected[knee]) return;
            now[knee] = rest[knee] + rootShift * .45f;
            if (!detected[ankle]) return;
            const Vec2 toAnkle = rest[ankle] - now[hip];
            const f32 span = toAnkle.length();
            const f32 upperLength = (rest[knee] - rest[hip]).length();
            const f32 lowerLength = (rest[ankle] - rest[knee]).length();
            if (span < 1.0f || upperLength < 1.0f || lowerLength < 1.0f) return;
            const Vec2 along = toAnkle / span, across{-along.y, along.x};
            const f32 reach = std::clamp(span, std::abs(upperLength - lowerLength), upperLength + lowerLength);
            const f32 distance = (upperLength * upperLength - lowerLength * lowerLength + reach * reach) / (2.0f * reach);
            const f32 bend = std::sqrt(std::max(0.0f, upperLength * upperLength - distance * distance));
            const Vec2 original = rest[knee] - rest[hip];
            const f32 side = original.x * across.x + original.y * across.y >= 0 ? 1.0f : -1.0f;
            now[knee] = now[hip] + along * distance + across * (bend * side);
        };
        leg(LeftHip, LeftKnee, LeftAnkle);
        leg(RightHip, RightKnee, RightAnkle);
        for (u32 joint = 0; joint < JointCount; ++joint) {
            if (detected[joint]) write_pin_pos(*layer, *effect, joint, normalized(now[joint]), FrameIndex{start.value + frame}, true);
        }
        if (frame == animationLength) break;
    }
    bump_model_revision_locked();
    comp->touch();
    project_->mark_dirty();
    request_render();
    return pins;
}

i32 Engine::puppet_auto_rig_parts(u64 layerId, u32 effectId, const f32* pose, u32 count) noexcept {
    if (!pose || count != 2 + 33 * 4) return -1;
    const LayerId sourceId = LayerId::unpack(layerId);
    std::shared_ptr<const ImagePixels> image;
    CompositionId parent{};
    u64 session = 0;
    auto compatible = [&](const Layer* layer) {
        return layer && !layer->locked && layer->kind == LayerKind::Image && !layer->threeD &&
               layer->visible && layer->duration().value >= 2 && layer->effects.size() == 1 &&
               layer->effects.front().id == effectId && layer->effects.front().enabled &&
               layer->effects.front().type == effect_type_id(puppet::kPuppetKey) &&
               layer->effects.front().params.size() >= puppet::kParamCount && layer->masks.empty() &&
               !layer->matteSource.valid() && layer->blendMode == BlendMode::Normal &&
               !layer->adjustment;
    };
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        InlineEditScope inlineScope(*this, layerId);
        const auto* comp = project_ ? current_composition() : nullptr;
        const auto* layer = comp ? comp->layer(sourceId) : nullptr;
        if (!compatible(layer)) return -3;
        for (u32 i = 0; i < puppet::kMaxPins; ++i) if (pin_on(layer->effects.front(), i)) return -2;
        const auto it = images_.find(layer->source.pack());
        if (it == images_.end() || !it->second) return -1;
        image = it->second;
        parent = project_->timeline().current();
        session = projectSession_;
    }
    const u32 w = image->width, h = image->height;
    if (!w || !h || image->rgba.size() / 4 < static_cast<usize>(w) * h) return -1;
    const auto cuts = autorig::cut(image->rgba.data(), w, h, pose);
    if (cuts.empty()) return -1;
    usize masks = 0;
    for (const auto& cut : cuts) masks += cut.contours.size();
    if (masks > kMaxMaskCount || std::any_of(cuts.begin(), cuts.end(), [](const autorig::Cut& cut) {
        return std::any_of(cut.contours.begin(), cut.contours.end(), [](const autorig::Contour& c) {
            return c.points.size() > 4096;
        });
    })) return -3;

    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ && projectSession_ == session && project_->timeline().current() == parent
        ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(sourceId) : nullptr;
    if (!compatible(layer) || comp->layers().count() + cuts.size() > kMaxLayerCount) return -3;
    const auto found = images_.find(layer->source.pack());
    if (found == images_.end() || found->second != image) return -1;
    for (u32 i = 0; i < puppet::kMaxPins; ++i) if (pin_on(layer->effects.front(), i)) return -2;
    // An image used as another layer's matte has to remain whole.
    bool usedAsMatte = false;
    comp->layers().for_each([&](LayerId, const Layer& sibling) { usedAsMatte |= sibling.matteSource == sourceId; });
    if (usedAsMatte) return -3;

    using autorig::Part;
    constexpr std::array names{"Tronco e base", "Rosto", "Cabelo", "Mão esquerda", "Mão direita",
                               "Braço esquerdo", "Braço direito", "Perna esquerda", "Perna direita",
                               "Pálpebra esquerda", "Pálpebra direita"};
    const std::string name = layer->name;
    const FrameIndex timelineStart = layer->start;
    const i64 length = layer->duration().value - 1;
    const f64 fps = std::clamp(comp->fps(), 1.0, 240.0);
    const i64 period = std::max<i64>(8, std::llround(fps * 2.6));
    const i64 step = std::max<i64>(1, period / 14);
    const i64 end = std::min(length, period * 32);
    const Vec2 imageSize{static_cast<f32>(w), static_cast<f32>(h)};
    const f32 unit = std::min(imageSize.x, imageSize.y);
    const auto normalize = [&](Vec2 p) { return Vec2{p.x / imageSize.x, p.y / imageSize.y}; };
    f32 outline[puppet::kOutline]{};
    const bool hasOutline = puppet_capture_outline(*comp, *layer, layer->local_time(layer->start), outline);
    auto addMask = [&](Layer& part, const autorig::Contour& contour, bool inverse) {
        Mask mask;
        mask.id = part.alloc_mask_id();
        mask.name = part.name;
        mask.operation = contour.hole != inverse ? MaskOperation::Subtract : MaskOperation::Add;
        // A narrow underlap keeps a moving cut from opening a transparent
        // seam across an otherwise connected illustration.
        if (inverse && !contour.hole) mask.expansion = -std::clamp(unit * .006f, 1.5f, 7.f);
        for (Vec2 p : contour.points) mask.points.push_back({p, {}, {}});
        part.masks.push_back(std::move(mask));
    };
    auto rig = [&](Layer& part, Part kind, Vec2 pivot, Vec2 tip) {
        EffectInstance& effect = part.effects.front();
        const FrameIndex first = part.local_time(part.start);
        // Keep enough of the source-coordinate mesh to carry all mask edges.
        // The image cutout is applied before the Puppet effect in the render graph.
        if (hasOutline) {
            for (u32 row = 0; row < puppet::kOutline; ++row)
                effect.params[puppet::kOutlineFirst + row].constant.v[0] = outline[row];
        }
        if ((tip - pivot).length() < 4) tip = pivot + Vec2{unit * .05f, 0};
        const Vec2 delta = tip - pivot;
        const std::array rest{pivot, tip, pivot + delta * .56f + Vec2{-delta.y, delta.x} * .27f};
        for (u32 pin = 0; pin < rest.size(); ++pin) {
            const Vec2 uv = normalize(rest[pin]);
            effect.params[puppet::pin_on(pin)].constant.v[0] = 1;
            auto& bind = effect.params[puppet::pin_rest(pin)].constant;
            bind.v[0] = uv.x;
            bind.v[1] = uv.y;
        }
        for (i64 frame = 0; ; frame = std::min(end, frame + step)) {
            const f32 phase = static_cast<f32>((frame % period) * 6.283185307 / period);
            const f32 sway = std::sin(phase), breath = .5f - .5f * std::cos(phase);
            const Vec2 root{unit * .004f * sway, -unit * .006f * breath};
            f32 angle = .012f * sway;
            if (kind == Part::Face || autorig::is_lid(kind)) angle += .024f * sway;
            if (kind == Part::Hair) angle += .045f * (.8f * sway - .24f * std::sin(phase * 2));
            if (kind == Part::LeftHand || kind == Part::RightHand)
                angle += .081f * sway * (kind == Part::LeftHand ? 1 : -1);
            if (kind == Part::LeftArm || kind == Part::RightArm)
                angle += .046f * sway * (kind == Part::LeftArm ? 1 : -1);
            if (kind == Part::LeftLeg || kind == Part::RightLeg)
                angle += .021f * sway * (kind == Part::LeftLeg ? -1 : 1);
            for (u32 pin = 0; pin < rest.size(); ++pin) {
                f32 bend = angle;
                if (kind == Part::Hair && pin > 0)
                    bend += .017f * (pin == 1 ? sway : std::sin(phase * 2));
                if ((kind == Part::LeftHand || kind == Part::RightHand) && pin == 1)
                    bend += .019f * std::sin(phase * 2);
                const f32 c = std::cos(bend), s = std::sin(bend);
                const Vec2 d = rest[pin] - pivot;
                const Vec2 position = pivot + root + Vec2{c * d.x - s * d.y, s * d.x + c * d.y};
                write_pin_pos(part, effect, pin, normalize(position), FrameIndex{first.value + frame}, true);
            }
            if (frame == end) break;
        }
    };
    auto blink = [&](Layer& part, const autorig::Cut& cut) {
        const Vec2 down = cut.down, across{down.y, -down.x};
        const f32 extent = 2 * std::max(imageSize.x, imageSize.y);
        f32 top = extent, bottom = -extent;
        for (const auto& contour : cut.contours) for (const Vec2 p : contour.points) {
            const f32 height = p.x * down.x + p.y * down.y;
            top = std::min(top, height);
            bottom = std::max(bottom, height);
        }
        top -= 1;
        bottom += 1;
        Mask clip;
        clip.id = part.alloc_mask_id();
        clip.name = part.name;
        clip.operation = MaskOperation::Intersect;
        const auto points = [&](f32 closure) {
            const f32 edge = top + (bottom - top) * closure;
            return std::vector<MaskPoint>{
                {across * -extent - down * extent, {}, {}}, {across * extent - down * extent, {}, {}},
                {across * extent + down * edge, {}, {}}, {across * -extent + down * edge, {}, {}},
            };
        };
        clip.points = points(0);
        const i64 first = part.local_time(part.start).value;
        const auto key = [&](i64 frame, f32 closure) {
            if (frame > end) return;
            MaskPathKey k;
            k.frame = first + frame;
            k.points = points(closure);
            clip.pathKeys.push_back(std::move(k));
        };
        key(0, 0);
        const i64 closing = std::max<i64>(1, std::llround(fps * .077));
        const i64 opening = std::max<i64>(1, std::llround(fps * .114));
        for (i64 start = std::llround(period * .43); start + closing + opening + 1 <= end; start += period) {
            key(start, 0);
            key(start + closing, 1);
            key(start + closing + 1, 1);
            key(start + closing + opening + 1, 0);
        }
        part.masks.push_back(std::move(clip));
    };

    // Reuse the original layer for the base so existing layer references and
    // transform keyframes keep the same ID. Copies share the image asset.
    history_.before_mutation(*comp, parent, "AutoRig 2D · camadas");
    std::vector<LayerId> made;
    made.reserve(cuts.size());
    const i32 z = comp->z_index_of(sourceId);
    for (const auto& cut : cuts) {
        const LayerId id = comp->duplicate_layer(sourceId, timelineStart);
        auto* part = comp->layer(id);
        if (!part) {
            for (const auto created : made) comp->remove_layer(created);
            return -3;
        }
        part->name = name + " · " + names[static_cast<usize>(cut.part)];
        part->magneticTrack = false;
        for (const auto& contour : cut.contours) addMask(*part, contour, false);
        if (autorig::is_lid(cut.part)) {
            part->kind = LayerKind::Shape;
            part->source = {};
            part->shape = {};
            part->shape.bounds = {0, 0, imageSize.x, imageSize.y};
            part->shape.fillColor = cut.fill;
            for (auto& mask : part->masks) if (mask.operation == MaskOperation::Add) mask.expansion = .5f;
            blink(*part, cut);
        }
        rig(*part, cut.part, cut.pivot, cut.tip);
        made.push_back(id);
    }
    for (usize i = 0; i < made.size(); ++i) comp->reorder_layer(made[i], static_cast<u32>(z + 1 + i));
    layer = comp->layer(sourceId);
    layer->name = name + " · " + names[0];
    for (const auto& cut : cuts)
        if (!autorig::is_lid(cut.part))
            for (const auto& contour : cut.contours) if (!contour.hole) addMask(*layer, contour, true);
    for (const auto& cut : cuts)
        if (!autorig::is_lid(cut.part))
            for (const auto& contour : cut.contours) if (contour.hole) addMask(*layer, contour, true);
    rig(*layer, Part::Body, {imageSize.x * .48f, imageSize.y * .74f},
        {imageSize.x * .45f, imageSize.y * .32f});
    bump_model_revision_locked();
    comp->touch();
    project_->mark_dirty();
    request_render();
    return static_cast<i32>(made.size() + 1);
}

bool Engine::puppet_move_pin(u64 layerId, u32 effectId, u32 pin, f32 u, f32 v, bool autoKey, bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = puppet_effect(l, effectId);
    if (!e || pin >= puppet::kMaxPins || !pin_on(*e, pin) || !std::isfinite(u) || !std::isfinite(v)) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "mover pino");
    bump_model_revision_locked();
    write_pin_pos(*l, *e, pin, Vec2{u, v}, l->local_time(editing_frame_locked()), autoKey);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::puppet_rotate_pins(u64 layerId, u32 effectId, u32 pivot, u32 mask, f32 u0, f32 v0, f32 u1, f32 v1,
                                bool autoKey, bool continuing) noexcept {
    if (!std::isfinite(u0) || !std::isfinite(v0) || !std::isfinite(u1) || !std::isfinite(v1)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = puppet_effect(l, effectId);
    if (!e || l->locked || pivot >= puppet::kMaxPins || !pin_on(*e, pivot)) return false;
    mask &= ~(1u << pivot);
    u32 moving = 0;
    for (u32 i = 0; i < puppet::kMaxPins; ++i)
        if ((mask >> i) & 1u) moving |= pin_on(*e, i) ? (1u << i) : 0u;
    if (!moving) return false;
    // Ângulo varrido em volta do pivô em px da camada (fração achataria o giro).
    f32 w, h;
    puppet_layer_size(*l, w, h);
    const FrameIndex local = l->local_time(editing_frame_locked());
    const Vec2 pv = pin_value(*l, *e, puppet::pin_pos(pivot), local);
    const Vec2 c{pv.x * w, pv.y * h};
    const Vec2 a = Vec2{u0 * w, v0 * h} - c, b = Vec2{u1 * w, v1 * h} - c;
    if (a.length_sq() < 1e-6f || b.length_sq() < 1e-6f) return false;
    f32 ang = std::atan2(b.y, b.x) - std::atan2(a.y, a.x);
    constexpr f32 kPi = 3.14159265358979323846f;
    while (ang > kPi) ang -= 2.0f * kPi;
    while (ang < -kPi) ang += 2.0f * kPi;
    if (!std::isfinite(ang) || std::fabs(ang) < 1e-7f) return false;
    const f32 cs = std::cos(ang), sn = std::sin(ang);
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "girar pinos");
    bump_model_revision_locked();
    for (u32 i = 0; i < puppet::kMaxPins; ++i) {
        if (!((moving >> i) & 1u)) continue;
        const Vec2 p = pin_value(*l, *e, puppet::pin_pos(i), local);
        const Vec2 d = Vec2{p.x * w, p.y * h} - c;
        const Vec2 r{c.x + d.x * cs - d.y * sn, c.y + d.x * sn + d.y * cs};
        write_pin_pos(*l, *e, i, Vec2{r.x / w, r.y / h}, local, autoKey);
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::puppet_remove_pin(u64 layerId, u32 effectId, u32 pin) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = puppet_effect(l, effectId);
    if (!e || pin >= puppet::kMaxPins || !pin_on(*e, pin)) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "apagar pino");
    bump_model_revision_locked();
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
