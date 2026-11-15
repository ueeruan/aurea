// =============================================================================
//  Aurea / engine / EngineAeLayers.cpp
//
//  AE layers (2026-11-12, gap_system itens 29 e 30, B10): as chaves de camada
//  do After Effects que faltavam. O AE é só a especificação de comportamento;
//  Android, iOS e o PC só chamam, a regra mora aqui.
//
//  - Qualidade da camada (Best / Draft / Wireframe) e a amostragem
//    (bilinear / bicúbica). O Renderer lê `Layer::quality` só na prévia: o
//    export sai sempre em Melhor.
//  - Clipe de animação do modelo 3D (o arquivo pode trazer vários).
//  - Guias da prévia (grade, margens de ação/título, réguas): só a saída na
//    tela (shaders/composite/output.frag), nunca o quadro nem o export.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/scene3d/SceneAsset.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {

bool Engine::set_layer_quality(u64 layerId, u32 quality, u32 sampling) noexcept {
    if (quality > 2 || sampling > 1) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    if (l->quality == quality && l->sampling == sampling) return true;
    const char* label = quality != l->quality
        ? (quality == 0 ? "qualidade melhor" : quality == 1 ? "qualidade rascunho" : "qualidade arame")
        : (sampling == 1 ? "amostragem bicúbica" : "amostragem bilinear");
    history_.before_mutation(*comp, project_->timeline().current(), label);
    bump_model_revision_locked();
    l->quality = static_cast<u8>(quality);
    l->sampling = static_cast<u8>(sampling);
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_model_animation_clip(u64 layerId, i32 clip) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Model3D || l->locked) return false;
    if (clip < -1) return false;
    if (clip >= 0) {
        // Só um clipe que o arquivo tem (o modelo precisa estar carregado).
        const auto it = models_.find(l->model.scene.pack());
        if (it == models_.end() || !it->second || static_cast<usize>(clip) >= it->second->animations.size()) return false;
    }
    if (l->model.animationClip == clip) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "clipe de animação");
    bump_model_revision_locked();
    l->model.animationClip = clip;
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::model_animation_clip(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    return l && l->kind == LayerKind::Model3D ? l->model.animationClip : -1;
}

std::vector<std::string> Engine::model_animation_clips(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    std::vector<std::string> names;
    if (!l || l->kind != LayerKind::Model3D) return names;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second) return names;
    names.reserve(it->second->animations.size());
    for (const scene3d::Animation& a : it->second->animations) names.push_back(a.name);
    return names;
}

void Engine::set_preview_guides(u32 flags, f32 gridSpacing) noexcept {
    const u32 f = flags & 7u;
    const f32 spacing = std::isfinite(gridSpacing) && gridSpacing > 0.0f ? std::clamp(gridSpacing, 4.0f, 4096.0f) : 100.0f;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (previewGuides_ == f && previewGridSpacing_ == spacing) return;
        previewGuides_ = f;
        previewGridSpacing_ = spacing;
    }
    // Só a saída na tela muda: o quadro guardado (e o pré-render) continua valendo.
    request_render();
}

u32 Engine::preview_guides() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return previewGuides_;
}

} // namespace aurea
