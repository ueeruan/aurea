// =============================================================================
//  Aurea / render / RendererAudio.cpp
//
//  EffectResources::audio_analysis do renderer: o som de UMA camada escolhida
//  (com os efeitos de áudio dela — o Tom de um sólido também), numa janela
//  em volta do quadro do `prepare` em curso, para a Forma de onda e o
//  Espectro de áudio.
//
//   - O som sai do MESMO mixer do preview/export (audio::mix) sobre um
//     snapshot só daquela camada; os blocos vêm do cache do mixer.
//   - Preview: nada bloqueia a thread de render. Bloco que falta é pedido ao
//     decodificador do cache e o quadro fica incompleto (o motor redesenha
//     quando chegar). Export: decodifica na hora — o quadro sai exato.
//   - O fluxo dos efeitos de áudio de cada instância segue de um quadro para
//     o seguinte (preview e export em estados separados).
// =============================================================================
#include "aurea/render/Renderer.hpp"

#include "aurea/audio/Audio.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/scene3d/Environment.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea {
namespace {

u64 mix64(u64 h, u64 v) noexcept {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 29);
}

u64 fbits(f32 v) noexcept { u32 b = 0; std::memcpy(&b, &v, sizeof(b)); return b; }

/// Blocos para a análise: o preview só pega o que já está decodificado (e
/// pede o resto); o export decodifica na hora.
class AnalysisBlocks final : public audio::BlockSource {
public:
    AnalysisBlocks(audio::AudioBlockCache& cache, bool final) : cache_(cache), final_(final) {}
    const audio::AudioBlock* block(u64 asset, i64 b) override {
        for (auto& h : held_) if (h.first == asset && h.second.first == b) return h.second.second.get();
        std::shared_ptr<const audio::AudioBlock> p = final_ ? cache_.fetch(asset, b) : cache_.find(asset, b);
        if (!p) {
            if (!final_) cache_.want(asset, b, 0);
            return nullptr;
        }
        held_.push_back({asset, {b, p}});
        return held_.back().second.second.get();
    }

private:
    audio::AudioBlockCache& cache_;
    bool final_;
    std::vector<std::pair<u64, std::pair<i64, std::shared_ptr<const audio::AudioBlock>>>> held_;
};

} // namespace

AudioAnalysisResult Renderer::audio_analysis(const AudioAnalysisRequest& request) noexcept {
    AudioAnalysisResult none;
    if (!backend_ || !planComp_ || !planProject_) return none;
    const Composition& comp = *planComp_;

    // A camada: a escolhida (se ainda existe nesta composição) ou a dona.
    u64 layerId = 0;
    if (request.layer && comp.layer(LayerId::unpack(request.layer))) layerId = request.layer;
    if (!layerId && request.host) {
        const OrderedIds<LayerId>& order = comp.order();
        for (u32 i = 0; i < order.size(); ++i) {
            if (comp.layer(order.at(i)) == request.host) { layerId = order.at(i).pack(); break; }
        }
    }
    if (!layerId) return none;

    // O cache de blocos: o do mixer do motor; sem ele (testes), um próprio.
    audio::AudioBlockCache* cache = sharedAudio_;
    if (!cache) {
        VideoSourceFactory* factory = planMedia_ ? planMedia_->factory() : nullptr;
        if (!ownAudio_ || ownAudioFactory_ != factory) {
            ownAudio_ = std::make_unique<audio::AudioBlockCache>(factory, 16ull << 20, false);
            ownAudioFactory_ = factory;
        }
        cache = ownAudio_.get();
    }
    auto snap = audio::build_layer_snapshot(comp, *planProject_, layerId, cache, audioResolve_, audioResolveCtx_);
    if (!snap || snap->clips.empty()) return none;

    // A janela: centrada no quadro, deslocada pelo "deslocamento do áudio".
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    const u32 count = std::clamp<u32>(request.count, 1u, 1024u);
    const u32 frames = static_cast<u32>(std::clamp(std::lround(std::clamp(request.durationMs, 1.0f, 2000.0f) * 48.0), 16l, 96000l));
    const i64 center = audio::frame_to_sample(planTime_.value, fps)
                     + static_cast<i64>(std::llround(static_cast<f64>(request.offsetMs) * 48.0));
    const i64 first = center - static_cast<i64>(frames / 2);

    // A textura é função do que toca naquela janela: chave = snapshot + janela
    // + pedido.
    u64 key = mix64(0xA0D10ull, layerId);
    for (const audio::AudioClip& c : snap->clips) {
        key = mix64(key, c.fxHash ? c.fxHash : mix64(mix64(c.asset, static_cast<u64>(c.start)),
                                                       mix64(static_cast<u64>(c.sourceAt0), fbits(c.gain * c.volume))));
        key = mix64(key, static_cast<u64>(c.end));
    }
    key = mix64(key, static_cast<u64>(first));
    key = mix64(key, (static_cast<u64>(frames) << 32) | (static_cast<u64>(count) << 1) | (request.spectrum ? 1u : 0u));
    key = mix64(key, (static_cast<u64>(request.channel) << 1) | (request.averaging ? 1u : 0u));
    key = mix64(key, (fbits(request.startHz) << 32) | fbits(request.endHz));
    if (auto it = spectra_.find(key); it != spectra_.end()) {
        it->second.lastFrame = frameNumber_;
        AudioAnalysisResult hit;
        hit.texture = it->second.texture;
        hit.peak = 0.0f;
        if (auto p = audioPeaks_.find(key); p != audioPeaks_.end()) hit.peak = p->second;
        return hit;
    }

    // O som da janela: o MESMO mix do preview/export (com os efeitos da camada).
    const u64 stateKey = mix64(mix64(layerId, request.instance ? request.instance->id : 0u), planFinal_ ? 1u : 0u);
    AudioVizState& vs = audioViz_[stateKey];
    if (!vs.mix) vs.mix = std::make_shared<audio::MixState>();
    vs.lastFrame = frameNumber_;
    if (audioViz_.size() > 32) {
        for (auto it = audioViz_.begin(); it != audioViz_.end();) {
            if (it->second.lastFrame + 120 < frameNumber_) it = audioViz_.erase(it);
            else ++it;
        }
    }
    std::vector<f32> pcm(static_cast<usize>(frames) * 2);
    // Síncrono no export e sem o cache do motor (testes, prévia avulsa).
    const bool sync = planFinal_ || !sharedAudio_;
    AnalysisBlocks blocks(*cache, sync);
    audio::MixStats stats;
    audio::mix(*snap, first, frames, blocks, pcm.data(), &stats, vs.mix.get());
    const bool complete = stats.missingBlocks == 0;
    if (!complete && !sync) incomplete_ = true;

    std::vector<f32> values(count, 0.0f);
    f32 peak = 0.0f;
    if (request.spectrum) {
        std::vector<f32> mono(frames);
        for (u32 i = 0; i < frames; ++i) mono[i] = 0.5f * (pcm[i * 2] + pcm[i * 2 + 1]);
        const u32 top = audio::analyze_linear_bands(mono.data(), frames, request.startHz, request.endHz, count,
                                                    request.averaging, values.data());
        peak = count > 1 ? (static_cast<f32>(top) + 0.5f) / static_cast<f32>(count) : 0.0f;
    } else {
        // Uma amostra exibida por trecho da janela: a de maior amplitude (com
        // o sinal) — a média de um trecho de onda daria zero.
        for (u32 m = 0; m < count; ++m) {
            const u64 a = static_cast<u64>(m) * frames / count;
            const u64 b = std::max<u64>(a + 1, static_cast<u64>(m + 1) * frames / count);
            f32 best = 0.0f;
            for (u64 i = a; i < b && i < frames; ++i) {
                const f32 l = pcm[i * 2], r = pcm[i * 2 + 1];
                const f32 v = request.channel == 1 ? l : request.channel == 2 ? r : 0.5f * (l + r);
                if (std::fabs(v) > std::fabs(best)) best = v;
            }
            values[m] = best;
        }
    }

    TextureDesc d;
    d.width = count;
    d.height = 1;
    d.format = SurfaceFormat::RGBA16F;
    d.sampled = true;
    d.transferDst = true;
    d.debugName = request.spectrum ? "espectro-bandas" : "forma-de-onda";
    auto tex = backend_->create_texture(d);
    if (!tex.ok()) return none;
    PendingUpload up;
    up.texture = *tex;
    up.bytesPerRow = count * 8;
    up.data.resize(static_cast<usize>(count) * 8);
    u16* px = reinterpret_cast<u16*>(up.data.data());
    for (u32 i = 0; i < count; ++i) {
        px[i * 4 + 0] = scene3d::float_to_half(std::clamp(values[i], -60000.0f, 60000.0f));
        px[i * 4 + 1] = scene3d::float_to_half(0.0f);
        px[i * 4 + 2] = scene3d::float_to_half(0.0f);
        px[i * 4 + 3] = scene3d::float_to_half(1.0f);
    }
    uploads_.push_back(std::move(up));
    // Janela incompleta (preview, bloco a caminho) não entra no cache: o
    // próximo quadro refaz com o som inteiro.
    if (complete) {
        spectra_[key] = LutTexture{*tex, frameNumber_};
        audioPeaks_[key] = peak;
        if (audioPeaks_.size() > 4096) audioPeaks_.clear();
    } else {
        // A textura ainda precisa viver até a GPU usar: guardada com uma
        // chave própria deste quadro, sai com os espectros velhos.
        spectra_[mix64(key, frameNumber_ ^ 0x1BADC0DEull)] = LutTexture{*tex, frameNumber_};
    }
    AudioAnalysisResult r;
    r.texture = *tex;
    r.peak = peak;
    return r;
}

} // namespace aurea
