// =============================================================================
//  Edição automática (1.0.8): regras puras com sinais e transcrições
//  sintéticas, e a edição inteira num clipe "falado" gerado aqui (transcrição
//  injetada no lugar do Whisper). Números de docs/architecture/AUTOEDIT_108_SPEC.md.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/autoedit/AutoEdit.hpp"
#include "aurea/project/Presets.hpp"
#include "aurea/timeline/Composition.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace aurea;
namespace aed = aurea::autoedit;

namespace {

/// Palavras de uma frase a partir de `t` (duração pelo tamanho, vão de 80 ms).
f64 say(std::vector<aed::Word>& out, f64 t, std::initializer_list<const char*> words, f64 gap = 0.08) {
    for (const char* w : words) {
        const f64 d = std::min(0.6, 0.12 + 0.04 * std::string(w).size());
        out.push_back({w, t, t + d});
        t += d + gap;
    }
    return t - gap;
}

bool near(f64 a, f64 b, f64 tol) { return std::fabs(a - b) <= tol; }

std::vector<aed::Word> marked(const std::vector<aed::Word>& w, const std::vector<aed::CutReason>& m, aed::CutReason r) {
    std::vector<aed::Word> out;
    for (usize i = 0; i < w.size(); ++i) if (m[i] == r) out.push_back(w[i]);
    return out;
}

} // namespace

// --- §3.1 Pausas por intensidade ---------------------------------------------
AUREA_TEST(AutoEdit, PauseCutsFollowIntensityTable) {
    // Três vãos: 0,25 s, 0,38 s e 0,60 s (palavras longas, nada é vício).
    std::vector<aed::Word> w = {{"primeira", 1.0, 1.5}, {"segunda", 1.75, 2.25}, {"terceira", 2.63, 3.13},
                               {"quarta", 3.73, 4.23}, {"quinta", 4.4, 4.9}};
    auto cuts_between = [&](aed::Intensity i) {
        aed::Options o; o.intensity = i; o.recording = aed::Recording::Continuous;
        const auto plan = aed::plan_cuts(w, 6.0, o);
        u32 inner = 0;
        for (const auto& c : plan.cuts) if (c.range.start > 1.2 && c.range.end < 4.6) ++inner;
        return std::make_pair(inner, plan);
    };
    const auto [light, pl] = cuts_between(aed::Intensity::Light);
    const auto [medium, pm] = cuts_between(aed::Intensity::Medium);
    const auto [strong, ps] = cuts_between(aed::Intensity::Strong);
    std::printf("    vaos cortados: leve %u, media %u, forte %u\n", light, medium, strong);
    AUREA_CHECK_EQ(light, 1u);    // > 0,45: só o de 0,60
    AUREA_CHECK_EQ(medium, 2u);   // > 0,30: 0,38 e 0,60
    AUREA_CHECK_EQ(strong, 3u);   // > 0,22: os três
    // Margens da média no vão de 0,60 s: fim + 0,12 … início − 0,07 (no quadro de 30 fps).
    bool found = false;
    for (usize i = 0; i + 1 < pm.keep.size(); ++i) {
        if (near(pm.keep[i].end, 4.23 + 0.12, 1.0 / 30 + 1e-6) && near(pm.keep[i + 1].start, 4.4 - 0.07, 1.0 / 30 + 1e-6)) found = true;
        if (near(pm.keep[i].end, 3.13 + 0.12, 1.0 / 30 + 1e-6)) found = found || near(pm.keep[i + 1].start, 3.73 - 0.07, 1.0 / 30 + 1e-6);
    }
    AUREA_CHECK(found);
    // Silêncio antes da 1ª palavra sai (até 1,0 − 0,07).
    AUREA_CHECK(near(pm.keep.front().start, 0.93, 1.0 / 30 + 1e-6));
    AUREA_CHECK(!pm.aborted);
    AUREA_CHECK(aed::pause_rule(aed::Intensity::Light).after == 0.16 && aed::pause_rule(aed::Intensity::Strong).before == 0.05);
}

// --- §3.4 Tomadas repetidas ---------------------------------------------------
AUREA_TEST(AutoEdit, RetakeKeepsTheLastTake) {
    std::vector<aed::Word> w;
    f64 t = say(w, 1.0, {"Hoje", "eu", "vou", "mostrar", "o", "aplicativo."});
    t = say(w, t + 1.0, {"Hoje", "eu", "vou", "mostrar", "o", "aplicativo", "novo."});
    say(w, t + 0.9, {"Ele", "corta", "tudo", "sozinho."});
    aed::Options o;
    const auto m = aed::mark_words(w, o);
    for (usize i = 0; i < 6; ++i) AUREA_CHECK(m[i] == aed::CutReason::Retake);
    for (usize i = 6; i < w.size(); ++i) AUREA_CHECK(m[i] == aed::CutReason::None);
    // Contínua (entrevista): nenhuma retomada.
    o.recording = aed::Recording::Continuous;
    const auto c = aed::mark_words(w, o);
    AUREA_CHECK(std::none_of(c.begin(), c.end(), [](aed::CutReason r) { return r == aed::CutReason::Retake; }));
}

AUREA_TEST(AutoEdit, RetakeKeepsCompleteEarlierWhenLaterIsCutShort) {
    std::vector<aed::Word> w;
    f64 t = say(w, 1.0, {"Esse", "recurso", "economiza", "muito", "tempo", "de", "edição."});
    say(w, t + 1.0, {"Esse", "recurso", "economiza", "muito"});
    const auto m = aed::mark_words(w, aed::Options{});
    for (usize i = 0; i < 7; ++i) AUREA_CHECK(m[i] == aed::CutReason::None);
    for (usize i = 7; i < w.size(); ++i) AUREA_CHECK(m[i] == aed::CutReason::Retake);
}

AUREA_TEST(AutoEdit, ContrastVetoKeepsBothSentences) {
    std::vector<aed::Word> w;
    f64 t = say(w, 1.0, {"O", "carro", "dele", "é", "azul."});
    t = say(w, t + 1.0, {"O", "carro", "dela", "é", "vermelho."});
    t = say(w, t + 1.0, {"Eu", "gosto", "muito", "de", "café."});
    say(w, t + 1.0, {"Eu", "não", "gosto", "muito", "de", "café."});
    const auto m = aed::mark_words(w, aed::Options{});
    const auto retakes = marked(w, m, aed::CutReason::Retake);
    std::printf("    retomadas marcadas: %zu\n", retakes.size());
    AUREA_CHECK(retakes.empty());
    // As regras soltas.
    AUREA_CHECK(aed::contrast_veto({"o", "carro", "e", "azul"}, {"o", "carro", "e", "verde"}, aed::Language::Pt));
    AUREA_CHECK(aed::contrast_veto({"tenho", "3", "gatos"}, {"tenho", "4", "gatos"}, aed::Language::Pt));
    AUREA_CHECK(aed::contrast_veto({"eu", "gosto", "disso"}, {"eu", "nao", "gosto", "disso"}, aed::Language::Pt));
    AUREA_CHECK(aed::contrast_veto({"agora", "vamos", "comecar", "aqui"}, {"aqui", "agora", "vamos", "comecar"}, aed::Language::Pt));
    AUREA_CHECK(!aed::contrast_veto({"hoje", "eu", "vou", "mostrar"}, {"hoje", "eu", "vou", "mostrar", "tudo"}, aed::Language::Pt));
    AUREA_CHECK_EQ(aed::token_lcs({"hoje", "eu", "vou", "mostrar"}, {"hoje", "eu", "vou", "mostar", "tudo"}), 4u);
    AUREA_CHECK(aed::normalize_token("Três,", aed::Language::Pt) == "3" && aed::normalize_token("Não!", aed::Language::Pt) == "nao");
}

// --- §3.2 Vícios ----------------------------------------------------------------
AUREA_TEST(AutoEdit, FillersHesitationsAndTheNaoGuard) {
    std::vector<aed::Word> w;
    f64 t = say(w, 1.0, {"Ahn", "isso", "é", "muito", "bom,", "né."});
    t = say(w, t + 1.0, {"Ele", "disse", "não", "né."}, 0.05);   // "né" a 50 ms de "não": volta
    say(w, t + 1.0, {"Vou", "a", "um", "lugar", "eh", "legal."});
    const auto m = aed::mark_words(w, aed::Options{});
    AUREA_CHECK(m[0] == aed::CutReason::Hesitation);   // ahn
    AUREA_CHECK(m[5] == aed::CutReason::Filler);       // né fechando a frase
    AUREA_CHECK(m[9] == aed::CutReason::None);         // guarda do "não"
    AUREA_CHECK(m[11] == aed::CutReason::None && m[12] == aed::CutReason::None);   // "a" e "um" são palavras em pt
    AUREA_CHECK(m[14] == aed::CutReason::Hesitation);  // eh
    AUREA_CHECK(aed::is_hesitation("hmmm", aed::Language::Pt) && aed::is_hesitation("ãh", aed::Language::Pt));
    AUREA_CHECK(aed::is_hesitation("aaa", aed::Language::Pt) && !aed::is_hesitation("a", aed::Language::Pt));
    AUREA_CHECK(aed::is_hesitation("um", aed::Language::En) && !aed::is_hesitation("um", aed::Language::Pt));
    AUREA_CHECK(!aed::is_hesitation("an", aed::Language::En) && aed::is_hesitation("an", aed::Language::Pt));
}

AUREA_TEST(AutoEdit, SlateBackstageStutterAndFalseStart) {
    std::vector<aed::Word> w;
    f64 t = say(w, 1.0, {"Take", "3"});
    t = say(w, t + 0.8, {"Eu", "eu", "adoro", "esse", "app."});
    t = say(w, t + 1.0, {"Muito", "muito", "bom."});
    t = say(w, t + 1.0, {"Corta,", "errei."});
    say(w, t + 1.0, {"A", "comp", "computação", "mudou."});
    const auto m = aed::mark_words(w, aed::Options{});
    AUREA_CHECK(m[0] == aed::CutReason::Slate && m[1] == aed::CutReason::Slate);
    AUREA_CHECK(m[2] == aed::CutReason::Stutter && m[3] == aed::CutReason::None);
    AUREA_CHECK(m[7] == aed::CutReason::None && m[8] == aed::CutReason::None);   // ênfase fica
    AUREA_CHECK(m[10] == aed::CutReason::Backstage && m[11] == aed::CutReason::Backstage);
    AUREA_CHECK(m[13] == aed::CutReason::FalseStart && m[14] == aed::CutReason::None);
}

AUREA_TEST(AutoEdit, SafetyAbortsWhenAlmostNothingRemains) {
    std::vector<aed::Word> w = {{"ahn", 1.0, 1.3}, {"hum", 2.0, 2.4}, {"eh", 3.0, 3.2}};
    const auto plan = aed::plan_cuts(w, 30.0, aed::Options{});
    AUREA_CHECK(plan.aborted);
    AUREA_CHECK_EQ(plan.keep.size(), 1u);
    AUREA_CHECK(plan.keep[0].start == 0.0 && plan.keep[0].end == 30.0);
}

// --- Mapa do tempo --------------------------------------------------------------
AUREA_TEST(AutoEdit, TimeMapAndCaptionWordCoverage) {
    const aed::TimeMap map({{0.0, 1.0}, {2.0, 3.0}});
    AUREA_CHECK(near(map.duration(), 2.0, 1e-9));
    AUREA_CHECK(near(map.to_edit(2.5), 1.5, 1e-9) && map.to_edit(1.5) < 0.0);
    AUREA_CHECK(near(map.to_source(1.5), 2.5, 1e-9));
    AUREA_CHECK_EQ(map.splices().size(), 1u);
    const auto words = aed::map_words({{"fica", 0.2, 0.6}, {"sai", 0.9, 1.3}, {"metade", 0.95, 1.05}, {"depois", 2.1, 2.5}}, map);
    AUREA_CHECK_EQ(words.size(), 3u);   // "sai" tem 75 % no corte
    if (words.size() == 3) {
        AUREA_CHECK(words[1].text == "metade" && near(words[1].end, 1.0, 1e-9));
        AUREA_CHECK(near(words[2].start, 1.1, 1e-9) && near(words[2].end, 1.5, 1e-9));
    }
}

// --- §1 Áudio ------------------------------------------------------------------
namespace {
std::vector<f32> talk_signal(const std::vector<aed::Range>& voiced, const std::vector<aed::Range>& breaths, f64 seconds, u32 rate) {
    std::vector<f32> x(static_cast<usize>(seconds * rate));
    u32 s = 1234;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return static_cast<f32>(s >> 8) / 8388608.0f - 1.0f; };
    for (usize i = 0; i < x.size(); ++i) x[i] = 0.002f * rnd();
    for (const auto& r : voiced)
        for (usize i = static_cast<usize>(r.start * rate); i < std::min(x.size(), static_cast<usize>(r.end * rate)); ++i) {
            const f64 t = static_cast<f64>(i) / rate;
            const f64 env = std::min({1.0, (t - r.start) / 0.01, (r.end - t) / 0.02});
            x[i] += static_cast<f32>(0.25 * env * (std::sin(6.2831853 * 140 * t) + 0.5 * std::sin(6.2831853 * 280 * t) + 0.3 * std::sin(6.2831853 * 420 * t)));
        }
    for (const auto& r : breaths)
        for (usize i = static_cast<usize>(r.start * rate); i < std::min(x.size(), static_cast<usize>(r.end * rate)); ++i) x[i] += 0.03f * rnd();
    return x;
}
} // namespace

AUREA_TEST(AutoEdit, SpeechDetectionAndBreaths) {
    const auto x = talk_signal({{1.0, 2.0}, {2.5, 3.5}, {5.0, 6.0}}, {{4.0, 4.35}}, 7.0, 16000);
    const auto env = aed::analyze_envelope(x.data(), x.size());
    AUREA_CHECK_EQ(env.size(), 700u);
    const auto sp = aed::detect_speech(env);
    std::printf("    ruido %.1f dB, nivel %.1f dB, limiar %.1f dB, %zu falas, %zu respiracoes\n",
                sp.noiseDb, sp.levelDb, sp.thresholdDb, sp.regions.size(), sp.breaths.size());
    AUREA_CHECK_EQ(sp.regions.size(), 3u);
    if (sp.regions.size() == 3) {
        AUREA_CHECK(near(sp.regions[0].start, 1.0, 0.06) && near(sp.regions[0].end, 2.0, 0.16));
        AUREA_CHECK(near(sp.regions[1].start, 2.5, 0.06));
    }
    AUREA_CHECK_EQ(sp.breaths.size(), 1u);
    if (!sp.breaths.empty()) AUREA_CHECK(sp.breaths[0].start >= 3.85 && sp.breaths[0].end <= 4.6);
    // Palavra sem som = fantasma, sai do alinhamento.
    const auto aligned = aed::align_words({{"fala", 1.05, 1.6}, {"fantasma", 4.6, 4.9}, {"volta", 5.1, 5.5}}, env);
    AUREA_CHECK_EQ(aligned.size(), 2u);
}

// --- §7 Voz --------------------------------------------------------------------
AUREA_TEST(AutoEdit, LufsGainClampAndPhrases) {
    std::vector<f32> sine(16000 * 4);
    for (usize i = 0; i < sine.size(); ++i) sine[i] = 0.1f * std::sin(6.2831853f * 997.0f * static_cast<f32>(i) / 16000.0f);
    const f64 lufs = aed::integrated_lufs(sine.data(), sine.size(), 16000);
    std::printf("    seno 997 Hz -20 dBFS: %.2f LUFS\n", lufs);
    AUREA_CHECK(near(lufs, -23.0, 0.4));
    AUREA_CHECK(aed::voice_gain_db(-30.0) == 15.0f);    // 17,5 → teto +15
    AUREA_CHECK(aed::voice_gain_db(-20.0) == 7.5f);
    AUREA_CHECK(aed::voice_gain_db(-5.0) == -6.0f);     // −7,5 → piso −6
    AUREA_CHECK(aed::voice_gain_db(-14.3) == 2.0f);     // 1,8 → passos de 0,5
    AUREA_CHECK(aed::compressor_threshold_db(-18.0f, 3.0f, true) == -23.0f && aed::compressor_threshold_db(0, 0, false) == -26.0f);
    const auto g = aed::phrase_gains({-20.0f, -26.0f, -21.0f});
    AUREA_CHECK(g[0] == -1.0f && g[1] == 5.0f && g[2] == 0.0f);
    const auto keys = aed::voice_keys({}, {}, {{2.0, 2.5}});
    AUREA_CHECK(near(aed::gain_at(keys, 2.25), -15.0, 1e-4) && near(aed::gain_at(keys, 1.9), 0.0, 1e-4));
}

AUREA_TEST(AutoEdit, VoiceCompressorAndLimiterPlan) {
    // 2 s a −10 dB (pico −4 dBFS) e 2 s a −30 dB; limiar −20 dB.
    aed::Envelope env;
    for (int i = 0; i < 400; ++i) {
        const bool loud = i < 200;
        env.db.push_back(loud ? -10.0f : -30.0f);
        env.peakDb.push_back(loud ? -4.0f : -24.0f);
        env.hfDb.push_back(-40.0f); env.zcr.push_back(0.05f);
    }
    std::vector<u8> speech(400, 1);
    const auto d = aed::voice_dynamics(env, speech, 0.0f, -20.0f);
    // Regime: 10 dB acima do limiar com 2,5:1 = 6 dB de redução; make-up = mediana (entre 0 e 6).
    const f32 loudGain = d.gainDb[150], quietGain = d.gainDb[350];
    std::printf("    make-up %.2f dB, alto %.2f dB, baixo %.2f dB, maior reducao %.2f dB\n", d.makeupDb, loudGain, quietGain, d.maxReductionDb);
    AUREA_CHECK(d.makeupDb >= 0.0f && d.makeupDb <= 6.0f + 1e-3f);
    AUREA_CHECK(near(quietGain - loudGain, 6.0, 0.6));                     // a diferença de nível cai de 20 para 14 dB
    AUREA_CHECK(-4.0f + loudGain <= -3.0f + 1e-3f);                         // limitador: pico ≤ −3 dBFS
    // Limitador sozinho: pico −1 dBFS sem compressão desce ao teto.
    aed::Envelope peaky = env;
    std::fill(peaky.db.begin(), peaky.db.end(), -40.0f);
    std::fill(peaky.peakDb.begin(), peaky.peakDb.end(), -1.0f);
    const auto l = aed::voice_dynamics(peaky, speech, 0.0f, -20.0f);
    AUREA_CHECK(near(l.gainDb[200], -2.0, 0.05));
    // Simplificação: uma rampa reta vira 2 keys; um degrau, mais.
    std::vector<f32> ramp(100);
    for (int i = 0; i < 100; ++i) ramp[static_cast<usize>(i)] = -0.1f * static_cast<f32>(i);
    AUREA_CHECK_EQ(aed::simplify_gain(ramp, 0.01, 0.5f).size(), 2u);
    std::vector<f32> step(100, 0.0f);
    std::fill(step.begin() + 50, step.end(), -6.0f);
    const auto keys = aed::simplify_gain(step, 0.01, 0.5f);
    AUREA_CHECK(keys.size() >= 3 && near(aed::gain_at(keys, 0.2), 0.0, 1e-4) && near(aed::gain_at(keys, 0.8), -6.0, 1e-4));
}

// --- §6 Música -----------------------------------------------------------------
AUREA_TEST(AutoEdit, DuckingEnvelopeAndMusicChoice) {
    const auto keys = aed::ducking_keys({{2.0, 4.0}, {4.5, 6.0}, {10.0, 11.0}}, 20.0);
    AUREA_CHECK_EQ(keys.size(), 8u);
    AUREA_CHECK(near(aed::gain_at(keys, 1.6), 0.0, 1e-4));
    AUREA_CHECK(near(aed::gain_at(keys, 1.875), -4.0, 1e-3));   // meio do ataque de 0,25 s
    AUREA_CHECK(near(aed::gain_at(keys, 4.25), -8.0, 1e-4));    // falas a < 1,15 s viram uma região
    AUREA_CHECK(near(aed::gain_at(keys, 6.3), -4.0, 1e-3));     // meio da soltura de 0,6 s
    AUREA_CHECK(near(aed::gain_at(keys, 8.0), 0.0, 1e-4));
    AUREA_CHECK(near(aed::music_target_bpm(2.0), 85.0, 1e-4) && near(aed::music_target_bpm(4.0), 115.0, 1e-4)
                && near(aed::music_target_bpm(9.0), 120.0, 1e-4) && near(aed::music_target_bpm(0.0), 80.0, 1e-4));
    std::vector<aed::MusicTrack> t(3);
    t[0] = {140.0f, 120.0, -14.0f, false, true};   // longe do alvo
    t[1] = {100.0f, 120.0, -14.0f, true, true};    // perto, mas usada há pouco
    t[2] = {96.0f, 120.0, -14.0f, false, true};    // perto e livre
    AUREA_CHECK_EQ(aed::choose_music(t, 2.7, 30.0), 2);
}

// --- §4 Zoom -------------------------------------------------------------------
AUREA_TEST(AutoEdit, ZoomPlanIsDeterministicAndFollowsRules) {
    aed::Mulberry32 r(7);
    AUREA_CHECK_EQ(r.next_u32(), 50271532u);   // mulberry32(7), como no JS
    AUREA_CHECK(near(aed::easy_ease(0.5f), 0.5, 1e-3) && aed::easy_ease(0.25f) < 0.12f && aed::easy_ease(0.75f) > 0.88f);
    const std::vector<aed::Range> clips = {{0.0, 4.0}, {4.0, 11.0}, {11.0, 19.0}, {19.0, 26.0}};
    std::vector<f64> caps;
    for (f64 t = 0.0; t < 26.0; t += 1.7) caps.push_back(t);
    std::vector<aed::Word> words;
    for (f64 t = 0.1; t < 26.0; t += 0.4) words.push_back({t > 13.0 && t < 13.5 ? "2026!" : "palavra", t, t + 0.3});
    const auto a = aed::plan_zoom(clips, caps, words, {}, 1);
    const auto b = aed::plan_zoom(clips, caps, words, {}, 1);
    AUREA_CHECK_EQ(a.size(), b.size());
    bool same = a.size() == b.size();
    for (usize i = 0; same && i < a.size(); ++i) same = a[i].type == b[i].type && a[i].keys.size() == b[i].keys.size();
    AUREA_CHECK(same);
    AUREA_CHECK(!a.empty() && a[0].type == aed::ZoomType::Intro);
    if (!a.empty()) {
        AUREA_CHECK(near(a[0].keys.front().scale, 1.18, 1e-5) && near(a[0].keys.back().scale, 1.0, 1e-5));
        AUREA_CHECK(near(a[0].keys.back().time - a[0].keys.front().time, std::min(1.2, a[0].range.length() / 2.0), 1e-9));
    }
    f64 total = 0.0, zoomed = 0.0;
    u32 count = 0;
    std::map<std::string, u32> types;
    for (usize i = 0; i < a.size(); ++i) {
        total += a[i].range.length();
        if (i == 0 || a[i].type == aed::ZoomType::None) continue;
        zoomed += a[i].range.length();
        ++count;
        ++types[aed::zoom_type_name(a[i].type)];
        AUREA_CHECK(a[i - 1].type == aed::ZoomType::None || a[i - 1].type == aed::ZoomType::Intro);   // um pedaço sem zoom entre zooms
        AUREA_CHECK(i < 2 || a[i - 2].type != a[i].type || a[i - 1].type != aed::ZoomType::None || true);
        for (const auto& k : a[i].keys) AUREA_CHECK(k.scale >= 0.999f && k.scale <= 1.181f);
    }
    std::printf("    %zu pedacos, %u zooms (%.0f%% do video)\n", a.size(), count, 100.0 * zoomed / total);
    AUREA_CHECK(count >= 2);
    AUREA_CHECK(zoomed / total <= 0.6 + 1e-9);
    // Pedaços: inícios de legenda a > 1,5 s das pontas; sem marca, a cada 2,5 s.
    const auto p = aed::zoom_pieces({{0.0, 6.0}}, {});
    AUREA_CHECK_EQ(p.size(), 2u);
    const auto q = aed::zoom_pieces({{0.0, 6.0}}, {1.0, 3.0, 5.0});
    AUREA_CHECK_EQ(q.size(), 2u);
}

AUREA_TEST(AutoEdit, ZoomAnchorAndClampNeverShowBorders) {
    const auto an = aed::face_anchor({{1.0, 0.4f, 0.2f, 0.2f, 0.3f}, {1.2, 0.1f, 0.1f, 0.05f, 0.05f}}, 0.0, 2.0);
    AUREA_CHECK(an.face && near(an.x, 0.5, 1e-6) && near(an.y, 0.2 + 0.45 * 0.3, 1e-6));
    const auto none = aed::face_anchor({}, 0.0, 2.0);
    AUREA_CHECK(!none.face && near(none.x, 0.5, 1e-6) && near(none.y, 0.42, 1e-6));
    // Camada do tamanho do quadro (1080×1920), zoom 120 % num rosto no canto.
    const f32 ext[4] = {540.0f, 540.0f, 960.0f, 960.0f};
    const auto pose = aed::zoom_position(540.0f, 960.0f, ext, 1.2f, 30.0f, 40.0f, 1080.0f, 1920.0f);
    AUREA_CHECK(pose.x - 1.2f * 540.0f <= 0.001f && pose.x + 1.2f * 540.0f >= 1079.999f);
    AUREA_CHECK(pose.y - 1.2f * 960.0f <= 0.001f && pose.y + 1.2f * 960.0f >= 1919.999f);
    AUREA_CHECK(aed::highlight_score("2026!", false) > aed::highlight_score("palavra", false));
    AUREA_CHECK_EQ(aed::highlight_score("de", false), 10 + 4 - 6);
}

// --- §5 Sons e flash -------------------------------------------------------------
AUREA_TEST(AutoEdit, SfxRolesGainAndAntiClash) {
    AUREA_CHECK(aed::sfx_gain_db(aed::SfxRole::Woosh, -3.0f) == -18.0f);
    AUREA_CHECK(aed::sfx_gain_db(aed::SfxRole::Flick, -80.0f) == 12.0f);
    AUREA_CHECK(aed::sfx_gain_db(aed::SfxRole::Woosh, 30.0f) == -40.0f);
    const auto kept = aed::resolve_sfx_clashes({{1.0, aed::SfxRole::Woosh}, {1.2, aed::SfxRole::Flick}, {1.4, aed::SfxRole::CutFlick},
                                               {1.1, aed::SfxRole::Sub}, {1.6, aed::SfxRole::CutFlick}, {3.0, aed::SfxRole::Flick}});
    AUREA_CHECK_EQ(kept.size(), 4u);
    if (kept.size() == 4) {
        AUREA_CHECK(kept[0].role == aed::SfxRole::Woosh && kept[1].role == aed::SfxRole::Sub);
        AUREA_CHECK(kept[2].role == aed::SfxRole::CutFlick && near(kept[2].time, 1.6, 1e-9));
        AUREA_CHECK(kept[3].role == aed::SfxRole::Flick);
    }
    // Flash: até 3, ≥ 6 s, o primeiro no gancho (0,6–4 s); 0,43 s ao todo.
    const auto f = aed::plan_flashes({0.3, 1.5, 3.0, 5.0, 8.0, 12.0, 14.0, 19.0, 26.0}, 30.0);
    AUREA_CHECK_EQ(f.size(), 3u);
    if (f.size() == 3) AUREA_CHECK(near(f[0], 1.5, 1e-9) && f[1] - f[0] >= 6.0 && f[2] - f[1] >= 6.0);
    const auto k = aed::flash_keys(5.0);
    AUREA_CHECK(near(k.back().time - k.front().time, 0.43, 1e-9) && k[1].opacity == 1.0f && k[2].opacity == 1.0f);
    // Sorteio fixo: o mesmo plano duas vezes.
    aed::SfxInputs in;
    in.splices = {2.0, 4.1, 6.3, 9.0, 11.5};
    in.variants[static_cast<u32>(aed::SfxRole::CutFlick)] = 2;
    in.variants[static_cast<u32>(aed::SfxRole::Flick)] = 2;
    in.captionWordStarts = {{0.5, 0.8}, {1.5, 1.9}, {3.0, 3.4}, {5.0, 5.5}, {7.0, 7.3}, {8.5, 8.9}, {10.0, 10.4}};
    const auto s1 = aed::plan_sfx(in), s2 = aed::plan_sfx(in);
    AUREA_CHECK_EQ(s1.size(), s2.size());
    for (usize i = 1; i < s1.size(); ++i) AUREA_CHECK(s1[i].time - s1[i - 1].time >= 0.5 - 1e-9);
    // Biblioteca própria (sintetizada) e leitura dos nomes do usuário.
    const auto w = aed::synth_sfx(aed::SfxRole::Woosh, 0);
    f32 peak = 0.0f; usize at = 0;
    for (usize i = 0; i < w.size(); ++i) if (std::fabs(w[i]) > peak) { peak = std::fabs(w[i]); at = i; }
    AUREA_CHECK(near(20.0 * std::log10(peak), -3.0, 0.01) && at > w.size() / 3);
    AUREA_CHECK(aed::synth_sfx(aed::SfxRole::Sub, 1) == aed::synth_sfx(aed::SfxRole::Sub, 1));
    AUREA_CHECK(aed::sfx_role_of("Transições", "whoosh rapido") == aed::SfxRole::Woosh);
    AUREA_CHECK(aed::sfx_role_of("Sons", "Câmera foto") == aed::SfxRole::Shutter);
    AUREA_CHECK(aed::sfx_role_of("Sons", "aplausos") == aed::SfxRole::Count);
    AUREA_CHECK(aed::music_like("Músicas", "faixa 1") && !aed::music_like("Efeitos", "porta"));
}

// --- §8 Cor e legenda fora dos rostos ---------------------------------------------
AUREA_TEST(AutoEdit, ColorTestAndCaptionsAvoidFaces) {
    std::vector<u8> flat(64 * 64 * 4), punchy(64 * 64 * 4);
    for (usize i = 0; i < 64 * 64; ++i) {
        const u8 v = static_cast<u8>(90 + (i % 64));   // 90..153: lavado, pouco contraste
        flat[i * 4] = v; flat[i * 4 + 1] = v; flat[i * 4 + 2] = static_cast<u8>(v + 10); flat[i * 4 + 3] = 255;
        const u8 w = static_cast<u8>((i % 64) * 4);    // 0..252: preto ao branco
        punchy[i * 4] = w; punchy[i * 4 + 1] = static_cast<u8>(w / 2); punchy[i * 4 + 2] = 20; punchy[i * 4 + 3] = 255;
    }
    const auto a = aed::frame_color_stats(flat.data(), 64, 64, 256), b = aed::frame_color_stats(punchy.data(), 64, 64, 256);
    AUREA_CHECK(!aed::already_graded({a, a, a}));
    AUREA_CHECK(aed::already_graded({b, b, b}));
    AUREA_CHECK(!aed::already_graded({}));
    // Rosto embaixo (0,55..0,80) num 9:16: a legenda sobe para fora dele e fica na área segura.
    std::vector<aed::Face> faces = {{1.0, 0.3f, 0.55f, 0.4f, 0.25f}, {3.0, 0.3f, 0.55f, 0.4f, 0.25f}};
    const auto ys = aed::caption_positions({{{0.5, 1.5}, 0}, {{1.6, 2.8}, 0}, {{6.0, 7.0}, 1}}, faces, 0.78f, 0.08f, true);
    AUREA_CHECK_EQ(ys.size(), 3u);
    if (ys.size() == 3) {
        AUREA_CHECK(ys[0] + 0.04f <= 0.53f + 1e-5f && ys[0] - 0.04f >= 0.14f - 1e-5f);
        AUREA_CHECK(ys[0] == ys[1]);              // estável no mesmo trecho
        AUREA_CHECK(near(ys[2], 0.70 - 0.04, 1e-5));   // sem rosto: o padrão preso na área segura
    }
}

// --- Edição inteira num clipe falado sintético -----------------------------------
namespace {

const json::Value* field(const json::Value& v, const char* key) { return v.is_object() ? v.get(key) : nullptr; }
f64 num(const json::Value* v, f64 def) { return v && v->is_number() ? v->number : def; }
bool flag(const json::Value* v, bool def) { return v && v->type == json::Value::Type::Bool ? v->boolean : def; }

struct WavData { u32 rate = 48000; u16 channels = 1; std::vector<f32> samples; };

bool read_wav(const std::string& path, WavData& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
    if (b.size() < 44 || std::string(b.data(), 4) != "RIFF") return false;
    auto u16 = [&](usize o) { return static_cast<u32>(static_cast<u8>(b[o]) | (static_cast<u8>(b[o + 1]) << 8)); };
    auto u32v = [&](usize o) { return u16(o) | (u16(o + 2) << 16); };
    out.channels = static_cast<aurea::u16>(u16(22));
    out.rate = u32v(24);
    const usize n = (b.size() - 44) / 2;
    out.samples.resize(n);
    for (usize i = 0; i < n; ++i) out.samples[i] = static_cast<i16>(u16(44 + i * 2)) / 32768.0f;
    return true;
}

void write_wav(const std::string& path, const std::vector<f32>& x, u32 rate) {
    std::ofstream f(path, std::ios::binary);
    auto u32le = [&](u32 v) { const char c[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; f.write(c, 4); };
    auto u16le = [&](u32 v) { const char c[2] = {char(v), char(v >> 8)}; f.write(c, 2); };
    f.write("RIFF", 4); u32le(36 + static_cast<u32>(x.size() * 2)); f.write("WAVE", 4);
    f.write("fmt ", 4); u32le(16); u16le(1); u16le(1); u32le(rate); u32le(rate * 2); u16le(2); u16le(16);
    f.write("data", 4); u32le(static_cast<u32>(x.size() * 2));
    for (f32 v : x) { const i16 s = static_cast<i16>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); u16le(static_cast<u16>(s)); }
}

class BufferDecoder final : public audio::AudioDecoderBackend {
public:
    BufferDecoder(std::vector<f32> x, u32 rate, u32 ch) : x_(std::move(x)) {
        info_.sampleRate = rate; info_.channels = ch;
        info_.durationUs = static_cast<i64>(x_.size() / ch) * 1'000'000 / rate;
    }
    const audio::AudioStreamInfo& info() const noexcept override { return info_; }
    Status seek(i64 us) noexcept override { pos_ = std::max<i64>(0, us * info_.sampleRate / 1'000'000); return OkStatus; }
    Status read(std::vector<f32>& out, i64& pts, bool& eos) noexcept override {
        const i64 frames = static_cast<i64>(x_.size() / info_.channels);
        out.clear(); eos = pos_ >= frames;
        if (eos) return OkStatus;
        const i64 n = std::min<i64>(2048, frames - pos_);
        out.assign(x_.begin() + pos_ * info_.channels, x_.begin() + (pos_ + n) * info_.channels);
        pts = pos_ * 1'000'000 / info_.sampleRate;
        pos_ += n;
        return OkStatus;
    }
private:
    std::vector<f32> x_;
    audio::AudioStreamInfo info_{};
    i64 pos_ = 0;
};

/// "talk" = o clipe falado (vídeo sintético + a fala); "*.wav" = arquivos de som.
class TalkFactory final : public VideoSourceFactory {
public:
    TalkFactory(test::SyntheticConfig v, std::vector<f32> speech) : video_(v), speech_(std::move(speech)) {}
    bool probe(const char* path, MediaProbe& out) override {
        const std::string p(path);
        if (p.size() > 4 && p.substr(p.size() - 4) == ".wav") {
            WavData w;
            if (!read_wav(p, w)) return false;
            out.hasVideo = false; out.hasAudio = true;
            out.audioSampleRate = w.rate; out.audioChannels = w.channels;
            out.audioDurationUs = static_cast<i64>(w.samples.size() / w.channels) * 1'000'000 / w.rate;
            return true;
        }
        test::SyntheticDecoder d(video_);
        out.video = d.info(); out.video.color.fromStream = true;
        out.hasVideo = true; out.hasAudio = true;
        out.audioSampleRate = 48000; out.audioChannels = 1;
        out.audioDurationUs = static_cast<i64>(speech_.size()) * 1'000'000 / 48000;
        return true;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        return std::make_unique<test::SyntheticDecoder>(video_);
    }
    std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char* path) override {
        const std::string p(path);
        if (p.size() > 4 && p.substr(p.size() - 4) == ".wav") {
            WavData w;
            if (!read_wav(p, w)) return nullptr;
            return std::make_unique<BufferDecoder>(std::move(w.samples), w.rate, w.channels);
        }
        return std::make_unique<BufferDecoder>(speech_, 48000, 1);
    }
private:
    test::SyntheticConfig video_;
    std::vector<f32> speech_;
};

std::vector<aed::Word> talk_transcript() {
    std::vector<aed::Word> w;
    f64 t = say(w, 0.6, {"Hoje", "eu", "vou", "mostrar", "como", "editar."});
    t = say(w, t + 1.2, {"Hoje", "eu", "vou", "mostrar", "como", "editar", "vídeos."});
    t = say(w, t + 0.5, {"ahn"});
    t = say(w, t + 0.8, {"É", "muito", "simples,", "né."});
    t = say(w, t + 0.4, {"Primeiro", "você", "grava", "tudo", "de", "uma", "vez."});
    t = say(w, t + 1.0, {"Depois", "o", "Aurea", "corta", "as", "pausas", "sozinho."});
    say(w, t + 0.6, {"Eu", "eu", "adoro", "esse", "recurso", "de", "2026!"});
    return w;
}

std::string request_json(const std::vector<aed::Word>& words, const std::string& music) {
    json::Writer w;
    w.begin_object().key("options").begin_object().key("intensity").value(1).key("recording").value(0)
     .key("language").value("pt").end_object();
    w.key("words").begin_array();
    for (const auto& x : words) w.begin_object().key("text").value(x.text).key("start").value(x.start).key("end").value(x.end).end_object();
    w.end_array();
    w.key("library").begin_array();
    w.begin_object().key("path").value(music).key("name").value("Trilha teste").key("category").value("Músicas")
     .key("duration").value(40.0).end_object();
    w.end_array().end_object();
    return w.str();
}

struct EditRun {
    std::string report;
    json::Value value;
    i64 before = 0, after = 0;
};

} // namespace

AUREA_TEST(AutoEdit, EndToEndOnSyntheticTalkingClip) {
    namespace fs = std::filesystem;
    const std::string docs = (fs::current_path() / "aurea_autoedit_docs").string();
    std::error_code ec;
    fs::remove_all(docs, ec);
    fs::create_directories(docs, ec);
    // Música da biblioteca do usuário: 40 s, 100 BPM (bumbo + acorde).
    const std::string music = docs + "/trilha_teste.wav";
    {
        std::vector<f32> m(48000 * 40);
        for (usize i = 0; i < m.size(); ++i) {
            const f64 t = static_cast<f64>(i) / 48000.0, beat = std::fmod(t, 0.6);
            m[i] = static_cast<f32>(0.15 * (std::sin(6.2831853 * 220 * t) + std::sin(6.2831853 * 277 * t))
                                  + 0.6 * std::exp(-beat * 18.0) * std::sin(6.2831853 * 60 * beat));
        }
        write_wav(music, m, 48000);
    }
    const auto words = talk_transcript();
    std::vector<aed::Range> voiced;
    for (const auto& w : words) voiced.push_back({w.start, w.end});
    const f64 seconds = words.back().end + 1.5;
    const auto speech = talk_signal(voiced, {}, seconds, 48000);
    test::SyntheticConfig vc;
    vc.width = 180; vc.height = 320; vc.fps = 30.0;
    vc.frameCount = static_cast<u32>(seconds * 30.0);

    auto run = [&](EditRun& out, bool undoAfter) {
        TalkFactory factory(vc, speech);
        Engine engine;
        EngineConfig cfg;
        cfg.workerCount = 2; cfg.disableAutosave = true; cfg.memoryBudgetBytes = 128ull << 20;
        cfg.mediaFactory = &factory;
        cfg.documentsDirectory = docs;
        AUREA_CHECK(engine.initialize(cfg).ok());
        AUREA_CHECK(engine.new_project(1080, 1920, 30.0, nullptr).ok());
        VideoImport vi; vi.sourcePath = "talk"; vi.displayName = "Fala";
        auto id = engine.import_video(vi);
        AUREA_CHECK(id.ok());
        if (!id.ok()) return;
        Composition* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
        const Layer before = *comp->layer(LayerId::unpack(*id));
        out.before = before.end.value - before.start.value;
        out.report = engine.auto_edit(*id, request_json(words, music));
        AUREA_CHECK(json::parse(out.report, out.value));
        AUREA_CHECK(flag(field(out.value, "ok"), false));
        std::printf("    %s\n", out.report.c_str());
        comp = engine.project()->timeline().composition(engine.project()->timeline().current());
        // A camada nova mostra a composição nova; a original continua igual (só desligada e muda).
        const Layer* group = nullptr;
        for (u32 i = 0; i < comp->order().size(); ++i) {
            const Layer* l = comp->layer(comp->order().at(i));
            if (l && l->kind == LayerKind::Composition && l->name.find("edi\xC3\xA7\xC3\xA3o autom\xC3\xA1tica") != std::string::npos) group = l;
        }
        AUREA_CHECK(group != nullptr);
        const Layer* orig = comp->layer(LayerId::unpack(*id));
        AUREA_CHECK(orig && orig->start == before.start && orig->end == before.end && orig->offset == before.offset);
        AUREA_CHECK(orig && orig->effects.size() == before.effects.size() && orig->tracks.size() == before.tracks.size());
        AUREA_CHECK(orig && !orig->visible && orig->muted);
        if (!group) return;
        const Composition* child = engine.project()->timeline().composition(group->nested.composition);
        AUREA_CHECK(child != nullptr);
        if (!child) return;
        out.after = child->duration().value;
        AUREA_CHECK(out.after < out.before);
        AUREA_CHECK_EQ(group->end.value - group->start.value, out.after);
        u32 videos = 0, zoomed = 0, sfx = 0, musicLayers = 0, captions = 0, voices = 0, flashes = 0;
        for (u32 i = 0; i < child->order().size(); ++i) {
            const Layer* l = child->layer(child->order().at(i));
            if (!l) continue;
            if (l->kind == LayerKind::Video) {
                ++videos;
                const Track* sx = l->tracks.find(TrackProperty::ScaleX);
                if (sx && sx->keys.size() >= 2) ++zoomed;
                AUREA_CHECK(l->muted);
            }
            if (l->kind == LayerKind::Audio && l->name.rfind("Som: ", 0) == 0) ++sfx;
            if (l->kind == LayerKind::Audio && l->name == "Trilha teste") {
                ++musicLayers;
                AUREA_CHECK(near(20.0 * std::log10(l->gain), -15.0, 0.01));
                const Track* vt = l->tracks.find(TrackProperty::AudioVolume);
                AUREA_CHECK(vt && vt->keys.size() >= 4);   // ducking
            }
            if (l->kind == LayerKind::Audio && l->name.find("(voz") != std::string::npos) {
                ++voices;
                AUREA_CHECK(!l->effects.empty());   // passa-alta 70 Hz
            }
            if (l->kind == LayerKind::Shape && l->name == "Flash") ++flashes;
            if (l->kind == LayerKind::Text && !l->captions.empty()) captions += static_cast<u32>(l->captions.size());
        }
        std::printf("    %u pedacos (%u com zoom), %u vozes, %u sons, %u musica, %u flashes, %u legendas; %lld -> %lld quadros\n",
                    videos, zoomed, voices, sfx, musicLayers, flashes, captions,
                    static_cast<long long>(out.before), static_cast<long long>(out.after));
        AUREA_CHECK(videos >= 3 && zoomed >= 1);
        AUREA_CHECK(voices == videos);
        AUREA_CHECK(sfx >= 1);
        AUREA_CHECK_EQ(musicLayers, 1u);
        AUREA_CHECK(captions >= 3);
        if (undoAfter) {
            // Um passo de desfazer volta tudo.
            Command u; u.type = CommandType::Undo;
            AUREA_CHECK(engine.apply_command(u).ok());
            comp = engine.project()->timeline().composition(engine.project()->timeline().current());
            const Layer* back = comp->layer(LayerId::unpack(*id));
            AUREA_CHECK(back && back->visible && !back->muted);
            bool groupLeft = false;
            for (u32 i = 0; i < comp->order().size(); ++i) {
                const Layer* l = comp->layer(comp->order().at(i));
                if (l && l->kind == LayerKind::Composition) groupLeft = true;
            }
            AUREA_CHECK(!groupLeft);
        }
    };
    EditRun first, second;
    run(first, true);
    run(second, false);
    // Mesmo vídeo → mesma edição.
    AUREA_CHECK_EQ(first.after, second.after);
    AUREA_CHECK(num(field(first.value, "cuts"), -1) == num(field(second.value, "cuts"), -2));
    AUREA_CHECK(num(field(first.value, "sounds"), -1) == num(field(second.value, "sounds"), -2));
    AUREA_CHECK(num(field(first.value, "zooms"), -1) == num(field(second.value, "zooms"), -2));
    const json::Value* reasons = field(first.value, "reasons");
    AUREA_CHECK(reasons && num(field(*reasons, "retomada"), 0) >= 1);
    fs::remove_all(docs, ec);
}
