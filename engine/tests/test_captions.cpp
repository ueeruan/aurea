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

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::test;

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
