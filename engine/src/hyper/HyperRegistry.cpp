// =============================================================================
//  Aurea / hyper / HyperRegistry.cpp — o registro das etapas Lite/Max.
//
//  A ampliação espacial usa o renderer. O registro seleciona as etapas
//  opcionais do grafo de processamento (limpeza, movimento e rostos).
// =============================================================================
#include "aurea/hyper/HyperStage.hpp"

#include "aurea/core/Log.hpp"

#include <mutex>

namespace aurea::hyper {

void StageRegistry::add(const StageDescriptor& descriptor) {
    if (!descriptor.create) return;
    entries_.push_back(descriptor);
}

const StageDescriptor* StageRegistry::find(StageKind kind, Path path, i32 mode,
                                           const DeviceProfile& profile) const noexcept {
    const StageDescriptor* best = nullptr;
    for (const StageDescriptor& d : entries_) {
        if (d.kind != kind || d.path != path) continue;
        if (d.mode >= 0 && mode >= 0 && d.mode != mode) continue;
        if (d.available && !d.available(profile)) continue;
        if (!best || d.priority > best->priority) best = &d;
    }
    return best;
}

#if !defined(AUREA_HYPER_HAS_NEURAL)
// Padrão até src/hyper/neural/NeuralStages.cpp existir (o CMake define a macro).
void register_neural_stages(StageRegistry&) {}
#endif

const StageRegistry& stage_registry() {
    static StageRegistry registry;
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            register_neural_stages(registry);
        } catch (...) {
            AUREA_LOG_ERROR("hyper: register_neural_stages lancou excecao; seguindo sem etapas opcionais");
        }
        AUREA_LOG_INFO("hyper: %u etapas Lite/Max registradas", static_cast<u32>(registry.all().size()));
    });
    return registry;
}

} // namespace aurea::hyper
