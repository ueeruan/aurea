// =============================================================================
//  Aurea / engine / EngineProxy.cpp
//
//  "Otimizar vídeos pesados" (0.2.5): o proxy de edição visto pela UI. A
//  política, a fila e o cache em disco moram no PreviewProxyService
//  (media/PreviewProxy.hpp); aqui a camada vira asset e o projeto entra na fila.
//
//  O export nunca passa por aqui: qualidade final abre o original
//  (MediaManager::source_for com finalQuality), com ou sem proxy pronto.
// =============================================================================
#include "aurea/Engine.hpp"

#include <algorithm>

namespace aurea {

void Engine::queue_project_proxies_locked() noexcept {
    if (!project_) return;
    // Asset sem camada (só no desfazer) não ocupa a fila nem o disco.
    const auto unused = project_->unreferenced_assets();
    project_->for_each_asset([&](AssetId id, const Asset& a) {
        if (a.kind != AssetKind::Video || std::find(unused.begin(), unused.end(), id) != unused.end()) return;
        (void)media_.proxies().request(a);
    });
}

void Engine::set_proxy_mode(u32 mode) noexcept {
    media_.proxies().set_mode(static_cast<ProxyMode>(std::min<u32>(mode, static_cast<u32>(ProxyMode::Off))));
    std::lock_guard<std::mutex> lock(modelMutex_);
    queue_project_proxies_locked();
    // A camada visível troca de fonte (proxy ↔ original) no próximo quadro.
    request_render();
}

u32 Engine::proxy_mode() const noexcept {
    return static_cast<u32>(media_.proxies().mode());
}

ProxyStatus Engine::proxy_status(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const Asset* a = l && l->kind == LayerKind::Video ? project_->asset(l->source) : nullptr;
    return a ? media_.proxies().status(*a) : ProxyStatus{};
}

ProxySummary Engine::proxy_summary() noexcept {
    return media_.proxies().summary();
}

bool Engine::regenerate_proxy(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const Asset* a = l && l->kind == LayerKind::Video ? project_->asset(l->source) : nullptr;
    if (!a || !media_.proxies().regenerate(*a)) return false;
    request_render();
    return true;
}

bool Engine::cancel_proxy(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const Asset* a = l && l->kind == LayerKind::Video ? project_->asset(l->source) : nullptr;
    return a && media_.proxies().cancel(*a);
}

u64 Engine::proxy_cache_bytes() noexcept {
    return media_.proxies().cache_bytes();
}

u64 Engine::clear_proxy_cache() noexcept {
    const u64 freed = media_.proxies().clear_cache();
    // Sem o arquivo, o preview volta ao original; o que a política pede entra
    // na fila de novo quando a camada for desenhada.
    request_render();
    return freed;
}

} // namespace aurea
