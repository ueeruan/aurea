// =============================================================================
//  Aurea / engine / EngineBgRemove.cpp
//
//  "Remover fundo" (0.4.5): a ação da camada de vídeo/imagem, a análise do
//  clipe com progresso/cancelamento e o arquivo dos recortes ao lado do
//  projeto (`<projeto>.bgmask`). O cálculo é do ai::BgRemoveService (worker
//  do renderer); as regras de chave/quadro são as mesmas do render.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/ai/BackgroundRemoval.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/render/Renderer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

EffectInstance* bg_instance(Composition* comp, u64 layerId, u32 effectId, Layer*& layer) noexcept {
    layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || (layer->kind != LayerKind::Image && layer->kind != LayerKind::Video)) return nullptr;
    EffectInstance* e = layer->find_effect(EffectId{effectId, 0});
    if (!e || e->type != effect_type_id(effect_keys::kBackgroundRemove)) return nullptr;
    return e;
}

/// O quadro da FONTE que a camada mostra no instante (a conta do render).
i64 bg_source_frame(const Project& project, const Composition& comp, const Layer& l, FrameIndex t) noexcept {
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

std::string bg_sidecar(const std::string& projectPath) { return projectPath.empty() ? std::string{} : projectPath + ".bgmask"; }

} // namespace

bool Engine::bg_remove_native_available() noexcept {
    return ai::bg_native_available(ai::kBgModelPerson);
}

void Engine::bg_remove_project_opened_locked() noexcept try {
    if (!bgStore_) bgStore_ = ai::make_bg_matte_store();
    const std::string sidecar = bg_sidecar(project_ ? project_->path() : std::string{});
    if (sidecar.empty() && bgStore_->path().empty()) {
        // Projeto ainda sem arquivo (motor reiniciado, projeto novo): o que a
        // prévia já calculou fica (a chave é o conteúdo da mídia; salvar poda).
        renderer_.set_bg_matte_store(bgStore_);
        return;
    }
    // O que a prévia calculou do projeto anterior (sem "Salvar") vai para o
    // arquivo dele antes de trocar.
    (void)bgStore_->flush();
    // Outro projeto: os recortes do anterior saem; os deste são lidos do
    // arquivo quando o primeiro Remover fundo pedir (projeto sem o efeito
    // nunca lê nada).
    bgStore_->attach(sidecar);
    renderer_.set_bg_matte_store(bgStore_);
} catch (...) {
}

void Engine::bg_remove_project_saved(const std::string& path) noexcept try {
    if (!bgStore_ || path.empty()) return;
    std::vector<u64> keys;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_) return;
        Renderer::bg_project_keys(*project_, keys);
    }
    // Só a mídia que ainda está no projeto; nada sujo = nada gravado.
    (void)bgStore_->flush_to(bg_sidecar(path), &keys);
} catch (...) {
}

i32 Engine::bg_remove_apply(u64 layerId) noexcept try {
    // Ordem dos locks do render (render → modelo): o serviço é do renderer.
    std::lock_guard<std::mutex> rl(renderMutex_);
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || (l->kind != LayerKind::Image && l->kind != LayerKind::Video)) return -1;
    const EffectTypeId type = effect_type_id(effect_keys::kBackgroundRemove);
    const ParameterRegistry* params = effectRegistry_.params(type);
    if (!params) return -1;
    EffectInstance* e = nullptr;
    for (EffectInstance& x : l->effects) if (x.type == type) { e = &x; break; }
    if (!e) {
        if (l->effects.size() >= kMaxEffectCount) return -1;
        history_.before_mutation(*comp, project_->timeline().current(), "remover fundo");
        bump_model_revision_locked();
        l->modelEffects = true;
        EffectInstance fresh;
        fresh.id = l->alloc_effect_id();
        fresh.type = type;
        initialize_instance(fresh, *params);
        l->effects.push_back(std::move(fresh));
        e = &l->effects.back();
        project_->mark_dirty();
    } else if (!e->enabled) {
        history_.before_mutation(*comp, project_->timeline().current(), "remover fundo");
        bump_model_revision_locked();
        e->enabled = true;
        project_->mark_dirty();
    }
    const u32 id = e->id;
    if (l->kind == LayerKind::Video) (void)renderer_.bg_analyze(*project_, *comp, *l, *e, &media_);
    request_render();
    return static_cast<i32>(id);
} catch (...) {
    return -1;
}

bool Engine::bg_remove_analyze(u64 layerId, u32 effectId) noexcept try {
    std::lock_guard<std::mutex> rl(renderMutex_);
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = nullptr;
    const EffectInstance* e = bg_instance(comp, layerId, effectId, l);
    if (!e) return false;
    const bool ok = renderer_.bg_analyze(*project_, *comp, *l, *e, &media_);
    request_render();
    return ok;
} catch (...) {
    return false;
}

void Engine::bg_remove_cancel() noexcept {
    // O serviço vive sob o renderMutex_ (criado/solto no render).
    std::lock_guard<std::mutex> rl(renderMutex_);
    if (auto* bg = renderer_.bg_service()) bg->cancel();
    request_render();
}

bool Engine::bg_remove_status(u64 layerId, u32 effectId, i64* out8) noexcept try {
    if (!out8) return false;
    for (u32 i = 0; i < 8; ++i) out8[i] = 0;
    // A UI pergunta a cada 250 ms: nunca espera um quadro em render. Sem o
    // lock do render agora, o último estado conhecido segue valendo na tela.
    std::unique_lock<std::mutex> rl(renderMutex_, std::try_to_lock);
    if (!rl.owns_lock()) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = nullptr;
    const EffectInstance* e = bg_instance(comp, layerId, effectId, l);
    if (!e) return false;
    const i64 frame = bg_source_frame(*project_, *comp, *l, playback_.current());
    return renderer_.bg_status(*project_, *comp, *l, *e, frame, out8);
} catch (...) {
    return false;
}

} // namespace aurea
