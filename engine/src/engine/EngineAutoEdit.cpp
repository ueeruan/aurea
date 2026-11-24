// =============================================================================
//  Edição automática (1.0.8): junta as entradas (áudio decodificado da mídia,
//  palavras da transcrição, quadros para rostos e cor, biblioteca de sons),
//  chama as regras puras de autoedit/AutoEdit.hpp e monta a edição numa
//  composição NOVA "<nome> — edição automática". A camada original fica como
//  estava (só desligada e muda, embaixo da camada nova). Um passo de desfazer.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/DefaultNames.hpp"
#include "aurea/audio/AudioEffects.hpp"
#include "aurea/audio/Beats.hpp"
#include "aurea/autoedit/AutoEdit.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/Presets.hpp"
#include "aurea/text/Captions.hpp"
#include "aurea/text/LocalWhisper.hpp"
#include "aurea/text/Text.hpp"

#include "../hyper/neural/NeuralStages.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace aurea {

namespace {
namespace aed = autoedit;

struct SoundInfo {
    bool ok = false;
    f32 peakDb = -120.0f;
    f64 peakAt = 0.0;     ///< segundos até o pico
    f64 duration = 0.0;
};

SoundInfo measure_sound(const std::vector<f32>& x, u32 rate) {
    SoundInfo s;
    if (x.empty() || rate == 0) return s;
    f32 peak = 0.0f;
    usize at = 0;
    for (usize i = 0; i < x.size(); ++i) if (std::fabs(x[i]) > peak) { peak = std::fabs(x[i]); at = i; }
    s.ok = peak > 0.0f;
    s.peakDb = 20.0f * std::log10(peak + 1e-12f);
    s.peakAt = static_cast<f64>(at) / rate;
    s.duration = static_cast<f64>(x.size()) / rate;
    return s;
}

/// Pico medido uma vez por arquivo (caminho) no processo.
std::mutex g_soundMutex;
std::map<std::string, SoundInfo> g_soundCache;

bool write_wav48(const std::string& path, const std::vector<f32>& x) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto u32le = [&](u32 v) { const u8 b[4] = {u8(v), u8(v >> 8), u8(v >> 16), u8(v >> 24)}; std::fwrite(b, 1, 4, f); };
    auto u16le = [&](u16 v) { const u8 b[2] = {u8(v), u8(v >> 8)}; std::fwrite(b, 1, 2, f); };
    const u32 bytes = static_cast<u32>(x.size() * 2);
    std::fwrite("RIFF", 1, 4, f); u32le(36 + bytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32le(16); u16le(1); u16le(1); u32le(48000); u32le(96000); u16le(2); u16le(16);
    std::fwrite("data", 1, 4, f); u32le(bytes);
    std::vector<i16> pcm(x.size());
    for (usize i = 0; i < x.size(); ++i) pcm[i] = static_cast<i16>(std::lround(std::clamp(x[i], -1.0f, 1.0f) * 32767.0f));
    const bool ok = std::fwrite(pcm.data(), 2, pcm.size(), f) == pcm.size();
    return std::fclose(f) == 0 && ok;
}

const json::Value* field(const json::Value& v, const char* key) { return v.is_object() ? v.get(key) : nullptr; }
f64 num(const json::Value* v, f64 def) { return v && v->is_number() && std::isfinite(v->number) ? v->number : def; }
bool flag(const json::Value* v, bool def) { return v && v->type == json::Value::Type::Bool ? v->boolean : def; }
std::string str(const json::Value* v) { return v && v->is_string() ? v->string : std::string{}; }

struct LibraryItem {
    std::string path, name, category;
    f64 duration = 0.0;
    f32 bpm = 0.0f;
};

void set_key(Track& t, i64 frame, f32 value, bool ease) {
    t.set(FrameIndex{frame}, value, ease ? Interpolation::Bezier : Interpolation::Hold);
    if (ease) t.set_interpolation(FrameIndex{frame}, Interpolation::Bezier, aed::kEaseX1, aed::kEaseY1, aed::kEaseX2, aed::kEaseY2, 1);
}

std::string error_json(const char* code, const std::string& message) {
    json::Writer w;
    w.begin_object().key("ok").value(false).key("error").value(code).key("message").value(message).end_object();
    return w.str();
}

} // namespace

std::string Engine::auto_edit(u64 sourceLayer, const std::string& requestJson) noexcept {
    static std::mutex running;
    std::unique_lock task(running, std::try_to_lock);
    if (!task.owns_lock()) return error_json("busy", "edicao automatica em andamento");
    autoEditCancelled.store(false);
    autoEditProgress.store(0);
    auto progress = [&](int step, int pct) { autoEditProgress.store(step * 100 + std::clamp(pct, 0, 99)); };
    auto cancelled = [&] { return autoEditCancelled.load(); };

    json::Value req;
    if (requestJson.size() > 8u * 1024 * 1024 || !json::parse(requestJson, req) || !req.is_object())
        return error_json("request", "pedido invalido");
    const json::Value* o = field(req, "options");
    aed::Options opt;
    opt.intensity = static_cast<aed::Intensity>(std::clamp<int>(static_cast<int>(num(field(o ? *o : req, "intensity"), 1)), 0, 2));
    opt.recording = num(field(o ? *o : req, "recording"), 0) >= 1 ? aed::Recording::Continuous : aed::Recording::Retakes;
    const std::string langCode = str(field(o ? *o : req, "language"));
    opt.language = aed::language_of(langCode.empty() ? "pt" : langCode);
    if (o) {
        opt.cuts = flag(field(*o, "cuts"), true); opt.zoom = flag(field(*o, "zoom"), true);
        opt.sounds = flag(field(*o, "sounds"), true); opt.music = flag(field(*o, "music"), true);
        opt.voice = flag(field(*o, "voice"), true); opt.color = flag(field(*o, "color"), true);
        opt.captions = flag(field(*o, "captions"), true);
    }
    const bool lightLeakAccess = flag(field(req, "lightLeak"), false);
    std::vector<aed::Word> words;
    if (const auto* list = field(req, "words"); list && list->is_array()) {
        for (const auto& w : list->array) {
            const std::string t = str(field(w, "text"));
            const f64 a = num(field(w, "start"), -1), b = num(field(w, "end"), -1);
            if (t.empty() || a < 0 || b < a || t.size() > 256) continue;
            words.push_back({t, a, b});
        }
    }
    std::sort(words.begin(), words.end(), [](const aed::Word& a, const aed::Word& b) { return a.start < b.start; });
    std::vector<LibraryItem> library;
    if (const auto* list = field(req, "library"); list && list->is_array()) {
        for (const auto& it : list->array) {
            LibraryItem li{str(field(it, "path")), str(field(it, "name")), str(field(it, "category")),
                           num(field(it, "duration"), 0), static_cast<f32>(num(field(it, "bpm"), 0))};
            if (!li.path.empty() && library.size() < 2000) library.push_back(std::move(li));
        }
    }
    std::sort(library.begin(), library.end(), [](const LibraryItem& a, const LibraryItem& b) { return a.path < b.path; });

    // --- 1. A camada e a mídia (com o modelo travado) -----------------------
    progress(0, 0);
    Layer src;
    Asset asset;
    std::string mediaPath;
    u64 session = 0;
    CompositionId compositionId;
    f64 fps = 30.0;
    u32 compW = 0, compH = 0;
    std::string baseName;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        Composition* comp = project_ ? current_composition() : nullptr;
        if (!comp) return error_json("project", "nenhum projeto aberto");
        const Layer* l = comp->layer(LayerId::unpack(sourceLayer));
        if (!l || l->kind != LayerKind::Video) return error_json("layer", "escolha um clipe de video");
        if (l->locked) return error_json("locked", "camada bloqueada");
        if (l->timeRemapEnabled || l->reversed || l->speed_track() || std::fabs(l->speed - 1.0f) > 1e-4f)
            return error_json("speed", "clipe com velocidade alterada");
        const Asset* a = project_->asset(l->source);
        if (!a) return error_json("layer", "camada sem midia");
        src = *l; asset = *a;
        mediaPath = resolve_asset_path(a->sourcePath);
        session = projectSession_;
        compositionId = project_->timeline().current();
        fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        compW = comp->width(); compH = comp->height();
        baseName = str(field(req, "name"));
        if (baseName.empty()) baseName = src.name.empty() ? std::string("Video") : src.name;
    }
    opt.fps = fps;
    const i64 clipFrames = src.end.value - src.start.value;
    if (clipFrames < static_cast<i64>(fps)) return error_json("short", "clipe curto demais");
    const f64 srcStart = static_cast<f64>(src.offset.value) / fps;   // segundo da mídia no início da camada
    const f64 sliceDur = static_cast<f64>(clipFrames) / fps;
    std::vector<aed::Word> sliceWords;
    for (const auto& w : words) {
        const f64 mid = (w.start + w.end) * 0.5 - srcStart;
        if (mid < 0.0 || mid >= sliceDur) continue;
        sliceWords.push_back({w.text, std::max(0.0, w.start - srcStart), std::min(sliceDur, w.end - srcStart)});
    }

    // --- 2. Áudio da fala ----------------------------------------------------
    progress(1, 0);
    std::vector<f32> voice;
    if (config_.mediaFactory && asset.has_audio() && !src.muted) {
        auto decoded = text::decode_mono16k(*config_.mediaFactory, mediaPath, autoEditCancelled, srcStart + sliceDur + 0.5);
        if (decoded.ok()) {
            const usize a = static_cast<usize>(std::llround(srcStart * aed::kAnalysisRate));
            const usize b = std::min(decoded->size(), static_cast<usize>(std::llround((srcStart + sliceDur) * aed::kAnalysisRate)));
            if (b > a) voice.assign(decoded->begin() + static_cast<std::ptrdiff_t>(a), decoded->begin() + static_cast<std::ptrdiff_t>(b));
        }
    }
    if (cancelled()) return error_json("cancelled", "cancelado");
    const aed::Envelope env = aed::analyze_envelope(voice.data(), voice.size());
    const aed::Speech speech = aed::detect_speech(env);
    const std::vector<aed::Word> aligned = env.size() ? aed::align_words(sliceWords, env) : sliceWords;

    // --- 3. Cortes e mapa do tempo -------------------------------------------
    progress(2, 0);
    const aed::CutPlan plan = aed::plan_cuts(aligned, sliceDur, opt, env.size() ? &env : nullptr, env.size() ? &speech : nullptr);
    // Trechos em quadros inteiros (bordas no quadro de vídeo).
    struct Piece { i64 src0, src1, edit0; };
    std::vector<Piece> pieces;
    std::vector<aed::Range> keep;
    i64 editFrames = 0;
    for (const auto& r : plan.keep) {
        const i64 a = std::clamp<i64>(std::llround(r.start * fps), 0, clipFrames);
        const i64 b = std::clamp<i64>(std::llround(r.end * fps), 0, clipFrames);
        if (b <= a) continue;
        if (!pieces.empty() && a <= pieces.back().src1) { pieces.back().src1 = std::max(pieces.back().src1, b); continue; }
        pieces.push_back({a, b, 0});
    }
    for (auto& p : pieces) { p.edit0 = editFrames; editFrames += p.src1 - p.src0; keep.push_back({p.src0 / fps, p.src1 / fps}); }
    if (pieces.empty()) return error_json("empty", "nada para manter");
    const aed::TimeMap map(keep);
    const std::vector<aed::Word> editWords = aed::map_words(aligned, map);
    const f64 editDur = map.duration();
    std::vector<aed::Range> clips;
    for (usize i = 0; i < keep.size(); ++i) clips.push_back({map.starts()[i], map.starts()[i] + keep[i].length()});

    // --- 4. Quadros: rostos e cor (miniaturas do motor + detector da plataforma) ---
    progress(3, 0);
    std::vector<aed::Face> faces;          // tempo da CÓPIA
    std::vector<aed::FrameColor> colorFrames;
    {
        const u64 assetKey = src.source.pack();
        bool thumbsWork = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
        auto grab = [&](f64 sliceSec, ThumbnailService::Image& img) {
            if (!thumbsWork) return false;
            const i64 us = static_cast<i64>(std::llround((srcStart + sliceSec) * 1e6));
            for (int tries = 0; tries < 60; ++tries) {
                {
                    std::lock_guard<std::mutex> lock(modelMutex_);
                    if (!project_ || session != projectSession_) return false;
                    if (thumbs_.video(assetKey, asset, us, 360, img) && img.width && img.height) return true;
                }
                if (cancelled() || std::chrono::steady_clock::now() > deadline) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            thumbsWork = false;   // a primeira que não chega desliga o resto (host sem decoder, por exemplo)
            return false;
        };
        std::vector<f64> times;
        if (hyper::face_detector_available() && (opt.zoom || opt.captions)) {
            const u32 count = static_cast<u32>(std::clamp(editDur, 1.0, 60.0));
            for (u32 i = 0; i < count; ++i) times.push_back(map.to_source((i + 0.5) * editDur / count));
        }
        u32 done = 0;
        for (f64 t : times) {
            if (cancelled()) break;
            ThumbnailService::Image img;
            if (!grab(t, img)) break;
            std::vector<u8> rgb(static_cast<usize>(img.width) * img.height * 3);
            for (usize i = 0, n = static_cast<usize>(img.width) * img.height; i < n; ++i) {
                rgb[i * 3] = img.rgba[i * 4]; rgb[i * 3 + 1] = img.rgba[i * 4 + 1]; rgb[i * 3 + 2] = img.rgba[i * 4 + 2];
            }
            hyper::FaceBox boxes[8];
            const u32 n = hyper::neural::detect_faces(rgb.data(), img.width, img.height, img.width * 3, boxes, 8);
            const f64 e = map.to_edit(t);
            for (u32 k = 0; k < n && e >= 0.0; ++k) {
                const auto& b = boxes[k];
                if (b.confidence < 0.3f || std::fabs(b.yawDegrees) > 60.0f || b.width <= 0 || b.height <= 0) continue;
                faces.push_back({e, b.x / img.width, b.y / img.height, b.width / img.width, b.height / img.height});
            }
            progress(3, static_cast<int>(100 * ++done / times.size()));
        }
        if (opt.color) {
            for (f64 f : {0.2, 0.5, 0.8}) {
                ThumbnailService::Image img;
                if (!grab(map.to_source(f * editDur), img)) break;
                colorFrames.push_back(aed::frame_color_stats(img.rgba.data(), img.width, img.height, img.width * 4));
            }
        }
    }
    if (cancelled()) return error_json("cancelled", "cancelado");

    // --- 5. Legendas (agrupadas no tempo da cópia) ---------------------------
    text::CaptionOptions capOpt;
    {
        const auto& templates = text::caption_templates();
        for (const auto& t : templates) if (std::string(t.id) == "highlight") text::apply_caption_template(t, capOpt);
        capOpt.maxWords = 3;
        capOpt.uppercase = true;
        const std::string custom = str(field(req, "captionOptions"));
        bool removeFillers = false;
        if (!custom.empty()) (void)presets::parse_caption_options(custom, capOpt, removeFillers);
        capOpt.highlight = true;
    }
    std::vector<text::CaptionWord> capWords;
    for (const auto& w : editWords) capWords.push_back({w.text, w.start, w.end});
    capWords = text::normalize_caption_words(capWords);
    const std::vector<text::CaptionGroup> groups = text::group_captions(capWords, capOpt);
    std::vector<f64> captionStarts;
    for (const auto& g : groups) captionStarts.push_back(g.start);

    // --- 6. Zoom, flash, luz e sons ------------------------------------------
    progress(4, 0);
    std::vector<aed::ZoomPiece> zooms;
    if (opt.zoom) zooms = aed::plan_zoom(clips, captionStarts, editWords, faces, 1);
    const std::vector<f64> splices = map.splices();
    const std::vector<f64> flashes = opt.zoom ? aed::plan_flashes(splices, editDur) : std::vector<f64>{};
    const std::vector<f64> leaks = opt.zoom && lightLeakAccess ? aed::plan_light_leaks(splices, editDur) : std::vector<f64>{};

    progress(5, 0);
    // Biblioteca de sons: arquivos do usuário por nome da pasta/arquivo e, nos
    // papéis sem arquivo, os sons do próprio Aurea (sintetizados uma vez).
    struct RoleFile { std::string path; SoundInfo info; };
    std::vector<RoleFile> roleFiles[aed::kSfxRoleCount];
    std::vector<aed::SfxEvent> sfx;
    const std::string docs = config_.documentsDirectory;
    if (opt.sounds && config_.mediaFactory) {
        for (const auto& item : library) {
            const aed::SfxRole role = aed::sfx_role_of(item.category, item.name);
            if (role == aed::SfxRole::Count || (item.duration > 0 && item.duration > 4.0 && role != aed::SfxRole::Riser)) continue;
            auto& files = roleFiles[static_cast<u32>(role)];
            if (files.size() >= 4) continue;
            SoundInfo info;
            {
                std::lock_guard<std::mutex> lock(g_soundMutex);
                if (auto it = g_soundCache.find(item.path); it != g_soundCache.end()) info = it->second;
            }
            if (!info.ok) {
                auto x = text::decode_mono16k(*config_.mediaFactory, item.path, autoEditCancelled, 10.0);
                if (!x.ok()) continue;
                info = measure_sound(*x, aed::kAnalysisRate);
                std::lock_guard<std::mutex> lock(g_soundMutex);
                g_soundCache[item.path] = info;
            }
            if (info.ok) files.push_back({item.path, info});
        }
        if (!docs.empty()) {
            std::error_code ec;
            const std::string dir = docs + "/autoedit/sfx";
            std::filesystem::create_directories(dir, ec);
            for (u32 r = 0; r < aed::kSfxRoleCount; ++r) {
                if (!roleFiles[r].empty()) continue;
                for (u32 v = 0; v < aed::kGeneratedVariants; ++v) {
                    const auto role = static_cast<aed::SfxRole>(r);
                    const std::string path = dir + "/" + aed::sfx_role_name(role) + "_" + std::to_string(v + 1) + ".wav";
                    const auto x = aed::synth_sfx(role, v, 48000);
                    if (!std::filesystem::exists(path, ec) && !write_wav48(path, x)) continue;
                    roleFiles[r].push_back({path, measure_sound(x, 48000)});
                }
            }
        }
        aed::SfxInputs in;
        in.zooms = zooms; in.flashes = flashes; in.splices = splices;
        for (u32 r = 0; r < aed::kSfxRoleCount; ++r) {
            in.variants[r] = static_cast<u32>(roleFiles[r].size());
            in.peakOffset[r] = roleFiles[r].empty() ? 0.0 : roleFiles[r][0].info.peakAt;
        }
        for (const auto& g : groups) {
            std::vector<f64> starts;
            for (u32 k = 0; k < g.count; ++k) starts.push_back(capWords[g.first + k].start);
            in.captionWordStarts.push_back(std::move(starts));
        }
        sfx = aed::plan_sfx(in);
    }
    if (cancelled()) return error_json("cancelled", "cancelado");

    // --- 7. Música -----------------------------------------------------------
    progress(6, 0);
    std::vector<aed::Range> speechEdit;
    for (const auto& w : editWords) {
        if (!speechEdit.empty() && w.start - speechEdit.back().end < 0.3) speechEdit.back().end = std::max(speechEdit.back().end, w.end);
        else speechEdit.push_back({w.start, w.end});
    }
    std::string musicPath, musicName;
    f64 musicEntry = 0.0, musicLength = 0.0;
    f32 duckDb = 8.0f;
    std::vector<f32> musicPcm;
    const std::string recentFile = docs.empty() ? std::string{} : docs + "/autoedit/recent_music.txt";
    if (opt.music && config_.mediaFactory) {
        std::vector<std::string> recent;
        if (!recentFile.empty()) { std::ifstream in(recentFile); for (std::string line; std::getline(in, line);) if (!line.empty()) recent.push_back(line); }
        std::vector<const LibraryItem*> cands;
        for (const auto& item : library) {
            if (cands.size() >= 8) break;
            const bool musicCat = aed::music_like(item.category, item.name);
            if (!musicCat && !(item.duration >= 30.0 && aed::sfx_role_of(item.category, item.name) == aed::SfxRole::Count)) continue;
            cands.push_back(&item);
        }
        std::vector<aed::MusicTrack> tracks;
        std::vector<std::vector<f32>> pcms;
        std::vector<f64> entries;
        for (const auto* item : cands) {
            if (cancelled()) break;
            auto x = text::decode_mono16k(*config_.mediaFactory, item->path, autoEditCancelled, 600.0);
            if (!x.ok() || x->size() < aed::kAnalysisRate * 5) { tracks.push_back({}); pcms.emplace_back(); entries.push_back(0); continue; }
            const aed::Envelope me = aed::analyze_envelope(x->data(), x->size());
            aed::MusicTrack t;
            t.duration = static_cast<f64>(x->size()) / aed::kAnalysisRate;
            f64 power = 0.0; usize n = 0;
            for (f32 d : me.db) if (d > -120.0f) { power += std::pow(10.0, d / 10.0); ++n; }
            t.energyDb = n ? static_cast<f32>(10.0 * std::log10(power / n + 1e-18)) : -120.0f;
            t.recent = std::find(recent.begin(), recent.end(), item->path) != recent.end();
            t.music = aed::music_like(item->category, item->name);
            t.bpm = item->bpm;
            const f64 entry = aed::music_entry(me);
            if (t.bpm <= 0.0f) {
                // BPM medido: até 60 s depois da entrada, levado a 48 kHz.
                const usize a = static_cast<usize>(entry * aed::kAnalysisRate);
                const usize b = std::min(x->size(), a + 60 * aed::kAnalysisRate);
                if (b > a + 6 * aed::kAnalysisRate) {
                    std::vector<f32> up((b - a) * 3);
                    for (usize i = 0; i + 1 < b - a; ++i)
                        for (u32 k = 0; k < 3; ++k) up[i * 3 + k] = (*x)[a + i] + ((*x)[a + i + 1] - (*x)[a + i]) * (k / 3.0f);
                    const auto beats = audio::detect_beats(up.data(), up.size());
                    if (beats.bpm >= 60.0 && beats.bpm <= 200.0) t.bpm = static_cast<f32>(beats.bpm);
                }
            }
            t.duration -= entry;
            tracks.push_back(t);
            entries.push_back(entry);
            pcms.push_back(std::move(*x));
        }
        const f64 wps = editDur > 0 ? static_cast<f64>(editWords.size()) / editDur : 2.0;
        const i32 pick = aed::choose_music(tracks, wps, editDur);
        if (pick >= 0) {
            musicPath = cands[static_cast<usize>(pick)]->path;
            musicName = cands[static_cast<usize>(pick)]->name;
            musicEntry = entries[static_cast<usize>(pick)];
            musicLength = std::min(editDur, tracks[static_cast<usize>(pick)].duration);
            const auto& m = pcms[static_cast<usize>(pick)];
            const usize a = static_cast<usize>(musicEntry * aed::kAnalysisRate);
            musicPcm.assign(m.begin() + static_cast<std::ptrdiff_t>(std::min(a, m.size())), m.end());
        }
    }

    // --- 8. Voz --------------------------------------------------------------
    progress(7, 0);
    f32 voiceGain = 0.0f, compThreshold = -26.0f;
    f64 lufs = -70.0;
    std::vector<aed::GainKey> voiceEnv;   // segundos do trecho (mídia − início da camada)
    aed::Dynamics dynamics;
    std::vector<f32> editedVoice;
    for (const auto& r : keep) {
        const usize a = std::min(voice.size(), static_cast<usize>(std::llround(r.start * aed::kAnalysisRate)));
        const usize b = std::min(voice.size(), static_cast<usize>(std::llround(r.end * aed::kAnalysisRate)));
        editedVoice.insert(editedVoice.end(), voice.begin() + static_cast<std::ptrdiff_t>(a), voice.begin() + static_cast<std::ptrdiff_t>(b));
    }
    if (opt.voice && !editedVoice.empty()) {
        lufs = aed::integrated_lufs(editedVoice.data(), editedVoice.size(), aed::kAnalysisRate);
        voiceGain = lufs > -69.0 ? aed::voice_gain_db(lufs) : 0.0f;
        std::vector<f32> speechDb;
        for (usize i = 0; i < env.size(); ++i) if (speech.active.size() > i && speech.active[i]) speechDb.push_back(env.db[i]);
        compThreshold = aed::compressor_threshold_db(aed::percentile(speechDb, 95.0f), voiceGain, !speechDb.empty());
        // Nível por frase (frases da transcrição que ficaram).
        std::vector<aed::Range> phrases;
        std::vector<f32> levels;
        for (const auto& s : aed::split_sentences(aligned)) {
            const aed::Range r{aligned[s.first].start, aligned[s.first + s.count - 1].end};
            if (map.to_edit((r.start + r.end) * 0.5) < 0.0) continue;
            f64 power = 0.0; usize n = 0;
            for (i64 f = static_cast<i64>(r.start / aed::kFrameSec); f < static_cast<i64>(r.end / aed::kFrameSec); ++f) {
                if (f < 0 || static_cast<usize>(f) >= env.size() || !speech.active[static_cast<usize>(f)]) continue;
                power += std::pow(10.0, env.db[static_cast<usize>(f)] / 10.0); ++n;
            }
            if (!n) continue;
            phrases.push_back(r);
            levels.push_back(static_cast<f32>(10.0 * std::log10(power / n + 1e-18)));
        }
        const std::vector<aed::GainKey> levelKeys = aed::voice_keys(phrases, aed::phrase_gains(levels), speech.breaths);
        // Compressor 2,5:1 e limitador −3 dBFS (análise) + nível por frase + respirações:
        // uma envoltória de volume por quadro, simplificada a 0,75 dB.
        dynamics = aed::voice_dynamics(env, speech.active, voiceGain, compThreshold);
        std::vector<f32> total(env.size());
        for (usize f = 0; f < env.size(); ++f) total[f] = aed::gain_at(levelKeys, static_cast<f64>(f) * aed::kFrameSec) + dynamics.gainDb[f];
        voiceEnv = aed::simplify_gain(total, aed::kFrameSec, 0.75f);
    }
    if (!musicPcm.empty()) duckDb = aed::duck_reduction_for_peak(editedVoice, voiceGain, musicPcm, speechEdit, editDur, 8.0f);
    const std::vector<aed::GainKey> duck = musicPath.empty() ? std::vector<aed::GainKey>{} : aed::ducking_keys(speechEdit, musicLength, duckDb);

    // --- 9. Cor --------------------------------------------------------------
    progress(8, 0);
    const bool graded = aed::already_graded(colorFrames);
    const bool applyColor = opt.color && !graded;

    // Arquivos de som usados: probe fora do lock.
    struct Probed { MediaProbe probe; bool ok = false; std::string stored; };
    std::map<std::string, Probed> probes;
    auto probe_file = [&](const std::string& path) {
        if (probes.count(path) || !config_.mediaFactory) return;
        Probed p;
        // Arquivo fora da pasta do app: copia para dentro (o projeto não perde o som).
        std::string usable = path;
        if (!docs.empty() && path.rfind(docs, 0) != 0) {
            std::error_code ec;
            const std::string dir = docs + "/autoedit/media";
            std::filesystem::create_directories(dir, ec);
            // Prefixo = hash do caminho: dois arquivos com o mesmo nome não se confundem.
            const std::string dest = dir + "/" + std::to_string(static_cast<u32>(effect_type_id(path))) + "_"
                                   + std::filesystem::path(path).filename().string();
            if (std::filesystem::exists(dest, ec) || std::filesystem::copy_file(path, dest, ec)) usable = dest;
        }
        p.ok = config_.mediaFactory->probe(usable.c_str(), p.probe) && p.probe.hasAudio && p.probe.audioSampleRate > 0
            && p.probe.audioDurationUs > 0;
        p.stored = usable;
        probes[path] = std::move(p);
    };
    if (!musicPath.empty()) probe_file(musicPath);
    for (const auto& e : sfx) {
        const auto& files = roleFiles[static_cast<u32>(e.role)];
        if (!files.empty()) probe_file(files[e.variant % files.size()].path);
    }
    if (cancelled()) return error_json("cancelled", "cancelado");

    // --- 10. Montagem (um passo de desfazer) ---------------------------------
    progress(9, 0);
    const bool fontReady = text::default_font() != nullptr;
    u32 zoomCount = 0, sfxCount = 0, captionCount = 0, flashCount = 0, leakCount = 0;
    u64 groupLayer = 0;
    std::string label;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        if (!project_ || session != projectSession_ || compositionId != project_->timeline().current()
            || state_ == EngineState::ShuttingDown)
            return error_json("changed", "projeto alterado durante a edicao");
        Composition* comp = current_composition();
        Layer* orig = comp ? comp->layer(LayerId::unpack(sourceLayer)) : nullptr;
        if (!orig || orig->source != src.source || orig->start != src.start || orig->end != src.end || orig->offset != src.offset)
            return error_json("changed", "o clipe mudou durante a edicao");
        Timeline& tl = project_->timeline();
        const CompositionId parentId = tl.current();
        history_.before_mutation(*comp, parentId, "edicao automatica");
        bump_model_revision_locked();
        label = baseName + " \xE2\x80\x94 edi\xC3\xA7\xC3\xA3o autom\xC3\xA1tica";
        const CompositionId cid = tl.create_composition(label, comp->width(), comp->height(), comp->fps());
        Composition* child = cid.valid() ? tl.composition(cid) : nullptr;
        comp = tl.composition(parentId);
        if (!child || !comp) return error_json("memory", "composicao nao criada");
        child->set_duration(FrameIndex{editFrames});
        child->set_transparent_background(true);
        child->set_nesting_depth(comp->nesting_depth() + 1);
        child->environment() = comp->environment();
        child->environment().showBackground = false;
        child->motion_blur() = comp->motion_blur();
        const f32 W = static_cast<f32>(compW), H = static_cast<f32>(compH);
        auto frame_of = [&](f64 sec) { return static_cast<i64>(std::llround(sec * fps)); };
        auto audio_asset = [&](const std::string& path, const std::string& name) -> AssetId {
            auto it = probes.find(path);
            if (it == probes.end() || !it->second.ok) return AssetId{};
            Asset a;
            a.kind = AssetKind::Audio;
            a.name = name;
            a.sourcePath = store_asset_path(it->second.stored);
            a.originalFilename = std::filesystem::path(path).filename().string();
            a.audio.sampleRate = it->second.probe.audioSampleRate;
            a.audio.channels = it->second.probe.audioChannels;
            a.audio.sampleCount = FrameIndex{it->second.probe.audioDurationUs * static_cast<i64>(it->second.probe.audioSampleRate) / 1'000'000};
            a.timebaseFps = fps;
            a.duration = FrameIndex{std::max<i64>(1, static_cast<i64>(std::ceil(static_cast<f64>(it->second.probe.audioDurationUs) * fps / 1e6 - 1e-6)))};
            return project_->add_asset(std::move(a));
        };
        std::map<std::string, AssetId> assetOf;
        auto asset_for = [&](const std::string& path, const std::string& name) {
            auto it = assetOf.find(path);
            if (it != assetOf.end()) return it->second;
            const AssetId id = audio_asset(path, name);
            assetOf[path] = id;
            return id;
        };

        // Vídeo: um pedaço por trecho mantido (mudo; o som vai nos pedaços de áudio).
        std::vector<LayerId> videoIds;
        const bool ownZoomTracks = src.tracks.find(TrackProperty::ScaleX) || src.tracks.find(TrackProperty::ScaleY)
                                || src.tracks.find(TrackProperty::PositionX) || src.tracks.find(TrackProperty::PositionY);
        for (usize i = 0; i < pieces.size(); ++i) {
            const LayerId id = child->add_layer(LayerKind::Video, src.name);
            Layer* v = child->layer(id);
            if (!v) return error_json("memory", "camada nao criada");
            *v = src;
            v->name = src.name + " " + std::to_string(i + 1);
            v->start = FrameIndex{pieces[i].edit0};
            v->end = FrameIndex{pieces[i].edit0 + (pieces[i].src1 - pieces[i].src0)};
            v->offset = FrameIndex{src.offset.value + pieces[i].src0};
            v->muted = true; v->audioUnlinked = true;
            v->visible = true; v->locked = false; v->solo = false;
            v->parent = LayerId{}; v->hasParentBasis = false;
            v->matteSource = LayerId{}; v->matteMode = MatteMode::None; v->clipToBelow = false;
            v->trackId = 0; v->magneticTrack = false;
            v->captions.clear();
            videoIds.push_back(id);
        }
        // Zoom: escala e posição com Easy Ease, presa para nunca mostrar a borda.
        if (!ownZoomTracks && asset.video.width && asset.video.height && src.transform.rotation.z == 0.0f) {
            const f32 vw = static_cast<f32>(asset.video.width), vh = static_cast<f32>(asset.video.height);
            const Vec3 P = src.transform.position, S = src.transform.scale, A = src.transform.anchor;
            const f32 ext[4] = {S.x * A.x, S.x * (vw - A.x), S.y * A.y, S.y * (vh - A.y)};
            for (const auto& z : zooms) {
                if (z.keys.empty()) continue;
                usize host = 0;
                for (usize i = 0; i < pieces.size(); ++i)
                    if (z.range.start + 1e-6 >= static_cast<f64>(pieces[i].edit0) / fps) host = i;
                Layer* v = child->layer(videoIds[host]);
                if (!v) continue;
                Track& sx = v->tracks.get_or_create(TrackProperty::ScaleX);
                Track& sy = v->tracks.get_or_create(TrackProperty::ScaleY);
                Track& px = v->tracks.get_or_create(TrackProperty::PositionX);
                Track& py = v->tracks.get_or_create(TrackProperty::PositionY);
                for (const auto& k : z.keys) {
                    const i64 tf = std::clamp<i64>(frame_of(k.time), v->start.value, v->end.value - 1);
                    const i64 local = tf - v->start.value + v->offset.value;
                    const f32 fx = P.x - ext[0] + k.ax * (ext[0] + ext[1]);
                    const f32 fy = P.y - ext[2] + k.ay * (ext[2] + ext[3]);
                    const aed::ZoomPose pose = aed::zoom_position(P.x, P.y, ext, k.scale, fx, fy, W, H);
                    set_key(sx, local, S.x * k.scale, k.ease);
                    set_key(sy, local, S.y * k.scale, k.ease);
                    set_key(px, local, pose.x, k.ease);
                    set_key(py, local, pose.y, k.ease);
                }
                if (z.type != aed::ZoomType::None) ++zoomCount;
            }
        }
        // Áudio da fala: pedaços com 1 quadro a mais de cada lado e fades de 2
        // quadros (potência constante no mixer) = crossfade de 2 quadros na emenda.
        const Track* srcVolume = src.tracks.find(TrackProperty::AudioVolume);
        const bool volumeAnimated = srcVolume && srcVolume->animated();
        const f32 baseVolume = srcVolume ? srcVolume->sample(FrameIndex{src.offset.value}) : 1.0f;
        if (asset.has_audio() && !src.muted) {
            for (usize i = 0; i < pieces.size(); ++i) {
                const bool head = i > 0, tail = i + 1 < pieces.size();
                const LayerId id = child->add_layer(LayerKind::Audio, src.name + " (voz " + std::to_string(i + 1) + ")");
                Layer* a = child->layer(id);
                if (!a) return error_json("memory", "camada nao criada");
                a->source = src.source;
                a->start = FrameIndex{pieces[i].edit0 - (head ? 1 : 0)};
                a->end = FrameIndex{pieces[i].edit0 + (pieces[i].src1 - pieces[i].src0) + (tail ? 1 : 0)};
                a->offset = FrameIndex{src.offset.value + pieces[i].src0 - (head ? 1 : 0)};
                a->gain = src.gain * (opt.voice ? std::pow(10.0f, voiceGain / 20.0f) : 1.0f);
                a->pan = src.pan;
                a->fadeIn = FrameIndex{head ? 2 : 0};
                a->fadeOut = FrameIndex{tail ? 2 : 0};
                if (srcVolume) a->tracks.get_or_create(TrackProperty::AudioVolume) = *srcVolume;
                if (opt.voice && !volumeAnimated && !voiceEnv.empty()) {
                    Track& vt = a->tracks.get_or_create(TrackProperty::AudioVolume);
                    const i64 l0 = a->offset.value, l1 = a->offset.value + (a->end.value - a->start.value);
                    auto put = [&](f64 sliceSec) {
                        const i64 local = std::clamp<i64>(src.offset.value + frame_of(sliceSec), l0, l1);
                        vt.set(FrameIndex{local}, baseVolume * std::pow(10.0f, aed::gain_at(voiceEnv, sliceSec) / 20.0f), Interpolation::Linear);
                    };
                    put(static_cast<f64>(l0 - src.offset.value) / fps);
                    for (const auto& k : voiceEnv) {
                        const i64 local = src.offset.value + frame_of(k.time);
                        if (local > l0 && local < l1) put(k.time);
                    }
                    put(static_cast<f64>(l1 - src.offset.value) / fps);
                }
                if (opt.voice) {
                    // Passa-alta 70 Hz (o efeito de áudio que o app já tem).
                    const EffectTypeId type = effect_type_id(audio::fx_keys::kHighLowPass);
                    if (const ParameterRegistry* params = effectRegistry_.params(type)) {
                        EffectInstance e;
                        e.id = a->alloc_effect_id();
                        e.type = type;
                        initialize_instance(e, *params);
                        for (u32 p = 0; p < params->count() && p < e.params.size(); ++p) {
                            const std::string pid = params->at(p).id;
                            if (pid == "filter") e.params[p].constant = ParamValue::scalar(0.0f);
                            else if (pid == "cutoff") e.params[p].constant = ParamValue::scalar(70.0f);
                            else if (pid == "dry_out") e.params[p].constant = ParamValue::scalar(0.0f);
                            else if (pid == "wet_out") e.params[p].constant = ParamValue::scalar(100.0f);
                        }
                        a->effects.push_back(std::move(e));
                    }
                }
            }
        }
        auto add_effect = [&](Layer& l, const char* key, std::initializer_list<std::pair<const char*, f32>> values) {
            const EffectTypeId type = effectRegistry_.find_key(key);
            const ParameterRegistry* params = effectRegistry_.params(type);
            if (!params) return false;
            EffectInstance e;
            e.id = l.alloc_effect_id();
            e.type = type;
            initialize_instance(e, *params);
            for (u32 p = 0; p < params->count() && p < e.params.size(); ++p)
                for (const auto& [id, v] : values) if (std::string(params->at(p).id) == id) e.params[p].constant = ParamValue::scalar(v);
            l.effects.push_back(std::move(e));
            return true;
        };
        auto add_adjustment = [&](const char* name, i64 f0, i64 f1) -> Layer* {
            const LayerId id = child->add_layer(LayerKind::Null, name);
            Layer* l = child->layer(id);
            if (!l) return nullptr;
            l->adjustment = true;
            l->start = FrameIndex{std::max<i64>(0, f0)};
            l->end = FrameIndex{std::min(editFrames, std::max(f1, f0 + 1))};
            l->transform.anchor = Vec3{50.0f, 50.0f, 0.0f};
            l->transform.position = Vec3{W * 0.5f, H * 0.5f, 0.0f};
            return l;
        };
        // Cor: "Correção de cor" numa camada de ajuste sobre o vídeo.
        bool colorApplied = false;
        if (applyColor) {
            if (Layer* l = add_adjustment("Cor", 0, editFrames)) {
                const aed::ColorGrade g;
                colorApplied = add_effect(*l, effect_keys::kColorGrade,
                                          {{"exposure", g.exposure}, {"contrast", g.contrast}, {"saturation", g.saturation}});
            }
        }
        // Luz (light leak, só quem tem acesso): 1 s centrado no corte, mesclagem Tela.
        for (f64 t : leaks) {
            Layer* l = add_adjustment("Luz", frame_of(t - 0.5), frame_of(t + 0.5));
            if (!l || !add_effect(*l, "aurea.light.light_leak", {{"mode", 0.0f}, {"seed", static_cast<f32>(leakCount + 1) * 0.37f}})) continue;
            Track& op = l->tracks.get_or_create(TrackProperty::Opacity);
            const i64 len = l->end.value - l->start.value;
            set_key(op, 0, 0.0f, true); set_key(op, len / 2, 1.0f, true); set_key(op, std::max<i64>(len - 1, len / 2 + 1), 0.0f, true);
            ++leakCount;
        }
        // Flash branco nas emendas.
        for (f64 t : flashes) {
            const auto keys = aed::flash_keys(t);
            const LayerId id = child->add_layer(LayerKind::Shape, "Flash");
            Layer* l = child->layer(id);
            if (!l) continue;
            l->shape.shapeType = 0;
            l->shape.bounds = Rect{0.0f, 0.0f, W, H};
            l->shape.fillColor = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
            l->shape.filled = true;
            l->transform.anchor = Vec3{W * 0.5f, H * 0.5f, 0.0f};
            l->transform.position = Vec3{W * 0.5f, H * 0.5f, 0.0f};
            l->start = FrameIndex{std::max<i64>(0, frame_of(keys.front().time))};
            l->end = FrameIndex{std::min(editFrames, frame_of(keys.back().time) + 1)};
            if (l->end.value <= l->start.value) { child->remove_layer(id); continue; }
            Track& op = l->tracks.get_or_create(TrackProperty::Opacity);
            for (const auto& k : keys) {
                const i64 local = std::clamp<i64>(frame_of(k.time), l->start.value, l->end.value) - l->start.value;
                op.set(FrameIndex{local}, k.opacity, Interpolation::Linear);
            }
            ++flashCount;
        }
        // Legendas: a faixa das AutoCaptions, já no tempo da cópia, fora dos rostos.
        if (opt.captions && fontReady && !groups.empty()) {
            const LayerId id = child->add_layer(LayerKind::Text, default_name(DefaultName::Captions));
            Layer* track = child->layer(id);
            if (track) {
                const u32 shortSide = std::min(compW, compH);
                track->start = FrameIndex{0};
                track->end = FrameIndex{editFrames};
                track->captionOptions = capOpt;
                std::string& fontPath = track->captionOptions.fontPath;
                if (!fontPath.empty() && fontPath.rfind("docs:", 0) != 0) fontPath = store_asset_path(fontPath);
                text::apply_caption_style(track->captionOptions, shortSide, track->text, track->tracks, {}, 0);
                const bool vertical = compH * 9 >= compW * 15;   // 9:16 (ou mais alto)
                const f32 boxH = capOpt.sizeFrac * static_cast<f32>(shortSide) * capOpt.lineSpacing * static_cast<f32>(capOpt.maxLines) / H;
                std::vector<aed::CaptionBlock> blocks;
                for (const auto& g : groups) {
                    u32 shot = 0;
                    for (usize i = 0; i < clips.size(); ++i) if (g.start + 1e-6 >= clips[i].start) shot = static_cast<u32>(i);
                    blocks.push_back({{g.start, g.end}, shot});
                }
                const std::vector<f32> ys = aed::caption_positions(blocks, faces, capOpt.posY, boxH, vertical);
                track->transform.position = Vec3{W * 0.5f, H * (ys.empty() ? capOpt.posY : ys.front()), 0};
                u32 made = 0;
                for (usize g = 0; g < groups.size(); ++g) {
                    const auto& cg = groups[g];
                    const i64 a = std::clamp<i64>(frame_of(cg.start), 0, editFrames - 1);
                    i64 b = std::max(frame_of(cg.end) + std::min<i64>(8, std::llround(fps * 0.25)), a + 1);
                    if (g + 1 < groups.size()) {
                        const i64 n = frame_of(groups[g + 1].start);
                        if (n < b || n - b < std::llround(fps * 0.3)) b = std::max(n, a + 1);
                    }
                    b = std::min(b, editFrames);
                    if (b <= a) continue;
                    text::CaptionSegment seg;
                    seg.id = ++made; seg.start = a; seg.end = b; seg.text = cg.text;
                    for (u32 w = 0; w < cg.count; ++w) {
                        const auto& in = capWords[cg.first + w];
                        const i64 begin = std::clamp<i64>(frame_of(in.start), a, b - 1);
                        const i64 finish = std::clamp<i64>(frame_of(in.end), begin + 1, b);
                        seg.words.push_back({in.text, begin, finish});
                    }
                    track->captions.push_back(std::move(seg));
                }
                text::apply_caption_gap(track->captions, track->captionOptions.gapFrames);
                // Posição estável no trecho: keys "segurar" na troca de altura.
                bool moves = false;
                for (f32 y : ys) moves = moves || std::fabs(y - ys.front()) > 1e-4f;
                if (moves) {
                    Track& py = track->tracks.get_or_create(TrackProperty::PositionY);
                    f32 last = -1.0f;
                    for (usize g = 0; g < groups.size() && g < ys.size(); ++g) {
                        if (std::fabs(ys[g] - last) < 1e-4f) continue;
                        const i64 at = g == 0 ? 0 : std::clamp<i64>(frame_of(groups[g].start), 0, editFrames - 1);
                        set_key(py, at, H * ys[g], false);
                        last = ys[g];
                    }
                }
                if (track->captions.empty()) child->remove_layer(id);
                else { track->text.content = track->captions.front().text; recenter_text(*track); captionCount = made; }
            }
        }
        // Música: entrada medida, −15 dB, fades 1 s/3 s, sem laço, ducking.
        if (!musicPath.empty()) {
            const AssetId mid = asset_for(musicPath, musicName.empty() ? std::string("Musica") : musicName);
            if (mid.valid()) {
                const LayerId id = child->add_layer(LayerKind::Audio, musicName.empty() ? std::string("Musica") : musicName);
                if (Layer* m = child->layer(id)) {
                    m->source = mid;
                    m->start = FrameIndex{0};
                    m->end = FrameIndex{std::clamp<i64>(frame_of(musicLength), 1, editFrames)};
                    m->offset = FrameIndex{frame_of(musicEntry)};
                    m->gain = std::pow(10.0f, aed::kMusicBaseDb / 20.0f);
                    m->fadeIn = FrameIndex{frame_of(aed::kMusicFadeIn)};
                    m->fadeOut = FrameIndex{frame_of(aed::kMusicFadeOut)};
                    if (!duck.empty()) {
                        Track& vt = m->tracks.get_or_create(TrackProperty::AudioVolume);
                        // Rampas lineares em dB: um key no meio de cada rampa.
                        for (usize k = 0; k < duck.size(); ++k) {
                            auto put = [&](f64 t) {
                                vt.set(FrameIndex{m->offset.value + std::clamp<i64>(frame_of(t), 0, m->end.value - m->start.value)},
                                       std::pow(10.0f, aed::gain_at(duck, t) / 20.0f), Interpolation::Linear);
                            };
                            put(duck[k].time);
                            if (k + 1 < duck.size() && duck[k + 1].db != duck[k].db) put((duck[k].time + duck[k + 1].time) * 0.5);
                        }
                    }
                }
            } else musicPath.clear();
        }
        // Sons.
        for (const auto& e : sfx) {
            const auto& files = roleFiles[static_cast<u32>(e.role)];
            if (files.empty()) continue;
            const RoleFile& f = files[e.variant % files.size()];
            const AssetId sid = asset_for(f.path, std::string("Som ") + aed::sfx_role_name(e.role));
            if (!sid.valid()) continue;
            const LayerId id = child->add_layer(LayerKind::Audio, std::string("Som: ") + aed::sfx_role_name(e.role));
            Layer* s = child->layer(id);
            if (!s) continue;
            s->source = sid;
            s->start = FrameIndex{std::clamp<i64>(frame_of(e.time), 0, editFrames - 1)};
            f64 length = f.info.duration;
            if (e.role == aed::SfxRole::Sub) { length = std::min(length, 0.9); s->fadeOut = FrameIndex{frame_of(0.35)}; }
            s->end = FrameIndex{std::min(editFrames, s->start.value + std::max<i64>(1, frame_of(length)))};
            s->offset = FrameIndex{0};
            s->gain = std::pow(10.0f, (aed::sfx_gain_db(e.role, f.info.peakDb) + e.gainDb) / 20.0f);
            ++sfxCount;
        }
        child->rebuild_draw_order();
        // A camada que mostra a edição, no lugar da original (que fica desligada e muda).
        const LayerId pl = comp->add_layer(LayerKind::Composition, label);
        Layer* p = comp->layer(pl);
        orig = comp->layer(LayerId::unpack(sourceLayer));
        if (!p || !orig) return error_json("memory", "camada nao criada");
        p->nested.composition = cid;
        p->start = src.start;
        p->end = FrameIndex{src.start.value + editFrames};
        p->offset = FrameIndex{0};
        const Vec3 center{W * 0.5f, H * 0.5f, 0.0f};
        p->transform.anchor = center;
        p->transform.position = center;
        orig->visible = false;
        orig->muted = true;
        comp->rebuild_draw_order();
        selection_.assign(1, pl.pack());
        groupLayer = pl.pack();
        configure_playback_locked(comp);
        project_->mark_dirty();
        request_render();
        (void)colorApplied;
    }
    // Músicas usadas por último (as 5 mais recentes saem do sorteio na próxima).
    if (!musicPath.empty() && !recentFile.empty()) {
        std::vector<std::string> recent{musicPath};
        { std::ifstream in(recentFile); for (std::string line; std::getline(in, line);) if (!line.empty() && line != musicPath && recent.size() < 5) recent.push_back(line); }
        std::ofstream out(recentFile, std::ios::trunc);
        for (const auto& r : recent) out << r << '\n';
    }
    progress(10, 99);

    // --- Relatório -----------------------------------------------------------
    std::map<std::string, u32> reasons;
    for (const auto& c : plan.cuts) ++reasons[aed::cut_reason_name(c.reason)];
    json::Writer w;
    w.begin_object().key("ok").value(true).key("layer").value(static_cast<i64>(groupLayer)).key("composition").value(label);
    w.key("sourceSeconds").value(sliceDur).key("editSeconds").value(editDur).key("aborted").value(plan.aborted);
    w.key("cuts").value(static_cast<u32>(plan.cuts.size())).key("reasons").begin_object();
    for (const auto& [k, v] : reasons) w.key(k).value(v);
    w.end_object();
    w.key("pieces").value(static_cast<u32>(pieces.size())).key("words").value(static_cast<u32>(sliceWords.size()));
    w.key("captionWords").value(static_cast<u32>(editWords.size()));
    w.key("zooms").value(zoomCount).key("faces").value(static_cast<u32>(faces.size()));
    w.key("flashes").value(flashCount).key("lightLeaks").value(leakCount).key("sounds").value(sfxCount);
    w.key("music").value(musicPath.empty() ? std::string{} : (musicName.empty() ? musicPath : musicName));
    w.key("duckDb").value(duckDb).key("lufs").value(lufs).key("voiceGainDb").value(voiceGain).key("compressorDb").value(compThreshold)
     .key("makeupDb").value(dynamics.makeupDb).key("maxReductionDb").value(dynamics.maxReductionDb);
    w.key("color").value(!opt.color ? "off" : (graded ? "already_graded" : "applied"));
    w.key("captions").value(captionCount);
    w.key("skipped").begin_array();
    w.value("palavra_atras").value("gancho").value("broll").value("jl").value("riser").value("sub");
    if (!lightLeakAccess) w.value("light_leak");
    if (!fontReady) w.value("legendas_sem_fonte");
    w.end_array().end_object();
    autoEditProgress.store(1000);
    return w.str();
}

} // namespace aurea
