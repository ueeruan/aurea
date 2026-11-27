// =============================================================================
//  Testes da FAIXA DE LEGENDAS (text/Captions.hpp + Engine::*caption*).
//
//  O contrato que estes testes travam:
//    - UMA faixa por origem: palavras e frases viram BLOCOS na mesma camada,
//      nunca uma camada por palavra (o que entupia a timeline);
//    - cada bloco guarda o tempo das PRÓPRIAS palavras, para destacar a fala;
//    - editar o texto preserva o tempo das palavras que continuam iguais;
//    - cortar/dividir/unir/mover/ajustar tempo são passos de desfazer;
//    - a UI lê os blocos numa travessia (query_captions), na régua da timeline.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/project/Presets.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/text/WhisperWords.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::test;

AUREA_TEST(Captions, TranscriptionRejectsReentryAndResultsFromReplacedProjects) {
    struct Factory final : VideoSourceFactory {
        Engine* engine = nullptr; u64 layer = 0;
        bool nestedRejected = false, cancellationPreserved = false;
        bool probe(const char*, MediaProbe& probe) override {
            probe.hasAudio = true; probe.audioSampleRate = 48000; probe.audioChannels = 2;
            probe.audioDurationUs = 1'000'000; return true;
        }
        std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override { return {}; }
        std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char*) override {
            engine->captionCancelled.store(true);
            auto nested = engine->transcribe_local(layer, "missing.bin", "");
            nestedRejected = !nested.ok() && nested.status().code() == Errc::InvalidState;
            cancellationPreserved = engine->captionCancelled.load();
            (void)engine->new_project(32, 32, 30, "replacement");
            return {};
        }
    } factory;
    Engine engine; factory.engine = &engine;
    EngineConfig config; config.workerCount = 2; config.disableAutosave = true; config.mediaFactory = &factory;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(32, 32, 30, nullptr).ok());
    VideoImport request; request.sourcePath = "synthetic"; request.displayName = "audio";
    auto audio = engine.import_audio(request);
    AUREA_CHECK(audio.ok()); if (!audio.ok()) return;
    factory.layer = *audio;
    auto result = engine.transcribe_local(*audio, "missing.bin", "");
    AUREA_CHECK(!result.ok() && result.status().code() == Errc::Cancelled);
    AUREA_CHECK(factory.nestedRejected && factory.cancellationPreserved);
}

AUREA_TEST(Captions, WhisperPreservesUnalignedPortugueseSubtokens) {
    std::vector<text::CaptionWord> result;
    text::WhisperWords words(result, 27, 28, 56);
    words.token(" trans", 1.1, 1.3, 1.1, 1.6);
    words.token("cri", -1, -1, 1.1, 1.6);
    words.token("ção", 1.4, 1.6, 1.1, 1.6);
    words.flush();
    AUREA_CHECK_EQ(result.size(), 1u);
    AUREA_CHECK(result[0].text == "transcrição");
    AUREA_CHECK(std::abs(result[0].start - 28.1) < .001);
    AUREA_CHECK(std::abs(result[0].end - 28.6) < .001);
}

AUREA_TEST(Captions, WhisperKeepsWordsWithOverlappingTimestamps) {
    std::vector<text::CaptionWord> result;
    text::WhisperWords words(result, 0, 0, 28);
    words.token(" uma", 1, 1.6, 1, 2);
    words.token(" palavra", 1.5, 2, 1, 2);
    words.flush();
    AUREA_CHECK_EQ(result.size(), 2u);
    AUREA_CHECK(result[0].text == "uma");
    AUREA_CHECK(result[1].text == "palavra");
    AUREA_CHECK(result[0].end <= result[1].start);
    AUREA_CHECK(result[1].start < result[1].end);
}

AUREA_TEST(Captions, WhisperOverlapChunkEmitsWordOnlyOnce) {
    std::vector<text::CaptionWord> result;
    text::WhisperWords first(result, 0, 0, 28);
    first.token(" antes", 27.5, 27.8, 27, 29);
    first.token(" depois", 28.1, 28.5, 27, 29);
    first.flush();
    text::WhisperWords second(result, 27, 28, 56);
    second.token(" antes", .5, .8, 0, 2);
    second.token(" depois", 1.1, 1.5, 0, 2);
    second.flush();
    AUREA_CHECK_EQ(result.size(), 2u);
    AUREA_CHECK(result[1].text == "depois");
    AUREA_CHECK(std::abs(result[1].start - 28.1) < .001);
}

namespace {

SyntheticConfig caption_audio_cfg() {
    SyntheticConfig c;
    c.audioRate = 48000;
    c.audioChannels = 2;
    c.audioFreq = 440.0;
    c.audioSeconds = 8.0;
    return c;
}

EngineConfig caption_headless() {
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    return cfg;
}

std::string caption_path(const char* name) { return std::string("aurea_leg_") + name + ".aurea"; }

/// Projeto com uma camada de vídeo e a faixa de legendas gerada a partir dela.
/// `words` palavras em intervalos de 0,5 s (sem pausa para quebrar bloco).
struct CaptionRig {
    SyntheticFactory factory;
    Engine engine;
    u64 video = 0;
    u64 trackId = 0;

    explicit CaptionRig(u32 words, text::CaptionOptions opt = {}) : factory(caption_audio_cfg()) {
        EngineConfig ec = caption_headless();
        ec.mediaFactory = &factory;
        AUREA_CHECK(engine.initialize(ec).ok());
        AUREA_CHECK(engine.new_project(1080, 1920, 30.0, nullptr).ok());
        VideoImport vi;
        vi.sourcePath = "s";
        vi.displayName = "s";
        auto id = engine.import_video(vi);
        AUREA_CHECK(id.ok());
        video = *id;
        static const char* lexicon[] = {"um", "dois", "tres", "quatro", "cinco", "seis"};
        std::vector<text::CaptionWord> list;
        f64 t = 1.0;
        for (u32 i = 0; i < words; ++i) {
            text::CaptionWord w;
            w.text = lexicon[i % 6];
            w.start = t;
            w.end = t + 0.3;
            t += 0.5;
            list.push_back(w);
        }
        const auto made = engine.create_captions(video, list, opt);
        AUREA_CHECK(made.ok());
        remember_track();
    }

    Composition* comp() {
        return engine.project()->timeline().composition(engine.project()->timeline().current());
    }
    const Layer* track() { return track_of(comp()); }
    Layer* track_mut() { return track_of_mut(comp()); }

    static const Layer* track_of(const Composition* c) {
        if (!c) return nullptr;
        for (u32 i = 0; i < c->order().size(); ++i) {
            const Layer* l = c->layer(c->order().at(i));
            if (l && l->kind == LayerKind::Text && !l->captions.empty()) return l;
        }
        return nullptr;
    }
    static Layer* track_of_mut(Composition* c) {
        return const_cast<Layer*>(track_of(c));
    }

    void remember_track() {
        Composition* c = comp();
        if (!c) return;
        for (u32 i = 0; i < c->order().size(); ++i) {
            const LayerId id = c->order().at(i);
            const Layer* l = c->layer(id);
            if (l && l->kind == LayerKind::Text && !l->captions.empty()) trackId = id.pack();
        }
    }

    bool edit(const std::string& command) { return engine.edit_caption_track(trackId, command); }
    usize blocks() {
        const Layer* l = track();
        return l ? l->captions.size() : 0;
    }
    std::string one_of(usize index) {
        const Layer* l = track();
        if (!l || index >= l->captions.size()) return "[]";
        return "[" + std::to_string(l->captions[index].id) + "]";
    }
    std::string range_of(usize from, usize to) {
        const Layer* l = track();
        std::string out = "[";
        bool first = true;
        for (usize i = from; l && i <= to && i < l->captions.size(); ++i) {
            if (!first) out += ",";
            out += std::to_string(l->captions[i].id);
            first = false;
        }
        return out + "]";
    }
};

} // namespace

// -----------------------------------------------------------------------------
// Uma faixa, muitos blocos
// -----------------------------------------------------------------------------

AUREA_TEST(Captions, WordsBecomeBlocksInASingleTrackWithTheirOwnTiming) {
    CaptionRig rig(6);
    const Layer* track = rig.track();
    AUREA_CHECK(track != nullptr);
    if (!track) return;
    // Nenhuma camada por palavra: a faixa é UMA só.
    u32 textLayers = 0;
    for (u32 i = 0; i < rig.comp()->order().size(); ++i) {
        const Layer* l = rig.comp()->layer(rig.comp()->order().at(i));
        if (l && l->kind == LayerKind::Text) ++textLayers;
    }
    AUREA_CHECK_EQ(textLayers, 1u);
    AUREA_CHECK(!track->captions.empty());
    // As palavras carregam o próprio tempo: é o que o destaque segue.
    for (const auto& s : track->captions) {
        AUREA_CHECK(!s.text.empty() && s.end > s.start);
        for (const auto& w : s.words) {
            AUREA_CHECK(!w.text.empty());
            AUREA_CHECK(w.start >= s.start && w.end <= s.end && w.end > w.start);
        }
    }
    std::printf("    6 palavras -> %zu blocos em 1 faixa\n", track->captions.size());
}

AUREA_TEST(Captions, TrackLayerIsFlaggedSoTheTimelineDrawsBlocks) {
    CaptionRig rig(6);
    std::vector<bridge::LayerRow> rows(16);
    std::vector<char> names(4096);
    const u32 n = rig.engine.query_layers(rows.data(), 16, names.data(), static_cast<u32>(names.size()));
    AUREA_CHECK(n >= 2);
    u32 flagged = 0;
    for (u32 i = 0; i < n; ++i) {
        if (!(rows[i].flags & bridge::kLayerRowFlagCaptions)) continue;
        ++flagged;
        AUREA_CHECK_EQ(rows[i].id, rig.trackId);
    }
    // Só a faixa; a camada de vídeo de origem não é faixa.
    AUREA_CHECK_EQ(flagged, 1u);
}

// -----------------------------------------------------------------------------
// query_captions
// -----------------------------------------------------------------------------

AUREA_TEST(Captions, QueryReturnsBlocksInTimelineFramesWithTheirText) {
    CaptionRig rig(6);
    std::vector<bridge::CaptionRow> rows(64);
    std::vector<char> text(4096);
    const u64 r = rig.engine.query_captions(rows.data(), 64, text.data(), 4096);
    const u32 count = static_cast<u32>(r >> 32), total = static_cast<u32>(r & 0xFFFFFFFF);
    AUREA_CHECK_EQ(count, total);
    AUREA_CHECK(total > 0);
    for (u32 i = 0; i < count; ++i) {
        AUREA_CHECK(rows[i].end > rows[i].start);
        AUREA_CHECK(rows[i].textLength > 0);
        AUREA_CHECK_EQ(rows[i].layerId, rig.trackId);
        if (i) AUREA_CHECK(rows[i].start >= rows[i - 1].start);
    }
    // O primeiro bloco bate com o modelo, texto e tempo.
    const Layer* track = rig.track();
    if (track && count) {
        const std::string body(text.data() + rows[0].textOffset, rows[0].textLength);
        AUREA_CHECK_EQ(body, track->captions[0].text);
        AUREA_CHECK_EQ(rows[0].id, track->captions[0].id);
        AUREA_CHECK_EQ(rows[0].start, static_cast<i32>(track->captions[0].start));
        AUREA_CHECK_EQ(rows[0].words, static_cast<u32>(track->captions[0].words.size()));
    }
}

AUREA_TEST(Captions, QueryReportsMoreThanItWroteSoTheUiCanGrow) {
    CaptionRig rig(24);
    std::vector<bridge::CaptionRow> rows(2);
    std::vector<char> text(4096);
    const u64 r = rig.engine.query_captions(rows.data(), 2, text.data(), 4096);
    AUREA_CHECK_EQ(static_cast<u32>(r >> 32), 2u);
    AUREA_CHECK(static_cast<u32>(r & 0xFFFFFFFF) > 2u);
    // Blob de texto pequeno: nunca escreve texto cortado — reporta que falta.
    std::vector<bridge::CaptionRow> wide(64);
    std::vector<char> tiny(4);
    const u64 r2 = rig.engine.query_captions(wide.data(), 64, tiny.data(), 4);
    AUREA_CHECK(static_cast<u32>(r2 >> 32) < static_cast<u32>(r2 & 0xFFFFFFFF));
}

// -----------------------------------------------------------------------------
// Edição
// -----------------------------------------------------------------------------

AUREA_TEST(Captions, SplitCutsABlockInTwoWithoutLosingOrRepeatingWords) {
    CaptionRig rig(6);
    const Layer* before = rig.track();
    AUREA_CHECK(before != nullptr);
    if (!before) return;
    const usize wasBlocks = before->captions.size();
    const u64 id = before->captions[0].id;
    const i64 start = before->captions[0].start, end = before->captions[0].end;
    const usize wasWords = before->captions[0].words.size();
    std::string wasJoined;
    for (const auto& w : before->captions[0].words) wasJoined += (wasJoined.empty() ? "" : " ") + w.text;
    AUREA_CHECK(wasWords >= 2 && start + 2 < end);
    const i64 cut = start + (end - start) / 2;
    AUREA_CHECK(rig.edit("{\"op\":\"split\",\"ids\":[" + std::to_string(id) + "],\"frame\":" + std::to_string(cut) + "}"));
    const Layer* after = rig.track();
    AUREA_CHECK(after != nullptr);
    if (!after) return;
    AUREA_CHECK_EQ(after->captions.size(), wasBlocks + 1);
    AUREA_CHECK_EQ(after->captions[0].start, start);
    AUREA_CHECK_EQ(after->captions[0].end, cut);
    AUREA_CHECK_EQ(after->captions[1].start, cut);
    AUREA_CHECK_EQ(after->captions[1].end, end);
    // Nenhuma palavra perdida e nenhuma nos dois lados.
    const usize left = after->captions[0].words.size(), right = after->captions[1].words.size();
    AUREA_CHECK_EQ(left + right, wasWords);
    AUREA_CHECK_EQ(after->captions[0].text + " " + after->captions[1].text, wasJoined);
    // Desfazer volta ao que era (um passo).
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(rig.engine.apply_command(undo).ok());
    AUREA_CHECK_EQ(rig.blocks(), wasBlocks);
}

AUREA_TEST(Captions, MergeJoinsNeighboursAndRefusesAcrossAGap) {
    CaptionRig rig(6);
    const Layer* before = rig.track();
    AUREA_CHECK(before != nullptr);
    if (!before) return;
    AUREA_CHECK(before->captions.size() >= 2);
    if (before->captions.size() < 2) return;
    const usize wasBlocks = before->captions.size();
    const i64 end2 = before->captions[1].end;
    const std::string text1 = before->captions[0].text, text2 = before->captions[1].text;
    const usize wasWords = before->captions[0].words.size() + before->captions[1].words.size();
    AUREA_CHECK(rig.edit("{\"op\":\"merge\",\"ids\":" + rig.range_of(0, 1) + "}"));
    const Layer* merged = rig.track();
    AUREA_CHECK(merged != nullptr);
    if (!merged) return;
    AUREA_CHECK_EQ(merged->captions.size(), wasBlocks - 1);
    AUREA_CHECK_EQ(merged->captions[0].text, text1 + " " + text2);
    AUREA_CHECK_EQ(merged->captions[0].end, end2);
    AUREA_CHECK_EQ(merged->captions[0].words.size(), wasWords);
    // Com um bloco no meio, unir é recusado: o texto do vão não existiria.
    if (rig.blocks() >= 3) {
        const usize now = rig.blocks();
        AUREA_CHECK(!rig.edit("{\"op\":\"merge\",\"ids\":" + rig.range_of(0, 2) + "}"));
        AUREA_CHECK_EQ(rig.blocks(), now);
    }
}

AUREA_TEST(Captions, MoveBringsTheWordsAlongAndTrimKeepsThemInside) {
    CaptionRig rig(6);
    const Layer* before = rig.track();
    AUREA_CHECK(before != nullptr);
    if (!before) return;
    const i64 start = before->captions[0].start, end = before->captions[0].end;
    const usize wasWords = before->captions[0].words.size();
    const i64 word0 = wasWords ? before->captions[0].words[0].start : 0;

    // Mover para CIMA do vizinho é recusado: a faixa é uma sequência sem
    // sobreposição (é o que mantém uma linha só na timeline).
    AUREA_CHECK(!rig.edit("{\"op\":\"move\",\"ids\":" + rig.one_of(0) + ",\"delta\":10}"));
    AUREA_CHECK_EQ(rig.track()->captions[0].start, start);
    // Para trás, no espaço livre antes do primeiro bloco, anda — com as palavras.
    AUREA_CHECK(rig.edit("{\"op\":\"move\",\"ids\":" + rig.one_of(0) + ",\"delta\":-5}"));
    const Layer* moved = rig.track();
    AUREA_CHECK(moved != nullptr);
    if (moved) {
        AUREA_CHECK_EQ(moved->captions[0].start, start - 5);
        AUREA_CHECK_EQ(moved->captions[0].end, end - 5);
        if (wasWords) AUREA_CHECK_EQ(moved->captions[0].words[0].start, word0 - 5);
    }
    // Ajustar o tempo do ÚLTIMO bloco (sem vizinho à direita para invadir).
    const usize last = rig.blocks() - 1;
    const Layer* tail = rig.track();
    AUREA_CHECK(tail != nullptr);
    if (!tail) return;
    const i64 tailStart = tail->captions[last].start;
    const usize tailWords = tail->captions[last].words.size();
    AUREA_CHECK(rig.edit("{\"op\":\"trim\",\"ids\":" + rig.one_of(last) + ",\"start\":" + std::to_string(tailStart) +
                         ",\"end\":" + std::to_string(tailStart + 6) + "}"));
    const Layer* trimmed = rig.track();
    AUREA_CHECK(trimmed != nullptr);
    if (trimmed) {
        AUREA_CHECK_EQ(trimmed->captions[last].start, tailStart);
        AUREA_CHECK_EQ(trimmed->captions[last].end, tailStart + 6);
        AUREA_CHECK_EQ(trimmed->captions[last].words.size(), tailWords);
        for (const auto& w : trimmed->captions[last].words) AUREA_CHECK(w.start >= tailStart && w.end <= tailStart + 6);
    }
    // Comandos malformados não mexem em nada.
    const usize now = rig.blocks();
    AUREA_CHECK(!rig.edit("{\"op\":\"trim\",\"ids\":" + rig.one_of(0) + ",\"start\":50,\"end\":10}"));
    AUREA_CHECK(!rig.edit("{\"op\":\"naoexiste\",\"ids\":" + rig.one_of(0) + "}"));
    AUREA_CHECK(!rig.edit("{\"op\":\"move\",\"ids\":[],\"delta\":3}"));
    AUREA_CHECK(!rig.edit("{\"op\":\"style\",\"ids\":" + rig.one_of(0) + ",\"style\":99}"));
    AUREA_CHECK_EQ(rig.blocks(), now);
    // Apagar tira só o escolhido.
    AUREA_CHECK(rig.edit("{\"op\":\"delete\",\"ids\":" + rig.one_of(0) + "}"));
    AUREA_CHECK_EQ(rig.blocks(), now - 1);
}

AUREA_TEST(Captions, EditingTextKeepsTheTimingOfTheWordsThatStay) {
    CaptionRig rig(6);
    const Layer* before = rig.track();
    AUREA_CHECK(before != nullptr);
    if (!before) return;
    const usize wasWords = before->captions[0].words.size();
    AUREA_CHECK(wasWords >= 2);
    const std::string original = before->captions[0].text;
    const std::string w0 = before->captions[0].words[0].text;
    const i64 a0 = before->captions[0].words[0].start, a1 = before->captions[0].words[0].end;
    const std::string w1 = before->captions[0].words[1].text;
    const i64 b0 = before->captions[0].words[1].start, b1 = before->captions[0].words[1].end;
    const i64 blockEnd = before->captions[0].end;
    const i64 lastWordEnd = before->captions[0].words.back().end;
    auto editText = [&](const std::string& value) {
        json::Writer quoted;
        quoted.value(value);
        return rig.edit("{\"op\":\"text\",\"ids\":" + rig.one_of(0) + ",\"text\":" + quoted.str() + "}");
    };
    // Acrescenta uma palavra no fim: as que já estavam ficam com o MESMO tempo.
    const std::string grown = original + " novinho";
    AUREA_CHECK(editText(grown));
    const Layer* after = rig.track();
    AUREA_CHECK(after != nullptr);
    if (!after) return;
    AUREA_CHECK_EQ(after->captions[0].text, grown);
    AUREA_CHECK_EQ(after->captions[0].words.size(), wasWords + 1);
    AUREA_CHECK_EQ(after->captions[0].words[0].text, w0);
    AUREA_CHECK_EQ(after->captions[0].words[0].start, a0);
    AUREA_CHECK_EQ(after->captions[0].words[0].end, a1);
    AUREA_CHECK_EQ(after->captions[0].words[1].text, w1);
    AUREA_CHECK_EQ(after->captions[0].words[1].start, b0);
    AUREA_CHECK_EQ(after->captions[0].words[1].end, b1);
    // A nova caiu no vão que sobrou, dentro do bloco.
    if (after->captions[0].words.size() <= wasWords) return;
    const auto& added = after->captions[0].words[wasWords];
    AUREA_CHECK_EQ(added.text, std::string("novinho"));
    AUREA_CHECK(added.start >= lastWordEnd && added.end <= blockEnd && added.end > added.start);
    // Tirar a palavra nova mantém o tempo de todas as outras.
    AUREA_CHECK(editText(original));
    const Layer* back = rig.track();
    AUREA_CHECK(back != nullptr);
    if (back) {
        AUREA_CHECK_EQ(back->captions[0].words.size(), wasWords);
        AUREA_CHECK_EQ(back->captions[0].words[0].start, a0);
        AUREA_CHECK_EQ(back->captions[0].words[1].start, b0);
        AUREA_CHECK_EQ(back->captions[0].words[1].end, b1);
    }
    // Um bloco sem texto não desenha nada: recusado.
    AUREA_CHECK(!rig.edit("{\"op\":\"text\",\"ids\":" + rig.one_of(0) + ",\"text\":\"\"}"));
    std::printf("    '%s' + palavra nova: %zu -> %zu palavras, tempos intactos\n", w0.c_str(), wasWords, wasWords + 1);
}

AUREA_TEST(Captions, StyleSwitchAndCaptionPresetKeepsTheBlocksAndTiming) {
    CaptionRig rig(6);
    const Layer* before = rig.track();
    AUREA_CHECK(before != nullptr);
    if (!before) return;
    const usize wasBlocks = before->captions.size();
    const i64 wasStart = before->captions[0].start;
    const u32 wasStyle = before->captionOptions.style;
    AUREA_CHECK(wasStyle != 5u);
    AUREA_CHECK(rig.edit("{\"op\":\"style\",\"ids\":" + rig.one_of(0) + ",\"style\":5}"));
    const Layer* restyled = rig.track();
    AUREA_CHECK(restyled != nullptr);
    if (restyled) {
        AUREA_CHECK_EQ(restyled->captionOptions.style, 5u);
        AUREA_CHECK_EQ(restyled->captions.size(), wasBlocks);
        AUREA_CHECK_EQ(restyled->captions[0].start, wasStart);
        // O estilo "Pop" é mais pesado que o "Clássico" de origem.
        AUREA_CHECK_EQ(restyled->text.fontWeight, 800);
    }
    // Um preset de legenda aplica estilo E posição, sem tocar nos blocos.
    text::CaptionOptions o;
    o.style = 4;
    o.posY = 0.22f;
    o.sizeFrac = 0.09f;
    const std::string json = presets::make_caption_preset("Karaoke", o, false);
    AUREA_CHECK(!json.empty());
    std::string escaped;
    for (char ch : json) { if (ch == '\\' || ch == '"') escaped += '\\'; escaped += ch; }
    AUREA_CHECK(rig.edit("{\"op\":\"preset\",\"ids\":" + rig.one_of(0) + ",\"preset\":\"" + escaped + "\"}"));
    const Layer* applied = rig.track();
    AUREA_CHECK(applied != nullptr);
    if (applied) {
        AUREA_CHECK_EQ(applied->captionOptions.style, 4u);
        AUREA_CHECK_EQ(applied->captions.size(), wasBlocks);
        AUREA_CHECK(std::fabs(applied->captionOptions.posY - 0.22f) < 1e-6f);
        AUREA_CHECK(std::fabs(applied->transform.position.y - static_cast<f32>(rig.comp()->height()) * 0.22f) < 1.0f);
    }
    // Preset que não é de legenda é recusado.
    AUREA_CHECK(!rig.edit("{\"op\":\"preset\",\"ids\":" + rig.one_of(0) + ",\"preset\":\"{\\\"aurea_preset\\\":1,\\\"kind\\\":\\\"text\\\"}\"}"));
}

AUREA_TEST(Captions, WordLayoutAndPersonalPresetKeepEditedWords) {
    CaptionRig rig(8);
    AUREA_CHECK(rig.track() != nullptr);
    if (!rig.track()) return;
    AUREA_CHECK(rig.edit("{\"op\":\"text\",\"ids\":" + rig.one_of(0) + ",\"text\":\"Fala corrigida\"}"));
    auto tokens = [&] {
        std::vector<std::string> result;
        for (const auto& block : rig.track()->captions)
            for (const auto& word : block.words) result.push_back(word.text);
        return result;
    };
    const auto before = tokens();
    const auto start = rig.track()->captions.front().start;
    AUREA_CHECK(rig.edit("{\"op\":\"layout\",\"words\":1}"));
    AUREA_CHECK_EQ(rig.track()->captions.size(), before.size());
    AUREA_CHECK_EQ(rig.track()->captions.front().start, start);
    AUREA_CHECK(tokens() == before);
    auto appearance = rig.track()->captionOptions;
    appearance.look |= text::kCaptionLookFont;
    appearance.fontFamily = "Caption Fixture";
    appearance.fontPath = "docs:Fonts/CaptionFixture.ttf";
    AUREA_CHECK(rig.engine.set_caption_options(rig.trackId, appearance));
    auto preset = rig.engine.save_caption_bundle(rig.trackId, "Palavra por palavra");
    AUREA_CHECK(!preset.empty());
    if (preset.empty()) return;
    AUREA_CHECK(rig.edit("{\"op\":\"layout\",\"words\":3}"));
    AUREA_CHECK(rig.track()->captions.size() < before.size());
    for (const auto& block : rig.track()->captions) AUREA_CHECK(block.words.size() <= 3);
    AUREA_CHECK(tokens() == before);
    const usize groupedCount = rig.track()->captions.size();
    AUREA_CHECK(rig.engine.apply_caption_bundle(rig.trackId, preset));
    AUREA_CHECK_EQ(rig.track()->captions.size(), groupedCount); // Legacy presets style existing blocks.
    AUREA_CHECK(rig.edit("{\"op\":\"layout\",\"words\":2}"));
    preset.insert(1, "\"reflowWords\":true,");
    AUREA_CHECK(rig.engine.apply_caption_bundle(rig.trackId, preset));
    AUREA_CHECK_EQ(rig.track()->captions.size(), before.size());
    AUREA_CHECK_EQ(rig.track()->text.fontPath, appearance.fontPath);
    AUREA_CHECK(tokens() == before);
    AUREA_CHECK(!rig.edit("{\"op\":\"layout\",\"words\":0}"));
}

AUREA_TEST(Captions, RegroupRebuildsOnlyTheChosenBlocks) {
    CaptionRig rig(8);
    const Layer* before = rig.track();
    AUREA_CHECK(before != nullptr);
    if (!before) return;
    AUREA_CHECK(before->captions.size() >= 2);
    if (before->captions.size() < 2) return;
    // Uma palavra por bloco: reagrupar pode voltar a juntar.
    AUREA_CHECK(rig.edit("{\"op\":\"regroup\",\"ids\":" + rig.range_of(0, 0) + "}"));
    const Layer* after = rig.track();
    AUREA_CHECK(after != nullptr);
    if (!after) return;
    AUREA_CHECK(after->captions.size() >= 1);
    // As palavras continuam todas lá, em ordem, dentro dos blocos.
    usize words = 0;
    i64 previous = 0;
    for (const auto& s : after->captions) {
        AUREA_CHECK(s.end > s.start);
        AUREA_CHECK(s.start >= previous);
        previous = s.end;
        for (const auto& w : s.words) {
            AUREA_CHECK(w.start >= s.start && w.end <= s.end && w.end > w.start);
            ++words;
        }
    }
    AUREA_CHECK_EQ(words, 8u);
    // Um intervalo com um bloco não escolhido no meio é recusado.
    if (after->captions.size() >= 3) {
        const usize now = rig.blocks();
        AUREA_CHECK(!rig.edit("{\"op\":\"regroup\",\"ids\":" + rig.range_of(0, 2) + "}"));
        AUREA_CHECK_EQ(rig.blocks(), now);
    }
}

AUREA_TEST(Captions, WrongIdsOrTheWrongLayerChangeNothing) {
    CaptionRig rig(6);
    const usize wasBlocks = rig.blocks();
    AUREA_CHECK(!rig.edit("{\"op\":\"delete\",\"ids\":[999999]}"));
    AUREA_CHECK(!rig.edit("{\"op\":\"delete\",\"ids\":[0]}"));
    // A camada de vídeo não é uma faixa de legendas.
    AUREA_CHECK(!rig.engine.edit_caption_track(rig.video, "{\"op\":\"delete\",\"ids\":" + rig.one_of(0) + "}"));
    AUREA_CHECK(!rig.engine.edit_caption_track(0, "{\"op\":\"delete\",\"ids\":" + rig.one_of(0) + "}"));
    AUREA_CHECK_EQ(rig.blocks(), wasBlocks);
}

// -----------------------------------------------------------------------------
// Serialização e preset
// -----------------------------------------------------------------------------

AUREA_TEST(Captions, BlocksAndPerWordTimingSurviveSaveAndReopen) {
    CaptionRig rig(6);
    const Layer* before = rig.track();
    AUREA_CHECK(before != nullptr);
    if (!before) return;
    const usize wasBlocks = before->captions.size();
    const usize wasWords = before->captions[0].words.size();
    const i64 start = before->captions[0].start;
    const i64 lastWord = wasWords ? before->captions[0].words.back().start : 0;
    const u32 style = before->captionOptions.style;
    const std::string path = caption_path("faixa");
    AUREA_CHECK(rig.engine.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.engine.load_project(path.c_str()).ok());
    const Layer* after = rig.track();
    AUREA_CHECK(after != nullptr);
    if (!after) return;
    AUREA_CHECK_EQ(after->captions.size(), wasBlocks);
    AUREA_CHECK_EQ(after->captions[0].start, start);
    AUREA_CHECK_EQ(after->captions[0].words.size(), wasWords);
    if (wasWords) AUREA_CHECK_EQ(after->captions[0].words.back().start, lastWord);
    AUREA_CHECK_EQ(after->captionOptions.style, style);
    std::remove(path.c_str());
}

AUREA_TEST(Captions, RichBundlePreservesWordsAndRejectsInvalidComponents) {
    CaptionRig rig(6);
    const auto original = rig.engine.caption_tracks();
    const auto bundle = rig.engine.save_caption_bundle(rig.trackId, "Legenda de teste");
    AUREA_CHECK(!bundle.empty());
    AUREA_CHECK(rig.engine.apply_caption_bundle(rig.trackId, bundle));
    AUREA_CHECK_EQ(rig.engine.caption_tracks(), original);
    AUREA_CHECK(!rig.engine.apply_caption_bundle(rig.trackId, "{\"schema\":1}"));
    AUREA_CHECK_EQ(rig.engine.caption_tracks(), original);
    if (const char* output = std::getenv("AUREA_CAPTION_BUNDLE_DUMP")) {
        std::ofstream file(output, std::ios::binary);
        file << bundle;
        AUREA_CHECK(file.good());
    }
}

AUREA_TEST(Captions, CaptionPresetRoundTripsEveryFieldTheTrackUses) {
    text::CaptionOptions o;
    o.mode = 1;
    o.maxWords = 3;
    o.maxChars = 24;
    o.maxLines = 3;
    o.style = 4;
    o.highlight = false;
    o.uppercase = true;
    o.breakOnPause = false;
    o.pauseSec = 0.45f;
    o.posY = 0.22f;
    o.sizeFrac = 0.09f;
    o.highlightColor = Vec4{0.1f, 0.2f, 0.3f, 1.0f};
    const std::string json = presets::make_caption_preset("Karaoke do Ruan", o, true);
    AUREA_CHECK(!json.empty());
    presets::Preset p;
    std::string error;
    AUREA_CHECK(presets::parse(json, p, nullptr, &error));
    AUREA_CHECK(p.kind == presets::PresetKind::Caption);
    AUREA_CHECK_EQ(p.name, std::string("Karaoke do Ruan"));
    AUREA_CHECK(p.removeFillers);
    AUREA_CHECK_EQ(p.caption.mode, o.mode);
    AUREA_CHECK_EQ(p.caption.maxWords, o.maxWords);
    AUREA_CHECK_EQ(p.caption.maxChars, o.maxChars);
    AUREA_CHECK_EQ(p.caption.maxLines, o.maxLines);
    AUREA_CHECK_EQ(p.caption.style, o.style);
    AUREA_CHECK_EQ(p.caption.highlight, o.highlight);
    AUREA_CHECK_EQ(p.caption.uppercase, o.uppercase);
    AUREA_CHECK_EQ(p.caption.breakOnPause, o.breakOnPause);
    AUREA_CHECK(std::fabs(p.caption.pauseSec - o.pauseSec) < 1e-6f);
    AUREA_CHECK(std::fabs(p.caption.posY - o.posY) < 1e-6f);
    AUREA_CHECK(std::fabs(p.caption.sizeFrac - o.sizeFrac) < 1e-6f);
    AUREA_CHECK(std::fabs(p.caption.highlightColor.x - o.highlightColor.x) < 1e-6f);
    AUREA_CHECK(std::fabs(p.caption.highlightColor.z - o.highlightColor.z) < 1e-6f);
    // Estilo fora da faixa válida é recusado na leitura.
    presets::Preset ignored;
    AUREA_CHECK(!presets::parse("{\"aurea_preset\":1,\"kind\":\"caption\",\"name\":\"x\",\"caption\":{\"style\":99}}", ignored, nullptr, &error));
}

// -----------------------------------------------------------------------------
// 0.4.6: aparência própria da legenda (fonte, cores, contorno, fundo, sombra),
// modelos prontos e troca de opções numa faixa já pronta.
// -----------------------------------------------------------------------------

namespace {
bool near_color(const Vec4& a, const Vec4& b) {
    return std::fabs(a.x - b.x) < 1e-4f && std::fabs(a.y - b.y) < 1e-4f && std::fabs(a.z - b.z) < 1e-4f && std::fabs(a.w - b.w) < 1e-4f;
}
bool undo_once(Engine& engine) {
    Command u;
    u.type = CommandType::Undo;
    return engine.apply_command(u).ok();
}
} // namespace

AUREA_TEST(Captions, ConvertTrackToEditableTextWithOneUndo) {
    text::CaptionOptions options;
    options.maxWords = 2;
    options.uppercase = true;
    options.style = 5;
    CaptionRig rig(8, options);
    Layer* source = rig.track_mut();
    AUREA_CHECK(source != nullptr && source->captions.size() > 1);
    if (!source || source->captions.size() <= 1) return;
    const LayerId id = LayerId::unpack(rig.trackId);
    auto& comp = *rig.comp();
    const auto rejectedLayer = comp.add_layer(LayerKind::Shape, "Needs the caption track");
    comp.layer(rejectedLayer)->matteSource = id;
    AUREA_CHECK(!rig.edit(R"({"op":"to_layers"})"));
    AUREA_CHECK(rig.track() != nullptr);
    comp.layer(rejectedLayer)->matteSource = {};
    source = rig.track_mut();
    const auto originalPlacement = source->transform.position;
    auto look = options;
    look.look = text::kCaptionLookFill;
    look.fill = {0.2f, 0.5f, 0.7f, 1.0f};
    source->captions[1].look = look;
    source->offset = FrameIndex{source->captions.front().start + 1};
    source->start = FrameIndex{17};
    const auto originalStart = source->start;
    const auto segments = source->captions;
    const auto firstIndex = static_cast<u32>(comp.z_index_of(id));
    const u32 originalLayerCount = comp.order().size();
    const i64 shift = source->start.value - source->offset.value;
    AUREA_CHECK(rig.edit(R"({"op":"to_layers","ids":[]})"));
    AUREA_CHECK(!rig.track());
    AUREA_CHECK_EQ(comp.order().size(), originalLayerCount + segments.size() - 1);
    AUREA_CHECK(comp.order().at(firstIndex) == id);
    AUREA_CHECK(comp.order().at(firstIndex + static_cast<u32>(segments.size())) == rejectedLayer);
    u32 previousTrack = 0;
    for (u32 i = 0; i < segments.size(); ++i) {
        const Layer* layer = comp.layer(comp.order().at(firstIndex + i));
        AUREA_CHECK(layer != nullptr);
        if (!layer) return;
        AUREA_CHECK(layer->kind == LayerKind::Text && layer->captions.empty());
        AUREA_CHECK(layer->trackId != previousTrack && layer->text.captionSource == 0);
        previousTrack = layer->trackId;
        AUREA_CHECK_EQ(layer->text.content, text::upper_text(segments[i].text));
        AUREA_CHECK_EQ(layer->start.value, std::max(originalStart.value, segments[i].start + shift));
        AUREA_CHECK_EQ(layer->end.value, segments[i].end + shift);
        AUREA_CHECK_EQ(layer->timeline_time(FrameIndex{segments[i].start}).value, segments[i].start + shift);
        AUREA_CHECK_EQ(layer->transform.position.y, originalPlacement.y);
        AUREA_CHECK_EQ(layer->text.animators.size(), 1u);
        const auto* timing = layer->tracks.find(TrackProperty::TextAnimParam, 0, text::kSelStart);
        AUREA_CHECK(timing && timing->keys.front().time.value == segments[i].words.front().start);
        if (i == 1) AUREA_CHECK(near_color(layer->text.color, look.fill));
    }
    AUREA_CHECK(!rig.edit(R"({"op":"to_layers"})"));
    AUREA_CHECK(undo_once(rig.engine));
    AUREA_CHECK_EQ(rig.blocks(), segments.size());
    AUREA_CHECK_EQ(rig.comp()->order().size(), originalLayerCount);
}

AUREA_TEST(Captions, LookChosenBeforeGeneratingIsOnTheTrack) {
    text::CaptionOptions o;
    o.style = 2;
    o.look = text::kCaptionLookFont | text::kCaptionLookFill | text::kCaptionLookStroke | text::kCaptionLookBox;
    o.fontFamily = "Serif Teste"; o.fontWeight = 500; o.fontItalic = true;
    o.fill = Vec4{1.0f, 0.2f, 0.4f, 1.0f};
    o.stroke = Vec4{0.0f, 0.0f, 1.0f, 1.0f}; o.strokeFrac = 0.0f;
    o.box = true; o.boxColor = Vec4{1.0f, 1.0f, 1.0f, 0.9f};
    CaptionRig rig(6, o);
    const Layer* l = rig.track();
    AUREA_CHECK(l != nullptr); if (!l) return;
    AUREA_CHECK_EQ(l->text.fontFamily, std::string("Serif Teste"));
    AUREA_CHECK_EQ(static_cast<u32>(l->text.fontWeight), 500u);
    AUREA_CHECK(l->text.fontItalic);
    AUREA_CHECK(near_color(l->text.color, o.fill));
    AUREA_CHECK_EQ(l->text.strokeWidth, 0.0f);          // contorno "nenhum" vence o do estilo
    AUREA_CHECK(l->text.background);
    AUREA_CHECK(near_color(l->text.backgroundColor, o.boxColor));
    AUREA_CHECK(l->text.shadow);                        // sem o bit, a sombra é a do estilo Destaque
}

AUREA_TEST(Captions, ChangingOptionsOnAReadyTrackIsOneUndoStep) {
    CaptionRig rig(6);
    const Layer* l = rig.track();
    AUREA_CHECK(l != nullptr); if (!l) return;
    const Vec4 styleColor = l->text.color;
    const f32 size = l->text.size, stroke = l->text.strokeWidth;
    const f32 y = l->transform.position.y;
    const auto blocks = rig.engine.caption_tracks();
    text::CaptionOptions o = l->captionOptions;
    o.look = text::kCaptionLookFill; o.fill = Vec4{0.0f, 1.0f, 0.0f, 1.0f};
    o.sizeFrac = l->captionOptions.sizeFrac * 2.0f;
    o.posY = 0.2f;
    // A camada da fala também acha a faixa (é o que o painel tem selecionado).
    AUREA_CHECK(rig.engine.set_caption_options(rig.video, o));
    l = rig.track();
    AUREA_CHECK(near_color(l->text.color, o.fill));
    AUREA_CHECK(std::fabs(l->text.size - std::round(size * 2.0f)) <= 1.0f);
    AUREA_CHECK(std::fabs(l->text.strokeWidth - stroke * l->text.size / size) < 0.01f);
    AUREA_CHECK(std::fabs(l->transform.position.y - 1920.0f * 0.2f) < 0.5f);
    AUREA_CHECK_EQ(rig.engine.caption_tracks(), blocks);   // blocos e tempos não mudam
    AUREA_CHECK(undo_once(rig.engine));
    l = rig.track();
    AUREA_CHECK(near_color(l->text.color, styleColor));
    AUREA_CHECK_EQ(l->text.size, size);
    AUREA_CHECK_EQ(l->transform.position.y, y);
}

AUREA_TEST(Captions, ClearingALookPartReturnsToTheStyle) {
    text::CaptionOptions o;
    o.style = 0;
    o.look = text::kCaptionLookFill | text::kCaptionLookFont;
    o.fill = Vec4{1, 0, 0, 1}; o.fontFamily = "Mono Teste";
    CaptionRig rig(4, o);
    text::CaptionOptions next = rig.track()->captionOptions;
    next.look = 0;
    AUREA_CHECK(rig.engine.set_caption_options(rig.trackId, next));
    const Layer* l = rig.track();
    AUREA_CHECK(near_color(l->text.color, Vec4{1, 1, 1, 1}));   // Clássico: branco
    AUREA_CHECK(l->text.fontFamily.empty());
    AUREA_CHECK_EQ(static_cast<u32>(l->text.fontWeight), 700u);
}

AUREA_TEST(Captions, SizeChangeKeepsTheLookFromTheTextPanel) {
    CaptionRig rig(4);
    // Cor posta pelo painel Texto (ou por um preset da comunidade), fora das opções.
    rig.track_mut()->text.color = Vec4{0.3f, 0.6f, 0.9f, 1.0f};
    text::CaptionOptions o = rig.track()->captionOptions;
    o.sizeFrac = 0.09f;
    o.uppercase = true;
    AUREA_CHECK(rig.engine.set_caption_options(rig.trackId, o));
    AUREA_CHECK(near_color(rig.track()->text.color, Vec4{0.3f, 0.6f, 0.9f, 1.0f}));
    AUREA_CHECK(rig.track()->captionOptions.uppercase);
    // Trocar o ESTILO refaz a aparência inteira.
    o.style = 3;
    AUREA_CHECK(rig.engine.set_caption_options(rig.trackId, o));
    AUREA_CHECK(!near_color(rig.track()->text.color, Vec4{0.3f, 0.6f, 0.9f, 1.0f}));
}

AUREA_TEST(Captions, SetOptionsRefusesLayersWithoutTrack) {
    CaptionRig rig(4);
    AUREA_CHECK(!rig.engine.set_caption_options(0, {}));
    AUREA_CHECK(rig.engine.caption_options(0).empty());
    AUREA_CHECK(!rig.engine.caption_options(rig.video).empty());
}

AUREA_TEST(Captions, FullOptionsSurviveSaveAndReopen) {
    text::CaptionOptions o;
    o.style = 1; o.uppercase = true; o.posY = 0.3f; o.sizeFrac = 0.05f; o.maxWords = 3;
    o.look = text::kCaptionLookAll;
    o.fontFamily = "Fonte X"; o.fontWeight = 900; o.fontItalic = true;
    o.fill = Vec4{0.1f, 0.2f, 0.3f, 1.0f}; o.stroke = Vec4{0.9f, 0.8f, 0.7f, 1.0f}; o.strokeFrac = 0.12f;
    o.box = true; o.boxColor = Vec4{0.5f, 0.5f, 0.5f, 0.5f}; o.shadow = true;
    CaptionRig rig(6, o);
    const std::string path = caption_path("aparencia");
    AUREA_CHECK(rig.engine.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.engine.load_project(path.c_str()).ok());
    const Layer* l = rig.track();
    AUREA_CHECK(l != nullptr); if (!l) { std::remove(path.c_str()); return; }
    const auto& c = l->captionOptions;
    AUREA_CHECK(c.uppercase);
    AUREA_CHECK(std::fabs(c.posY - 0.3f) < 1e-6f);
    AUREA_CHECK(std::fabs(c.sizeFrac - 0.05f) < 1e-6f);
    AUREA_CHECK_EQ(c.maxWords, 3u);
    AUREA_CHECK_EQ(c.look, text::kCaptionLookAll);
    AUREA_CHECK_EQ(c.fontFamily, std::string("Fonte X"));
    AUREA_CHECK_EQ(static_cast<u32>(c.fontWeight), 900u);
    AUREA_CHECK(c.fontItalic);
    AUREA_CHECK(near_color(c.fill, o.fill));
    AUREA_CHECK(near_color(c.stroke, o.stroke));
    AUREA_CHECK(std::fabs(c.strokeFrac - 0.12f) < 1e-6f);
    AUREA_CHECK(c.box && c.shadow);
    AUREA_CHECK(near_color(c.boxColor, o.boxColor));
    std::remove(path.c_str());
}

AUREA_TEST(Captions, OptionsJsonRoundTripsLookAndOldPresetsHaveNone) {
    text::CaptionOptions o;
    o.style = 5; o.look = text::kCaptionLookFill | text::kCaptionLookShadow;
    o.fill = Vec4{1, 0.5f, 0, 1}; o.shadow = true; o.fontFamily = "Y"; o.fontPath = "docs:fonts/y.ttf";
    const std::string json = presets::caption_options_json(o, false);
    text::CaptionOptions back; bool fillers = true;
    AUREA_CHECK(presets::parse_caption_options(json, back, fillers));
    AUREA_CHECK(!fillers);
    AUREA_CHECK_EQ(back.style, 5u);
    AUREA_CHECK_EQ(back.look, o.look);
    AUREA_CHECK(near_color(back.fill, o.fill));
    AUREA_CHECK(back.shadow);
    AUREA_CHECK_EQ(back.fontPath, o.fontPath);
    // Preset de antes da 0.4.6: sem aparência própria.
    presets::Preset p; std::string error;
    AUREA_CHECK(presets::parse("{\"aurea_preset\":1,\"kind\":\"caption\",\"name\":\"x\",\"caption\":{\"style\":1}}", p, nullptr, &error));
    AUREA_CHECK_EQ(p.caption.look, 0u);
    // Bits desconhecidos e cor fora de 0..1 são recusados.
    AUREA_CHECK(!presets::parse_caption_options("{\"look\":64}", back, fillers));
    AUREA_CHECK(!presets::parse_caption_options("{\"fill\":[2,0,0,1]}", back, fillers));
    AUREA_CHECK(!presets::parse_caption_options("[]", back, fillers));
}

AUREA_TEST(Captions, TemplatesAreDistinctAndKeepTheUsersFont) {
    const auto& list = text::caption_templates();
    AUREA_CHECK(list.size() >= 14u);
    std::vector<std::string> ids;
    for (const auto& t : list) {
        ids.push_back(t.id);
        AUREA_CHECK(t.options.style < text::kCaptionStyleCount);
        AUREA_CHECK_EQ(t.options.look & text::kCaptionLookFont, 0u);   // modelo não escolhe fonte
    }
    std::sort(ids.begin(), ids.end());
    AUREA_CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    text::CaptionOptions mine;
    mine.look = text::kCaptionLookFont; mine.fontFamily = "Minha"; mine.posY = 0.3f; mine.maxWords = 2;
    const auto& bubble = *std::find_if(list.begin(), list.end(), [](const auto& t) { return std::string(t.id) == "bubble"; });
    text::apply_caption_template(bubble, mine);
    AUREA_CHECK_EQ(mine.style, bubble.options.style);
    AUREA_CHECK(mine.look & text::kCaptionLookFont);
    AUREA_CHECK(mine.look & text::kCaptionLookBox);
    AUREA_CHECK_EQ(mine.fontFamily, std::string("Minha"));
    AUREA_CHECK(std::fabs(mine.posY - 0.3f) < 1e-6f);   // posição e agrupamento continuam
    AUREA_CHECK_EQ(mine.maxWords, 2u);
    // O JSON da interface tem id, opções e a prévia resolvida.
    json::Value root;
    AUREA_CHECK(json::parse(presets::caption_templates_json(), root));
    AUREA_CHECK(root.is_array() && root.array.size() == list.size());
    const json::Value* preview = root.array.empty() ? nullptr : root.array[0].get("preview");
    AUREA_CHECK(preview && preview->get("fill") && preview->get("strokeFrac"));
}

// -----------------------------------------------------------------------------
// 0.4.6: estilo por bloco — marcar alguns blocos e mudar só eles.
// -----------------------------------------------------------------------------

namespace {
std::string look_command(const std::string& ids, const text::CaptionOptions& o) {
    json::Writer w;
    w.begin_object().key("op").value("look").key("options").value(presets::caption_options_json(o, true));
    std::string out = w.end_object().str();
    out.insert(out.size() - 1, ",\"ids\":" + ids);
    return out;
}
} // namespace

AUREA_TEST(Captions, BlockLookChangesOnlyTheChosenBlocksAndIsOneUndoStep) {
    CaptionRig rig(12);
    AUREA_CHECK(rig.blocks() >= 3u); if (rig.blocks() < 3u) return;
    const auto trackOptions = presets::caption_options_json(rig.track()->captionOptions, true);
    const auto content = rig.track()->text.content;
    text::CaptionOptions o;
    o.style = 3; o.uppercase = true; o.posY = 0.2f; o.maxWords = 1;   // posição/agrupamento não entram
    o.look = text::kCaptionLookFill; o.fill = Vec4{1, 0, 0, 1};
    AUREA_CHECK(rig.edit(look_command(rig.one_of(1), o)));
    const Layer* l = rig.track();
    AUREA_CHECK(!l->captions[0].look && !l->captions[2].look);
    AUREA_CHECK(l->captions[1].look.has_value());
    if (l->captions[1].look) {
        const auto& b = *l->captions[1].look;
        AUREA_CHECK_EQ(b.style, 3u);
        AUREA_CHECK(b.uppercase);
        AUREA_CHECK(near_color(b.fill, o.fill));
        // As opções do bloco: a aparência dele, o resto da faixa.
        const auto merged = text::caption_block_options(l->captionOptions, l->captions[1]);
        AUREA_CHECK_EQ(merged.style, 3u);
        AUREA_CHECK_EQ(merged.maxWords, l->captionOptions.maxWords);
        AUREA_CHECK_EQ(merged.posY, l->captionOptions.posY);
    }
    // A faixa não muda: opções e texto da camada continuam.
    AUREA_CHECK_EQ(presets::caption_options_json(l->captionOptions, true), trackOptions);
    AUREA_CHECK_EQ(l->text.content, content);
    // O JSON dos blocos diz qual tem estilo próprio.
    json::Value root;
    AUREA_CHECK(json::parse(rig.engine.caption_tracks(), root) && root.is_array() && !root.array.empty());
    if (root.is_array() && !root.array.empty()) {
        const auto* segs = root.array[0].get("segments");
        AUREA_CHECK(segs && segs->array.size() >= 3u);
        if (segs && segs->array.size() >= 3u) {
            AUREA_CHECK(segs->array[0].get("look") == nullptr);
            const auto* look = segs->array[1].get("look");
            AUREA_CHECK(look && look->is_string());
            text::CaptionOptions back; bool rf = false;
            AUREA_CHECK(look && presets::parse_caption_options(look->string, back, rf) && back.style == 3u);
        }
    }
    // Mandar o mesmo de novo não cria passo vazio: um desfazer volta tudo.
    AUREA_CHECK(rig.edit(look_command(rig.one_of(1), o)));
    AUREA_CHECK(undo_once(rig.engine));
    AUREA_CHECK(!rig.track()->captions[1].look);
}

AUREA_TEST(Captions, BlockLookOnManyBlocksAndClearReturnsToTheTrack) {
    CaptionRig rig(12);
    AUREA_CHECK(rig.blocks() >= 3u); if (rig.blocks() < 3u) return;
    text::CaptionOptions o; o.style = 1;
    AUREA_CHECK(rig.edit(look_command(rig.range_of(0, rig.blocks() - 1), o)));
    for (const auto& s : rig.track()->captions) AUREA_CHECK(s.look && s.look->style == 1u);
    AUREA_CHECK(rig.edit("{\"op\":\"clear_look\",\"ids\":" + rig.one_of(0) + "}"));
    AUREA_CHECK(!rig.track()->captions[0].look);
    AUREA_CHECK(rig.track()->captions[1].look.has_value());
    // Limpar o que já está limpo: nada muda, sem passo de desfazer.
    AUREA_CHECK(rig.edit("{\"op\":\"clear_look\",\"ids\":" + rig.one_of(0) + "}"));
    AUREA_CHECK(undo_once(rig.engine));
    AUREA_CHECK(rig.track()->captions[0].look.has_value());
    // Opções inválidas não mexem em nada.
    AUREA_CHECK(!rig.edit("{\"op\":\"look\",\"ids\":" + rig.one_of(0) + ",\"options\":\"{nada\"}"));
    AUREA_CHECK(!rig.edit("{\"op\":\"look\",\"ids\":" + rig.one_of(0) + "}"));
}

AUREA_TEST(Captions, SplitKeepsTheBlockLookOnBothHalves) {
    CaptionRig rig(12);
    const Layer* l = rig.track();
    AUREA_CHECK(l && !l->captions.empty()); if (!l || l->captions.empty()) return;
    const i64 cut = (l->captions[0].start + l->captions[0].end) / 2;
    AUREA_CHECK(cut > l->captions[0].start); if (cut <= l->captions[0].start) return;
    text::CaptionOptions o; o.style = 4;
    AUREA_CHECK(rig.edit(look_command(rig.one_of(0), o)));
    AUREA_CHECK(rig.edit("{\"op\":\"split\",\"ids\":" + rig.one_of(0) + ",\"frame\":" + std::to_string(cut) + "}"));
    l = rig.track();
    AUREA_CHECK(l->captions[0].look && l->captions[0].look->style == 4u);
    AUREA_CHECK(l->captions[1].look && l->captions[1].look->style == 4u);
}

AUREA_TEST(Captions, BlockLookIsDrawnOverTheTrackTextAndKeepsUserAnimators) {
    text::CaptionOptions track;   // Clássico, sem aparência própria
    TextData t; TrackSet tracks;
    text::apply_caption_style(track, 1080, t, tracks, {}, 0);
    t.color = Vec4{0.3f, 0.6f, 0.9f, 1.0f};            // cor do painel Texto
    TextAnimator mine; mine.name = "Meu";
    t.animators.push_back(mine);
    // Só o contorno no bloco: a cor do painel Texto fica.
    text::CaptionOptions block = track;
    block.look = text::kCaptionLookStroke; block.stroke = Vec4{1, 0, 0, 1}; block.strokeFrac = 0.1f;
    TextData a = t;
    text::apply_caption_block_look(track, block, 1080, a);
    AUREA_CHECK(near_color(a.color, t.color));
    AUREA_CHECK(near_color(a.strokeColor, block.stroke));
    AUREA_CHECK_EQ(a.animators.size(), static_cast<usize>(1));
    // Outro estilo: a aparência é a do estilo do bloco, os animadores ficam.
    block = track; block.style = 1;   // Caixa
    TextData b = t;
    text::apply_caption_block_look(track, block, 1080, b);
    AUREA_CHECK(b.background);
    AUREA_CHECK_EQ(b.size, t.size);    // tamanho é o da faixa
    AUREA_CHECK_EQ(b.animators.size(), static_cast<usize>(1));
    AUREA_CHECK(!b.animators.empty() && b.animators[0].name == "Meu");
}

AUREA_TEST(Captions, BlockLookSurvivesSaveAndReopen) {
    CaptionRig rig(12);
    AUREA_CHECK(rig.blocks() >= 2u); if (rig.blocks() < 2u) return;
    text::CaptionOptions o;
    o.style = 2; o.highlight = false; o.uppercase = true; o.highlightColor = Vec4{0, 1, 1, 1};
    o.look = text::kCaptionLookAll; o.fontFamily = "Bloco"; o.fontWeight = 300; o.fontItalic = true;
    o.fill = Vec4{0.1f, 0.2f, 0.3f, 1}; o.stroke = Vec4{0.4f, 0.5f, 0.6f, 1}; o.strokeFrac = 0.05f;
    o.box = true; o.boxColor = Vec4{0.7f, 0.8f, 0.9f, 0.5f}; o.shadow = true;
    AUREA_CHECK(rig.edit(look_command(rig.one_of(1), o)));
    text::CaptionOptions want;
    text::copy_caption_appearance(o, want);
    const std::string path = caption_path("bloco");
    AUREA_CHECK(rig.engine.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.engine.load_project(path.c_str()).ok());
    const Layer* l = rig.track();
    AUREA_CHECK(l && l->captions.size() >= 2u); if (!l || l->captions.size() < 2u) { std::remove(path.c_str()); return; }
    AUREA_CHECK(!l->captions[0].look);
    AUREA_CHECK(l->captions[1].look.has_value());
    if (l->captions[1].look)
        AUREA_CHECK_EQ(presets::caption_options_json(*l->captions[1].look, true), presets::caption_options_json(want, true));
    // O preset da faixa (bundle) aplica na faixa e não apaga o estilo do bloco.
    rig.remember_track();
    const auto bundle = rig.engine.save_caption_bundle(rig.trackId, "Com bloco");
    AUREA_CHECK(!bundle.empty());
    AUREA_CHECK(rig.engine.apply_caption_bundle(rig.trackId, bundle));
    AUREA_CHECK(rig.track()->captions[1].look.has_value());
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// 0.5.3: legendas no jeito do Premiere (lista, transcrição, criar, arquivos)
// -----------------------------------------------------------------------------

namespace {
std::string id_list(const Layer* l, std::initializer_list<usize> at) {
    std::string out = "[";
    for (usize i : at) out += (out.size() > 1 ? "," : "") + std::to_string(l->captions[i].id);
    return out + "]";
}
} // namespace

AUREA_TEST(Captions, DefaultsOfTheNewOptionsDrawExactlyLikeBefore) {
    // Projeto antigo: as opções novas nascem nos padrões e o texto sai igual ao de antes.
    text::CaptionOptions o;
    o.style = 1;
    o.look = text::kCaptionLookBox | text::kCaptionLookShadow;
    o.box = true; o.shadow = true;
    TextData t; TrackSet tracks;
    text::apply_caption_style(o, 1000, t, tracks, {}, 0);
    const f32 size = t.size;
    AUREA_CHECK(std::fabs(t.backgroundPadding - size * 0.3f) < 1e-4f);
    AUREA_CHECK(std::fabs(t.backgroundRadius - size * 0.25f) < 1e-4f);
    AUREA_CHECK(std::fabs(t.shadowBlur - size * 0.12f) < 1e-4f);
    AUREA_CHECK(near_color(t.shadowColor, Vec4{0, 0, 0, 0.6f}));
    AUREA_CHECK_EQ(t.alignment, 1u);
    AUREA_CHECK(std::fabs(t.lineHeight - 1.15f) < 1e-6f);
    // Grupos iguais com a duração mínima padrão (0,8 s) e sem fim de frase forçado.
    std::vector<text::CaptionWord> words;
    for (u32 i = 0; i < 12; ++i) words.push_back({i % 4 == 3 ? "fim." : "palavra", 1.0 + i * 0.4, 1.3 + i * 0.4});
    text::CaptionOptions a, b;
    b.minDurationSec = 0.8f;
    const auto ga = text::group_captions(words, a), gb = text::group_captions(words, b);
    AUREA_CHECK_EQ(ga.size(), gb.size());
    for (usize i = 0; i < ga.size() && i < gb.size(); ++i) AUREA_CHECK(ga[i].start == gb[i].start && ga[i].end == gb[i].end && ga[i].text == gb[i].text);
}

AUREA_TEST(Captions, LayoutBoxAndShadowOptionsReachTheText) {
    text::CaptionOptions o;
    o.look = text::kCaptionLookLayout | text::kCaptionLookBox | text::kCaptionLookShadow;
    o.align = 0; o.lineSpacing = 1.6f; o.box = true; o.boxPadding = 0.5f; o.boxRadius = 0.0f;
    o.shadow = true; o.shadowColor = Vec4{1, 0, 0, 1}; o.shadowBlur = 0.3f;
    TextData t; TrackSet tracks;
    text::apply_caption_style(o, 1000, t, tracks, {}, 0);
    AUREA_CHECK_EQ(t.alignment, 0u);
    AUREA_CHECK(std::fabs(t.lineHeight - 1.6f) < 1e-6f);
    AUREA_CHECK(std::fabs(t.backgroundPadding - t.size * 0.5f) < 1e-4f);
    AUREA_CHECK(t.backgroundRadius == 0.0f);
    AUREA_CHECK(near_color(t.shadowColor, Vec4{1, 0, 0, 1}));
    AUREA_CHECK(std::fabs(t.shadowBlur - t.size * 0.3f) < 1e-4f);
    // Na faixa pronta: trocar só o alinhamento mexe só no alinhamento (um passo).
    CaptionRig rig(6, o);
    text::CaptionOptions next = o; next.align = 2;
    AUREA_CHECK(rig.engine.set_caption_options(rig.trackId, next));
    const Layer* l = rig.track();
    AUREA_CHECK(l != nullptr); if (!l) return;
    AUREA_CHECK_EQ(l->text.alignment, 2u);
    AUREA_CHECK(std::fabs(l->text.lineHeight - 1.6f) < 1e-6f);
    AUREA_CHECK(undo_once(rig.engine));
    AUREA_CHECK_EQ(rig.track()->text.alignment, 0u);
}

AUREA_TEST(Captions, MinimumDurationGapAndSentenceBreakShapeTheBlocks) {
    std::vector<text::CaptionWord> words = {{"Oi.", 1.0, 1.2}, {"Tudo", 1.3, 1.5}, {"bem?", 1.6, 1.8}, {"Sim", 1.9, 2.1}};
    text::CaptionOptions o;
    o.maxWords = 4; o.maxChars = 40;
    o.breakOnSentence = true;
    const auto g = text::group_captions(words, o);
    // Fim de frase fecha a legenda: "Oi." | "Tudo bem?" | "Sim".
    AUREA_CHECK_EQ(g.size(), 3u);
    if (g.size() == 3) { AUREA_CHECK_EQ(g[0].text, std::string("Oi.")); AUREA_CHECK_EQ(g[1].text, std::string("Tudo bem?")); }
    // Duração mínima de 2 s: o bloco fica até o seguinte (sem invadir).
    text::CaptionOptions slow;
    slow.mode = 1; slow.minDurationSec = 2.0f;
    const auto s = text::group_captions(words, slow);
    for (usize i = 0; i + 1 < s.size(); ++i) AUREA_CHECK(std::fabs(s[i].end - s[i + 1].start) < 1e-9);
    AUREA_CHECK(s.back().end - s.back().start >= 2.0 - 1e-9);
    // Intervalo de 4 quadros entre os blocos gerados.
    text::CaptionOptions gap;
    gap.maxWords = 1; gap.gapFrames = 4;
    CaptionRig rig(6, gap);
    const Layer* l = rig.track();
    AUREA_CHECK(l != nullptr); if (!l) return;
    AUREA_CHECK(l->captions.size() >= 2);
    for (usize i = 0; i + 1 < l->captions.size(); ++i) {
        const auto& a = l->captions[i];
        AUREA_CHECK(l->captions[i + 1].start - a.end >= 4 || a.end - a.start == 1);
    }
    AUREA_CHECK(text::valid_caption_track(l->captions));
}

AUREA_TEST(Captions, InsertNudgeAndSplitAtTheTextCursorAreUndoableSteps) {
    CaptionRig rig(6);
    const Layer* l = rig.track();
    AUREA_CHECK(l != nullptr); if (!l) return;
    const usize was = l->captions.size();
    // Inserir na agulha depois do último bloco: até 2 s, sem invadir nada.
    const i64 after = l->captions.back().end + 10;
    AUREA_CHECK(rig.edit("{\"op\":\"insert\",\"playhead\":" + std::to_string(after) + ",\"text\":\"nova legenda\"}"));
    l = rig.track();
    AUREA_CHECK_EQ(l->captions.size(), was + 1);
    AUREA_CHECK_EQ(l->captions.back().start, after);
    AUREA_CHECK_EQ(l->captions.back().end, after + 60);
    AUREA_CHECK_EQ(l->captions.back().words.size(), 2u);
    AUREA_CHECK(text::valid_caption_track(l->captions));
    // Inserir dentro de um bloco cai depois dele (e dos que encostam nele), sem invadir nenhum.
    const i64 inside = l->captions[0].start + 1;
    const i64 firstEnd = l->captions[0].end;
    if (rig.edit("{\"op\":\"insert\",\"playhead\":" + std::to_string(inside) + ",\"text\":\"x\"}")) {
        AUREA_CHECK(text::valid_caption_track(rig.track()->captions));
        bool after = false;
        for (const auto& s : rig.track()->captions) after = after || (s.text == "x" && s.start >= firstEnd);
        AUREA_CHECK(after);
        AUREA_CHECK(undo_once(rig.engine));
    }
    AUREA_CHECK(undo_once(rig.engine));
    AUREA_CHECK_EQ(rig.blocks(), was);
    // Entrada/saída de um quadro presas no vizinho.
    l = rig.track();
    if (l->captions.size() >= 2) {
        const i64 neighbour = l->captions[1].start;
        AUREA_CHECK(rig.edit("{\"op\":\"nudge\",\"ids\":" + id_list(l, {0}) + ",\"out\":100000}"));
        l = rig.track();
        AUREA_CHECK_EQ(l->captions[0].end, neighbour);
        const i64 start = l->captions[0].start;
        AUREA_CHECK(rig.edit("{\"op\":\"nudge\",\"ids\":" + id_list(l, {0}) + ",\"in\":1}"));
        AUREA_CHECK_EQ(rig.track()->captions[0].start, start + 1);
        for (const auto& w : rig.track()->captions[0].words) AUREA_CHECK(w.start >= start + 1);
        AUREA_CHECK(undo_once(rig.engine) && undo_once(rig.engine));
    }
    // Dividir no cursor do texto: as 2 primeiras palavras ficam.
    l = rig.track();
    const auto first = l->captions[0];
    AUREA_CHECK(first.words.size() >= 3);
    AUREA_CHECK(rig.edit("{\"op\":\"split_word\",\"ids\":" + id_list(l, {0}) + ",\"words\":2}"));
    l = rig.track();
    AUREA_CHECK_EQ(l->captions.size(), was + 1);
    AUREA_CHECK_EQ(l->captions[0].words.size(), 2u);
    AUREA_CHECK_EQ(l->captions[1].start, first.words[2].start);
    AUREA_CHECK_EQ(l->captions[1].words.front().text, first.words[2].text);
    AUREA_CHECK(!rig.edit("{\"op\":\"split_word\",\"ids\":" + id_list(l, {0}) + ",\"words\":2}"));   // nada à direita
    AUREA_CHECK(undo_once(rig.engine));
    AUREA_CHECK_EQ(rig.blocks(), was);
}

AUREA_TEST(Captions, ReplaceKeepsTheTimingAndRefusesWhenNothingMatches) {
    CaptionRig rig(6);
    const Layer* l = rig.track();
    AUREA_CHECK(l != nullptr); if (!l) return;
    const auto before = l->captions;
    AUREA_CHECK(!rig.edit("{\"op\":\"replace\",\"find\":\"inexistente\",\"replace\":\"x\"}"));
    // "UM" acha "um" sem diferenciar maiúsculas; a palavra trocada fica no lugar dela.
    AUREA_CHECK(rig.edit("{\"op\":\"replace\",\"find\":\"UM\",\"replace\":\"Olá\"}"));
    l = rig.track();
    AUREA_CHECK(l->captions[0].text.find("Olá") != std::string::npos);
    AUREA_CHECK_EQ(l->captions[0].words.size(), before[0].words.size());
    for (usize w = 1; w < before[0].words.size(); ++w) AUREA_CHECK_EQ(l->captions[0].words[w].start, before[0].words[w].start);
    AUREA_CHECK(!rig.edit("{\"op\":\"replace\",\"find\":\"olá\",\"replace\":\"y\",\"matchCase\":true}"));
    AUREA_CHECK(undo_once(rig.engine));
    AUREA_CHECK_EQ(rig.track()->captions[0].text, before[0].text);
    std::string t = "Ação AÇÃO ação";
    AUREA_CHECK_EQ(text::replace_caption_text(t, "ação", "x", false), 3u);
    AUREA_CHECK_EQ(t, std::string("x x x"));
}

AUREA_TEST(Captions, ExportSrtAndVttInTimelineTimeAndReadBack) {
    CaptionRig rig(6);
    const Layer* l = rig.track();
    AUREA_CHECK(l != nullptr); if (!l) return;
    const std::string srt = rig.engine.caption_export(rig.video, false);
    const std::string vtt = rig.engine.caption_export(rig.trackId, true);
    AUREA_CHECK(srt.rfind("1\n00:00:", 0) == 0);
    AUREA_CHECK(srt.find(" --> ") != std::string::npos && srt.find(',') != std::string::npos);
    AUREA_CHECK(vtt.rfind("WEBVTT\n\n1\n", 0) == 0);
    // Lido de volta: as mesmas palavras, na mesma ordem.
    auto words = [](const std::vector<text::CaptionWord>& v) { std::string s; for (const auto& w : v) s += w.text + " "; return s; };
    std::string expected;
    for (const auto& s : l->captions) { std::string t = s.text; std::replace(t.begin(), t.end(), '\n', ' '); expected += t + " "; }
    auto squash = [](std::string s) { std::string o; for (char c : s) if (c != ' ') o += c; return o; };
    AUREA_CHECK_EQ(squash(words(text::parse_srt(srt))), squash(expected));
    AUREA_CHECK_EQ(squash(words(text::parse_srt(vtt))), squash(expected));
    // Sem hora no WebVTT ("mm:ss.mmm") também lê.
    const auto shortVtt = text::parse_srt("WEBVTT\n\n00:01.000 --> 00:02.500\nolá mundo\n");
    AUREA_CHECK_EQ(shortVtt.size(), 2u);
    if (shortVtt.size() == 2) AUREA_CHECK(std::fabs(shortVtt[0].start - 1.0) < 1e-9 && std::fabs(shortVtt[1].end - 2.5) < 1e-9);
    AUREA_CHECK(rig.engine.caption_export(0, false).empty());
}

AUREA_TEST(Captions, FromWordsRebuildsOnlyTheRangeTheSelectionCovers) {
    CaptionRig rig(12);
    const Layer* l = rig.track();
    AUREA_CHECK(l != nullptr); if (!l) return;
    const auto before = l->captions;
    AUREA_CHECK(before.size() >= 3);
    // Trecho da transcrição sobre o segundo bloco (segundos da mídia, como a transcrição guarda).
    const f64 fps = 30.0;
    const auto& mid = before[1];
    std::string words = "[";
    for (usize w = 0; w < mid.words.size(); ++w) {
        if (w) words += ",";
        words += "{\"text\":\"novo" + std::to_string(w) + "\",\"start\":" + std::to_string(mid.words[w].start / fps + 0.001) +
                 ",\"end\":" + std::to_string(mid.words[w].end / fps - 0.001) + "}";
    }
    words += "]";
    AUREA_CHECK(rig.edit("{\"op\":\"from_words\",\"words\":" + words + "}"));
    l = rig.track();
    AUREA_CHECK(text::valid_caption_track(l->captions));
    AUREA_CHECK_EQ(l->captions.front().text, before.front().text);   // o de antes fica
    AUREA_CHECK_EQ(l->captions.back().text, before.back().text);     // o de depois também
    bool rebuilt = false;
    for (const auto& s : l->captions) rebuilt = rebuilt || s.text.find("novo0") != std::string::npos;
    AUREA_CHECK(rebuilt);
    AUREA_CHECK(undo_once(rig.engine));
    AUREA_CHECK_EQ(rig.track()->captions[1].text, mid.text);
    AUREA_CHECK(!rig.edit("{\"op\":\"from_words\",\"words\":[]}"));
}

AUREA_TEST(Captions, NewOptionsAndBlockLookSurviveSaveAndReopen) {
    text::CaptionOptions o;
    o.minDurationSec = 1.5f; o.gapFrames = 3; o.breakOnSentence = true;
    o.look = text::kCaptionLookLayout | text::kCaptionLookBox | text::kCaptionLookShadow;
    o.align = 2; o.lineSpacing = 1.4f; o.box = true; o.boxPadding = 0.6f; o.boxRadius = 0.1f;
    o.shadow = true; o.shadowColor = Vec4{0.2f, 0.3f, 0.4f, 0.5f}; o.shadowBlur = 0.2f;
    CaptionRig rig(6, o);
    text::CaptionOptions own;
    own.look = text::kCaptionLookLayout; own.align = 0; own.lineSpacing = 2.0f;
    AUREA_CHECK(rig.edit(look_command(rig.one_of(0), own)));
    const std::string path = caption_path("premiere");
    AUREA_CHECK(rig.engine.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.engine.load_project(path.c_str()).ok());
    const Layer* l = rig.track();
    AUREA_CHECK(l != nullptr); if (!l) { std::remove(path.c_str()); return; }
    const auto& c = l->captionOptions;
    AUREA_CHECK(std::fabs(c.minDurationSec - 1.5f) < 1e-6f);
    AUREA_CHECK_EQ(c.gapFrames, 3u);
    AUREA_CHECK(c.breakOnSentence);
    AUREA_CHECK_EQ(c.align, 2u);
    AUREA_CHECK(std::fabs(c.lineSpacing - 1.4f) < 1e-6f);
    AUREA_CHECK(std::fabs(c.boxPadding - 0.6f) < 1e-6f && std::fabs(c.boxRadius - 0.1f) < 1e-6f);
    AUREA_CHECK(near_color(c.shadowColor, o.shadowColor));
    AUREA_CHECK(std::fabs(c.shadowBlur - 0.2f) < 1e-6f);
    AUREA_CHECK(l->captions[0].look.has_value());
    if (l->captions[0].look) {
        AUREA_CHECK_EQ(l->captions[0].look->align, 0u);
        AUREA_CHECK(std::fabs(l->captions[0].look->lineSpacing - 2.0f) < 1e-6f);
    }
    // O JSON das opções leva e traz tudo; preset antigo (sem as chaves) fica nos padrões.
    text::CaptionOptions back; bool rf = true;
    AUREA_CHECK(presets::parse_caption_options(presets::caption_options_json(c, true), back, rf));
    AUREA_CHECK_EQ(back.gapFrames, 3u);
    AUREA_CHECK_EQ(back.align, 2u);
    text::CaptionOptions old; rf = true;
    AUREA_CHECK(presets::parse_caption_options("{\"style\":1}", old, rf));
    AUREA_CHECK(std::fabs(old.minDurationSec - 0.8f) < 1e-6f && old.gapFrames == 0 && old.align == 1 && !old.breakOnSentence);
    std::remove(path.c_str());
}

AUREA_TEST(Captions, TemplatesKeepTheUsersLayoutUnlessTheyBringTheirOwn) {
    text::CaptionOptions mine;
    mine.look = text::kCaptionLookLayout; mine.align = 2; mine.lineSpacing = 1.8f;
    const text::CaptionTemplate* news = nullptr;
    const text::CaptionTemplate* classic = nullptr;
    for (const auto& t : text::caption_templates()) {
        if (std::string(t.id) == "news") news = &t;
        if (std::string(t.id) == "classic") classic = &t;
    }
    AUREA_CHECK(news && classic); if (!news || !classic) return;
    text::CaptionOptions a = mine;
    text::apply_caption_template(*classic, a);
    AUREA_CHECK((a.look & text::kCaptionLookLayout) && a.align == 2);
    text::CaptionOptions b = mine;
    text::apply_caption_template(*news, b);
    AUREA_CHECK((b.look & text::kCaptionLookLayout) && b.align == 0);
}

AUREA_TEST(Captions, TranscriptSeekUsesSpeedReverseAndRemapping) {
    CaptionRig rig(4);
    auto* source = rig.comp()->layer(LayerId::unpack(rig.video));
    AUREA_CHECK(source != nullptr); if (!source) return;
    source->start = FrameIndex{30}; source->end = FrameIndex{150};
    source->offset = FrameIndex{12}; source->speed = 2.f;
    AUREA_CHECK_EQ(rig.engine.caption_word_frame(rig.video, 1.0), 39ll);
    source->reversed = true;
    AUREA_CHECK_EQ(rig.engine.caption_word_frame(rig.video, 1.0), 140ll);
    source->timeRemapEnabled = true;
    source->timeRemap.set(FrameIndex{12}, 0.f);
    source->timeRemap.set(FrameIndex{131}, 119.f);
    const i64 mapped = rig.engine.caption_word_frame(rig.video, 2.0);
    AUREA_CHECK(mapped >= 30 && mapped < 150);
    const f64 error = std::abs(source->source_frame(FrameIndex{mapped}) - 60.0);
    for (i64 f = 30; f < 150; ++f)
        AUREA_CHECK(error <= std::abs(source->source_frame(FrameIndex{f}) - 60.0));
    AUREA_CHECK_EQ(rig.engine.caption_word_frame(rig.video, -1), -1ll);
    AUREA_CHECK_EQ(rig.engine.caption_word_frame(rig.video, std::numeric_limits<f64>::quiet_NaN()), -1ll);
}
