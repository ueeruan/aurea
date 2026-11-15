// =============================================================================
//  Aurea / engine / EngineAeAssist.cpp
//
//  Lote X do AE (2026-11-12, gap_system itens 15 e 18): os assistentes que
//  CRIAM camadas a partir de outra coisa. O AE é só a especificação de
//  comportamento; as interfaces só chamam, a regra mora aqui.
//
//  - Convert Audio to Keyframes (`audio_to_keyframes`): o som da composição
//    (ou de uma camada) vira um nulo com três Controles deslizantes (canal
//    esquerdo, canal direito e os dois) com um keyframe por quadro.
//
//  Diferenças do AE, de propósito:
//  - o valor é o RMS do quadro × 100 (0 = silêncio, 100 = onda quadrada no
//    teto; uma senoide no teto dá 70,7). O AE não publica a conta dele.
//  - os efeitos do Aurea não têm nome por instância: os três controles saem
//    na ordem do AE (esquerdo, direito, ambos) e aparecem como "Controle
//    deslizante", "Controle deslizante 2" e "Controle deslizante 3".
//  - `layerId` != 0 converte só o som daquela camada (no AE é sempre o da
//    composição).
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/export/OfflineAudioBlockSource.hpp"
#include "aurea/export/OfflineAudioSnapshot.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace aurea {
namespace {

/// Teto de quadros convertidos de uma vez (uma hora a 30 fps): três trilhas
/// com um keyframe por quadro, mais a cópia do desfazer.
constexpr i64 kMaxAudioKeyFrames = 108000;

/// "Nome", "Nome 2", "Nome 3"...: o menor número livre na composição.
std::string free_layer_name(const Composition& comp, const std::string& base) {
    for (u32 n = 1; n < 100000; ++n) {
        const std::string candidate = n == 1 ? base : base + " " + std::to_string(n);
        bool used = false;
        for (u32 i = 0; i < comp.order().size() && !used; ++i) {
            const Layer* l = comp.layer(comp.order().at(i));
            used = l && l->name == candidate;
        }
        if (!used) return candidate;
    }
    return base;
}

/// Amostra (48 kHz) em que o quadro `frame` da composição começa.
i64 frame_sample(i64 frame, f64 fps) noexcept {
    return static_cast<i64>(std::llround(static_cast<long double>(frame) * audio::kMixRate / fps));
}

} // namespace

Result<u64> Engine::audio_to_keyframes(u64 layerId, const char* name) noexcept {
    VideoSourceFactory* factory = config_.mediaFactory;
    if (!factory) return Status{Errc::InvalidState, "sem decodificador"};
    const EffectTypeId sliderType = effect_type_id(effect_keys::kSliderControl);
    const ParameterRegistry* sliderParams = effectRegistry_.params(sliderType);
    if (!sliderParams) sliderParams = expr::builtin_effects().params(sliderType);
    if (!sliderParams) return Status{Errc::InvalidState, "controle deslizante indisponivel"};

    // 1 · Sob o lock: o intervalo e o retrato do som (o retrato não guarda
    // ponteiro do projeto; a mixagem roda fora do lock).
    audio::AudioBlockCache blocks(factory, 16ull << 20, false);
    std::unique_ptr<OfflineAudioSnapshot> graph;
    std::shared_ptr<audio::AudioMixSnapshot> single;
    u64 session = 0;
    u32 revision = 0;
    CompositionId composition{};
    f64 fps = 30.0;
    i64 first = 0, last = 0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        InlineEditScope inlineScope(*this, layerId, true);
        const Composition* comp = project_ ? current_composition() : nullptr;
        if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
        fps = comp->fps();
        if (!std::isfinite(fps) || fps <= 0.0) return Status{Errc::InvalidArgument, "taxa de quadros invalida"};
        first = comp->work_in();
        last = comp->work_out();
        if (layerId != 0) {
            const Layer* layer = comp->layer(LayerId::unpack(layerId));
            if (!layer) return Status{Errc::InvalidArgument, "camada inexistente"};
            // Só o trecho em que a camada existe, dentro da área de trabalho.
            first = std::max(first, layer->start.value);
            last = std::min(last, layer->end.value);
        }
        if (last <= first) return Status{Errc::InvalidArgument, "trecho vazio"};
        if (last - first > kMaxAudioKeyFrames)
            return Status{Errc::OutOfMemory, "trecho grande demais para converter o audio em keyframes; reduza a area de trabalho"};
        if (layerId != 0) {
            single = audio::build_layer_snapshot(*comp, *project_, layerId, &blocks, &Engine::audio_path_resolver, this, &effectRegistry_);
            if (!single) return Status{Errc::OutOfMemory, "retrato do audio"};
            if (!single->audible()) return Status{Errc::InvalidArgument, "camada sem som"};
        } else {
            auto made = OfflineAudioSnapshot::create(*comp, *project_, &blocks, &Engine::audio_path_resolver, this);
            if (!made.ok()) return made.status();
            graph = std::move(*made);
            if (!graph->snapshot() || !graph->snapshot()->audible())
                return Status{Errc::InvalidArgument, "composicao sem som"};
        }
        session = projectSession_;
        composition = project_->timeline().current();
        revision = modelRevision_.load(std::memory_order_acquire);
    }
    const auto stale = [&] { return modelRevision_.load(std::memory_order_acquire) != revision; };

    // 2 · Fora do lock: mixa o trecho em pedaços e soma a energia de cada quadro.
    const usize frames = static_cast<usize>(last - first);
    std::vector<f32> left(frames, 0.0f), right(frames, 0.0f), both(frames, 0.0f);
    {
        std::atomic<bool> cancel{false};
        OfflineAudioBlockSource source(blocks, cancel);
        audio::MixState state;
        constexpr u32 kChunk = 4096;
        std::vector<f32> pcm(static_cast<usize>(kChunk) * audio::kMixChannels);
        const i64 s0 = frame_sample(first, fps), s1 = frame_sample(last, fps);
        usize frame = 0;
        i64 frameEnd = frame_sample(first + 1, fps);
        long double sumL = 0.0L, sumR = 0.0L;
        i64 count = 0;
        const auto close_frame = [&] {
            if (frame < frames && count > 0) {
                const long double n = static_cast<long double>(count);
                left[frame] = static_cast<f32>(std::sqrt(sumL / n) * 100.0L);
                right[frame] = static_cast<f32>(std::sqrt(sumR / n) * 100.0L);
                both[frame] = static_cast<f32>(std::sqrt((sumL + sumR) / (2.0L * n)) * 100.0L);
            }
            sumL = sumR = 0.0L;
            count = 0;
        };
        for (i64 at = s0; at < s1;) {
            if (stale()) return Status{Errc::Cancelled, "projeto alterado durante a conversao do audio"};
            const u32 n = static_cast<u32>(std::min<i64>(kChunk, s1 - at));
            source.begin_chunk();
            if (graph) {
                const Status mixed = graph->mix(at, n, source, pcm.data(), nullptr, &state);
                if (!source.status().ok()) return source.status();
                if (!mixed.ok()) return mixed;
            } else {
                audio::MixStats stats;
                audio::mix(*single, at, n, source, pcm.data(), &stats, &state);
                if (!source.status().ok()) return source.status();
                if (stats.missingBlocks) return Status{Errc::IoError, "audio ilegivel"};
            }
            for (u32 i = 0; i < n; ++i) {
                while (at + i >= frameEnd && frame < frames) {
                    close_frame();
                    ++frame;
                    frameEnd = frame_sample(first + static_cast<i64>(frame) + 1, fps);
                }
                const f32 l = pcm[static_cast<usize>(i) * 2], r = pcm[static_cast<usize>(i) * 2 + 1];
                if (std::isfinite(l)) sumL += static_cast<long double>(l) * l;
                if (std::isfinite(r)) sumR += static_cast<long double>(r) * r;
                ++count;
            }
            at += n;
        }
        close_frame();
    } // O PCM e o decodificador saem antes da cópia do desfazer.

    // 3 · De volta ao lock: o nulo, os três controles e as trilhas. Um desfazer.
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || session != projectSession_ || project_->timeline().current() != composition || stale())
        return Status{Errc::Cancelled, "projeto alterado durante a conversao do audio"};
    history_.before_mutation(*comp, composition, "converter áudio em keyframes");
    bump_model_revision_locked();
    const std::string base = name && *name ? std::string(name) : std::string("Amplitude de áudio");
    const LayerId lid = comp->add_layer(LayerKind::Null, free_layer_name(*comp, base));
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    l->start = FrameIndex{first};
    l->end = FrameIndex{last};
    l->transform.anchor = Vec3{50.0f, 50.0f, 0.0f};
    l->transform.position = Vec3{static_cast<f32>(comp->width()) * 0.5f, static_cast<f32>(comp->height()) * 0.5f, 0.0f};
    const std::vector<f32>* channels[3] = {&left, &right, &both};
    for (const std::vector<f32>* values : channels) {
        EffectInstance fx;
        fx.id = l->alloc_effect_id();
        fx.type = sliderType;
        initialize_instance(fx, *sliderParams);
        fx.expanded = false;
        if (!fx.params.empty() && !values->empty()) fx.params[0].constant.v[0] = values->front();
        const u32 fxId = fx.id;
        l->effects.push_back(std::move(fx));
        Track& track = l->tracks.get_or_create(TrackProperty::EffectParam, fxId, param_track_key(0, 0));
        track.staticValue = values->empty() ? 0.0f : values->front();
        track.keys.clear();
        track.keys.reserve(values->size());
        for (usize i = 0; i < values->size(); ++i) {
            Keyframe k;
            k.time = FrameIndex{static_cast<i64>(i)};   // tempo da camada: 0 = primeiro quadro dela
            k.value = (*values)[i];
            k.interp = Interpolation::Linear;
            track.keys.push_back(k);
        }
    }
    comp->touch();
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

} // namespace aurea
