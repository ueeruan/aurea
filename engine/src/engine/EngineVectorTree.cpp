// =============================================================================
//  Aurea / engine / EngineVectorTree.cpp
//
//  Lote X do AE (2026-11-12, gap_system item 26): a árvore do conteúdo da
//  camada vetorial para o painel do Aurea PC — listar e editar os itens na
//  ordem do After Effects (vector/VectorTree.hpp). O painel do celular
//  continua no documento de sempre (EngineVector.cpp), que vê o primeiro item
//  de cada tipo dos grupos do primeiro nível.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/vector/VectorTree.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace aurea {
namespace {

Layer* tree_layer(Composition* comp, u64 id) noexcept {
    Layer* l = comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    return l && l->kind == LayerKind::Shape && l->shape.shapeType == kShapeVector ? l : nullptr;
}

/// As trilhas VectorParam do item de sempre de um tipo (as que deixam de
/// valer quando o primeiro item do tipo passa a ser outro).
bool slot_param(VectorItemKind kind, u32 param) noexcept {
    switch (kind) {
        case VectorItemKind::Trim: return param == kVecTrimStart || param == kVecTrimEnd || param == kVecTrimOffset;
        case VectorItemKind::Fill: return param == kVecFillOpacity;
        case VectorItemKind::Stroke:
            return param == kVecStrokeWidth || param == kVecDashOffset || param == kVecStrokeOpacity || (param >= kVecDash1 && param <= kVecGap3);
        case VectorItemKind::Repeater:
            return (param >= kVecRepCopies && param <= kVecRepEndOpacity) || param == kVecRepScaleY;
        default: return false;
    }
}

/// Depois de uma edição num grupo do PRIMEIRO nível: as trilhas guardadas por
/// índice acompanham os itens (ou saem com eles).
void remap_tracks(Layer& l, u32 group, const vector::TreeRemap& remap) {
    if (!remap.changed) return;
    const auto mapped = [&](u32 old) { return old < remap.map.size() ? remap.map[old] : old; };
    if (remap.kind == VectorItemKind::Path) {
        l.tracks.remove_if([&](const Track& t) {
            return t.property == TrackProperty::VectorPath && t.effectIndex == group && mapped(t.effectParamIndex) == kInvalidIndex;
        });
        for (u32 i = 0; i < l.tracks.size(); ++i) {
            Track& t = l.tracks.at(i);
            if (t.property == TrackProperty::VectorPath && t.effectIndex == group) t.effectParamIndex = mapped(t.effectParamIndex);
        }
    } else if (remap.kind == VectorItemKind::Modifier) {
        const auto modifier_of = [](const Track& t) { return (t.effectParamIndex - kVecModParamBase) / kVecModParams; };
        l.tracks.remove_if([&](const Track& t) {
            return t.property == TrackProperty::VectorParam && t.effectIndex == group && is_vector_modifier_param(t.effectParamIndex)
                && mapped(modifier_of(t)) == kInvalidIndex;
        });
        for (u32 i = 0; i < l.tracks.size(); ++i) {
            Track& t = l.tracks.at(i);
            if (t.property != TrackProperty::VectorParam || t.effectIndex != group || !is_vector_modifier_param(t.effectParamIndex)) continue;
            const u32 slot = (t.effectParamIndex - kVecModParamBase) % kVecModParams;
            t.effectParamIndex = vector_modifier_param(mapped(modifier_of(t)), slot);
        }
    } else if (!remap.map.empty() && remap.map[0] != 0) {
        // O primeiro item do tipo mudou de dono: as trilhas dele não valem para o novo.
        const VectorItemKind kind = remap.kind;
        l.tracks.remove_if([&](const Track& t) {
            return t.property == TrackProperty::VectorParam && t.effectIndex == group && slot_param(kind, t.effectParamIndex);
        });
    }
}

} // namespace

bool Engine::vector_tree(u64 layerId, std::vector<f32>& out, std::string& names) noexcept {
    out.clear();
    names.clear();
    std::lock_guard<std::mutex> lock(modelMutex_);
    InlineEditScope inlineScope(*this, layerId);
    const Layer* l = tree_layer(project_ ? current_composition() : nullptr, layerId);
    if (!l) return false;
    std::vector<vector::TreeRow> rows;
    vector::list_tree(l->shape.vector, rows);
    out.push_back(1.0f);   // versão do fluxo
    out.push_back(static_cast<f32>(rows.size()));
    bool first = true;
    for (const vector::TreeRow& r : rows) {
        out.push_back(static_cast<f32>(r.depth));
        out.push_back(static_cast<f32>(r.kind));
        out.push_back(static_cast<f32>(r.ordinal));
        out.push_back(static_cast<f32>(r.position));
        out.push_back(r.enabled ? 1.0f : 0.0f);
        out.push_back(static_cast<f32>(r.valueCount));
        out.insert(out.end(), r.values, r.values + r.valueCount);
        if (r.kind == VectorItemKind::Group) {
            std::string nm = r.name;
            std::replace(nm.begin(), nm.end(), '\n', ' ');
            if (!first) names.push_back('\n');
            names += nm;
            first = false;
        }
    }
    return true;
}

bool Engine::vector_tree_edit(u64 layerId, const u32* path, u32 depth, u32 op, u32 a, u32 b, u32 c,
                              const f32* values, u32 count, const char* name, bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = tree_layer(comp, layerId);
    if (!l || l->locked || count > 64 || (count > 0 && !values)) return false;
    VectorData next = l->shape.vector;   // edita uma cópia: recusa não deixa nada pela metade
    const std::string label = name ? std::string(name) : std::string();
    vector::TreeRemap remap;
    bool ok = false;
    if (op == kVectorTreeAddGroup) {
        // Grupo novo no primeiro nível (vazio, na ordem livre), no fim da lista = por cima.
        if (next.groups.size() >= 4096) return false;
        VectorGroup g;
        g.name = label.empty() ? std::string("Grupo") : label;
        g.fill.enabled = false;
        g.stroke.enabled = false;
        g.freeOrder = true;
        next.groups.push_back(std::move(g));
        ok = true;
    } else {
        VectorGroup* g = vector::group_at(next, path, depth);
        if (!g) return false;
        switch (op) {
            case kVectorTreeAdd:
                if (a >= static_cast<u32>(VectorItemKind::Count)) return false;
                ok = vector::add_item(*g, static_cast<VectorItemKind>(a), b, c, label, &remap);
                break;
            case kVectorTreeRemove: ok = vector::remove_item(*g, a, &remap); break;
            case kVectorTreeMove:
                ok = vector::move_item(*g, a, b, &remap);
                // The mobile order panel must preserve the keyed primary paint/operator.
                // Extra items of these types do not yet have independent parameter tracks.
                if (ok && c != 0 && depth == 1 && !remap.map.empty() && remap.map[0] != 0) {
                    for (u32 i = 0; i < l->tracks.size(); ++i) {
                        const auto& track = l->tracks.at(i);
                        if (track.property == TrackProperty::VectorParam && track.effectIndex == path[0] &&
                            slot_param(remap.kind, track.effectParamIndex)) return false;
                    }
                }
                break;
            case kVectorTreeEnable: ok = vector::set_item_enabled(*g, a, b != 0); break;
            case kVectorTreeValues: ok = vector::set_item_values(*g, a, values, count); break;
            case kVectorTreeRename:
                if (label.empty() || label.size() > 256) return false;
                g->name = label;
                ok = true;
                break;
            case kVectorTreeGroupValues: {
                // O transform do PRÓPRIO grupo do caminho (os mesmos campos da linha de grupo).
                if (count < 10) return false;
                for (u32 i = 0; i < count; ++i) if (!std::isfinite(values[i])) return false;
                g->position = Vec2{values[0], values[1]};
                g->anchor = Vec2{values[2], values[3]};
                g->scale = Vec2{values[4], values[5]};
                g->rotation = values[6];
                g->opacity = std::clamp(values[7], 0.0f, 100.0f);
                g->skew = std::clamp(values[8], -85.0f, 85.0f);
                g->skewAxis = values[9];
                ok = true;
                break;
            }
            case kVectorTreeFreeOrder:
                // Passa o grupo para a ordem livre sem mudar o desenho (a = 0 volta
                // para a ordem de sempre, se o grupo não tiver nada que dependa dela).
                if (a != 0) {
                    vector::materialize_order(*g);
                    ok = true;
                } else if (g->children.empty() && g->moreFills.empty() && g->moreStrokes.empty() && g->moreTrims.empty()
                           && g->moreRepeaters.empty() && g->moreMerges.empty()) {
                    g->freeOrder = false;
                    g->order.clear();
                    ok = true;
                }
                break;
            default: return false;
        }
    }
    if (!ok) return false;
    if (next == l->shape.vector) return true;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "conteúdo da forma");
    bump_model_revision_locked();
    l->shape.vector = std::move(next);
    if (depth == 1 && path) remap_tracks(*l, path[0], remap);
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
