#include "aurea/animation/Curve.hpp"

namespace aurea {

Track& TrackSet::get_or_create(TrackProperty p, u32 effectIndex,
                               u32 effectParamIndex) noexcept {
    if (Track* existing = find(p, effectIndex, effectParamIndex)) {
        return *existing;
    }
    Track t;
    t.property = p;
    t.effectIndex = effectIndex;
    t.effectParamIndex = effectParamIndex;
    tracks_.push_back(t);
    // `push_back` pode realocar; o índice é devolvido, não o ponteiro interior,
    // então quem chamou pega o elemento de novo por `at()`. Devolver a
    // referência aqui continua válido porque ela aponta para o vetor já
    // realocado.
    return tracks_.back();
}

Track* TrackSet::find(TrackProperty p, u32 effectIndex, u32 effectParamIndex) noexcept {
    for (auto& t : tracks_) {
        if (t.property == p && t.effectIndex == effectIndex
            && t.effectParamIndex == effectParamIndex) {
            return &t;
        }
    }
    return nullptr;
}

const Track* TrackSet::find(TrackProperty p, u32 effectIndex,
                            u32 effectParamIndex) const noexcept {
    for (const auto& t : tracks_) {
        if (t.property == p && t.effectIndex == effectIndex
            && t.effectParamIndex == effectParamIndex) {
            return &t;
        }
    }
    return nullptr;
}

f32 TrackSet::sample_or(TrackProperty p, FrameIndex t, f32 fallback,
                        u32 effectIndex, u32 effectParamIndex) const noexcept {
    const Track* track = find(p, effectIndex, effectParamIndex);
    if (!track) return fallback;
    return track->sample(t);
}

void TrackSet::set_static(TrackProperty p, f32 value, u32 effectIndex,
                          u32 effectParamIndex) noexcept {
    Track& t = get_or_create(p, effectIndex, effectParamIndex);
    if (t.keys.empty()) {
        // Valor estático: guardado em `staticValue`, sem keyframe nenhum. Uma
        // layer com 30 propriedades ajustadas e nenhuma animada não carrega 30
        // vetores de keyframe.
        t.staticValue = value;
    } else {
        // Já animada: mexer no valor move o keyframe do tempo atual, se houver
        // um. Sem keyframe no tempo atual, o valor estático não é usado (a
        // animação manda) — então não há o que alterar, e alterar seria mentir
        // sobre o que a UI mostra.
        t.staticValue = value;
    }
}

bool TrackSet::has_animation() const noexcept {
    for (const auto& t : tracks_) {
        if (t.animated()) return true;   // keyframes ou expressão ligada
    }
    return false;
}

} // namespace aurea
