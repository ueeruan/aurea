// =============================================================================
//  Aurea / engine / EngineHeadbop.cpp
//
//  Headbop (0.5.2): a análise que liga a detecção de corpo das plataformas
//  (ML Kit Pose / rostos no Android, Vision no iOS — o mesmo formato do
//  Motion Capture) ao rig do efeito, a batida do áudio, o tap tempo e o
//  "Converter em keyframes".
//
//  A análise roda na thread de quem chama (a UI chama fora da main, como o
//  Motion Capture), com progresso e cancelamento atômicos; o resultado entra
//  no projeto num passo de desfazer: o rig parado nos parâmetros ocultos, a
//  trilha por quadro (vídeo) e a grade de batidas no RigStore
//  (`<cache>/headbop/<ficha>.hbr`, regenerável — sem ela vale o rig parado).
//
//  A sessão fica numa tabela deste arquivo (por motor), não num membro do
//  Engine: o layout da classe não muda.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/audio/Audio.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Headbop.hpp"
#include "aurea/rig/Mocap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>

namespace aurea {
namespace {

struct HeadbopSession {
    std::mutex work;
    u64 layer = 0;
    u32 effect = 0;
    std::vector<f32> data;   ///< headbop::kFrameFloats por quadro
    f32 aspect = 1.0f;
    headbop::AnalysisStatus status;
};

std::mutex gSessionsMutex;
std::unordered_map<const Engine*, std::shared_ptr<HeadbopSession>> gSessions;

std::shared_ptr<HeadbopSession> session_of(const Engine* e, bool create) {
    std::lock_guard<std::mutex> lock(gSessionsMutex);
    auto it = gSessions.find(e);
    if (it != gSessions.end()) return it->second;
    if (!create) return nullptr;
    auto s = std::make_shared<HeadbopSession>();
    gSessions[e] = s;
    return s;
}

EffectInstance* headbop_instance(Composition* comp, u64 layerId, u32 effectId, Layer*& layer) noexcept {
    layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer) return nullptr;
    EffectInstance* e = layer->find_effect(EffectId{effectId, 0});
    if (!e || e->type != effect_type_id(headbop::kHeadbopKey) || e->params.size() < headbop::kParamCount) return nullptr;
    return e;
}

void set_const(EffectInstance& e, u32 param, f32 x, f32 y = 0.0f) noexcept {
    if (param >= e.params.size()) return;
    e.params[param].constant.v[0] = x;
    e.params[param].constant.v[1] = y;
}

/// Rig de cada quadro a partir dos pontos empacotados.
headbop::RigTrack build_track(const std::vector<f32>& data, f32 aspect, bool video,
                              const std::atomic<bool>& cancel, std::atomic<f32>& progress) {
    using namespace headbop;
    const usize frames = data.size() / kFrameFloats;
    // Pessoa 0 (detector de pose): limpa com o mesmo filtro do Motion Capture.
    std::vector<rig::PoseFrame> mainIn;
    std::map<f64, std::vector<RigFace>> byTime;
    for (usize i = 0; i < frames; ++i) {
        const f32* f = &data[i * kFrameFloats];
        if (!std::isfinite(f[0])) continue;
        const u32 person = std::isfinite(f[1]) && f[1] > 0.5f ? static_cast<u32>(f[1] + 0.5f) : 0u;
        if (person == 0 && video) {
            rig::PoseFrame pf;
            pf.time_s = f[0];
            for (u32 k = 0; k < rig::kPoseLandmarks; ++k)
                pf.pts[k] = rig::PoseLandmark{f[2 + k * 4], f[3 + k * 4], f[4 + k * 4], f[5 + k * 4]};
            mainIn.push_back(pf);
            continue;
        }
        RigFace face;
        if (face_from_landmarks(f + 2, aspect, face)) byTime[video ? f[0] : 0.0].push_back(face);
        else (void)byTime[video ? f[0] : 0.0];
    }
    progress.store(0.1f);
    if (!mainIn.empty()) {
        std::vector<rig::PoseFrame> clean;
        rig::MocapParams mp;
        mp.imageAspect = aspect;
        mp.minConfidence = 0.35f;
        rig::MocapReport report;
        if (mainIn.size() < 2 || !rig::clean_frames(mainIn, mp.sanitized(), clean, report)) clean = mainIn;
        for (const rig::PoseFrame& pf : clean) {
            if (cancel.load()) return {};
            f32 pts[33 * 4];
            for (u32 k = 0; k < 33; ++k) {
                pts[k * 4] = pf.pts[k].x; pts[k * 4 + 1] = pf.pts[k].y;
                pts[k * 4 + 2] = pf.pts[k].z; pts[k * 4 + 3] = pf.pts[k].confidence;
            }
            RigFace face;
            auto& slot = byTime[pf.time_s];
            if (face_from_landmarks(pts, aspect, face)) slot.push_back(face);
        }
    }
    progress.store(0.25f);
    RigTrack track;
    for (auto& [t, people] : byTime) {
        RigFrame fr;
        fr.time = t;
        frame_from_people(people, fr);
        track.frames.push_back(fr);
    }
    if (video) smooth_track(track);
    return track;
}

} // namespace

bool Engine::headbop_begin(u64 layerId, u32 effectId) noexcept {
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        Composition* comp = project_ ? current_composition() : nullptr;
        Layer* l = nullptr;
        if (!headbop_instance(comp, layerId, effectId, l)) return false;
    }
    auto s = session_of(this, true);
    std::lock_guard<std::mutex> lock(s->work);
    s->layer = layerId;
    s->effect = effectId;
    s->data.clear();
    s->aspect = 1.0f;
    s->status.cancel.store(false);
    s->status.state.store(0);
    s->status.progress.store(0.0f);
    s->status.frames.store(0);
    return true;
}

bool Engine::headbop_push_frames(const f32* data, u32 frames, f32 aspect) noexcept {
    auto s = session_of(this, false);
    if (!s || !data || !frames) return false;
    std::lock_guard<std::mutex> lock(s->work);
    // Teto: 20 min a 30 qps.
    constexpr usize kMaxFrames = 36000;
    const usize have = s->data.size() / headbop::kFrameFloats;
    if (have + frames > kMaxFrames) frames = static_cast<u32>(kMaxFrames - std::min(kMaxFrames, have));
    s->data.insert(s->data.end(), data, data + static_cast<usize>(frames) * headbop::kFrameFloats);
    if (std::isfinite(aspect) && aspect > 0.0f) s->aspect = aspect;
    s->status.frames.store(static_cast<i32>(s->data.size() / headbop::kFrameFloats));
    return true;
}

void Engine::headbop_cancel() noexcept {
    if (auto s = session_of(this, false)) s->status.cancel.store(true);
}

bool Engine::headbop_analyze() noexcept {
    auto s = session_of(this, false);
    if (!s) return false;
    std::lock_guard<std::mutex> work(s->work);
    auto& st = s->status;
    st.cancel.store(false);
    st.state.store(1);
    st.progress.store(0.0f);
    auto fail = [&](i32 state) { st.state.store(state); return false; };
    {
        using namespace headbop;
        RigStore::shared().set_directory(config_.cacheDirectory.empty() ? std::string{} : config_.cacheDirectory + "/headbop");
        // 1. O que a análise precisa, sob o lock do modelo.
        bool video = false;
        u64 audioLayer = 0;
        i64 effStart = 0, effEnd = 0;
        f64 fps = 30.0;
        u64 revisionKey = 0;
        {
            std::lock_guard<std::mutex> lock(modelMutex_);
            Composition* comp = project_ ? current_composition() : nullptr;
            Layer* l = nullptr;
            EffectInstance* e = headbop_instance(comp, s->layer, s->effect, l);
            if (!e) return fail(3);
            video = l->kind == LayerKind::Video;
            effStart = l->start.value;
            effEnd = l->end.value;
            fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
            revisionKey = l->source.pack();
            // Camada de áudio: a escolhida; senão a primeira com som (áudio antes de vídeo).
            const u64 ref = e->params[kAudioLayer].constant.ref;
            auto hasSound = [&](const Layer* x) {
                if (!x || (x->kind != LayerKind::Audio && x->kind != LayerKind::Video)) return false;
                const Asset* a = project_->asset(x->source);
                return a && a->has_audio();
            };
            if (ref && hasSound(comp->layer(LayerId::unpack(ref)))) audioLayer = ref;
            if (!audioLayer) {
                const OrderedIds<LayerId>& order = comp->order();
                for (u32 pass = 0; pass < 2 && !audioLayer; ++pass) {
                    for (u32 i = 0; i < order.size(); ++i) {
                        const Layer* x = comp->layer(order.at(i));
                        if (!hasSound(x) || (pass == 0 && x->kind != LayerKind::Audio)) continue;
                        audioLayer = order.at(i).pack();
                        break;
                    }
                }
            }
        }
        // 2. Rig por quadro.
        const RigTrack track = build_track(s->data, s->aspect, video, st.cancel, st.progress);
        if (st.cancel.load()) return fail(4);
        RigFrame still = video ? median_frame(track) : (track.frames.empty() ? RigFrame{} : track.frames.front());
        st.people.store(static_cast<i32>(still.count));
        st.progress.store(0.3f);

        // 3. Batidas do áudio (grade em segundos da COMPOSIÇÃO).
        BeatGrid grid;
        if (audioLayer) {
            std::vector<f32> mono;
            f64 atStart = 0.0, rate = 1.0;
            i64 seg0 = 0;
            bool reversed = false;
            i64 clipStart = 0;
            u64 key = 0;
            audio::AudioAssetRef aref;
            bool ok = false;
            {
                std::lock_guard<std::mutex> lock(modelMutex_);
                Composition* comp = project_ ? current_composition() : nullptr;
                const Layer* a = comp ? comp->layer(LayerId::unpack(audioLayer)) : nullptr;
                const Asset* asset = a ? project_->asset(a->source) : nullptr;
                if (a && asset && asset->has_audio() && std::isfinite(a->speed) && a->speed > 0.f && !a->speed_track()
                    && !a->timeRemapEnabled && a->end.value > a->start.value) {
                    // Até ~170 s a partir de um pouco antes do efeito.
                    clipStart = std::max(a->start.value, effStart - static_cast<i64>(2 * fps));
                    const i64 clipEnd = std::min(a->end.value, clipStart + static_cast<i64>(170 * fps));
                    if (clipEnd - clipStart > static_cast<i64>(3 * fps)) {
                        const f64 sourceFps = asset->timebaseFps > 0 ? asset->timebaseFps : fps;
                        const long double duration = asset->audio.sampleCount.value > 0 && asset->audio.sampleRate > 0
                            ? static_cast<long double>(asset->audio.sampleCount.value) * audio::kMixRate / asset->audio.sampleRate
                            : static_cast<long double>(asset->duration.value) * audio::kMixRate / sourceFps;
                        key = a->source.pack();
                        rate = a->speed;
                        reversed = a->reversed;
                        atStart = a->source_frame(FrameIndex{clipStart});
                        aref = audio::AudioAssetRef{resolve_asset_path(asset->sourcePath), static_cast<i64>(duration)};
                        const long double srcA = atStart;
                        const long double srcB = srcA + (reversed ? -1.L : 1.L) * (clipEnd - clipStart) * rate;
                        const auto sampleAt = [&](long double frame, bool upper) {
                            const long double samples = frame * audio::kMixRate / fps;
                            return static_cast<i64>(std::clamp(upper ? std::ceil(samples) : std::floor(samples), 0.L,
                                                               static_cast<long double>(aref.durationSamples)));
                        };
                        const i64 s0 = sampleAt(std::min(srcA, srcB), false), s1 = sampleAt(std::max(srcA, srcB), true);
                        if (std::isfinite(atStart) && s1 - s0 >= static_cast<i64>(audio::kMixRate) * 3) {
                            mono.assign(static_cast<usize>(s1 - s0), 0.0f);
                            seg0 = s0;
                            ok = true;
                        }
                    }
                }
            }
            VideoSourceFactory* factory = config_.mediaFactory;
            if (ok && factory) {
                const i64 s0 = seg0;
                const i64 s1 = s0 + static_cast<i64>(mono.size());
                audio::AudioBlockCache blocks(factory, 2ull << 20, false);
                blocks.register_asset(key, aref);
                const i64 firstBlock = s0 / audio::kBlockFrames;
                const i64 lastBlock = (s1 - 1) / audio::kBlockFrames;
                for (i64 block = firstBlock; block <= lastBlock && ok; ++block) {
                    if (st.cancel.load()) return fail(4);
                    const auto pcm = blocks.fetch(key, block);
                    if (!pcm) { ok = false; break; }
                    const i64 base = block * audio::kBlockFrames;
                    const i64 last = std::min(base + static_cast<i64>(audio::kBlockFrames), s1);
                    if (static_cast<u64>(std::max<i64>(0, last - base)) * 2 > pcm->pcm.size()) { ok = false; break; }
                    for (i64 i = std::max(base, s0); i < last; ++i) {
                        const usize o = static_cast<usize>(i - base) * 2;
                        const f32 v = 0.5f * (pcm->pcm[o] + pcm->pcm[o + 1]);
                        mono[static_cast<usize>(i - s0)] = std::isfinite(v) ? v : 0.0f;
                    }
                    st.progress.store(0.3f + 0.4f * static_cast<f32>(block - firstBlock + 1) / static_cast<f32>(lastBlock - firstBlock + 1));
                }
                if (ok) {
                    const BeatGrid local = AnalysisCache::shared().get_or_analyze(mono.data(), mono.size());
                    // Segundos do trecho → segundos da composição (a conta do Detectar batidas).
                    auto toComp = [&](f64 seconds) {
                        const long double source = (static_cast<long double>(s0) / audio::kMixRate + seconds) * fps;
                        const long double elapsed = reversed ? (atStart - source) / rate : (source - atStart) / rate;
                        return static_cast<f64>((clipStart + elapsed) / fps);
                    };
                    grid.bpm = local.bpm;
                    for (usize i = 0; i < local.beats.size(); ++i) {
                        const f64 c = toComp(local.beats[i]);
                        if (!std::isfinite(c)) continue;
                        grid.beats.push_back(c);
                        if (i < local.strength.size()) grid.strength.push_back(local.strength[i]);
                    }
                    if (reversed) {
                        std::reverse(grid.beats.begin(), grid.beats.end());
                        std::reverse(grid.strength.begin(), grid.strength.end());
                    }
                    if (grid.strength.size() != grid.beats.size()) grid.strength.clear();
                    grid.phaseSec = grid.beats.empty() ? toComp(local.phaseSec) : grid.beats.front();
                    if (!std::isfinite(grid.phaseSec)) grid.phaseSec = 0.0;
                }
            }
        }
        if (st.cancel.load()) return fail(4);
        st.bpm.store(static_cast<f32>(grid.bpm));
        st.progress.store(0.9f);

        // 4. Grava no projeto (um passo de desfazer) e no RigStore.
        auto entry = std::make_shared<StoreEntry>();
        if (video) entry->track = track;
        entry->grid = grid;
        const u32 token = RigStore::shared().new_token();
        if (token) (void)RigStore::shared().put(token, entry);
        {
            std::lock_guard<std::mutex> lock(modelMutex_);
            Composition* comp = project_ ? current_composition() : nullptr;
            Layer* l = nullptr;
            EffectInstance* e = headbop_instance(comp, s->layer, s->effect, l);
            if (!e || l->source.pack() != revisionKey) return fail(3);
            history_.before_mutation(*comp, project_->timeline().current(), "headbop: analisar");
            bump_model_revision_locked();
            set_const(*e, kRigToken, static_cast<f32>(token));
            set_const(*e, kRigState, static_cast<f32>(still.count ? 1 + still.count : 1));
            for (u32 i = 0; i < kMaxFaces; ++i) {
                const RigFace& f = still.faces[i];
                const u32 b = kRigFirst + i * kFaceParams;
                set_const(*e, b + 0, f.center.x, f.center.y);
                set_const(*e, b + 1, f.axes.x, f.axes.y);
                set_const(*e, b + 2, f.roll, f.yaw);
                set_const(*e, b + 3, f.pitch, f.depth);
                set_const(*e, b + 4, f.neck.x, f.neck.y);
                set_const(*e, b + 5, f.shoulderA.x, f.shoulderA.y);
                set_const(*e, b + 6, f.shoulderB.x, f.shoulderB.y);
            }
            if (grid.bpm > 0.0) {
                set_const(*e, kAutoBpm, static_cast<f32>(grid.bpm));
                set_const(*e, kAutoPhase, static_cast<f32>(grid.phaseSec));
            }
            // O pivô manual começa no pescoço encontrado (a alça do palco parte daí).
            if (still.count && !(e->params[kPivotManual].constant.v[0] > 0.5f))
                set_const(*e, kPivot, still.faces[0].neck.x, still.faces[0].neck.y);
            l->modelEffects = true;
            project_->mark_dirty();
        }
        s->data.clear();
        s->data.shrink_to_fit();
        st.progress.store(1.0f);
        st.state.store(2);
        request_render();
        return true;
    }
}

bool Engine::headbop_status(u64 layerId, u32 effectId, i64* out8) noexcept {
    if (!out8) return false;
    for (u32 i = 0; i < 8; ++i) out8[i] = 0;
    if (auto s = session_of(this, false); s && s->layer == layerId && s->effect == effectId) {
        out8[0] = s->status.state.load();
        out8[1] = static_cast<i64>(std::lround(s->status.progress.load() * 1000.0f));
        out8[2] = s->status.people.load();
        out8[3] = static_cast<i64>(std::lround(s->status.bpm.load() * 100.0f));
        out8[4] = s->status.frames.load();
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = nullptr;
    const EffectInstance* e = headbop_instance(comp, layerId, effectId, l);
    if (!e) return false;
    const i32 state = static_cast<i32>(std::lround(e->params[headbop::kRigState].constant.v[0]));
    out8[5] = state;   // 0 sem análise, 1 sem pessoa, 2.. pessoas + 1
    const u32 src = e->params[headbop::kBeatSource].constant.as_enum();
    const f32 bpm = src == 1 ? e->params[headbop::kBpm].constant.v[0]
                  : src == 2 ? e->params[headbop::kTapBpm].constant.v[0] : e->params[headbop::kAutoBpm].constant.v[0];
    out8[6] = static_cast<i64>(std::lround(std::isfinite(bpm) ? bpm * 100.0f : 0.0f));
    out8[7] = e->params[headbop::kPoseSource].constant.as_enum();
    return true;
}

bool Engine::headbop_set_taps(u64 layerId, u32 effectId, const f64* times, u32 count) noexcept {
    if (!times || count < 2) return false;
    std::vector<f64> taps(times, times + count);
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = nullptr;
    EffectInstance* e = headbop_instance(comp, layerId, effectId, l);
    if (!e || l->locked) return false;
    const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
    const headbop::BeatGrid g = headbop::grid_from_taps(taps, static_cast<f64>(comp->duration().value) / fps);
    if (!(g.bpm > 0.0) || !std::isfinite(g.bpm)) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "headbop: tap");
    bump_model_revision_locked();
    set_const(*e, headbop::kTapBpm, static_cast<f32>(std::clamp(g.bpm, 20.0, 400.0)));
    set_const(*e, headbop::kTapPhase, static_cast<f32>(std::isfinite(g.phaseSec) ? g.phaseSec : 0.0));
    set_const(*e, headbop::kBeatSource, static_cast<f32>(headbop::BeatSource::Tap));
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::headbop_bake(u64 layerId, u32 effectId) noexcept {
    using namespace headbop;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = nullptr;
    EffectInstance* e = headbop_instance(comp, layerId, effectId, l);
    if (!e || l->locked) return -1;
    const ParameterRegistry* params = effectRegistry_.params(e->type);
    if (!params || params->count() < kParamCount) return -1;
    const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
    const i64 first = l->start.value, last = l->end.value;
    if (last <= first) return -1;
    // Teto: 9000 quadros (5 min a 30 qps) — keys por canal.
    const i64 frames = std::min<i64>(last - first, 9000);
    const f32 tokenF = e->params[kRigToken].constant.v[0];
    const u32 token = std::isfinite(tokenF) && tokenF >= 1.0f ? static_cast<u32>(std::min(tokenF, 16777215.0f)) : 0u;
    const std::shared_ptr<const StoreEntry> entry = token ? RigStore::shared().get(token) : nullptr;
    std::vector<std::array<f32, kPoseChannels>> poses(static_cast<usize>(frames));
    for (i64 k = 0; k < frames; ++k) {
        const FrameIndex local = l->local_time(FrameIndex{first + k});
        f32 v[kSeed + 1];
        for (u32 p = 0; p <= kSeed; ++p) v[p] = evaluate_param(l->tracks, *e, p, params->at(p), local).v[0];
        Params mp = motion_params(v, kSeed + 1);
        auto at = [&](u32 p) { return evaluate_param(l->tracks, *e, p, params->at(p), local).v[0]; };
        apply_secondary(mp, at(kHair), at(kCloth), at(kPhysics));
        const u32 src = v[kBeatSource] <= 0.0f ? 0u : static_cast<u32>(v[kBeatSource] + 0.5f);
        const BeatGrid grid = beat_grid(src, at(kAutoBpm), at(kAutoPhase), at(kTapBpm), at(kTapPhase), entry.get());
        poses[static_cast<usize>(k)] = pose_channels(evaluate(mp, grid, static_cast<f64>(first + k) / fps));
    }
    history_.before_mutation(*comp, project_->timeline().current(), "headbop: converter em keyframes");
    bump_model_revision_locked();
    for (u32 c = 0; c < kPoseChannels; ++c) {
        Track& t = l->tracks.get_or_create(TrackProperty::EffectParam, e->id, param_track_key(bake_param(c), 0));
        t.keys.clear();
        for (i64 k = 0; k < frames; ++k)
            (void)t.set(l->local_time(FrameIndex{first + k}), poses[static_cast<usize>(k)][c], Interpolation::Linear);
        e->params[bake_param(c)].constant.v[0] = poses.empty() ? 0.0f : poses.front()[c];
    }
    set_const(*e, kPoseSource, static_cast<f32>(PoseSource::Keyframes));
    l->modelEffects = true;
    project_->mark_dirty();
    request_render();
    return static_cast<i32>(frames);
}

} // namespace aurea
