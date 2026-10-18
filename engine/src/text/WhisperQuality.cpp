// =============================================================================
//  Aurea / text / WhisperQuality.cpp — ver WhisperQuality.hpp
// =============================================================================
#include "aurea/text/WhisperQuality.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>

namespace aurea::text {
namespace {

u32 decode(const std::string& s, usize& i) noexcept {
    const auto c = static_cast<unsigned char>(s[i]);
    const usize n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 4;
    if (n == 0) { ++i; return c; }
    if (n == 4 || i + n >= s.size()) { ++i; return 0xFFFD; }
    u32 cp = c & (0x3Fu >> n);
    for (usize k = 1; k <= n; ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) { ++i; return 0xFFFD; }
        cp = (cp << 6) | (b & 0x3Fu);
    }
    i += n + 1;
    return cp;
}

void encode(std::string& o, u32 cp) {
    if (cp < 0x80) o += static_cast<char>(cp);
    else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

/// Pontuação, símbolo, espaço ou largura zero: não conta para comparar frases.
bool ignorable(u32 c) noexcept {
    if (c < 0x80) return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
    return (c >= 0x80 && c <= 0xBF) || c == 0xD7 || c == 0xF7 || (c >= 0x2000 && c <= 0x206F) || (c >= 0x2190 && c <= 0x2BFF) ||
           (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF00 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) ||
           (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65) || c == 0x060C || c == 0x061B || c == 0x061F ||
           c == 0x06D4 || c == 0x0964 || c == 0x0965 || c >= 0x1F000 || c == 0xFFFD || (c >= 0xFE00 && c <= 0xFE0F);
}

u32 lower(u32 c) noexcept {
    if (c >= 'A' && c <= 'Z') return c + 0x20;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;
    if (c == 0x0130) return 'i';
    if (((c >= 0x0100 && c <= 0x0137) || (c >= 0x014A && c <= 0x0177)) && !(c & 1)) return c + 1;
    if (((c >= 0x0139 && c <= 0x0148) || (c >= 0x0179 && c <= 0x017E)) && (c & 1)) return c + 1;
    if (c >= 0x0410 && c <= 0x042F) return c + 0x20;
    if (c >= 0x0400 && c <= 0x040F) return c + 0x50;
    if (c >= 0x0391 && c <= 0x03A9) return c + 0x20;
    return c;
}

/// Minúsculas sem pontuação nem espaço: "Obrigado por assistir!" → "obrigadoporassistir".
std::string key_of(const std::string& text) {
    std::string o;
    for (usize i = 0; i < text.size();) {
        const u32 c = decode(text, i);
        if (!ignorable(c)) encode(o, lower(c));
    }
    return o;
}

/// Palavra para comparar repetição (só pontuação: o próprio texto).
std::string word_key(const std::string& text) {
    std::string k = key_of(text);
    return k.empty() ? text : k;
}

/// Crédito de legenda: o Whisper aprendeu com legendas da internet e o
/// devolve em silêncio, música e fim de vídeo. Ninguém fala isso no vídeo.
const char* const kCredits[] = {
    "amaraorg", "legendaspelacomunidade", "legendaadrianazanotto", "legendadopor", "legendasporamara",
    "subtituladopor", "subtítulosporlacomunidad", "subtitlesby", "subtitledby", "captionedby", "captionsby",
    "transcriptionby", "transcribedbyeso", "soustitrage", "soustitresréalisés", "soustitrespar",
    "untertitelimauftrag", "untertitelvon", "untertitelder", "sottotitolicreati", "sottotitoliacuradi",
    "subtitleoleh", "dimatorzok",
    "\xD1\x81\xD1\x83\xD0\xB1\xD1\x82\xD0\xB8\xD1\x82\xD1\x80\xD1\x8B\xD1\x81\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD0\xB2\xD0\xB0\xD0\xBB",  // субтитрысоздавал
    "\xD1\x81\xD1\x83\xD0\xB1\xD1\x82\xD0\xB8\xD1\x82\xD1\x80\xD1\x8B\xD0\xB4\xD0\xB5\xD0\xBB\xD0\xB0\xD0\xBB",  // субтитрыделал
    "\xD1\x80\xD0\xB5\xD0\xB4\xD0\xB0\xD0\xBA\xD1\x82\xD0\xBE\xD1\x80\xD1\x81\xD1\x83\xD0\xB1\xD1\x82\xD0\xB8\xD1\x82\xD1\x80\xD0\xBE\xD0\xB2",  // редакторсубтитров
    "\xD9\x86\xD8\xA7\xD9\x86\xD8\xB3\xD9\x8A\xD9\x82\xD9\x86\xD9\x82\xD8\xB1",  // نانسيقنقر
    "\xE5\xAD\x97\xE5\xB9\x95\xE7\x94\xB1",  // 字幕由
    "\xE5\xAD\x97\xE5\xB9\x95\xE6\x8F\x90\xE4\xBE\x9B",  // 字幕提供
    "\xE5\xAD\x97\xE5\xB9\x95\xE5\xBF\x97\xE6\x84\xBF\xE8\x80\x85",  // 字幕志愿者
    "\xE8\xAF\xB7\xE4\xB8\x8D\xE5\x90\x9D\xE7\x82\xB9\xE8\xB5\x9E",  // 请不吝点赞
    "\xE6\x98\x8E\xE9\x95\x9C\xE4\xB8\x8E\xE7\x82\xB9\xE7\x82\xB9",  // 明镜与点点
    "\xEC\x9E\x90\xEB\xA7\x89\xEC\xA0\x9C\xEA\xB3\xB5",  // 자막제공
    "\xEC\x9E\x90\xEB\xA7\x89\xEC\xA0\x9C\xEC\x9E\x91",  // 자막제작
};

/// Despedida de canal: pode ser dita de verdade no fim do vídeo, então só
/// sai quando o áudio embaixo não sustenta a frase.
const char* const kClosings[] = {
    "obrigadoporassistir", "obrigadaporassistir", "inscrevasenocanal", "deixeseulike", "tchautchau",
    "forwatching", "pleasesubscribe", "likeandsubscribe", "dontforgettosubscribe",
    "graciasporver", "suscríbete", "suscribete", "mercidavoirregardé", "abonnezvous", "dankefürszuschauen",
    "grazieperlavisione", "grazieperaverguardato", "iscrivetevi", "terimakasihtelahmenonton",
    "terimakasihsudahmenonton", "janganlupasubscribe", "izlediğiniziçinteşekkür",
    "\xD1\x81\xD0\xBF\xD0\xB0\xD1\x81\xD0\xB8\xD0\xB1\xD0\xBE\xD0\xB7\xD0\xB0\xD0\xBF\xD1\x80\xD0\xBE\xD1\x81\xD0\xBC\xD0\xBE\xD1\x82\xD1\x80",  // спасибозапросмотр
    "\xD0\xBF\xD1\x80\xD0\xBE\xD0\xB4\xD0\xBE\xD0\xBB\xD0\xB6\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5\xD1\x81\xD0\xBB\xD0\xB5\xD0\xB4\xD1\x83\xD0\xB5\xD1\x82",  // продолжениеследует
    "\xD0\xBF\xD0\xBE\xD0\xB4\xD0\xBF\xD0\xB8\xD1\x81\xD1\x8B\xD0\xB2\xD0\xB0\xD0\xB9\xD1\x82\xD0\xB5\xD1\x81\xD1\x8C",  // подписывайтесь
    "\xD8\xA7\xD8\xB4\xD8\xAA\xD8\xB1\xD9\x83\xD9\x88\xD8\xA7\xD9\x81\xD9\x8A\xD8\xA7\xD9\x84\xD9\x82\xD9\x86\xD8\xA7\xD8\xA9",  // اشتركوافيالقناة
    "\xD8\xB4\xD9\x83\xD8\xB1\xD8\xA7\xD9\x84\xD9\x83\xD9\x85\xD8\xB9\xD9\x84\xD9\x89\xD8\xA7\xD9\x84\xD9\x85\xD8\xB4\xD8\xA7\xD9\x87\xD8\xAF\xD8\xA9",  // شكرالكمعلىالمشاهدة
    "\xE6\x84\x9F\xE8\xB0\xA2\xE8\xA7\x82\xE7\x9C\x8B",  // 感谢观看
    "\xE8\xB0\xA2\xE8\xB0\xA2\xE8\xA7\x82\xE7\x9C\x8B",  // 谢谢观看
    "\xE3\x81\x94\xE8\xA6\x96\xE8\x81\xB4\xE3\x81\x82\xE3\x82\x8A\xE3\x81\x8C\xE3\x81\xA8\xE3\x81\x86",  // ご視聴ありがとう
    "\xE3\x83\x81\xE3\x83\xA3\xE3\x83\xB3\xE3\x83\x8D\xE3\x83\xAB\xE7\x99\xBB\xE9\x8C\xB2",  // チャンネル登録
    "\xEC\x8B\x9C\xEC\xB2\xAD\xED\x95\xB4\xEC\xA3\xBC\xEC\x85\x94\xEC\x84\x9C",  // 시청해주셔서
    "\xEA\xB5\xAC\xEB\x8F\x85\xEA\xB3\xBC\xEC\xA2\x8B\xEC\x95\x84\xEC\x9A\x94",  // 구독과좋아요
    "\xE0\xA4\xA6\xE0\xA5\x87\xE0\xA4\x96\xE0\xA4\xA8\xE0\xA5\x87\xE0\xA4\x95\xE0\xA5\x87\xE0\xA4\xB2\xE0\xA4\xBF\xE0\xA4\x8F"
    "\xE0\xA4\xA7\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xAF\xE0\xA4\xB5\xE0\xA4\xBE\xE0\xA4\xA6",  // देखनेकेलिएधन्यवाद
};

/// Pausa melhor para cortar uma fala sem fim entre `from` e `to`.
f64 best_cut(const SpeechActivity& a, f64 from, f64 to, bool& clean) {
    const i64 n = static_cast<i64>(a.voiced.size());
    auto index = [&](f64 t) { return std::clamp<i64>(static_cast<i64>(std::floor((t - a.start) / a.frame)), 0, n); };
    const i64 f0 = index(from), f1 = index(to);
    clean = false;
    if (f1 <= f0) return to;
    // Silêncio mais longo (0,5 s já basta: entre dois assim, o mais tardio
    // deixa a janela mais cheia de contexto).
    i64 bestLen = 0, bestMid = -1;
    for (i64 f = f0; f < f1;) {
        if (a.voiced[static_cast<usize>(f)]) { ++f; continue; }
        i64 g = f;
        while (g < f1 && !a.voiced[static_cast<usize>(g)]) ++g;
        const i64 len = std::min<i64>(g - f, 50);
        if (len >= bestLen) { bestLen = len; bestMid = (f + g) / 2; }
        f = g;
    }
    if (bestLen >= 15) {
        clean = true;
        return a.start + (static_cast<f64>(bestMid) + 0.5) * a.frame;
    }
    // Fala sem pausa nenhuma: o vale de energia (média de 50 ms) no último terço.
    const i64 lo = std::max(f0, f1 - (f1 - f0) / 3);
    f32 low = std::numeric_limits<f32>::infinity();
    i64 at = f1 - 1;
    for (i64 f = lo; f < f1; ++f) {
        f32 sum = 0; int count = 0;
        for (i64 k = std::max<i64>(0, f - 2); k <= std::min(n - 1, f + 2); ++k) { sum += a.db[static_cast<usize>(k)]; ++count; }
        const f32 mean = sum / static_cast<f32>(std::max(1, count));
        if (mean < low) { low = mean; at = f; }
    }
    return a.start + (static_cast<f64>(at) + 0.5) * a.frame;
}

} // namespace

f64 SpeechActivity::voiced_ratio(f64 from, f64 to) const noexcept {
    const i64 n = static_cast<i64>(voiced.size());
    const i64 f0 = std::clamp<i64>(static_cast<i64>(std::floor((from - start) / frame)), 0, n);
    const i64 f1 = std::clamp<i64>(static_cast<i64>(std::ceil((to - start) / frame)), 0, n);
    if (f1 <= f0) return 1.0;
    i64 on = 0;
    for (i64 f = f0; f < f1; ++f) on += voiced[static_cast<usize>(f)] ? 1 : 0;
    return static_cast<f64>(on) / static_cast<f64>(f1 - f0);
}

SpeechActivity analyze_speech(const f32* mono, usize count, u32 rate, f64 start) {
    SpeechActivity a;
    a.start = start;
    const usize hop = std::max<usize>(1, rate / 100);
    a.frame = static_cast<f64>(hop) / static_cast<f64>(std::max<u32>(1, rate));
    const usize frames = mono ? count / hop : 0;
    a.db.resize(frames);
    a.voiced.assign(frames, 0);
    if (frames == 0) return a;
    for (usize f = 0; f < frames; ++f) {
        f64 sum = 0.0;
        for (usize k = 0; k < hop; ++k) { const f64 x = mono[f * hop + k]; sum += x * x; }
        a.db[f] = static_cast<f32>(10.0 * std::log10(sum / static_cast<f64>(hop) + 1e-12));
    }
    // Limiar relativo ao chão de ruído deste trecho: a mesma fala baixa de um
    // microfone de lapela e o ruído de rua não cabem num limiar fixo.
    std::vector<f32> sorted = a.db;
    auto percentile = [&](f64 q) {
        const usize i = std::min(frames - 1, static_cast<usize>(q * static_cast<f64>(frames - 1)));
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(i), sorted.end());
        return sorted[i];
    };
    const f32 noise = percentile(0.10), peak = percentile(0.98);
    f32 threshold;
    if (peak < -58.0f || (peak - noise < 8.0f && peak < -48.0f)) threshold = std::numeric_limits<f32>::infinity();
    else if (peak - noise < 8.0f) threshold = -std::numeric_limits<f32>::infinity();   // sem contraste (música, fala contínua): não decide
    else threshold = std::max(-60.0f, noise + std::clamp(0.3f * (peak - noise), 6.0f, 18.0f));
    a.floorDb = noise;
    a.thresholdDb = threshold;
    for (usize f = 0; f < frames; ++f) a.voiced[f] = (a.db[f] > threshold || a.db[f] > -38.0f) ? 1 : 0;
    // Furo < 150 ms dentro da fala é a mesma fala; voz < 60 ms é estalo.
    auto runs = [&](u8 value, usize shorter, u8 to, bool inner) {
        for (usize f = 0; f < frames;) {
            if (a.voiced[f] != value) { ++f; continue; }
            usize g = f;
            while (g < frames && a.voiced[g] == value) ++g;
            if (g - f < shorter && (!inner || (f > 0 && g < frames))) std::fill(a.voiced.begin() + static_cast<std::ptrdiff_t>(f), a.voiced.begin() + static_cast<std::ptrdiff_t>(g), to);
            f = g;
        }
    };
    runs(0, 15, 1, true);
    runs(1, 6, 0, false);
    const f64 end = a.end();
    for (usize f = 0; f < frames;) {
        if (!a.voiced[f]) { ++f; continue; }
        usize g = f;
        while (g < frames && a.voiced[g]) ++g;
        const f64 r0 = std::max(start, start + static_cast<f64>(f) * a.frame - 0.2);
        const f64 r1 = std::min(end, start + static_cast<f64>(g) * a.frame + 0.3);
        if (!a.regions.empty() && r0 <= a.regions.back().second) a.regions.back().second = std::max(a.regions.back().second, r1);
        else a.regions.emplace_back(r0, r1);
        f = g;
    }
    return a;
}

WhisperWindow plan_whisper_window(const SpeechActivity& a, f64 cursor, f64 own, f64 limit, bool final) {
    WhisperWindow w;
    w.ownBegin = own;
    // Trechos de fala a partir do cursor; pausa até 1 s fica dentro do trecho.
    std::vector<std::pair<f64, f64>> pieces;
    for (const auto& [r0, r1] : a.regions) {
        const f64 s = std::max(r0, cursor), e = std::min(r1, limit);
        if (e - s < 0.05) continue;
        if (!pieces.empty() && s - pieces.back().second <= kWhisperMergeGapSec) pieces.back().second = e;
        else pieces.emplace_back(s, e);
    }
    auto skip = [&](f64 next) {
        w.spans.clear();
        w.next = next;
        w.nextOwn = own;
        w.ownEnd = own;
        return w;
    };
    // Só silêncio: nada vai ao modelo (é no silêncio que ele inventa texto).
    // A margem de 1 s pega o começo de uma fala logo depois do que foi lido.
    if (pieces.empty()) return skip(final ? limit : std::max(cursor, limit - 1.0));
    f64 packed = 0.0;
    for (const auto& [s, e] : pieces) {
        const bool open = !final && e >= limit - 0.05;   // a fala continua depois do que foi lido
        if (w.spans.empty()) {
            if (e - s > kWhisperWindowSec) {
                // Fala sem pausa longa por mais de 28 s: corta na melhor pausa
                // da segunda metade. Sem pausa nenhuma, 1 s de sobreposição.
                bool clean = false;
                const f64 c = best_cut(a, s + 0.5 * kWhisperWindowSec, s + kWhisperWindowSec, clean);
                w.spans.emplace_back(s, c);
                w.ownEnd = c;
                w.next = clean ? c : c - 1.0;
                w.nextOwn = c;
                return w;
            }
            if (open) return skip(s);   // lê mais antes de transcrever este trecho
            w.spans.emplace_back(s, e);
            packed = e - s;
            continue;
        }
        if (open || packed + kWhisperGapSec + (e - s) > kWhisperWindowSec) {
            const f64 boundary = 0.5 * (w.spans.back().second + s);
            w.ownEnd = w.next = w.nextOwn = boundary;
            return w;
        }
        w.spans.emplace_back(s, e);
        packed += kWhisperGapSec + (e - s);
    }
    const f64 tail = final ? limit : std::max(w.spans.back().second, limit - 1.0);
    w.ownEnd = w.next = w.nextOwn = tail;
    return w;
}

f64 WhisperTimeMap::source(f64 t) const noexcept {
    if (pieces.empty()) return t;
    const auto it = std::upper_bound(pieces.begin(), pieces.end(), t, [](f64 v, const Piece& p) { return v < p.packed0; });
    if (it == pieces.begin()) return pieces.front().source0;
    const Piece& p = *(it - 1);
    if (t <= p.packed1) return p.source0 + (t - p.packed0);
    const f64 end = p.source0 + (p.packed1 - p.packed0);
    if (it == pieces.end()) return end;
    return t < 0.5 * (p.packed1 + it->packed0) ? end : it->source0;
}

WhisperModelInfo whisper_model_info(i32 vocab, i32 audioLayers, i32 textLayers) noexcept {
    WhisperModelInfo m;
    m.english = vocab == 51864;
    switch (audioLayers) {
        case 4: m.size = WhisperModelSize::Tiny; break;
        case 6: m.size = WhisperModelSize::Base; break;
        case 12: m.size = WhisperModelSize::Small; break;
        case 24: m.size = WhisperModelSize::Medium; break;
        case 32: m.size = textLayers == 4 ? WhisperModelSize::Turbo : vocab >= 51866 ? WhisperModelSize::LargeV3 : WhisperModelSize::Large; break;
        default: break;
    }
    return m;
}

WhisperModelInfo read_whisper_model_info(const std::string& path) noexcept {
    std::ifstream file(path, std::ios::binary);
    std::uint32_t magic = 0;
    std::int32_t h[11] = {};
    if (!file.read(reinterpret_cast<char*>(&magic), sizeof magic) || magic != 0x67676d6cu) return {};
    if (!file.read(reinterpret_cast<char*>(h), sizeof h)) return {};
    // n_vocab, n_audio_ctx, n_audio_state, n_audio_head, n_audio_layer, n_text_ctx, n_text_state, n_text_head, n_text_layer, …
    return whisper_model_info(h[0], h[4], h[8]);
}

int whisper_thread_count(unsigned hardware) noexcept {
    if (hardware == 0) return 4;
    return std::clamp(static_cast<int>(hardware) - 1, 2, 8);
}

WhisperPhrase classify_whisper_phrase(const std::string& text) {
    const std::string key = key_of(text);
    if (key.empty()) return WhisperPhrase::NonSpeech;
    // Anotação entre colchetes/parênteses/asteriscos: "[Música]", "(risos)", "*suspira*".
    std::string trimmed = text;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\n')) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\n' || trimmed.back() == '.')) trimmed.pop_back();
    if (trimmed.size() >= 2) {
        const char f = trimmed.front(), l = trimmed.back();
        if ((f == '[' && l == ']') || (f == '(' && l == ')') || (f == '*' && l == '*')) return WhisperPhrase::NonSpeech;
    }
    if (trimmed.rfind("\xE2\x99\xAA", 0) == 0) return WhisperPhrase::NonSpeech;   // "♪ …"
    for (const char* c : kCredits) if (key.find(c) != std::string::npos) return WhisperPhrase::Credit;
    for (const char* c : kClosings) {
        const std::string pattern(c);
        if (key.find(pattern) != std::string::npos && key.size() <= pattern.size() + 40) return WhisperPhrase::Closing;
    }
    return WhisperPhrase::None;
}

usize drop_repeated_ngrams(std::vector<CaptionWord>& words) {
    if (words.size() < 3) return 0;
    std::vector<std::string> keys(words.size());
    for (usize i = 0; i < words.size(); ++i) keys[i] = word_key(words[i].text);
    auto same = [&](usize a, usize b, usize n) {
        for (usize k = 0; k < n; ++k) if (keys[a + k] != keys[b + k]) return false;
        return true;
    };
    std::vector<CaptionWord> out;
    out.reserve(words.size());
    usize dropped = 0;
    for (usize i = 0; i < words.size();) {
        usize bestN = 0, bestReps = 0;
        for (usize n = 1; n <= 6 && i + 2 * n <= words.size(); ++n) {
            usize reps = 1;
            while (i + (reps + 1) * n <= words.size() && same(i, i + reps * n, n)) ++reps;
            // "não não não" acontece; seis vezes seguidas é o decodificador em laço.
            const usize need = n == 1 ? 6 : n == 2 ? 4 : 3;
            if (reps >= need && reps * n > bestReps * bestN) { bestN = n; bestReps = reps; }
        }
        if (bestN == 0) { out.push_back(std::move(words[i])); ++i; continue; }
        const usize keep = bestN == 1 ? 2 : 1;
        for (usize k = 0; k < keep * bestN; ++k) out.push_back(std::move(words[i + k]));
        dropped += (bestReps - keep) * bestN;
        i += bestReps * bestN;
    }
    words = std::move(out);
    return dropped;
}

usize drop_prompt_echo(std::vector<CaptionWord>& words, const std::vector<CaptionWord>& before) {
    const usize most = std::min<usize>({16, words.size(), before.size()});
    for (usize m = most; m >= 3; --m) {
        bool same = true;
        for (usize k = 0; k < m && same; ++k)
            same = word_key(words[k].text) == word_key(before[before.size() - m + k].text);
        if (same) {
            words.erase(words.begin(), words.begin() + static_cast<std::ptrdiff_t>(m));
            return m;
        }
    }
    return 0;
}

std::string whisper_prompt_tail(const std::vector<CaptionWord>& words, usize maxWords, usize maxBytes) {
    usize first = words.size(), bytes = 0;
    while (first > 0 && words.size() - first < maxWords && bytes + words[first - 1].text.size() + 1 <= maxBytes) {
        --first;
        bytes += words[first].text.size() + 1;
    }
    // Começa numa frase inteira quando dá (o modelo continua o estilo dela).
    for (usize i = first; i + 4 < words.size(); ++i) {
        const std::string& t = words[i].text;
        const char last = t.empty() ? 0 : t.back();
        if (last == '.' || last == '?' || last == '!') { first = i + 1; break; }
    }
    std::string out;
    for (usize i = first; i < words.size(); ++i) out = caption_join(out, words[i].text);
    return out;
}

void refine_word_timing(std::vector<CaptionWord>& words, const SpeechActivity& a) {
    if (words.empty()) return;
    const i64 n = static_cast<i64>(a.voiced.size());
    auto index = [&](f64 t) { return static_cast<i64>(std::floor((t - a.start) / a.frame)); };
    auto time = [&](i64 f) { return a.start + static_cast<f64>(f) * a.frame; };
    for (CaptionWord& w : words) {
        const i64 f0 = index(w.start), f1 = index(w.end);
        if (n == 0 || f0 < 0 || f1 > n || f1 <= f0) continue;
        // Começo no silêncio: anda até o ataque da voz.
        if (!a.voiced[static_cast<usize>(f0)]) {
            i64 v = f0;
            while (v < f1 && !a.voiced[static_cast<usize>(v)]) ++v;
            if (v < f1 && v - f0 >= 8) w.start = std::max(w.start, time(v) - 0.02);
        }
        // Fim no silêncio: a palavra acaba onde a voz acaba (o destaque não
        // fica parado em cima de uma pausa).
        for (i64 f = std::max(f0, index(w.start + 0.12)); f < f1;) {
            if (a.voiced[static_cast<usize>(f)]) { ++f; continue; }
            i64 g = f;
            while (g < f1 && !a.voiced[static_cast<usize>(g)]) ++g;
            if (g - f >= 25 || (g == f1 && g - f >= 15)) { w.end = std::max(w.start + 0.08, time(f) + 0.05); break; }
            f = g;
        }
    }
    // O alinhamento estica a palavra até a seguinte; a mediana diz quanto uma
    // palavra dura nesta fala.
    std::vector<f64> d;
    d.reserve(words.size());
    for (const CaptionWord& w : words) d.push_back(std::max(0.0, w.end - w.start));
    std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(d.size() / 2), d.end());
    const f64 cap = std::clamp(3.0 * d[d.size() / 2], 0.8, 3.0);
    for (usize i = 0; i < words.size(); ++i) {
        CaptionWord& w = words[i];
        if (i) w.start = std::max(w.start, words[i - 1].start);
        w.end = std::min(w.end, w.start + cap);
        if (i + 1 < words.size()) w.end = std::min(w.end, std::max(w.start + 0.01, words[i + 1].start));
        w.end = std::max(w.end, w.start + 0.01);
    }
}

} // namespace aurea::text
