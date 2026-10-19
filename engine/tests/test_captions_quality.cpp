// =============================================================================
//  Testes da QUALIDADE das legendas automáticas (0.2.5).
//
//  O contrato que estes testes travam:
//    - o agrupamento corta onde a frase respira (vírgula, fim de frase,
//      pausa), não onde o bloco enche; sem palavra órfã, sem bloco terminando
//      em artigo/preposição, com mínimo de 0,8 s na tela e máximo de 4 s;
//    - pontuação solta gruda na palavra, ideogramas não ganham espaço;
//    - silêncio puro nunca vira janela do Whisper e as janelas cortam em pausa;
//    - frases típicas de alucinação, laços e eco do prompt saem;
//    - o tempo das palavras acompanha a energia da voz.
//  Nada aqui carrega modelo: é o que roda em volta do Whisper.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/text/Captions.hpp"
#include "aurea/text/WhisperQuality.hpp"
#include "aurea/text/WhisperWords.hpp"
#include "aurea/timeline/Composition.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

std::vector<text::CaptionWord> timed(const std::vector<std::string>& texts, f64 start, f64 length, f64 gap) {
    std::vector<text::CaptionWord> out;
    f64 t = start;
    for (const auto& w : texts) { out.push_back({w, t, t + length}); t += length + gap; }
    return out;
}

std::string last_word(const std::string& block) {
    const auto p = block.find_last_of(" \n");
    return p == std::string::npos ? block : block.substr(p + 1);
}

/// Fala sintética: tom modulado (sílabas) onde `speech` diz, ruído baixo no resto.
std::vector<f32> synthetic_speech(f64 seconds, const std::vector<std::pair<f64, f64>>& speech) {
    const usize n = static_cast<usize>(seconds * 16000);
    std::vector<f32> pcm(n);
    std::uint32_t seed = 12345;
    for (usize i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const f64 t = static_cast<f64>(i) / 16000.0;
        f64 v = (static_cast<f64>(seed >> 8) / 16777216.0 - 0.5) * 2e-4;
        for (const auto& [a, b] : speech)
            if (t >= a && t < b) v += 0.3 * (0.6 + 0.4 * std::sin(2 * 3.14159265 * 4 * t)) * std::sin(2 * 3.14159265 * 220 * t);
        pcm[i] = static_cast<f32>(v);
    }
    return pcm;
}

} // namespace

// -----------------------------------------------------------------------------
// Agrupamento
// -----------------------------------------------------------------------------

AUREA_TEST(CaptionQuality, BlocksEndWhereTheSentenceBreathesNotWhereTheyFill) {
    std::vector<text::CaptionWord> w = {
        {"Olá", 0.0, 0.3}, {"pessoal,", 0.3, 0.7}, {"hoje", 1.0, 1.3}, {"vamos", 1.3, 1.6}, {"editar.", 1.6, 2.0},
    };
    text::CaptionOptions o;
    o.maxWords = 3;
    const auto g = text::group_captions(w, o);
    AUREA_CHECK_EQ(g.size(), 2u);
    if (g.size() == 2) {
        AUREA_CHECK_EQ(g[0].text, std::string("Olá pessoal,"));
        AUREA_CHECK_EQ(g[1].text, std::string("hoje vamos editar."));
        AUREA_CHECK_EQ(g[1].first, 2u);
        AUREA_CHECK_EQ(g[1].count, 3u);
    }
}

AUREA_TEST(CaptionQuality, NoOrphanWordAndNoBlockEndsOnAPreposition) {
    text::CaptionOptions o;
    o.maxWords = 4;
    const auto five = text::group_captions(timed({"eu", "gosto", "muito", "desse", "lugar"}, 0, 0.3, 0.1), o);
    for (const auto& g : five) AUREA_CHECK(g.count >= 2);
    const auto weak = text::group_captions(timed({"eu", "gosto", "muito", "de", "café", "com", "leite", "quente"}, 0, 0.3, 0.05), o);
    AUREA_CHECK(weak.size() >= 2);
    for (usize i = 0; i + 1 < weak.size(); ++i) {
        const std::string end = last_word(weak[i].text);
        AUREA_CHECK(end != "de" && end != "com");
    }
}

AUREA_TEST(CaptionQuality, ShortBlocksStayReadableAndSmallGapsDoNotFlicker) {
    text::CaptionOptions o;
    std::vector<text::CaptionWord> w = {{"Sim.", 0.0, 0.2}, {"Claro.", 1.2, 1.4}, {"Vamos", 2.4, 2.6}, {"lá.", 2.7, 2.9}};
    const auto g = text::group_captions(w, o);
    AUREA_CHECK_EQ(g.size(), 3u);
    if (g.size() == 3) {
        AUREA_CHECK_NEAR(g[0].end, 0.8, 1e-9);   // mínimo na tela
        AUREA_CHECK_NEAR(g[1].end, 2.0, 1e-9);
        AUREA_CHECK_NEAR(g[2].end, 3.2, 1e-9);
        AUREA_CHECK_EQ(g[2].text, std::string("Vamos lá."));
    }
    // Vão de 0,2 s entre duas frases: o primeiro bloco fica até o segundo.
    const auto close = text::group_captions({{"Oi.", 0.0, 0.9}, {"Tudo", 1.1, 1.4}, {"bem?", 1.4, 1.9}}, o);
    AUREA_CHECK_EQ(close.size(), 2u);
    if (close.size() == 2) AUREA_CHECK_NEAR(close[0].end, close[1].start, 1e-9);
    // Mínimo nunca invade o bloco seguinte.
    const auto tight = text::group_captions({{"Um.", 0.0, 0.2}, {"Dois.", 0.5, 0.7}, {"Três.", 1.0, 1.2}}, o);
    for (usize i = 0; i + 1 < tight.size(); ++i) AUREA_CHECK(tight[i].end <= tight[i + 1].start + 1e-9);
}

AUREA_TEST(CaptionQuality, BlocksNeverLastMoreThanFourSeconds) {
    text::CaptionOptions o;
    o.maxWords = 6;
    o.maxChars = 40;
    const auto words = timed({"bem", "devagar", "cada", "palavra", "demora", "muito"}, 0, 0.9, 0.05);
    const auto g = text::group_captions(words, o);
    AUREA_CHECK(g.size() >= 2);
    for (const auto& b : g) {
        const f64 spoken = words[b.first + b.count - 1].end - words[b.first].start;
        AUREA_CHECK(b.count == 1 || spoken <= text::kCaptionMaxBlockSec + 1e-9);
    }
}

AUREA_TEST(CaptionQuality, TwoLinesAreBalancedAndKeepPhrasesTogether) {
    text::CaptionOptions o;
    o.maxWords = 6;
    o.maxChars = 18;
    const auto a = text::group_captions(timed({"Olá", "pessoal,", "hoje", "vamos"}, 0, 0.3, 0), o);
    AUREA_CHECK_EQ(a.size(), 1u);
    if (!a.empty()) AUREA_CHECK_EQ(a[0].text, std::string("Olá pessoal,\nhoje vamos"));
    // O guloso deixaria "um" pendurado no fim da linha de cima.
    const auto b = text::group_captions(timed({"Eu", "quero", "muito", "um", "café", "quente"}, 0, 0.3, 0.05), o);
    AUREA_CHECK_EQ(b.size(), 1u);
    if (!b.empty()) AUREA_CHECK_EQ(b[0].text, std::string("Eu quero muito\num café quente"));
}

AUREA_TEST(CaptionQuality, LooseWhisperPunctuationAndCapitalsAreCleanedUp) {
    const auto out = text::normalize_caption_words({
        {" olá", 0.0, 0.3}, {",", 0.3, 0.35}, {"tudo", 0.4, 0.6}, {"bem", 0.6, 0.8}, {" ?", 0.8, 0.85},
        {"\xC2\xBF", 1.0, 1.0}, {"qué", 1.0, 1.2}, {"tal?", 1.2, 1.5}, {"o", 1.6, 1.7}, {"Sr.", 1.7, 1.9},
        {"silva", 1.9, 2.2}, {"  muito   bem ", 2.2, 2.6}, {"", 2.6, 2.7}, {"ação.", 2.7, 3.0}, {"étapa", 3.0, 3.3},
    });
    std::vector<std::string> texts;
    for (const auto& w : out) texts.push_back(w.text);
    const std::vector<std::string> expected = {"olá,", "tudo", "bem?", "\xC2\xBFQu\xC3\xA9", "tal?", "O", "Sr.", "silva",
                                               "muito bem", "ação.", "\xC3\x89tapa"};
    AUREA_CHECK_EQ(texts.size(), expected.size());
    for (usize i = 0; i < std::min(texts.size(), expected.size()); ++i) AUREA_CHECK_EQ(texts[i], expected[i]);
    if (out.size() > 2) AUREA_CHECK_NEAR(out[0].end, 0.35, 1e-9);
    // Idempotente: limpar de novo não muda nada.
    const auto again = text::normalize_caption_words(out);
    AUREA_CHECK_EQ(again.size(), out.size());
    for (usize i = 0; i < std::min(again.size(), out.size()); ++i) AUREA_CHECK_EQ(again[i].text, out[i].text);
}

AUREA_TEST(CaptionQuality, IdeographsBecomeWordsAndJoinWithoutSpaces) {
    std::vector<text::CaptionWord> result;
    text::WhisperWords words(result, 0, 0, 30);
    words.token("\xE4\xBD\xA0", 0.0, 0.2, 0, 1);              // 你
    words.token("\xE5\xA5\xBD", 0.2, 0.4, 0, 1);              // 好
    words.token("\xEF\xBC\x8C", 0.4, 0.45, 0, 1);             // ，
    words.token("\xE4\xB8\x96\xE7\x95\x8C", 0.5, 0.9, 0, 1);  // 世界
    words.token(" ok", 1.0, 1.2, 0, 2);
    words.token(" ?", 1.2, 1.3, 0, 2);
    words.flush();
    AUREA_CHECK_EQ(result.size(), 4u);
    if (result.size() == 4) {
        AUREA_CHECK_EQ(result[1].text, std::string("\xE5\xA5\xBD\xEF\xBC\x8C"));
        AUREA_CHECK_EQ(result[3].text, std::string("ok?"));
    }
    AUREA_CHECK_EQ(text::caption_join("\xE4\xBD\xA0\xE5\xA5\xBD", "\xE4\xB8\x96\xE7\x95\x8C"), std::string("\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C"));
    AUREA_CHECK_EQ(text::caption_join("hello", "world"), std::string("hello world"));
    AUREA_CHECK_EQ(text::caption_join("bem", "?"), std::string("bem?"));
    AUREA_CHECK_EQ(text::caption_width("\xE4\xBD\xA0\xE5\xA5\xBD"), 4u);
    AUREA_CHECK_EQ(text::caption_width("a\xC3\xA7\xC3\xA3o"), 4u);
    AUREA_CHECK_EQ(text::caption_width("e\xCC\x81"), 1u);   // e + acento combinante
    text::CaptionOptions o;
    const auto g = text::group_captions({{"\xE4\xBD\xA0", 0.0, 0.2}, {"\xE5\xA5\xBD", 0.2, 0.4}, {"\xE4\xB8\x96\xE7\x95\x8C", 0.5, 0.9}}, o);
    AUREA_CHECK_EQ(g.size(), 1u);
    if (!g.empty()) AUREA_CHECK_EQ(g[0].text, std::string("\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C"));
}

AUREA_TEST(CaptionQuality, GeneratedBlocksStoreTheCleanedWords) {
    SyntheticConfig cfg;
    cfg.audioRate = 48000;
    cfg.audioChannels = 2;
    cfg.audioSeconds = 8.0;
    SyntheticFactory factory(cfg);
    EngineConfig ec;
    ec.workerCount = 2;
    ec.memoryBudgetBytes = 64ull * 1024 * 1024;
    ec.disableAutosave = true;
    ec.mediaFactory = &factory;
    Engine e;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(1080, 1920, 30.0, nullptr).ok());
    VideoImport vi;
    vi.sourcePath = "s";
    vi.displayName = "s";
    auto vid = e.import_video(vi);
    AUREA_CHECK(vid.ok());
    if (!vid.ok()) return;
    const std::vector<text::CaptionWord> words = {
        {" Olá", 1.0, 1.3}, {",", 1.3, 1.35}, {"tudo", 1.5, 1.8}, {"bem", 1.8, 2.1}, {" ?", 2.1, 2.15}, {"sim", 3.5, 3.8}, {"claro.", 3.8, 4.2},
    };
    text::CaptionOptions o;
    auto made = e.create_captions(*vid, words, o);
    AUREA_CHECK(made.ok());
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const Layer* track = nullptr;
    for (u32 i = 0; comp && i < comp->order().size(); ++i) {
        const Layer* l = comp->layer(comp->order().at(i));
        if (l && l->kind == LayerKind::Text && !l->captions.empty()) track = l;
    }
    AUREA_CHECK(track != nullptr);
    if (!track) return;
    AUREA_CHECK_EQ(track->captions.size(), 2u);
    if (track->captions.size() != 2) return;
    AUREA_CHECK_EQ(track->captions[0].text, std::string("Olá, tudo bem?"));
    AUREA_CHECK_EQ(track->captions[0].words.size(), 3u);
    AUREA_CHECK_EQ(track->captions[1].text, std::string("Sim claro."));
    for (const auto& s : track->captions) for (const auto& w : s.words) AUREA_CHECK(w.text != "," && w.text != "?");
    e.shutdown();
}

// -----------------------------------------------------------------------------
// Voz por energia e janelas do Whisper
// -----------------------------------------------------------------------------

AUREA_TEST(CaptionQuality, SilenceNeverBecomesAWhisperWindow) {
    const std::vector<f32> quiet(16000 * 10, 0.0f);
    const auto silent = text::analyze_speech(quiet.data(), quiet.size(), 16000, 0.0);
    AUREA_CHECK(silent.regions.empty());
    const auto none = text::plan_whisper_window(silent, 0.0, 0.0, 10.0, true);
    AUREA_CHECK(none.spans.empty());
    AUREA_CHECK_NEAR(none.next, 10.0, 1e-9);
    // Fala, 3,5 s de silêncio, fala: dois trechos colados numa janela só.
    const auto pcm = synthetic_speech(12.0, {{2.0, 5.0}, {9.0, 11.0}});
    const auto a = text::analyze_speech(pcm.data(), pcm.size(), 16000, 100.0);
    AUREA_CHECK_EQ(a.regions.size(), 2u);
    const auto w = text::plan_whisper_window(a, 100.0, 100.0, 112.0, true);
    AUREA_CHECK_EQ(w.spans.size(), 2u);
    if (w.spans.size() == 2) {
        AUREA_CHECK_NEAR(w.spans[0].first, 101.8, 0.03);
        AUREA_CHECK_NEAR(w.spans[0].second, 105.3, 0.03);
        AUREA_CHECK_NEAR(w.spans[1].first, 108.8, 0.03);
        AUREA_CHECK_NEAR(w.spans[1].second, 111.3, 0.03);
    }
    AUREA_CHECK_NEAR(w.next, 112.0, 1e-9);
    AUREA_CHECK(a.voiced_ratio(106.0, 108.0) < 0.05);
    AUREA_CHECK(a.voiced_ratio(102.5, 104.5) > 0.95);
    // Fala que continua além do que foi lido: espera ler mais (sem chamar o modelo).
    const auto open = text::plan_whisper_window(text::analyze_speech(pcm.data(), 16000 * 10, 16000, 0.0), 6.0, 6.0, 10.0, false);
    AUREA_CHECK(open.spans.empty());
    AUREA_CHECK_NEAR(open.next, 8.8, 0.03);
}

AUREA_TEST(CaptionQuality, LongSpeechIsCutAtAPauseOrOverlapsOneSecond) {
    const auto paused = synthetic_speech(40.0, {{0.0, 20.0}, {20.6, 40.0}});
    const auto a = text::analyze_speech(paused.data(), paused.size(), 16000, 0.0);
    const auto w = text::plan_whisper_window(a, 0.0, 0.0, 40.0, true);
    AUREA_CHECK_EQ(w.spans.size(), 1u);
    AUREA_CHECK(w.ownEnd > 20.0 && w.ownEnd < 20.6);
    AUREA_CHECK_NEAR(w.next, w.ownEnd, 1e-9);   // corte limpo: sem sobreposição
    const auto nonstop = synthetic_speech(40.0, {{0.0, 40.0}});
    const auto b = text::analyze_speech(nonstop.data(), nonstop.size(), 16000, 0.0);
    const auto h = text::plan_whisper_window(b, 0.0, 0.0, 40.0, true);
    AUREA_CHECK_EQ(h.spans.size(), 1u);
    AUREA_CHECK(h.ownEnd >= 14.0 && h.ownEnd <= text::kWhisperWindowSec);
    AUREA_CHECK_NEAR(h.next, h.ownEnd - 1.0, 1e-9);
    AUREA_CHECK_NEAR(h.nextOwn, h.ownEnd, 1e-9);
}

AUREA_TEST(CaptionQuality, PackedTimeMapsBackToTheMedia) {
    text::WhisperTimeMap map;
    map.pieces = {{0.0, 2.0, 10.0}, {2.5, 4.0, 20.0}};
    AUREA_CHECK_NEAR(map.source(1.0), 11.0, 1e-9);
    AUREA_CHECK_NEAR(map.source(2.1), 12.0, 1e-9);   // primeira metade do vão: fim do trecho
    AUREA_CHECK_NEAR(map.source(2.4), 20.0, 1e-9);   // segunda metade: começo do próximo
    AUREA_CHECK_NEAR(map.source(3.0), 20.5, 1e-9);
    AUREA_CHECK_NEAR(map.source(9.0), 21.5, 1e-9);
}

AUREA_TEST(CaptionQuality, WordTimesFollowTheVoiceEnergy) {
    const auto pcm = synthetic_speech(3.0, {{1.0, 1.5}});
    const auto a = text::analyze_speech(pcm.data(), pcm.size(), 16000, 0.0);
    std::vector<text::CaptionWord> w = {{"oi", 0.9, 2.5}};
    text::refine_word_timing(w, a);
    AUREA_CHECK_NEAR(w[0].start, 0.98, 0.02);
    AUREA_CHECK_NEAR(w[0].end, 1.55, 0.02);
    std::vector<text::CaptionWord> many = {{"a", 0.0, 0.3}, {"b", 0.2, 0.5}, {"c", 0.5, 4.5}};
    text::refine_word_timing(many, text::SpeechActivity{});
    AUREA_CHECK(many[0].end <= many[1].start);
    AUREA_CHECK(many[2].end - many[2].start <= 3.0 + 1e-9);
}

// -----------------------------------------------------------------------------
// Alucinação
// -----------------------------------------------------------------------------

AUREA_TEST(CaptionQuality, KnownWhisperHallucinationsAreRecognized) {
    using text::WhisperPhrase;
    AUREA_CHECK(text::classify_whisper_phrase(" Legendas pela comunidade Amara.org") == WhisperPhrase::Credit);
    AUREA_CHECK(text::classify_whisper_phrase("Subtitles by the Amara.org community") == WhisperPhrase::Credit);
    AUREA_CHECK(text::classify_whisper_phrase("\xD0\xA1\xD1\x83\xD0\xB1\xD1\x82\xD0\xB8\xD1\x82\xD1\x80\xD1\x8B "
                                              "\xD1\x81\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD0\xB2\xD0\xB0\xD0\xBB DimaTorzok") == WhisperPhrase::Credit);
    AUREA_CHECK(text::classify_whisper_phrase(" Obrigado por assistir!") == WhisperPhrase::Closing);
    AUREA_CHECK(text::classify_whisper_phrase("Thanks for watching.") == WhisperPhrase::Closing);
    AUREA_CHECK(text::classify_whisper_phrase(" [Música]") == WhisperPhrase::NonSpeech);
    AUREA_CHECK(text::classify_whisper_phrase("(risos)") == WhisperPhrase::NonSpeech);
    AUREA_CHECK(text::classify_whisper_phrase(" \xE2\x99\xAA") == WhisperPhrase::NonSpeech);
    AUREA_CHECK(text::classify_whisper_phrase(" ...") == WhisperPhrase::NonSpeech);
    AUREA_CHECK(text::classify_whisper_phrase(" Hoje vamos editar um vídeo.") == WhisperPhrase::None);
    // Uma frase longa que só contém a despedida é fala de verdade.
    AUREA_CHECK(text::classify_whisper_phrase(" Antes de tudo, obrigado por assistir até aqui, porque o assunto de hoje é comprido") == WhisperPhrase::None);
}

AUREA_TEST(CaptionQuality, RepetitionLoopsAndPromptEchoesAreDropped) {
    std::vector<std::string> loop = {"Então"};
    for (int i = 0; i < 8; ++i) { loop.push_back("e"); loop.push_back("aí,"); }
    loop.push_back("fim");
    auto words = timed(loop, 0, 0.2, 0);
    AUREA_CHECK_EQ(text::drop_repeated_ngrams(words), 14u);
    AUREA_CHECK_EQ(words.size(), 4u);
    auto speech = timed({"não", "não", "não", "quero"}, 0, 0.2, 0);
    AUREA_CHECK_EQ(text::drop_repeated_ngrams(speech), 0u);
    const auto before = timed({"vamos", "começar", "o", "vídeo", "agora."}, 0, 0.2, 0);
    auto echo = timed({"o", "vídeo", "agora.", "Primeiro", "passo"}, 2, 0.2, 0);
    AUREA_CHECK_EQ(text::drop_prompt_echo(echo, before), 3u);
    AUREA_CHECK_EQ(echo.size(), 2u);
    auto fresh = timed({"outra", "coisa", "agora."}, 2, 0.2, 0);
    AUREA_CHECK_EQ(text::drop_prompt_echo(fresh, before), 0u);
    // O prompt é curto e começa numa frase inteira.
    const auto tail = text::whisper_prompt_tail(timed({"um", "dois.", "Três", "quatro", "cinco", "seis", "sete"}, 0, 0.2, 0));
    AUREA_CHECK_EQ(tail, std::string("Três quatro cinco seis sete"));
    std::vector<std::string> long_(60, "palavra");
    AUREA_CHECK(text::whisper_prompt_tail(timed(long_, 0, 0.2, 0)).size() <= 200u);
}

AUREA_TEST(CaptionQuality, ModelSizeComesFromTheHeaderAndThreadsFromTheCpu) {
    AUREA_CHECK(text::whisper_model_info(51865, 4, 4).size == text::WhisperModelSize::Tiny);
    AUREA_CHECK(text::whisper_model_info(51864, 6, 6).size == text::WhisperModelSize::Base);
    AUREA_CHECK(text::whisper_model_info(51864, 6, 6).english);
    AUREA_CHECK(text::whisper_model_info(51865, 12, 12).size == text::WhisperModelSize::Small);
    AUREA_CHECK(text::whisper_model_info(51866, 32, 4).size == text::WhisperModelSize::Turbo);
    AUREA_CHECK(text::whisper_model_info(51866, 32, 32).size == text::WhisperModelSize::LargeV3);
    AUREA_CHECK(text::whisper_model_info(51865, 7, 7).size == text::WhisperModelSize::Unknown);
    AUREA_CHECK(text::read_whisper_model_info("nao-existe.bin").size == text::WhisperModelSize::Unknown);
    AUREA_CHECK_EQ(text::whisper_thread_count(0), 4);
    AUREA_CHECK_EQ(text::whisper_thread_count(2), 2);
    AUREA_CHECK_EQ(text::whisper_thread_count(6), 5);
    AUREA_CHECK_EQ(text::whisper_thread_count(16), 8);
}
