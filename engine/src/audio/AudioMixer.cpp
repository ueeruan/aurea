// =============================================================================
//  Aurea / audio / AudioMixer.cpp — snapshot da timeline e mixagem.
// =============================================================================
#include "aurea/audio/Audio.hpp"
#include "AudioInternal.hpp"

#include "aurea/expr/Expression.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/timeline/Composition.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace aurea::audio {
namespace {

constexpr f32 kHalfPi = 1.57079632679489661923f;

/// Duração da trilha de áudio do asset, em amostras a 48 kHz.
i64 asset_length(const Asset& a) {
    if (a.audio.sampleCount.value > 0 && a.audio.sampleRate > 0) {
        return a.audio.sampleCount.value * static_cast<i64>(kMixRate) / a.audio.sampleRate;
    }
    return frame_to_sample(a.duration.value, a.timebaseFps > 0.0 ? a.timebaseFps : 30.0);
}

bool has_sound(const Layer& l, const Project& project) {
    if (l.kind != LayerKind::Video && l.kind != LayerKind::Audio) return false;
    const Asset* a = project.asset(l.source);
    return a && a->has_audio();
}

/// Mistura de hash (fluxo dos efeitos: identidade e conteúdo do clipe).
u64 hmix(u64 h, u64 v) noexcept {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 29);
}
u64 hbits(f64 v) noexcept { u64 b = 0; std::memcpy(&b, &v, sizeof(b)); return b; }
u64 hbits(f32 v) noexcept { u32 b = 0; std::memcpy(&b, &v, sizeof(b)); return b; }

/// O tipo de estágio de um efeito de áudio da camada (false = não é de áudio).
bool fx_kind_of(EffectTypeId type, FxKind& kind) noexcept {
    struct Row { EffectTypeId id; FxKind kind; };
    static const Row rows[] = {
        {effect_type_id(fx_keys::kBackwards), FxKind::Backwards},
        {effect_type_id(fx_keys::kDelay), FxKind::Delay},
        {effect_type_id(fx_keys::kFlangeChorus), FxKind::FlangeChorus},
        {effect_type_id(fx_keys::kHighLowPass), FxKind::HighLowPass},
        {effect_type_id(fx_keys::kStereoMixer), FxKind::StereoMixer},
        {effect_type_id(fx_keys::kModulator), FxKind::Modulator},
        {effect_type_id(fx_keys::kParametricEq), FxKind::ParametricEq},
        {effect_type_id(fx_keys::kReverb), FxKind::Reverb},
        {effect_type_id(fx_keys::kTone), FxKind::Tone},
    };
    for (const Row& r : rows) {
        if (r.id == type) { kind = r.kind; return true; }
    }
    return false;
}

bool has_tone(const Layer& l) noexcept {
    const EffectTypeId tone = effect_type_id(fx_keys::kTone);
    for (const EffectInstance& e : l.effects) if (e.enabled && e.type == tone) return true;
    return false;
}

/// Os estágios dos efeitos de áudio de `l` (em ordem), com os parâmetros
/// resolvidos: constantes, ou um valor por quadro da camada quando animados.
/// `reverse` inverte a cada Reverso ligado.
void layer_stages(const Layer& l, f64 fps, i64 shift, std::vector<FxStage>& out, bool& reverse) {
    const EffectRegistry& registry = expr::builtin_effects();
    const i64 ls = frame_to_sample(l.start.value, fps);
    const i64 frames = std::max<i64>(1, l.end.value - l.start.value + 1);
    for (const EffectInstance& e : l.effects) {
        FxKind kind{};
        if (!e.enabled || !fx_kind_of(e.type, kind)) continue;
        const ParameterRegistry* reg = registry.params(e.type);
        if (!reg || e.params.size() < reg->count()) continue;
        FxStage s;
        s.kind = kind;
        s.envShift = shift;
        s.fps = fps;
        s.frame0 = l.start.value;
        s.origin = ls + shift;
        s.params.resize(reg->count());
        for (u32 i = 0; i < reg->count(); ++i) {
            const ParamSpec& spec = reg->at(i);
            FxParam& p = s.params[i];
            const Track* t = l.tracks.find(TrackProperty::EffectParam, e.id, param_track_key(i, 0));
            const bool animated = component_count(spec.type) > 0
                               && (e.params[i].source == ParamSource::Expression || (t && t->animated()));
            p.value = evaluate_param(l.tracks, e, i, spec, FrameIndex{l.offset.value}).v[0];
            if (!animated) continue;
            p.byFrame.resize(static_cast<usize>(frames));
            for (i64 f = 0; f < frames; ++f) {
                p.byFrame[static_cast<usize>(f)] = evaluate_param(l.tracks, e, i, spec, FrameIndex{l.offset.value + f}).v[0];
            }
        }
        // Frequências de oscilador: a fase é a integral (sem salto com keyframe).
        auto integrate = [&](u32 i) {
            FxParam& p = s.params[i];
            if (p.byFrame.empty()) return;
            p.integral.resize(p.byFrame.size());
            f64 acc = 0.0;
            for (usize k = 0; k < p.byFrame.size(); ++k) {
                p.integral[k] = acc;
                if (k + 1 < p.byFrame.size()) acc += 0.5 * (static_cast<f64>(p.byFrame[k]) + p.byFrame[k + 1]) / fps;
            }
        };
        auto top = [&](u32 i) {
            const FxParam& p = s.params[i];
            return p.byFrame.empty() ? p.value : *std::max_element(p.byFrame.begin(), p.byFrame.end());
        };
        auto bottom = [&](u32 i) {
            const FxParam& p = s.params[i];
            return p.byFrame.empty() ? p.value : *std::min_element(p.byFrame.begin(), p.byFrame.end());
        };
        switch (kind) {
            case FxKind::Backwards: reverse = !reverse; break;
            case FxKind::Delay: s.maxDelayMs = std::clamp(top(fxp::kDelayTime), 1.0f, 10000.0f); break;
            case FxKind::FlangeChorus:
                integrate(fxp::kFlangeRate);
                s.maxDelayMs = std::clamp(top(fxp::kVoiceSeparation), 0.0f, 1000.0f)
                             * (std::clamp(top(fxp::kVoices), 1.0f, 8.0f) + std::clamp(top(fxp::kFlangeDepth) / 100.0f, 0.0f, 1.0f)) + 1.0f;
                break;
            case FxKind::Modulator: {
                integrate(fxp::kModRate);
                const f32 rate = std::max(0.01f, bottom(fxp::kModRate));
                const f32 dev = std::clamp(top(fxp::kModDepth) / 100.0f, 0.0f, 1.0f);
                s.maxDelayMs = std::min(2000.0f, 1000.0f * dev / (2.0f * rate)) + 1.0f;
                break;
            }
            case FxKind::Reverb: s.maxDelayMs = std::clamp(top(fxp::kReverbTime), 1.0f, 2000.0f); break;
            case FxKind::Tone:
                for (u32 k = 0; k < 5; ++k) integrate(fxp::kFreq1 + k);
                break;
            default: break;
        }
        out.push_back(std::move(s));
    }
}

u64 stage_hash(u64 h, const FxStage& s) noexcept {
    h = hmix(h, static_cast<u64>(s.kind));
    h = hmix(h, static_cast<u64>(s.envShift));
    h = hmix(h, static_cast<u64>(s.frame0));
    h = hmix(h, hbits(s.fps));
    for (const FxParam& p : s.params) {
        h = hmix(h, hbits(p.value));
        for (f32 v : p.byFrame) h = hmix(h, hbits(v));
    }
    return h;
}

/// Tudo o que muda o que o fluxo do clipe produz.
u64 clip_hash(const AudioClip& c) noexcept {
    u64 h = hmix(0x41, c.asset);
    for (i64 v : {c.start, c.end, c.sourceAt0, c.sourceLength, c.envShift, c.fadeFrom, c.fadeTo, c.fadeIn, c.fadeOut,
                  c.volumeFrame0, c.srcFrame0, c.revFrom, c.revTo, c.preroll}) {
        h = hmix(h, static_cast<u64>(v));
    }
    h = hmix(h, hbits(c.rate));
    h = hmix(h, hbits(c.sourceStartF));
    h = hmix(h, hbits(c.gain));
    h = hmix(h, hbits(c.pan));
    h = hmix(h, hbits(c.volume));
    h = hmix(h, hbits(c.fps));
    h = hmix(h, c.reverse ? 1u : 0u);
    for (f32 v : c.volumeByFrame) h = hmix(h, hbits(v));
    for (f64 v : c.srcByFrame) h = hmix(h, hbits(v));
    for (const AudioSend& s : c.sends) {
        h = hmix(h, s.kind);
        h = hmix(h, hbits(s.wet));
        h = hmix(h, hbits(s.seconds));
        h = hmix(h, hbits(s.rate));
        h = hmix(h, hbits(s.decay));
    }
    for (const FxStage& s : c.chain) h = stage_hash(h, s);
    return h;
}

struct Flatten {
    const Project& project;
    AudioBlockCache* cache;
    AssetPathResolver resolve;
    void* ctx;
    std::vector<AudioClip>& out;
    /// Só esta camada (análise visual): ignora mudo e solo na raiz.
    u64 only = 0;

    /// Acrescenta os sons de `comp`. `shift`: amostra da raiz − amostra de
    /// `comp`. [lo, hi): janela visível na raiz (a pré-comp corta o que passa
    /// da borda da layer que a contém). `gain` acumulado das pré-comps.
    /// `inherited`: os efeitos de áudio das pré-comps que contêm `comp` (de
    /// dentro para fora); `path` identifica o caminho (fluxo dos efeitos).
    void add(const Composition& comp, i64 shift, i64 lo, i64 hi, f32 gain, u32 depth,
             const std::vector<FxStage>& inherited, u64 path) {
        if (depth > 16) return;
        const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
        const bool root = depth == 0 && only != 0;
        bool anySolo = false;
        comp.layers().for_each([&](LayerId, const Layer& l) {
            if (l.solo && (has_sound(l, project) || l.kind == LayerKind::Composition || has_tone(l))) anySolo = true;
        });
        comp.layers().for_each([&](LayerId id, const Layer& l) {
            if (root && id.pack() != only) return;
            if (!root && (l.muted || (anySolo && !l.solo))) return;
            const bool tone = has_tone(l);
            // Remapeamento por curva ainda não chega ao áudio; quadro
            // congelado não tem som. Um som fora de sincronia é pior que silêncio.
            // (O Tom é gerado: não depende do tempo da fonte.)
            if (!tone && (!l.timeRemapEnabled && l.speed <= 0.0f)) return;
            const i64 ls = frame_to_sample(l.start.value, fps);
            const i64 le = frame_to_sample(l.end.value, fps);
            if (le <= ls) return;
            const f32 volume = l.tracks.sample_or(TrackProperty::AudioVolume, FrameIndex{l.offset.value}, 1.0f);
            const u64 key = hmix(path, id.pack());
            std::vector<FxStage> own;
            bool reverse = false;
            layer_stages(l, fps, shift, own, reverse);
            if (l.kind == LayerKind::Composition && !tone) {
                const Composition* child = project.timeline().composition(l.nested.composition);
                if (!child || child == &comp) return;
                const f64 cfps = child->fps() > 0.0 ? child->fps() : 30.0;
                // Instante 0 da filha toca em ls − offset (offset na régua da filha).
                const i64 childShift = shift + ls - frame_to_sample(l.offset.value, cfps);
                // Os efeitos de áudio da pré-comp valem para o som de dentro:
                // entram DEPOIS dos de cada camada filha (o Reverso da pré-comp
                // não se aplica — cada filha tem a sua régua de fonte).
                std::vector<FxStage> chain;
                for (FxStage& s : own) if (s.kind != FxKind::Backwards) chain.push_back(std::move(s));
                chain.insert(chain.end(), inherited.begin(), inherited.end());
                add(*child, childShift, std::max(lo, ls + shift), std::min(hi, le + shift),
                    gain * l.gain * volume, depth + 1, chain, key);
                return;
            }
            if (!has_sound(l, project) && !tone) return;
            AudioClip c;
            const Asset* a = tone ? nullptr : project.asset(l.source);
            if (a && a->has_audio()) {
                c.asset = l.source.pack();
                if (cache) cache->register_asset(c.asset, AudioAssetRef{resolve ? resolve(ctx, a->sourcePath) : a->sourcePath,
                                                                        asset_length(*a)});
            }
            c.start = std::max(lo, ls + shift);
            c.end = std::min(hi, le + shift);
            if (c.end <= c.start) return;
            // Amostra da fonte em `ls` (início da layer) = offset do conteúdo.
            const i64 srcAtLs = frame_to_sample(l.offset.value, fps);
            c.sourceAt0 = srcAtLs + (c.start - shift - ls);
            if (c.asset && l.timeRemapEnabled && !l.timeRemap.keys.empty()) {
                // Curva de tempo: posição da fonte quadro a quadro (a MESMA
                // função do vídeo), interpolada amostra a amostra no mix.
                c.srcFrame0 = l.start.value;
                const i64 nf = l.end.value - l.start.value + 1;
                c.srcByFrame.resize(static_cast<usize>(nf));
                for (i64 i = 0; i < nf; ++i) {
                    c.srcByFrame[static_cast<usize>(i)] =
                        l.source_frame_f(static_cast<f64>(l.start.value + i)) * kMixRate / fps;
                }
                c.rate = 0.0;   // marca o caminho fracionário
            } else if (c.asset && (l.speed != 1.0f || l.reversed)) {
                c.rate = static_cast<f64>(l.speed) * (l.reversed ? -1.0 : 1.0);
                // A mesma função de tempo do vídeo (Layer::source_frame), em amostras.
                const f64 srcFrameAtLs = l.source_frame(l.start);
                c.sourceStartF = srcFrameAtLs * kMixRate / fps + static_cast<f64>(c.start - shift - ls) * c.rate;
            }
            for (const auto& effect:l.effects) {
                if(!effect.enabled || effect.params.size()<3 || c.sends.size()>=8) continue;
                u32 kind=3;
                if(effect.type==effect_type_id("aurea.audio.reverb")) kind=0;
                if(effect.type==effect_type_id("aurea.audio.flanger")) kind=1;
                if(effect.type==effect_type_id("aurea.audio.echo")) kind=2;
                if(kind==3) continue;
                auto f=[&](u32 i,f32 lo,f32 hi){f32 v=effect.params[i].constant.v[0]; return std::isfinite(v)?std::clamp(v,lo,hi):lo;};
                if (kind == 1 && effect.params.size() < 4) continue;
                c.sends.push_back(AudioSend{kind,f(0,0,100)/100,f(1,kind==1?.1f:10.f,kind==1?10.f:500.f)/1000,
                    kind == 1 ? f(2,.01f,5) : .3f, f(kind == 1 ? 3 : 2,0,90)/100});
            }
            c.sourceLength = a ? asset_length(*a) : 0;
            c.gain = gain * l.gain;
            c.pan = std::clamp(l.pan, -1.0f, 1.0f);
            c.envShift = shift;
            c.fadeFrom = ls;
            c.fadeTo = le;
            c.fadeIn = frame_to_sample(l.fadeIn.value, fps);
            c.fadeOut = frame_to_sample(l.fadeOut.value, fps);
            c.fps = fps;
            const Track* vt = l.tracks.find(TrackProperty::AudioVolume);
            if (vt && vt->animated()) {
                c.volumeFrame0 = l.start.value;
                const i64 n = l.end.value - l.start.value + 1;
                c.volumeByFrame.resize(static_cast<usize>(n));
                for (i64 i = 0; i < n; ++i) {
                    // Keyframes vivem no tempo LOCAL da layer (como os de transform).
                    c.volumeByFrame[static_cast<usize>(i)] = std::max(0.0f, vt->sample(FrameIndex{l.offset.value + i}));
                }
            } else {
                c.volume = std::max(0.0f, volume);
            }
            if (c.gain * (c.volumeByFrame.empty() ? c.volume : 1.0f) <= 0.0f) return;
            // Efeitos de áudio: os da camada, o envelope dela e os das
            // pré-comps que a contêm. Sem nenhum, o caminho de sempre.
            if (!own.empty() || !inherited.empty() || tone) {
                c.chain = std::move(own);
                FxStage env;
                env.kind = FxKind::Envelope;
                c.chain.push_back(std::move(env));
                c.chain.insert(c.chain.end(), inherited.begin(), inherited.end());
                c.reverse = reverse;
                c.revFrom = ls + shift;
                c.revTo = le + shift;
                c.preroll = chain_preroll(c.chain);
                c.streamKey = key;
                c.fxHash = clip_hash(c);
            }
            out.push_back(std::move(c));
        });
    }
};

f32 envelope(const AudioClip& c, i64 t) noexcept {
    const i64 own = t - c.envShift;
    f32 g = c.gain;
    if (c.volumeByFrame.empty()) {
        g *= c.volume;
    } else {
        const f64 f = static_cast<f64>(own) * c.fps / kMixRate - static_cast<f64>(c.volumeFrame0);
        const i64 n = static_cast<i64>(c.volumeByFrame.size());
        const i64 i = std::clamp<i64>(static_cast<i64>(std::floor(f)), 0, n - 1);
        const i64 j = std::min(i + 1, n - 1);
        const f32 a = static_cast<f32>(std::clamp(f - static_cast<f64>(i), 0.0, 1.0));
        g *= c.volumeByFrame[static_cast<usize>(i)] + (c.volumeByFrame[static_cast<usize>(j)] - c.volumeByFrame[static_cast<usize>(i)]) * a;
    }
    // Igual potência: seno de 0 a π/2. No meio do fade o nível é −3 dB, e dois
    // clipes cruzando somam potência constante (sem o "buraco" do fade linear).
    if (c.fadeIn > 0 && own < c.fadeFrom + c.fadeIn) {
        const f32 x = std::clamp(static_cast<f32>(own - c.fadeFrom) / static_cast<f32>(c.fadeIn), 0.0f, 1.0f);
        g *= std::sin(x * kHalfPi);
    }
    if (c.fadeOut > 0 && own >= c.fadeTo - c.fadeOut) {
        const f32 x = std::clamp(static_cast<f32>(c.fadeTo - own) / static_cast<f32>(c.fadeOut), 0.0f, 1.0f);
        g *= std::sin(x * kHalfPi);
    }
    return g;
}

/// Limitador suave acima de −1 dBFS: abaixo disso o sinal passa intacto (bit a
/// bit); acima, satura com tanh em vez de ceifar (sem o estalo do clip duro).
inline f32 soft_limit(f32 x) noexcept {
    constexpr f32 t = 0.891251f;
    const f32 ax = std::fabs(x);
    if (ax <= t) return x;
    const f32 y = t + (1.0f - t) * std::tanh((ax - t) / (1.0f - t));
    return x < 0.0f ? -y : y;
}

} // namespace

std::shared_ptr<AudioMixSnapshot> build_snapshot(const Composition& comp, const Project& project, AudioBlockCache* cache,
                                                 AssetPathResolver resolve, void* resolveCtx) {
    auto snap = std::make_shared<AudioMixSnapshot>();
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    snap->endSample = frame_to_sample(comp.duration().value, fps);
    Flatten f{project, cache, resolve, resolveCtx, snap->clips};
    // Volume com expressão: avaliado quadro a quadro aqui (sob o lock do modelo).
    const expr::Scope exprScope(project.timeline());
    f.add(comp, 0, 0, snap->endSample, 1.0f, 0, {}, 0);
    return snap;
}

std::shared_ptr<AudioMixSnapshot> build_layer_snapshot(const Composition& comp, const Project& project, u64 layerId,
                                                       AudioBlockCache* cache, AssetPathResolver resolve,
                                                       void* resolveCtx) {
    auto snap = std::make_shared<AudioMixSnapshot>();
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    snap->endSample = frame_to_sample(comp.duration().value, fps);
    if (layerId == 0) return snap;
    Flatten f{project, cache, resolve, resolveCtx, snap->clips};
    f.only = layerId;
    const expr::Scope exprScope(project.timeline());
    f.add(comp, 0, 0, snap->endSample, 1.0f, 0, {}, 0x5157);
    return snap;
}

namespace {
/// Amostra (fracionária) da fonte que toca na amostra `t` da timeline raiz.
f64 clip_pos(const AudioClip& c, i64 t) noexcept {
    if (!c.srcByFrame.empty()) {
        const f64 fr = static_cast<f64>(t - c.envShift) * c.fps / kMixRate - static_cast<f64>(c.srcFrame0);
        const i64 n = static_cast<i64>(c.srcByFrame.size());
        const i64 i = std::clamp<i64>(static_cast<i64>(std::floor(fr)), 0, n - 2 < 0 ? 0 : n - 2);
        if (n < 2) return c.srcByFrame[0];
        const f64 k = std::clamp(fr - static_cast<f64>(i), 0.0, 1.0);
        return c.srcByFrame[static_cast<usize>(i)] + (c.srcByFrame[static_cast<usize>(i + 1)] - c.srcByFrame[static_cast<usize>(i)]) * k;
    }
    return c.sourceStartF + static_cast<f64>(t - c.start) * c.rate;
}
} // namespace

namespace detail {
f32 clip_envelope(const AudioClip& c, i64 t) noexcept { return envelope(c, t); }
f64 clip_source_pos(const AudioClip& c, i64 t) noexcept { return clip_pos(c, t); }
} // namespace detail

void mix(const AudioMixSnapshot& snap, i64 start, u32 frames, BlockSource& blocks, f32* out, MixStats* stats,
         MixState* state) noexcept {
    std::fill(out, out + static_cast<usize>(frames) * kMixChannels, 0.0f);
    const i64 stop = start + frames;
    u32 missing = 0;
    const u64 epoch = state ? state->next_epoch() : 0;
    for (const AudioClip& c : snap.clips) {
        const i64 s0 = std::max(start, c.start);
        const i64 s1 = std::min(stop, c.end);
        if (s0 >= s1) continue;
        if (!c.chain.empty()) {
            // Efeitos de áudio: o fluxo do clipe (estado entre chamadas).
            detail::mix_fx_clip(c, s0, s1, start, blocks, out, state, epoch, missing);
            continue;
        }
        // Balanço (a fonte já é estéreo): o lado oposto desce, o próprio fica.
        const f32 balL = c.pan > 0.0f ? 1.0f - c.pan : 1.0f;
        const f32 balR = c.pan < 0.0f ? 1.0f + c.pan : 1.0f;
        if (!c.sends.empty()) {
            // Stateless finite impulse responses: identical at any seek/block size.
            // Bounded, allocation-free cache: never lock the block store per tap.
            std::array<i64, 32> cachedIndices;
            cachedIndices.fill(-1);
            std::array<const AudioBlock*, 32> cachedBlocks{};
            struct Taps { u32 count = 0; std::array<f64, 24> delay{}; std::array<f32, 24> gain{}; };
            std::array<Taps, 8> tapsBySend{};
            for (usize j=0; j<std::min<usize>(c.sends.size(), tapsBySend.size()); ++j) {
                const auto& fx=c.sends[j]; auto& taps=tapsBySend[j];
                if (fx.kind==1) continue;
                taps.count=fx.kind==0?24:6;
                f32 norm=0;
                for(u32 i=1;i<=taps.count;++i) {
                    const f32 d=fx.kind==0?(.19f*i+.07f*std::sin(i*2.39996f)):static_cast<f32>(i);
                    taps.delay[i-1]=d*fx.seconds*kMixRate;
                    taps.gain[i-1]=std::pow(fx.decay,fx.kind==0?(i-1)*.22f:static_cast<f32>(i-1));
                    norm+=taps.gain[i-1];
                }
                for(u32 i=0;i<taps.count;++i) taps.gain[i]/=std::max(1.f,norm);
            }
            auto read=[&](f64 timeline,f32& left,f32& right) {
                left=right=0;
                if(timeline<c.start || timeline>=c.end) return;
                const i64 ti=static_cast<i64>(std::floor(timeline));
                auto position=[&](i64 t){return c.rate==1.0?static_cast<f64>(c.sourceAt0+t-c.start):clip_pos(c,t);};
                const f64 pos=position(ti)+(position(ti+1)-position(ti))*(timeline-ti);
                const i64 at=static_cast<i64>(std::floor(pos)); const f32 frac=static_cast<f32>(pos-at);
                for(u32 neighbor=0;neighbor<2;++neighbor) {
                    const i64 index=at+neighbor; if(index<0 || index>=c.sourceLength) continue;
                    const i64 blockIndex = index / kBlockFrames;
                    const usize slot = static_cast<usize>(blockIndex) % cachedIndices.size();
                    if (cachedIndices[slot] != blockIndex) {
                        cachedIndices[slot] = blockIndex;
                        cachedBlocks[slot] = blocks.block(c.asset, blockIndex);
                    }
                    const auto* b = cachedBlocks[slot];
                    if(!b) { ++missing; continue; }
                    const usize off=static_cast<usize>(index%kBlockFrames)*2;
                    if(off+1>=b->pcm.size()) continue;
                    const f32 w=neighbor?frac:1-frac;
                    left+=b->pcm[off]*w; right+=b->pcm[off+1]*w;
                }
            };
            for(i64 t=s0;t<s1;++t) {
                f32 left,right; read(static_cast<f64>(t),left,right);
                for(usize j=0;j<std::min<usize>(c.sends.size(),tapsBySend.size());++j) {
                    const auto& fx=c.sends[j];
                    if(fx.wet<=0) continue;
                    f32 wl=0,wr=0;
                    if(fx.kind==1) {
                        const f64 phase=6.283185307179586*fx.rate*static_cast<f64>(t-c.start)/kMixRate;
                        const f64 delay=(.2+.8*(.5+.5*std::sin(phase)))*fx.seconds*kMixRate;
                        f32 gain=1, norm=0;
                        for(u32 i=1;i<=4;++i) {
                            f32 l,r; read(t-delay*i,l,r);
                            wl+=l*gain; wr+=r*gain; norm+=gain; gain*=fx.decay;
                        }
                        wl/=norm; wr/=norm;
                    } else {
                        const auto& taps=tapsBySend[j];
                        for(u32 i=0;i<taps.count;++i) {
                            f32 l,r; read(t-taps.delay[i],l,r);
                            const f32 gain=taps.gain[i];
                            // Decorrelated stereo reflections, with bounded energy.
                            wl+=(fx.kind==0 && i%2?r:l)*gain;
                            wr+=(fx.kind==0 && i%2?l:r)*gain;
                        }
                    }
                    left+=wl*fx.wet; right+=wr*fx.wet;
                }
                const f32 g=envelope(c,t); auto* o=out+static_cast<usize>(t-start)*2;
                o[0]+=left*g*balL; o[1]+=right*g*balR;
            }
            continue;
        }
        if (c.rate != 1.0) {
            // Leitura fracionária entre amostras vizinhas (que podem estar em
            // blocos diferentes).
            i64 bA = -1, bB = -1;
            const AudioBlock* blkA = nullptr;
            const AudioBlock* blkB = nullptr;
            auto sample_at = [&](i64 idx, f32& l, f32& r) -> bool {
                if (idx < 0 || idx >= c.sourceLength) return false;
                const i64 b = idx / kBlockFrames;
                const AudioBlock* blk = nullptr;
                if (b == bA) blk = blkA;
                else if (b == bB) blk = blkB;
                else {
                    blk = blocks.block(c.asset, b);
                    if (!blk) ++missing;
                    bB = bA; blkB = blkA;
                    bA = b; blkA = blk;
                }
                if (!blk) return false;
                const usize off = static_cast<usize>(idx - b * kBlockFrames) * 2;
                if (off + 1 >= blk->pcm.size()) return false;
                l = blk->pcm[off];
                r = blk->pcm[off + 1];
                return true;
            };
            for (i64 t = s0; t < s1; ++t) {
                const f64 pos = clip_pos(c, t);
                if (pos < 0.0 || pos >= static_cast<f64>(c.sourceLength)) continue;
                const i64 i0 = static_cast<i64>(pos);
                const f32 fr = static_cast<f32>(pos - static_cast<f64>(i0));
                f32 l0 = 0, r0 = 0, l1 = 0, r1 = 0;
                if (!sample_at(i0, l0, r0)) continue;
                if (!sample_at(i0 + 1, l1, r1)) { l1 = l0; r1 = r0; }
                const f32 g = envelope(c, t);
                f32* o = out + static_cast<usize>(t - start) * 2;
                o[0] += (l0 + (l1 - l0) * fr) * g * balL;
                o[1] += (r0 + (r1 - r0) * fr) * g * balR;
            }
            continue;
        }
        i64 curBlock = -1;
        const AudioBlock* blk = nullptr;
        for (i64 t = s0; t < s1; ++t) {
            const i64 src = c.sourceAt0 + (t - c.start);
            if (src < 0 || src >= c.sourceLength) continue;
            const i64 b = src / kBlockFrames;
            if (b != curBlock) {
                curBlock = b;
                blk = blocks.block(c.asset, b);
                if (!blk) ++missing;
            }
            if (!blk) continue;
            const usize off = static_cast<usize>(src - b * kBlockFrames) * 2;
            if (off + 1 >= blk->pcm.size()) continue;
            const f32 g = envelope(c, t);
            f32* o = out + static_cast<usize>(t - start) * 2;
            o[0] += blk->pcm[off] * g * balL;
            o[1] += blk->pcm[off + 1] * g * balR;
        }
    }
    f32 peak = 0.0f;
    for (usize i = 0; i < static_cast<usize>(frames) * kMixChannels; ++i) {
        out[i] = soft_limit(out[i]);
        peak = std::max(peak, std::fabs(out[i]));
    }
    if (stats) {
        stats->missingBlocks += missing;
        stats->peak = std::max(stats->peak, peak);
    }
    // Fluxos de clipes que saíram do snapshot (apagados, editados) não ficam
    // para sempre.
    if (state) state->sweep(epoch);
}

void blocks_needed(const AudioMixSnapshot& snap, i64 start, i64 frames, std::vector<std::pair<u64, i64>>& out) {
    const i64 stop = start + frames;
    for (const AudioClip& c : snap.clips) {
        if (c.asset == 0) continue;   // Tom: gerado, sem fonte
        f64 lookback=0;
        for(const auto& fx:c.sends) if(fx.wet>0) lookback=std::max(lookback,static_cast<f64>(fx.seconds)*(fx.kind==1?4:6)*kMixRate);
        // Efeitos de áudio: a pré-rolagem lê a fonte antes do ponto.
        if (!c.chain.empty()) lookback = std::max(lookback, static_cast<f64>(c.preroll));
        i64 s0 = std::max(start-static_cast<i64>(std::ceil(lookback))-2, c.start);
        i64 s1 = std::min(stop, c.end);
        if (s0 >= s1) continue;
        if (c.reverse) {
            // Reverso: o trecho [s0, s1) lê o espelho dele dentro da camada.
            const i64 mirror = c.revFrom + c.revTo - 1;
            const i64 a0 = mirror - (s1 - 1), a1 = mirror - s0 + 1;
            s0 = a0;
            s1 = a1;
        }
        i64 a = 0, z = 0;
        if (!c.srcByFrame.empty()) {
            // A curva pode ir e voltar: extremos amostrados a cada quadro.
            f64 lo = 1e300, hi = -1e300;
            const i64 step = std::max<i64>(1, static_cast<i64>(kMixRate / std::max(1.0, c.fps)));
            for (i64 t = s0; t < s1 + step; t += step) {
                const f64 p = clip_pos(c, std::min(t, s1));
                lo = std::min(lo, p);
                hi = std::max(hi, p);
            }
            a = std::max<i64>(0, static_cast<i64>(std::floor(lo)));
            z = std::min(c.sourceLength, static_cast<i64>(std::ceil(hi)) + 1);
        } else if (c.rate != 1.0) {
            const f64 pa = c.sourceStartF + static_cast<f64>(s0 - c.start) * c.rate;
            const f64 pz = c.sourceStartF + static_cast<f64>(s1 - c.start) * c.rate;
            a = std::max<i64>(0, static_cast<i64>(std::floor(std::min(pa, pz))));
            z = std::min(c.sourceLength, static_cast<i64>(std::ceil(std::max(pa, pz))) + 1);
        } else {
            a = std::max<i64>(0, c.sourceAt0 + (s0 - c.start));
            z = std::min(c.sourceLength, c.sourceAt0 + (s1 - c.start));
        }
        if (z <= a) continue;
        for (i64 b = a / kBlockFrames; b <= (z - 1) / kBlockFrames; ++b) out.emplace_back(c.asset, b);
    }
}

} // namespace aurea::audio
