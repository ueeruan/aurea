// =============================================================================
//  Aurea / engine / EngineAeTimeline.cpp
//
//  Timeline do AE (2026-11-11, gap_system itens 3, 4 e 8 e o bug B6). O After
//  Effects é só a especificação de comportamento; Android e iOS só chamam, a
//  regra mora aqui.
//
//  - Marcas com duração e comentário, na régua e nas CAMADAS (a marca da
//    camada mora em tempo local e anda com ela; `thisLayer.marker`).
//  - Escalar no tempo os keyframes escolhidos (o Alt+arrastar do AE) e o
//    Easy Ease / In / Out da seleção, cada um um passo de desfazer.
//  - Área de trabalho: a prévia repete dentro dela, a exportação pode sair só
//    com ela e "Aparar composição à área de trabalho" (Engine.cpp, junto do
//    aparar de clipe).
//  - Alinhar/distribuir no espaço com a conta no motor (antes cada tela tinha
//    a sua e eram vários passos de desfazer).
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace aurea {

namespace {

constexpr i64 kMarkerFrameLimit = i64{1} << 31;

bool marker_text_ok(const std::string& label, const std::string& comment) noexcept {
    return label.size() <= kMaxMarkerText && comment.size() <= kMaxMarkerText;
}

/// Insere mantendo a ordem (troca a do mesmo quadro).
void put_layer_marker(std::vector<Marker>& list, Marker m) {
    auto it = std::lower_bound(list.begin(), list.end(), m.frame,
                               [](const Marker& a, FrameIndex f) { return a.frame.value < f.value; });
    if (it != list.end() && it->frame == m.frame) *it = std::move(m);
    else list.insert(it, std::move(m));
}

std::vector<Marker>::iterator find_marker(std::vector<Marker>& list, i64 frame) {
    return std::find_if(list.begin(), list.end(), [frame](const Marker& m) { return m.frame.value == frame; });
}

} // namespace

// =============================================================================
// Marcas da composição: duração e comentário
// =============================================================================
bool Engine::edit_marker_ex(i64 from, i64 to, u32 color, const std::string& label,
                            i64 duration, const std::string& comment) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || to < 0 || to >= comp->duration().value || !marker_text_ok(label, comment)
        || duration < 0 || duration > kMaxMarkerDuration) return false;
    const auto& markers = comp->markers();
    const auto source = std::find_if(markers.begin(), markers.end(), [from](const Marker& m) { return m.frame.value == from; });
    if (from >= 0 && source == markers.end()) return false;
    if (std::any_of(markers.begin(), markers.end(), [from, to](const Marker& m) {
            return m.frame.value == to && m.frame.value != from;
        })) return false;
    color |= 0xFF000000u;
    if (source != markers.end() && from == to && source->color == color && source->label == label
        && source->duration == duration && source->comment == comment) return true;
    Marker edited{FrameIndex{to}, color, source == markers.end() ? kMarkerManual : source->kind, label};
    edited.duration = duration;
    edited.comment = comment;
    history_.before_mutation(*comp, project_->timeline().current(), from < 0 ? "adicionar marca" : "editar marca");
    if (from >= 0) comp->remove_marker_at(FrameIndex{from});
    comp->put_marker(std::move(edited));
    bump_revision_without_audio_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::marker_details(i64 frame, i64& duration, std::string& comment) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    for (const Marker& m : comp->markers()) {
        if (m.frame.value != frame) continue;
        duration = m.duration;
        comment = m.comment;
        return true;
    }
    return false;
}

// =============================================================================
// Marcas da camada (tempo local; a interface fala em quadros da composição)
// =============================================================================
bool Engine::edit_layer_marker(u64 layerId, i64 fromFrame, i64 toFrame, u32 color, const std::string& label,
                               i64 duration, const std::string& comment) noexcept {
    if (toFrame < 0 || toFrame >= kMarkerFrameLimit || fromFrame >= kMarkerFrameLimit
        || duration < 0 || duration > kMaxMarkerDuration || !marker_text_ok(label, comment)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || l->markers.size() >= 100000u) return false;
    const i64 from = fromFrame >= 0 ? l->local_time(FrameIndex{fromFrame}).value : -1;
    const i64 to = l->local_time(FrameIndex{toFrame}).value;
    auto source = fromFrame >= 0 ? find_marker(l->markers, from) : l->markers.end();
    if (fromFrame >= 0 && source == l->markers.end()) return false;
    if (std::any_of(l->markers.begin(), l->markers.end(), [&](const Marker& m) {
            return m.frame.value == to && (fromFrame < 0 || m.frame.value != from);
        })) return false;
    color |= 0xFF000000u;
    if (source != l->markers.end() && from == to && source->color == color && source->label == label
        && source->duration == duration && source->comment == comment) return true;
    Marker edited{FrameIndex{to}, color, source == l->markers.end() ? kMarkerManual : source->kind, label};
    edited.duration = duration;
    edited.comment = comment;
    history_.before_mutation(*comp, project_->timeline().current(),
                             fromFrame < 0 ? "adicionar marca da camada" : "editar marca da camada");
    if (source != l->markers.end()) l->markers.erase(source);
    put_layer_marker(l->markers, std::move(edited));
    bump_revision_without_audio_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::move_layer_marker(u64 layerId, i64 fromFrame, i64 toFrame) noexcept {
    if (fromFrame < 0 || toFrame < 0 || fromFrame >= kMarkerFrameLimit || toFrame >= kMarkerFrameLimit) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return false;
    const i64 from = l->local_time(FrameIndex{fromFrame}).value, to = l->local_time(FrameIndex{toFrame}).value;
    auto source = find_marker(l->markers, from);
    if (source == l->markers.end()) return false;
    if (from == to) return true;
    if (find_marker(l->markers, to) != l->markers.end()) return false;   // não engole outra marca
    Marker m = *source;
    m.frame = FrameIndex{to};
    history_.before_mutation(*comp, project_->timeline().current(), "mover marca da camada");
    l->markers.erase(source);
    put_layer_marker(l->markers, std::move(m));
    bump_revision_without_audio_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::delete_layer_marker(u64 layerId, i64 frame) noexcept {
    if (frame < 0 || frame >= kMarkerFrameLimit) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return false;
    const auto found = find_marker(l->markers, l->local_time(FrameIndex{frame}).value);
    if (found == l->markers.end()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "remover marca da camada");
    l->markers.erase(found);
    bump_revision_without_audio_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::query_layer_markers(u64 layerId, i64* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return 0;
    for (u32 i = 0; i < l->markers.size() && i < capacity && out; ++i) {
        const Marker& m = l->markers[i];
        out[i * 4] = l->timeline_time(m.frame).value;
        out[i * 4 + 1] = m.duration;
        out[i * 4 + 2] = static_cast<i64>(m.color);
        out[i * 4 + 3] = static_cast<i64>(m.kind);
    }
    return static_cast<u32>(l->markers.size());
}

u32 Engine::query_all_layer_markers(i64* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return 0;
    u32 total = 0;
    for (u32 i = 0; i < comp->order().size(); ++i) {
        const LayerId id = comp->order().at(i);
        const Layer* l = comp->layer(id);
        if (!l) continue;
        for (const Marker& m : l->markers) {
            if (out && total < capacity) {
                out[total * 4] = static_cast<i64>(id.pack());
                out[total * 4 + 1] = l->timeline_time(m.frame).value;
                out[total * 4 + 2] = m.duration;
                out[total * 4 + 3] = static_cast<i64>(m.color);
            }
            ++total;
        }
    }
    return total;
}

bool Engine::layer_marker_text(u64 layerId, i64 frame, std::string& label, std::string& comment) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    const i64 local = l->local_time(FrameIndex{frame}).value;
    for (const Marker& m : l->markers) {
        if (m.frame.value != local) continue;
        label = m.label;
        comment = m.comment;
        return true;
    }
    return false;
}

// =============================================================================
// Keyframes: escalar no tempo e Easy Ease
// =============================================================================
u32 Engine::scale_keyframe_selection(u64 layerId, const i64* references, u32 count,
                                     i64 anchorFrame, f64 factor) noexcept {
    if (!std::isfinite(factor) || factor <= 0.0 || factor == 1.0 || anchorFrame < INT32_MIN || anchorFrame > INT32_MAX)
        return 0;
    return retime_keyframe_selection(layerId, references, count, 0, false, anchorFrame, factor);
}

u32 Engine::ease_keyframe_selection(u64 layerId, const i64* refs, u32 count, u32 mode) noexcept {
    if (!refs || count == 0 || count > 16384 || mode > kEasyEaseOut) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->locked) return 0;
    // Trechos (índice do key da ESQUERDA) que ganham ease na saída / na chegada.
    struct Edit { Track* track; std::vector<u32> out, in; };
    std::vector<Edit> edits;
    u32 picked = 0;
    for (u32 i = 0; i < count; ++i) {
        const i64* ref = refs + i * 4;
        if (ref[0] < 0 || ref[0] > 65535 || ref[1] < -1 || ref[1] > UINT32_MAX
            || ref[2] < 0 || ref[2] > UINT32_MAX || ref[3] < INT32_MIN || ref[3] > INT32_MAX) return 0;
        const auto property = static_cast<TrackProperty>(ref[0]);
        // Caminhos (máscara, forma, malha) não têm valor escalar nem bézier por trecho aqui.
        if (property == TrackProperty::MeshWarp || property == TrackProperty::VectorPath
            || property == TrackProperty::MaskPath) continue;
        Track* track = property == TrackProperty::TimeRemap ? &layer->timeRemap
                     : property == TrackProperty::Speed ? layer->tracks.find(property)
                     : layer->tracks.find(property, static_cast<u32>(ref[1]), static_cast<u32>(ref[2]));
        if (!track) continue;
        const u32 key = track->find_exact(FrameIndex{ref[3]});
        if (key == kInvalidIndex) continue;
        auto it = std::find_if(edits.begin(), edits.end(), [track](const Edit& e) { return e.track == track; });
        if (it == edits.end()) { edits.push_back({track, {}, {}}); it = edits.end() - 1; }
        const u32 last = static_cast<u32>(track->keys.size()) - 1;
        if (mode != kEasyEaseIn && key < last) it->out.push_back(key);
        if (mode != kEasyEaseOut && key > 0) it->in.push_back(key - 1);
        if ((mode != kEasyEaseIn && key < last) || (mode != kEasyEaseOut && key > 0)) ++picked;
    }
    if (picked == 0) return 0;
    history_.before_mutation(*comp, project_->timeline().current(),
                             mode == kEasyEaseIn ? "easy ease entrada" : mode == kEasyEaseOut ? "easy ease saida" : "easy ease");
    constexpr f32 kInfluence = 1.0f / 3.0f;   // 33,33 % do AE, velocidade zero no key
    for (Edit& e : edits) {
        std::vector<u32> segments = e.out;
        segments.insert(segments.end(), e.in.begin(), e.in.end());
        std::sort(segments.begin(), segments.end());
        segments.erase(std::unique(segments.begin(), segments.end()), segments.end());
        for (const u32 s : segments) {
            Keyframe& k = e.track->keys[s];
            if (k.interp != Interpolation::Bezier && k.interp != Interpolation::CustomCurve) {
                // O lado que não recebe ease guarda o jeito da curva de antes:
                // chegada/saída suave nos presets que já eram suaves ali, senão
                // a reta (a velocidade média do trecho).
                const bool easedOut = k.interp == Interpolation::EaseIn || k.interp == Interpolation::EaseInOut;
                const bool easedIn = k.interp == Interpolation::EaseOut || k.interp == Interpolation::EaseInOut;
                k.bx1 = kInfluence; k.by1 = easedOut ? 0.0f : kInfluence;
                k.bx2 = 1.0f - kInfluence; k.by2 = easedIn ? 1.0f : 1.0f - kInfluence;
                k.interp = Interpolation::Bezier;
                k.easePower = 1;
            }
            if (std::find(e.out.begin(), e.out.end(), s) != e.out.end()) { k.bx1 = kInfluence; k.by1 = 0.0f; }
            if (std::find(e.in.begin(), e.in.end(), s) != e.in.end()) { k.bx2 = 1.0f - kInfluence; k.by2 = 1.0f; }
            k.easingPreset = 0;
        }
        e.track->lastIndex = 0;
    }
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return picked;
}

// =============================================================================
// Área de trabalho
// =============================================================================
bool Engine::set_work_area(i64 in, i64 out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    const i64 duration = comp->duration().value;
    const bool clear = in < 0 || out <= in;
    if (!clear) {
        in = std::clamp<i64>(in, 0, duration - 1);
        out = std::clamp<i64>(out, in + 1, duration);
    }
    const i64 nextIn = clear || (in == 0 && out == duration) ? -1 : in;
    const i64 nextOut = nextIn < 0 ? -1 : out;
    if (nextIn == comp->work_in_raw() && nextOut == comp->work_out_raw()) return true;
    history_.before_mutation(*comp, project_->timeline().current(), nextIn < 0 ? "limpar area de trabalho" : "area de trabalho");
    comp->set_work_area(nextIn, nextOut);
    configure_playback_locked(comp);
    bump_revision_without_audio_locked();
    project_->mark_dirty();
    return true;
}

bool Engine::query_work_area(i64& in, i64& out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) { in = 0; out = 0; return false; }
    in = comp->work_in();
    out = comp->work_out();
    return comp->has_work_area();
}

void Engine::set_export_work_area_only(bool on) noexcept {
    exportWorkAreaOnly_.store(on, std::memory_order_relaxed);
}

bool Engine::export_work_area_only() const noexcept {
    return exportWorkAreaOnly_.load(std::memory_order_relaxed);
}

// =============================================================================
// Alinhar / distribuir no espaço (B6)
// =============================================================================
u32 Engine::align_layers(const u64* layerIds, u32 count, u32 mode) noexcept {
    if (!layerIds || count == 0 || count > 4096 || mode > 7) return 0;
    f32 cw = 0.0f, ch = 0.0f;
    std::vector<u64> ids;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        const Composition* comp = project_ ? current_composition() : nullptr;
        if (!comp) return 0;
        cw = static_cast<f32>(comp->width());
        ch = static_cast<f32>(comp->height());
        for (u32 i = 0; i < count; ++i) {
            const Layer* l = comp->layer(LayerId::unpack(layerIds[i]));
            if (l && !l->locked && std::find(ids.begin(), ids.end(), layerIds[i]) == ids.end()) ids.push_back(layerIds[i]);
        }
    }
    // A caixa é a que o palco mostra: os cantos do motor (pais e câmera
    // incluídos) ou, sem eles, o retângulo da mídia com posição/âncora/escala/giro.
    struct Box { u64 id; f32 x, y, l, t, r, b, m00, m10, m01, m11; };   // m = parte linear do pai (colunas)
    std::vector<Box> boxes;
    for (const u64 id : ids) {
        bridge::LayerDetailPOD pod;
        if (!query_layer_detail(id, pod)) continue;
        f32 xs[4], ys[4];
        if (pod.geomFlags & bridge::kGeomCornersValid) {
            for (int i = 0; i < 4; ++i) { xs[i] = pod.corners[i * 2]; ys[i] = pod.corners[i * 2 + 1]; }
        } else {
            const bool image = pod.kind == static_cast<u32>(LayerKind::Image);
            const f32 w = pod.sourceWidth > 0 ? static_cast<f32>(pod.sourceWidth) : image && pod.anchor[0] > 0 ? pod.anchor[0] * 2.0f : 0.0f;
            const f32 h = pod.sourceHeight > 0 ? static_cast<f32>(pod.sourceHeight) : image && pod.anchor[1] > 0 ? pod.anchor[1] * 2.0f : 0.0f;
            if (w <= 0.0f || h <= 0.0f) continue;
            const f32 rad = pod.rotation[2] * 3.14159265358979f / 180.0f;
            const f32 cs = std::cos(rad), sn = std::sin(rad);
            for (int i = 0; i < 4; ++i) {
                const f32 lx = (i == 1 || i == 2) ? w : 0.0f, ly = i >= 2 ? h : 0.0f;
                const f32 dx = (lx - pod.anchor[0]) * pod.scale[0], dy = (ly - pod.anchor[1]) * pod.scale[1];
                xs[i] = pod.position[0] + dx * cs - dy * sn;
                ys[i] = pod.position[1] + dx * sn + dy * cs;
            }
        }
        Box box{id, pod.position[0], pod.position[1], xs[0], ys[0], xs[0], ys[0],
                pod.parentAffine[0], pod.parentAffine[1], pod.parentAffine[2], pod.parentAffine[3]};
        for (int i = 1; i < 4; ++i) {
            box.l = std::min(box.l, xs[i]); box.r = std::max(box.r, xs[i]);
            box.t = std::min(box.t, ys[i]); box.b = std::max(box.b, ys[i]);
        }
        if (!std::isfinite(box.l) || !std::isfinite(box.r) || !std::isfinite(box.t) || !std::isfinite(box.b)) continue;
        boxes.push_back(box);
    }
    // Deslocamento de cada caixa na composição.
    std::vector<std::pair<f32, f32>> deltas(boxes.size(), {0.0f, 0.0f});
    if (mode <= 5) {
        if (cw <= 0.0f || ch <= 0.0f) return 0;
        for (usize i = 0; i < boxes.size(); ++i) {
            const Box& b = boxes[i];
            switch (mode) {
                case 0: deltas[i].first = -b.l; break;
                case 1: deltas[i].first = cw * 0.5f - (b.l + b.r) * 0.5f; break;
                case 2: deltas[i].first = cw - b.r; break;
                case 3: deltas[i].second = -b.t; break;
                case 4: deltas[i].second = ch * 0.5f - (b.t + b.b) * 0.5f; break;
                default: deltas[i].second = ch - b.b; break;
            }
        }
    } else {
        // Vãos iguais: as das pontas ficam, as do meio se espalham.
        if (boxes.size() < 3) return 0;
        const bool horizontal = mode == 6;
        std::vector<usize> order(boxes.size());
        for (usize i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](usize a, usize b) {
            return horizontal ? boxes[a].l < boxes[b].l : boxes[a].t < boxes[b].t;
        });
        const auto lo = [&](const Box& b) { return horizontal ? b.l : b.t; };
        const auto hi = [&](const Box& b) { return horizontal ? b.r : b.b; };
        f32 span = hi(boxes[order.back()]) - lo(boxes[order.front()]);
        f32 sizes = 0.0f;
        for (const Box& b : boxes) sizes += hi(b) - lo(b);
        const f32 gap = (span - sizes) / static_cast<f32>(boxes.size() - 1);
        f32 cursor = lo(boxes[order.front()]);
        for (const usize i : order) {
            const f32 delta = cursor - lo(boxes[i]);
            cursor += hi(boxes[i]) - lo(boxes[i]) + gap;
            (horizontal ? deltas[i].first : deltas[i].second) = delta;
        }
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return 0;
    history_.begin_group(mode >= 6 ? "distribuir" : "alinhar");
    u32 moved = 0;
    for (usize i = 0; i < boxes.size(); ++i) {
        const Box& b = boxes[i];
        f32 dx = deltas[i].first, dy = deltas[i].second;
        if (std::fabs(dx) < 1e-4f && std::fabs(dy) < 1e-4f) continue;
        // A posição mora no espaço do PAI: o deslocamento da composição volta
        // pela inversa da parte linear do pai (sem pai é a identidade).
        const f32 det = b.m00 * b.m11 - b.m01 * b.m10;
        if (std::fabs(det) > 1e-8f) {
            const f32 px = (b.m11 * dx - b.m01 * dy) / det, py = (-b.m10 * dx + b.m00 * dy) / det;
            dx = px; dy = py;
        }
        bool ok = false;
        for (int axis = 0; axis < 2; ++axis) {
            Command c;
            c.type = CommandType::KeyframeInsert;
            c.keyframe.track = TrackRef{LayerId::unpack(b.id), axis == 0 ? TrackProperty::PositionX : TrackProperty::PositionY,
                                        kInvalidIndex, 0};
            c.keyframe.time = FrameIndex{0};
            c.keyframe.value = axis == 0 ? b.x + dx : b.y + dy;
            // A regra do gesto de transform: animada ganha keyframe no quadro
            // que a prévia mostra, parada muda o valor.
            c.keyframe.onlyIfChanged = kAutoKeyOnlyIfChanged | kAutoKeyAtPlayhead | kAutoKeyStaticWhenUnanimated;
            if (apply_command_internal(c, nullptr, true).ok()) ok = true;
        }
        if (ok) ++moved;
    }
    history_.end_group();
    if (moved) project_->mark_dirty();
    request_render();
    return moved;
}

} // namespace aurea
