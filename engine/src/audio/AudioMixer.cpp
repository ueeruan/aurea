// =============================================================================
//  Aurea / audio / AudioMixer.cpp — snapshot da timeline e mixagem.
// =============================================================================
#include "aurea/audio/Audio.hpp"

#include "aurea/expr/Expression.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/timeline/Composition.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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

struct Flatten {
    const Project& project;
    AudioBlockCache* cache;
    AssetPathResolver resolve;
    void* ctx;
    std::vector<AudioClip>& out;

    /// Acrescenta os sons de `comp`. `shift`: amostra da raiz − amostra de
    /// `comp`. [lo, hi): janela visível na raiz (a pré-comp corta o que passa
    /// da borda da layer que a contém). `gain` acumulado das pré-comps.
    void add(const Composition& comp, i64 shift, i64 lo, i64 hi, f32 gain, u32 depth) {
        if (depth > 16) return;
        const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
        bool anySolo = false;
        comp.layers().for_each([&](LayerId, const Layer& l) {
            if (l.solo && (has_sound(l, project) || l.kind == LayerKind::Composition)) anySolo = true;
        });
        comp.layers().for_each([&](LayerId, const Layer& l) {
            if (l.muted || (anySolo && !l.solo)) return;
            // Remapeamento por curva ainda não chega ao áudio; quadro
            // congelado não tem som. Um som fora de sincronia é pior que silêncio.
            if ((!l.timeRemapEnabled && l.speed <= 0.0f)) return;
            const i64 ls = frame_to_sample(l.start.value, fps);
            const i64 le = frame_to_sample(l.end.value, fps);
            if (le <= ls) return;
            const f32 volume = l.tracks.sample_or(TrackProperty::AudioVolume, FrameIndex{l.offset.value}, 1.0f);
            if (l.kind == LayerKind::Composition) {
                const Composition* child = project.timeline().composition(l.nested.composition);
                if (!child || child == &comp) return;
                const f64 cfps = child->fps() > 0.0 ? child->fps() : 30.0;
                // Instante 0 da filha toca em ls − offset (offset na régua da filha).
                const i64 childShift = shift + ls - frame_to_sample(l.offset.value, cfps);
                add(*child, childShift, std::max(lo, ls + shift), std::min(hi, le + shift),
                    gain * l.gain * volume, depth + 1);
                return;
            }
            if (!has_sound(l, project)) return;
            const Asset& a = *project.asset(l.source);
            const u64 key = l.source.pack();
            if (cache) cache->register_asset(key, AudioAssetRef{resolve ? resolve(ctx, a.sourcePath) : a.sourcePath,
                                                                asset_length(a)});
            AudioClip c;
            c.asset = key;
            c.start = std::max(lo, ls + shift);
            c.end = std::min(hi, le + shift);
            if (c.end <= c.start) return;
            // Amostra da fonte em `ls` (início da layer) = offset do conteúdo.
            const i64 srcAtLs = frame_to_sample(l.offset.value, fps);
            c.sourceAt0 = srcAtLs + (c.start - shift - ls);
            if (l.timeRemapEnabled && !l.timeRemap.keys.empty()) {
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
            } else if (l.speed != 1.0f || l.reversed) {
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
            c.sourceLength = asset_length(a);
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
    f.add(comp, 0, 0, snap->endSample, 1.0f, 0);
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

void mix(const AudioMixSnapshot& snap, i64 start, u32 frames, BlockSource& blocks, f32* out, MixStats* stats) noexcept {
    std::fill(out, out + static_cast<usize>(frames) * kMixChannels, 0.0f);
    const i64 stop = start + frames;
    u32 missing = 0;
    for (const AudioClip& c : snap.clips) {
        const i64 s0 = std::max(start, c.start);
        const i64 s1 = std::min(stop, c.end);
        if (s0 >= s1) continue;
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
}

void blocks_needed(const AudioMixSnapshot& snap, i64 start, i64 frames, std::vector<std::pair<u64, i64>>& out) {
    const i64 stop = start + frames;
    for (const AudioClip& c : snap.clips) {
        f64 lookback=0;
        for(const auto& fx:c.sends) if(fx.wet>0) lookback=std::max(lookback,static_cast<f64>(fx.seconds)*(fx.kind==1?4:6)*kMixRate);
        const i64 s0 = std::max(start-static_cast<i64>(std::ceil(lookback))-2, c.start);
        const i64 s1 = std::min(stop, c.end);
        if (s0 >= s1) continue;
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
