// =============================================================================
//  Aurea / engine / EngineOmegaLayer.cpp
//
//  Omega 0.3.2, área 04 (painel da camada): as regras que o painel de 200 dp
//  da referência pede e o motor ainda não tinha. Android e iOS só chamam; a
//  conta mora aqui, igual para as duas plataformas.
//
//  P0-3 · fileira de tempo contextual: trio (cabeçote dentro) ou par
//         Mover/Expandir (cabeçote fora), e mover a camada para TERMINAR no
//         cabeçote.
//  P1-7 · velocidade em todos os tipos: os botões de retempo (esticar ou
//         encurtar uma ponta até o cabeçote mudando a velocidade) e a
//         velocidade constante nas camadas sem fonte (forma, texto, nulo…), que
//         reescala no tempo a animação da própria camada.
//  P0-2 · o que o botão Áudio mostra (`hasAnyAudio` da referência: todo vídeo,
//         todo áudio e o grupo com som dentro).
//  P2-16 · mídia da camada ausente no aparelho.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <vector>

namespace aurea {

/// Estado do painel entre chamadas: a camada "de partida" de um arrasto de
/// velocidade. Cada passo do arrasto reescala a partir DELA (e não do passo
/// anterior), então mil eventos de toque não acumulam arredondamento nem
/// fundem keyframes vizinhos — como a referência, que grava a partir do estado
/// do início do arrasto.
struct Engine::OmegaLayerState {
    u64 speedLayer = 0;          ///< camada (empacotada) da partida guardada
    u32 speedRevision = 0;       ///< modelRevision_ logo depois do último passo
    const Composition* speedComp = nullptr;
    Layer speedBase{};           ///< a camada como estava antes do 1º passo
};

void Engine::OmegaLayerStateDeleter::operator()(OmegaLayerState* s) const noexcept { delete s; }

namespace {

constexpr f32 kMinSpeed = 0.05f;
constexpr f32 kMaxSpeed = 16.0f;
constexpr i64 kFrameLimit = i64{1} << 31;

/// Tempo LOCAL reescalado em volta do ponto de entrada (`anchor`).
i64 rescale_time(i64 t, i64 anchor, f64 scale) noexcept {
    return anchor + static_cast<i64>(std::llround(static_cast<f64>(t - anchor) * scale));
}

/// Chaves no tempo novo; duas que caem no mesmo quadro viram uma (fica a última).
void rescale_track(Track& t, i64 anchor, f64 scale) {
    std::vector<Keyframe> out;
    out.reserve(t.keys.size());
    for (Keyframe k : t.keys) {
        k.time = FrameIndex{rescale_time(k.time.value, anchor, scale)};
        if (!out.empty() && out.back().time == k.time) out.back() = k;
        else out.push_back(k);
    }
    t.keys = std::move(out);
    t.lastIndex = 0;
}

template <typename Keys>
void rescale_frames(Keys& keys, i64 anchor, f64 scale) {
    for (auto& key : keys) key.frame = rescale_time(key.frame, anchor, scale);
    keys.erase(std::unique(keys.begin(), keys.end(),
                           [](const auto& a, const auto& b) { return a.frame == b.frame; }), keys.end());
}

/// Toda a animação da camada no tempo local × `scale` (a partir da entrada).
void rescale_layer_animation(Layer& l, f64 scale) {
    const i64 anchor = l.offset.value;
    for (u32 i = 0; i < l.tracks.size(); ++i) rescale_track(l.tracks.at(i), anchor, scale);
    for (auto& legacy : l.timeRemapLegacyTracks) rescale_track(legacy, anchor, scale);
    for (auto& mask : l.masks) { rescale_frames(mask.pathKeys, anchor, scale); mask.cacheKey = 0; }
    for (auto& group : l.shape.vector.groups) for (auto& path : group.paths) rescale_frames(path.keys, anchor, scale);
    for (auto& effect : l.effects) for (auto& mesh : effect.meshes) rescale_frames(mesh.keys, anchor, scale);
}

/// `hasAnyAudio` da referência: o botão Áudio vale para TODO vídeo (sem olhar a
/// faixa de som), todo áudio e o grupo com algum desses dentro. Imagem e o
/// resto, não.
bool layer_has_any_audio(const Project& project, const Layer& l, int depth) noexcept {
    switch (l.kind) {
        case LayerKind::Video:
        case LayerKind::Audio:
            return true;
        case LayerKind::Composition: {
            if (depth > 8) return false;
            const Composition* child = project.timeline().composition(l.nested.composition);
            if (!child) return false;
            bool found = false;
            for (u32 i = 0; i < child->order().size() && !found; ++i) {
                const Layer* item = child->layer(child->order().at(i));
                found = item && layer_has_any_audio(project, *item, depth + 1);
            }
            return found;
        }
        default:
            return false;
    }
}

} // namespace

Engine::OmegaLayerState& Engine::omega_layer_state() noexcept {
    if (!omegaLayer_) omegaLayer_.reset(new OmegaLayerState());
    return *omegaLayer_;
}

u32 Engine::query_layer_panel(u64 layerId, i64 frame) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || frame < 0 || frame >= kFrameLimit) return 0;
    const i64 s = layer->start.value, e = layer->end.value;
    u32 bits = 0;
    if (frame < s) {
        bits |= kLayerPanelExtendStart | kLayerPanelMoveStart | kLayerPanelRetimeExtendStart;
    } else if (frame > e) {
        bits |= kLayerPanelExtendEnd | kLayerPanelMoveEnd | kLayerPanelRetimeExtendEnd;
    } else {
        // Nas duas bordas o trio continua (como `query_clip_time_actions`); o
        // retempo de dentro só vale com o cabeçote estritamente dentro.
        bits |= kLayerPanelTrimStart | kLayerPanelSplit | kLayerPanelTrimEnd;
        if (frame > s && frame < e) bits |= kLayerPanelRetimeShortenStart | kLayerPanelRetimeShortenEnd;
    }
    if (layer->locked) {
        bits |= kLayerPanelLocked;
        bits &= ~(kLayerPanelMoveStart | kLayerPanelMoveEnd | kLayerPanelRetimeExtendStart |
                  kLayerPanelRetimeShortenStart | kLayerPanelRetimeShortenEnd | kLayerPanelRetimeExtendEnd);
    }
    if (layer_has_any_audio(*project_, *layer, 0)) bits |= kLayerPanelHasAudio;
    if (const Track* gain = layer->tracks.find(TrackProperty::AudioVolume); gain && gain->keys.size() > 1)
        bits |= kLayerPanelGainKeys;
    // A velocidade constante "reescalando a animação" vale sem curva de tempo
    // e sem velocidade animada (essas têm o painel de velocidade completo).
    const bool keyedTime = (layer->timeRemapEnabled && !layer->timeRemap.keys.empty()) || layer->speed_track() != nullptr;
    if (!keyedTime) bits |= kLayerPanelSpeedBakeable;
    // Mídia ausente: o arquivo da fonte não está mais onde o projeto aponta.
    // URI de conteúdo (sem caminho de arquivo) não dá para conferir: fica.
    if (layer->kind == LayerKind::Video || layer->kind == LayerKind::Image || layer->kind == LayerKind::Audio) {
        const Asset* asset = project_->asset(layer->source);
        if (!asset) {
            bits |= kLayerPanelMissingMedia;
        } else if (!asset->sourcePath.empty() && asset->sourcePath.find("://") == std::string::npos) {
            std::error_code ec;
            const std::string resolved = resolve_asset_path(asset->sourcePath);
            if (!resolved.empty() && !std::filesystem::exists(std::filesystem::u8path(resolved), ec) && !ec)
                bits |= kLayerPanelMissingMedia;
        }
    }
    return bits;
}

bool Engine::move_layer_to_playhead(u64 layerId, i64 frame, bool alignEnd) noexcept {
    if (frame < 0 || frame >= kFrameLimit) return false;
    i64 target = frame;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        const Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!layer || layer->locked) return false;
        const i64 length = layer->end.value - layer->start.value;
        if (length <= 0) return false;
        // Terminar no cabeçote = o fim (exclusivo) fica no cabeçote, a mesma
        // conta do "Fins no cabeçote" e do aparar fim do AUREA.
        if (alignEnd) target = std::max<i64>(0, frame - length);
        if (target == layer->start.value) return false;
    }
    // Mover preserva duração, fonte e keyframes; o histórico é o da op 6.
    return edit_clip_time(layerId, 6, target);
}

bool Engine::retime_layer_to_playhead(u64 layerId, u32 edge, i64 frame) noexcept {
    if (edge > 3 || frame < 0 || frame >= kFrameLimit) return false;
    i64 ns = 0, ne = 0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        const Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
        if (!layer || layer->locked) return false;
        const i64 s = layer->start.value, e = layer->end.value;
        ns = s; ne = e;
        switch (edge) {
            case 0: if (frame >= s) return false; ns = frame; break;                    // esticar o início
            case 1: if (frame <= s || frame >= e) return false; ns = frame; break;      // encurtar o início
            case 2: if (frame <= s || frame >= e) return false; ne = frame; break;      // encurtar o fim
            default: if (frame <= e) return false; ne = frame; break;                   // esticar o fim
        }
    }
    // A mesma conta das alças de velocidade da timeline (área 03): o trecho da
    // fonte fica, a velocidade vira antiga × duração antiga / nova.
    return stretch_layer_span(layerId, ns, ne);
}

bool Engine::set_layer_speed_baked(u64 layerId, f32 speed) noexcept {
    if (!std::isfinite(speed)) return false;
    const f32 s = std::clamp(speed, kMinSpeed, kMaxSpeed);
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->locked) return false;
    if ((layer->timeRemapEnabled && !layer->timeRemap.keys.empty()) || layer->speed_track()) return false;

    OmegaLayerState& st = omega_layer_state();
    const u32 revision = modelRevision_.load(std::memory_order_acquire);
    // Mesmo arrasto = ninguém mexeu no modelo desde o último passo dele.
    if (st.speedLayer != layerId || st.speedComp != comp || st.speedRevision != revision) {
        st.speedBase = *layer;
        st.speedLayer = layerId;
        st.speedComp = comp;
    }
    const Layer& base = st.speedBase;
    const f32 baseSpeed = base.speed > 0.0f ? base.speed : 1.0f;
    const i64 d0 = base.end.value - base.start.value;
    if (d0 <= 0) return false;
    // A duração acompanha (2× = metade do tempo) e a animação da camada anda
    // junto: forma, texto e nulo passam a tocar `s` vezes mais rápido; vídeo e
    // áudio mantêm o mesmo trecho da fonte (velocidade × duração constante).
    const f64 scale = static_cast<f64>(baseSpeed) / static_cast<f64>(s);
    const i64 d1 = std::max<i64>(1, static_cast<i64>(std::llround(static_cast<f64>(d0) * scale)));
    if (base.start.value + d1 >= kFrameLimit) return false;
    if (d1 == layer->end.value - layer->start.value && std::fabs(layer->speed - s) < 1e-6f) return true;

    Layer next = base;
    // A escala real (a duração é inteira) para a fonte não escorregar.
    const f64 exact = static_cast<f64>(d1) / static_cast<f64>(d0);
    rescale_layer_animation(next, exact);
    rescale_track(next.timeRemap, next.offset.value, exact);
    next.speed = static_cast<f32>(std::clamp(static_cast<f64>(baseSpeed) / exact, static_cast<f64>(kMinSpeed),
                                             static_cast<f64>(kMaxSpeed)));
    next.end = FrameIndex{next.start.value + d1};
    history_.before_mutation(*comp, project_->timeline().current(), "velocidade");
    *layer = std::move(next);
    i64 duration = comp->duration().value;
    comp->layers().for_each([&](LayerId, const Layer& item) { duration = std::max(duration, item.end.value); });
    if (duration != comp->duration().value) comp->set_duration(FrameIndex{duration});
    configure_playback_locked(comp);
    bump_model_revision_locked();
    st.speedRevision = modelRevision_.load(std::memory_order_acquire);
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
