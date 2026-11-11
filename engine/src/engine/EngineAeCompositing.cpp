// =============================================================================
//  Aurea / engine / EngineAeCompositing.cpp
//
//  AE compositing (2026-11-10, gap_system itens 9, 11 e 13): as chaves de
//  composição do After Effects que faltavam. O AE é só a especificação de
//  comportamento; Android e iOS só chamam, a regra mora aqui.
//
//  - Tímida (Shy) por camada e "Ocultar camadas tímidas" da composição: só
//    organização da timeline, o quadro não muda.
//  - Preservar transparência (o T do AE): a camada entra como source-atop
//    sobre a pilha já composta abaixo (Renderer::composite_draws).
//  - Queda do feather da máscara: suave (gaussiana, a de sempre) ou linear.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

namespace aurea {

bool Engine::set_layer_shy(u64 layerId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    if (l->shy == on) return true;
    history_.before_mutation(*comp, project_->timeline().current(), on ? "camada tímida" : "camada não tímida");
    bump_model_revision_locked();
    l->shy = on;
    project_->mark_dirty();
    return true;   // só organização: nenhum pixel muda
}

bool Engine::set_hide_shy(bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    if (comp->hide_shy() == on) return true;
    history_.before_mutation(*comp, project_->timeline().current(), on ? "ocultar camadas tímidas" : "mostrar camadas tímidas");
    bump_model_revision_locked();
    comp->set_hide_shy(on);
    project_->mark_dirty();
    return true;
}

bool Engine::hide_shy() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    return comp && comp->hide_shy();
}

bool Engine::set_layer_preserve_transparency(u64 layerId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    if (l->preserveTransparency == on) return true;
    if (l->locked) return false;
    history_.before_mutation(*comp, project_->timeline().current(),
                             on ? "preservar transparência" : "não preservar transparência");
    bump_model_revision_locked();
    l->preserveTransparency = on;
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_mask_falloff(u64 layerId, u32 maskId, u32 falloff) noexcept {
    if (falloff > 1) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return false;
    Mask* m = nullptr;
    for (Mask& candidate : l->masks) if (candidate.id == maskId) { m = &candidate; break; }
    if (!m) return false;
    if (m->featherFalloff == falloff) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "queda do feather");
    bump_model_revision_locked();
    m->featherFalloff = static_cast<u8>(falloff);
    m->cacheKey = 0;
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
