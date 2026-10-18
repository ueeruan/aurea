// =============================================================================
//  Aurea / text / Captions.cpp — ver Captions.hpp
// =============================================================================
#include "aurea/text/Captions.hpp"

#include "aurea/animation/Curve.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace aurea::text {

const CaptionSegment* active_caption(const std::vector<CaptionSegment>& segments, i64 frame) noexcept {
    auto it = std::upper_bound(segments.begin(), segments.end(), frame,
        [](i64 value, const CaptionSegment& s) { return value < s.start; });
    if (it == segments.begin()) return nullptr;
    --it;
    return frame < it->end ? &*it : nullptr;
}

bool valid_caption_track(const std::vector<CaptionSegment>& segments) noexcept {
    if (segments.size() > 100000) return false;
    i64 previous = 0;
    std::vector<u64> ids;
    for (const auto& s : segments) {
        if (!s.id || s.start < previous || s.end <= s.start || s.end > 100000000 || s.text.size() > 16384 || s.words.size() > 1024) return false;
        previous = s.end; ids.push_back(s.id);
        i64 last = s.start;
        for (const auto& w : s.words) {
            if (w.text.empty() || w.text.size() > 4096 || w.start < last || w.end <= w.start || w.end > s.end) return false;
            last = w.start;
        }
    }
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

void edit_caption_text(CaptionSegment& s, const std::string& text) {
    std::vector<std::string> words;
    for (usize p = 0; p < text.size();) {
        p = text.find_first_not_of(" \t\r\n", p); if (p == std::string::npos) break;
        const auto end = text.find_first_of(" \t\r\n", p);
        words.push_back(text.substr(p, end == std::string::npos ? end : end - p));
        if (end == std::string::npos) break; p = end;
    }
    if (words.size() > 1024 || text.size() > 16384) return;
    // LCS preserves timings through insertions/deletions, including repeated words.
    const usize n = words.size(), m = s.words.size();
    std::vector<u16> lcs((n + 1) * (m + 1));
    auto at = [&](usize i, usize j) -> u16& { return lcs[i * (m + 1) + j]; };
    for (usize i = n; i-- > 0;) for (usize j = m; j-- > 0;)
        at(i,j) = words[i] == s.words[j].text ? static_cast<u16>(1 + at(i+1,j+1)) : std::max(at(i+1,j),at(i,j+1));
    std::vector<CaptionToken> next(n); std::vector<bool> matched(n, false);
    for (usize i = 0, j = 0; i < n && j < m;) {
        if (words[i] == s.words[j].text) { next[i] = s.words[j]; matched[i] = true; ++i; ++j; }
        else if (at(i+1,j) >= at(i,j+1)) ++i; else ++j;
    }
    for (usize i = 0; i < n;) {
        if (matched[i]) { ++i; continue; }
        usize end = i; while (end < n && !matched[end]) ++end;
        const i64 a = i ? next[i-1].end : s.start;
        const i64 b = end < n ? next[end].start : s.end;
        // A zero gap cannot hold a new spoken word. Keep it visually attached
        // to the neighbour instead of changing the neighbour's original timing.
        for (usize j = i; j < end; ++j) {
            i64 from = a + std::max<i64>(0,b-a) * static_cast<i64>(j-i) / static_cast<i64>(end-i);
            i64 to = a + std::max<i64>(0,b-a) * static_cast<i64>(j-i+1) / static_cast<i64>(end-i);
            from = std::clamp(from, s.start, s.end-1); to = std::clamp(to, from+1, s.end);
            next[j] = {words[j], from, to};
        }
        i = end;
    }
    s.text = text; s.words = std::move(next);
}

namespace {

/// Caracteres visíveis (UTF-8: conta code points, não bytes).
u32 cp_len(const std::string& s) {
    u32 n = 0;
    for (unsigned char c : s) n += (c & 0xC0u) != 0x80u;
    return n;
}

/// Minúsculas ASCII + Latin-1 (acentos do português), sem pontuação nas pontas.
std::string fold(const std::string& w) {
    std::string o;
    for (usize i = 0; i < w.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(w[i]);
        if (c < 0x80) {
            if (std::isalnum(c)) o += static_cast<char>(std::tolower(c));
        } else if (c == 0xC3 && i + 1 < w.size()) {
            unsigned char d = static_cast<unsigned char>(w[i + 1]);
            if (d >= 0x80 && d <= 0x9E && d != 0x97) d += 0x20;   // À..Þ → à..þ
            o += static_cast<char>(c);
            o += static_cast<char>(d);
            ++i;
        } else {
            o += static_cast<char>(c);
        }
    }
    return o;
}

/// MAIÚSCULAS (ASCII + Latin-1).
std::string upper(const std::string& s) {
    std::string o = s;
    for (usize i = 0; i < o.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(o[i]);
        if (c < 0x80) o[i] = static_cast<char>(std::toupper(c));
        else if (c == 0xC3 && i + 1 < o.size()) {
            unsigned char d = static_cast<unsigned char>(o[i + 1]);
            if (d >= 0xA0 && d <= 0xBE && d != 0xB7) o[i + 1] = static_cast<char>(d - 0x20);
            ++i;
        }
    }
    return o;
}

void key(TrackSet& tr, u32 param, i64 f, f32 v, Interpolation in) {
    Track& t = tr.get_or_create(TrackProperty::TextAnimParam, 0, param);
    (void)t.set(FrameIndex{f}, v, in);
}

Vec4 rgb(u32 hex, f32 a = 1.0f) {
    return Vec4{static_cast<f32>((hex >> 16) & 0xFF) / 255.0f, static_cast<f32>((hex >> 8) & 0xFF) / 255.0f,
                static_cast<f32>(hex & 0xFF) / 255.0f, a};
}

// --- UTF-8 da legenda (0.2.5) ------------------------------------------------

/// Próximo code point; sequência inválida ou cortada = U+FFFD e anda um byte.
u32 next_cp(const std::string& s, usize& i) noexcept {
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

u32 first_cp(const std::string& s) noexcept {
    if (s.empty()) return 0;
    usize i = 0;
    return next_cp(s, i);
}

/// Último code point completo (um token do Whisper pode acabar no meio de um).
u32 last_cp(const std::string& s) noexcept {
    if (s.empty()) return 0;
    usize j = s.size() - 1;
    while (j > 0 && s.size() - j < 4 && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) --j;
    usize i = j;
    const u32 cp = next_cp(s, i);
    return i == s.size() ? cp : 0xFFFD;
}

void put_cp(std::string& o, u32 cp) {
    if (cp < 0x80) o += static_cast<char>(cp);
    else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

/// Ideograma ou kana: escritas que não põem espaço entre as palavras.
bool ideographic(u32 c) noexcept {
    return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF) || (c >= 0x3400 && c <= 0x4DBF) ||
           (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF66 && c <= 0xFF9F) ||
           (c >= 0x20000 && c <= 0x3FFFF);
}
bool cjk_punct(u32 c) noexcept {
    return (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF01 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) ||
           (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65);
}
bool wide(u32 c) noexcept {
    return ideographic(c) || cjk_punct(c) || (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0x2FFF) ||
           (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
           (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1F64F) || (c >= 0x1F900 && c <= 0x1F9FF);
}
/// Acentos combinantes, sinais não espaçados (devanágari, árabe) e larguras zero.
bool zero_width(u32 c) noexcept {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0483 && c <= 0x0489) || (c >= 0x0591 && c <= 0x05BD) ||
           (c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) || c == 0x0670 || (c >= 0x06D6 && c <= 0x06ED) ||
           (c >= 0x0900 && c <= 0x0902) || c == 0x093A || c == 0x093C || (c >= 0x0941 && c <= 0x0948) || c == 0x094D ||
           (c >= 0x0951 && c <= 0x0957) || (c >= 0x0962 && c <= 0x0963) || (c >= 0x200B && c <= 0x200F) ||
           (c >= 0xFE00 && c <= 0xFE0F);
}
/// Fecha algo: vai grudado na palavra anterior.
bool closing(u32 c) noexcept {
    switch (c) {
        case '.': case ',': case ';': case ':': case '!': case '?': case ')': case ']': case '}': case '"': case '\'':
        case '%': case '-': case 0x00BB: case 0x2013: case 0x2014: case 0x2019: case 0x201D: case 0x2026: case 0x203A:
        case 0x3001: case 0x3002: case 0x300D: case 0x300F: case 0x3011: case 0xFF01: case 0xFF09: case 0xFF0C:
        case 0xFF0E: case 0xFF1A: case 0xFF1B: case 0xFF1F: case 0x060C: case 0x061B: case 0x061F: case 0x06D4:
        case 0x0964: case 0x0965: return true;
        default: return false;
    }
}
/// Abre algo ("¿", "«", "("): vai grudado na palavra seguinte.
bool opening(u32 c) noexcept {
    switch (c) {
        case '(': case '[': case '{': case 0x00A1: case 0x00AB: case 0x00BF: case 0x2018: case 0x201C: case 0x2039:
        case 0x300C: case 0x300E: case 0x3010: case 0xFF08: return true;
        default: return false;
    }
}
/// Aspas/parênteses que podem vir depois do ponto final ("fim.»").
bool trailing_closer(u32 c) noexcept {
    return c == ')' || c == ']' || c == '}' || c == '"' || c == '\'' || c == 0x00BB || c == 0x2019 || c == 0x201D ||
           c == 0x203A || c == 0x300D || c == 0x300F || c == 0x3011 || c == 0xFF09;
}

template <class F> bool every_cp(const std::string& s, F&& f) {
    if (s.empty()) return false;
    for (usize i = 0; i < s.size();) if (!f(next_cp(s, i))) return false;
    return true;
}

/// Espaços nas pontas fora e qualquer sequência de espaço vira um só.
std::string collapse_spaces(const std::string& s) {
    std::string o;
    bool space = false;
    for (const char ch : s) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { space = !o.empty(); continue; }
        if (space) { o += ' '; space = false; }
        o += ch;
    }
    return o;
}

/// A palavra sem as aspas/parênteses do fim: o que sobra decide o fim de frase.
std::string strip_closers(const std::string& w) {
    std::string s = w;
    while (!s.empty() && trailing_closer(last_cp(s))) {
        usize j = s.size() - 1;
        while (j > 0 && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) --j;
        s.erase(j);
    }
    return s;
}

bool ellipsis(const std::string& s) {
    return last_cp(s) == 0x2026 || (s.size() >= 2 && s[s.size() - 1] == '.' && s[s.size() - 2] == '.');
}

bool ends_sentence(const std::string& w) {
    const std::string s = strip_closers(w);
    const u32 c = last_cp(s);
    if (c == '!' || c == '?' || c == 0x3002 || c == 0xFF01 || c == 0xFF1F || c == 0xFF0E || c == 0x061F || c == 0x06D4 ||
        c == 0x0964 || c == 0x0965) return true;
    if (c != '.' || ellipsis(s)) return false;
    // Abreviação não fecha frase ("o Sr. Silva", "e.g.").
    static const char* const abbreviations[] = {"sr", "sra", "srta", "dr", "dra", "prof", "profa", "eng", "av", "mr", "mrs",
                                                "ms", "st", "vs", "etc", "eg", "ie", "cf", "pag", "p\xC3\xA1g", "n\xC2\xBA"};
    const std::string f = fold(s);
    for (const char* a : abbreviations) if (f == a) return false;
    return true;
}

bool clause_end(const std::string& w) {
    const std::string s = strip_closers(w);
    const u32 c = last_cp(s);
    return c == ',' || c == ';' || c == ':' || c == 0xFF0C || c == 0x3001 || c == 0xFF1B || c == 0xFF1A || c == 0x060C ||
           c == 0x061B || c == 0x2013 || c == 0x2014 || c == '-' || ellipsis(s);
}

/// Artigo, preposição ou conjunção: o bloco que termina nela deixa a frase no ar.
bool weak_word(const std::string& w) {
    const u32 c = last_cp(w);
    if (c < 0x80 && !std::isalpha(static_cast<int>(c))) return false;
    if (closing(c)) return false;
    static const char* const weak[] = {
        // pt
        "a", "o", "as", "os", "um", "uma", "uns", "umas", "de", "da", "do", "das", "dos", "em", "no", "na", "nos", "nas", "e",
        "ou", "que", "por", "pra", "para", "com", "sem", "se", "mas", "ao", "aos", "\xC3\xA0", "\xC3\xA0s", "pelo", "pela",
        "num", "numa", "meu", "minha", "seu", "sua", "nosso", "nossa", "esse", "essa", "este", "esta", "como", "quando", "porque",
        // en
        "the", "an", "of", "to", "and", "or", "but", "in", "on", "at", "for", "with", "from", "by", "my", "your", "his", "her",
        "our", "their", "if", "as", "into", "than", "because",
        // es / it / fr / de / id / tr
        "el", "la", "los", "las", "unos", "unas", "del", "al", "y", "en", "con", "sin", "pero", "mi", "tu", "su", "le", "les",
        "une", "des", "du", "aux", "et", "qui", "dans", "pour", "avec", "sur", "mon", "ma", "mes", "ce", "cette", "mais", "il",
        "lo", "gli", "di", "della", "dei", "che", "ma", "alla", "der", "die", "das", "den", "dem", "ein", "eine", "einen",
        "einem", "und", "oder", "zu", "im", "mit", "von", "f\xC3\xBCr", "auf", "aber", "dass", "ke", "dari", "yang", "dan",
        "atau", "untuk", "dengan", "ve", "ile", "bir", "i\xC3\xA7in",
        // ru
        "\xD0\xB8", "\xD0\xB2", "\xD0\xBD\xD0\xB0", "\xD1\x81", "\xD0\xBA", "\xD0\xBF\xD0\xBE", "\xD0\xB0", "\xD0\xBD\xD0\xBE",
        "\xD1\x87\xD1\x82\xD0\xBE", "\xD0\xB4\xD0\xBB\xD1\x8F",
    };
    const std::string f = fold(w);
    if (f.empty()) return false;
    for (const char* x : weak) if (f == x) return true;
    return false;
}

/// Maiúscula na primeira letra (latim, cirílico, grego). O "i" ASCII fica: em
/// turco a maiúscula dele é "İ", e o idioma não chega até aqui.
std::string capitalize_first(const std::string& w) {
    for (usize i = 0; i < w.size();) {
        const usize at = i;
        const u32 c = next_cp(w, i);
        if (opening(c) || c == '"' || c == '\'') continue;
        u32 up = c;
        if (c >= 'a' && c <= 'z' && c != 'i') up = c - 0x20;
        else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) up = c - 0x20;
        else if (((c >= 0x0100 && c <= 0x0137) || (c >= 0x014A && c <= 0x0177)) && (c & 1)) up = c - 1;
        else if (((c >= 0x0139 && c <= 0x0148) || (c >= 0x0179 && c <= 0x017E)) && !(c & 1)) up = c - 1;
        else if (c >= 0x0430 && c <= 0x044F) up = c - 0x20;
        else if (c >= 0x0450 && c <= 0x045F) up = c - 0x50;
        else if (c >= 0x03B1 && c <= 0x03C9 && c != 0x03C2) up = c - 0x20;
        if (up == c) return w;
        std::string o = w.substr(0, at);
        put_cp(o, up);
        o += w.substr(i);
        return o;
    }
    return w;
}

bool glue(const std::string& left, const std::string& right) noexcept {
    if (caption_closing_punctuation(right)) return true;
    const u32 l = last_cp(left), r = first_cp(right);
    return (ideographic(l) || cjk_punct(l)) && (ideographic(r) || cjk_punct(r));
}

} // namespace

u32 caption_width(const std::string& s) noexcept {
    u32 n = 0;
    for (usize i = 0; i < s.size();) {
        const u32 c = next_cp(s, i);
        n += zero_width(c) ? 0u : wide(c) ? 2u : 1u;
    }
    return n;
}

std::string caption_join(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    return glue(left, right) ? left + right : left + " " + right;
}

bool caption_script_boundary(const std::string& left, const std::string& right) noexcept {
    if (left.empty() || right.empty()) return false;
    const u32 l = last_cp(left);
    return ideographic(first_cp(right)) && (ideographic(l) || cjk_punct(l));
}

bool caption_closing_punctuation(const std::string& s) noexcept {
    return every_cp(s, [](u32 c) { return closing(c) || c == 0x00B7; });
}

std::vector<CaptionWord> normalize_caption_words(const std::vector<CaptionWord>& words) {
    std::vector<CaptionWord> out;
    out.reserve(words.size());
    std::string open;
    f64 openStart = 0.0;
    for (const CaptionWord& w : words) {
        std::string t = collapse_spaces(w.text);
        if (t.empty()) continue;
        f64 a = w.start, b = w.end;
        if (!std::isfinite(a)) a = out.empty() ? 0.0 : out.back().end;
        if (!std::isfinite(b) || b < a) b = a;
        // Vírgula ou "?" soltos (o Whisper às vezes manda " ?") vão para a
        // palavra de antes: nunca começam uma linha sozinhos.
        if (open.empty() && !out.empty() && caption_closing_punctuation(t)) {
            out.back().text += t;
            out.back().end = std::max(out.back().end, b);
            continue;
        }
        if (every_cp(t, [](u32 c) { return opening(c); })) {
            if (open.empty()) openStart = a;
            open += t;
            continue;
        }
        if (!open.empty()) { t = open + t; a = std::min(openStart, a); open.clear(); }
        out.push_back(CaptionWord{std::move(t), a, b});
    }
    if (!open.empty()) out.push_back(CaptionWord{open, openStart, openStart});
    for (usize i = 1; i < out.size(); ++i)
        if (ends_sentence(out[i - 1].text)) out[i].text = capitalize_first(out[i].text);
    return out;
}

const char* caption_style_name(u32 id) noexcept {
    static const char* const names[kCaptionStyleCount] = {"Clássico", "Caixa", "Destaque", "Neon", "Karaokê", "Pop", "Legenda suave", "Legenda luminosa"};
    return id < kCaptionStyleCount ? names[id] : nullptr;
}

bool is_filler_word(const std::string& word) {
    static const char* const fillers[] = {
        "hum", "hmm", "hm", "ahn", "ah", "eh", "\xC3\xA9h", "er", "uh", "um", "uhm", "erm", "tipo", "n\xC3\xA9", "tipo assim",
        "mm", "mhm", "aham", "ahm", "eee", "ééé", "este", "eeh",
    };
    const std::string f = fold(word);
    if (f.empty()) return false;
    for (const char* x : fillers) if (f == x) return true;
    return false;
}

std::vector<CaptionWord> remove_filler_words(const std::vector<CaptionWord>& words) {
    std::vector<CaptionWord> out;
    out.reserve(words.size());
    for (const CaptionWord& w : words) if (!is_filler_word(w.text)) out.push_back(w);
    return out;
}

std::vector<CaptionGroup> group_captions(const std::vector<CaptionWord>& words, const CaptionOptions& opt) {
    struct Item {
        std::string text;
        f64 start = 0, end = 0;
        u32 index = 0, width = 0;
        f32 units = 1;              ///< ideograma/kana = meia palavra (o token do Whisper é curto)
        bool sentence = false, clause = false, weak = false;
    };
    std::vector<Item> items;
    items.reserve(words.size());
    for (u32 i = 0; i < words.size(); ++i) {
        // Espaços das pontas (o Whisper devolve " palavra") e duplicados no meio.
        std::string w = collapse_spaces(words[i].text);
        if (w.empty()) continue;
        if (opt.uppercase) w = upper(w);
        Item it;
        it.start = std::isfinite(words[i].start) ? words[i].start : (items.empty() ? 0.0 : items.back().end);
        it.end = std::isfinite(words[i].end) ? std::max(words[i].end, it.start) : it.start;
        it.index = i;
        it.width = caption_width(w);
        it.units = every_cp(w, [](u32 c) { return ideographic(c) || cjk_punct(c); }) ? 0.5f : 1.0f;
        it.sentence = ends_sentence(w);
        it.clause = clause_end(w);
        it.weak = weak_word(w);
        it.text = std::move(w);
        items.push_back(std::move(it));
    }
    std::vector<CaptionGroup> out;
    if (items.empty()) return out;
    const usize n = items.size();
    const u32 maxWords = opt.mode == 1 ? 1u : std::clamp<u32>(opt.maxWords, 1, 20);
    const u32 maxChars = std::clamp<u32>(opt.maxChars, 4, 80);
    const u32 maxLines = std::clamp<u32>(opt.maxLines, 1, 4);
    const f64 pause = static_cast<f64>(std::max(0.05f, opt.pauseSec));
    auto gap = [&](usize k) { return k + 1 < n ? std::max(0.0, items[k + 1].start - items[k].end) : 0.0; };
    auto forced = [&](usize k) { return k + 1 < n && opt.breakOnPause && gap(k) > pause; };
    auto sep = [&](usize k) { return glue(items[k].text, items[k + 1].text) ? 0u : 1u; };

    // Blocos [a, b] de palavras. Programação dinâmica sobre a fala inteira: o
    // corte guloso de antes fechava o bloco onde ele enchia ("Olá pessoal,
    // hoje" | "vamos editar."), não onde a frase respira.
    std::vector<std::pair<usize, usize>> blocks;
    if (maxWords == 1) {
        for (usize k = 0; k < n; ++k) blocks.emplace_back(k, k);
    } else {
        constexpr f64 kNone = std::numeric_limits<f64>::infinity();
        std::vector<f64> best(n + 1, kNone);
        std::vector<usize> from(n + 1, 0);
        best[0] = 0.0;
        for (usize i = 0; i < n; ++i) {
            if (best[i] == kNone) continue;
            f32 units = 0;
            u32 lines = 1, line = 0;
            f64 inside = 0.0;
            for (usize j = i; j < n; ++j) {
                if (j > i) {
                    // Atravessar uma pausa longa nunca; fim de frase, vírgula e
                    // pausa curta podem, mas custam (duas frases curtas cabem).
                    if (forced(j - 1)) break;
                    inside += items[j - 1].sentence ? 3.0 : items[j - 1].clause ? 0.25 : 0.0;
                    inside += 0.8 * std::min(1.0, gap(j - 1) / pause);
                }
                units += items[j].units;
                if (j == i) {
                    line = items[j].width;
                } else {
                    if (units > static_cast<f32>(maxWords) + 1e-3f) break;
                    const u32 s = sep(j - 1);
                    if (line + s + items[j].width <= maxChars) line += s + items[j].width;
                    else { ++lines; line = items[j].width; }
                    if (lines > maxLines || items[j].end - items[i].start > kCaptionMaxBlockSec) break;
                }
                // Base por bloco (menos blocos, mais cheios) e um termo convexo:
                // no empate, blocos do mesmo tamanho ("3 + 3", não "2 + 4").
                f64 cost = 1.0 + inside + 0.02 * static_cast<f64>(units) * static_cast<f64>(units);
                if (j + 1 < n && !forced(j)) {
                    const Item& last = items[j];
                    if (!last.sentence) cost += last.clause ? 0.3 : 1.2 * (1.0 - std::min(1.0, gap(j) / pause));
                    if (last.weak) cost += 2.5;   // "eu gosto muito de" | "café": pior que um bloco a mais
                }
                if (j == i) cost += 1.5;   // palavra órfã pisca sozinha na tela
                const f64 shown = items[j].end - items[i].start + std::min(gap(j), kCaptionGapCloseSec);
                if (shown < kCaptionMinDisplaySec) cost += 2.0 * (kCaptionMinDisplaySec - shown) / kCaptionMinDisplaySec;
                if (best[i] + cost < best[j + 1] - 1e-9) { best[j + 1] = best[i] + cost; from[j + 1] = i; }
            }
        }
        for (usize k = n; k > 0; k = from[k]) blocks.emplace_back(from[k], k - 1);
        std::reverse(blocks.begin(), blocks.end());
    }

    // Linhas de um bloco: o mínimo de linhas e, nelas, o corte mais
    // equilibrado (soma dos quadrados), de preferência depois de pontuação e
    // nunca deixando artigo/preposição no fim da linha de cima.
    auto layout = [&](usize a, usize b) {
        const usize c = b - a + 1;
        std::vector<u32> lineOf(c, 0);
        u32 lines = 1, line = items[a].width;
        for (usize k = a + 1; k <= b; ++k) {
            const u32 s = sep(k - 1);
            if (line + s + items[k].width <= maxChars) line += s + items[k].width;
            else { ++lines; line = items[k].width; }
            lineOf[k - a] = lines - 1;
        }
        if (lines > 1 && c <= 64) {
            auto span = [&](usize p, usize q) {   // largura das palavras [p, q) do bloco
                u32 w = 0;
                for (usize k = p; k < q; ++k) w += items[a + k].width + (k > p ? sep(a + k - 1) : 0u);
                return w;
            };
            const f64 big = static_cast<f64>(maxChars) * maxChars;
            constexpr f64 kNone = std::numeric_limits<f64>::infinity();
            std::vector<f64> dp((lines + 1) * (c + 1), kNone);
            std::vector<usize> cut((lines + 1) * (c + 1), 0);
            auto at = [&](usize l, usize k) -> usize { return l * (c + 1) + k; };
            dp[at(0, 0)] = 0;
            for (usize l = 1; l <= lines; ++l) {
                for (usize k = l; k <= c; ++k) {
                    for (usize p = l - 1; p < k; ++p) {
                        if (dp[at(l - 1, p)] == kNone) continue;
                        const u32 w = span(p, k);
                        if (w > maxChars && k - p > 1) continue;
                        f64 cost = static_cast<f64>(w) * w;
                        if (l < lines) {
                            const Item& end = items[a + k - 1];
                            if (end.weak) cost += 0.25 * big;
                            if (end.sentence || end.clause) cost -= 0.15 * big;
                        }
                        if (dp[at(l - 1, p)] + cost < dp[at(l, k)]) { dp[at(l, k)] = dp[at(l - 1, p)] + cost; cut[at(l, k)] = p; }
                    }
                }
            }
            if (dp[at(lines, c)] != kNone) {
                usize k = c;
                for (usize l = lines; l > 0; --l) {
                    const usize p = cut[at(l, k)];
                    for (usize q = p; q < k; ++q) lineOf[q] = static_cast<u32>(l - 1);
                    k = p;
                }
            }
        }
        std::string text = items[a].text;
        for (usize k = a + 1; k <= b; ++k) {
            if (lineOf[k - a] != lineOf[k - a - 1]) text += "\n";
            else if (sep(k - 1)) text += " ";
            text += items[k].text;
        }
        return text;
    };

    out.reserve(blocks.size());
    for (const auto& [a, b] : blocks) {
        CaptionGroup g;
        g.text = layout(a, b);
        g.first = items[a].index;
        g.count = items[b].index - items[a].index + 1;
        g.start = items[a].start;
        g.end = g.start;
        for (usize k = a; k <= b; ++k) g.end = std::max(g.end, items[k].end);
        out.push_back(std::move(g));
    }
    // Vão pequeno entre blocos pisca a tela vazia: o bloco fica até o próximo.
    // Bloco curto ganha o mínimo legível, sem nunca invadir o seguinte.
    for (usize k = 0; k < out.size(); ++k) {
        CaptionGroup& g = out[k];
        const f64 next = k + 1 < out.size() ? out[k + 1].start : std::numeric_limits<f64>::infinity();
        if (next < g.end) g.end = std::max(g.start, next);
        else if (next - g.end < kCaptionGapCloseSec) g.end = next;
        if (g.end - g.start < kCaptionMinDisplaySec) g.end = std::max(g.end, std::min(g.start + kCaptionMinDisplaySec, next));
    }
    return out;
}

std::vector<CaptionWord> parse_srt(const std::string& srt) {
    std::vector<CaptionWord> out;
    auto parse_time = [](const std::string& s, usize p, f64& t) {
        int h = 0, m = 0, sec = 0, ms = 0;
        if (std::sscanf(s.c_str() + p, "%d:%d:%d%*[,.]%d", &h, &m, &sec, &ms) < 3) return false;
        t = h * 3600.0 + m * 60.0 + sec + ms / 1000.0;
        return true;
    };
    usize i = 0;
    while (i < srt.size()) {
        const usize eol = srt.find('\n', i);
        std::string line = srt.substr(i, eol == std::string::npos ? std::string::npos : eol - i);
        i = eol == std::string::npos ? srt.size() : eol + 1;
        const usize arrow = line.find("-->");
        if (arrow == std::string::npos) continue;
        f64 a = 0, b = 0;
        if (!parse_time(line, 0, a) || !parse_time(line, arrow + 3 + (line.size() > arrow + 3 && line[arrow + 3] == ' '), b)) continue;
        // Texto do bloco: até a linha vazia.
        std::string text;
        while (i < srt.size()) {
            const usize e2 = srt.find('\n', i);
            std::string l2 = srt.substr(i, e2 == std::string::npos ? std::string::npos : e2 - i);
            i = e2 == std::string::npos ? srt.size() : e2 + 1;
            while (!l2.empty() && (l2.back() == '\r' || l2.back() == ' ')) l2.pop_back();
            if (l2.empty()) break;
            // Sem marcação (<i>, {\an8}).
            std::string clean;
            int depth = 0;
            for (char c : l2) {
                if (c == '<' || c == '{') { ++depth; continue; }
                if (c == '>' || c == '}') { depth = std::max(0, depth - 1); continue; }
                if (!depth) clean += c;
            }
            text += (text.empty() ? "" : " ") + clean;
        }
        std::vector<std::string> ws;
        usize p = 0;
        while (p < text.size()) {
            while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
            usize q = p;
            while (q < text.size() && !std::isspace(static_cast<unsigned char>(text[q]))) ++q;
            if (q > p) ws.push_back(text.substr(p, q - p));
            p = q;
        }
        if (ws.empty() || b <= a) continue;
        u32 total = 0;
        for (const std::string& w : ws) total += cp_len(w) + 1;
        f64 t = a;
        for (const std::string& w : ws) {
            const f64 d = (b - a) * static_cast<f64>(cp_len(w) + 1) / static_cast<f64>(total);
            out.push_back(CaptionWord{w, t, t + d});
            t += d;
        }
    }
    return out;
}

std::string upper_text(const std::string& s) { return upper(s); }

void apply_caption_style(const CaptionOptions& opt, u32 shortSide, TextData& t, TrackSet& tr, const std::vector<i64>& wf, i64 endFrame) {
    const f32 size = std::round(std::clamp(opt.sizeFrac, 0.02f, 0.25f) * static_cast<f32>(std::max(1u, shortSide)));
    t.size = size;
    t.alignment = 1;
    t.lineHeight = 1.15f;
    t.animators.clear();
    tr.remove_if([](const Track& x) { return x.property == TrackProperty::TextAnimParam; });
    // Zera o que o estilo define. Sem isto, trocar de estilo (ou aplicar um
    // preset) HERDA a sombra/caixa/cor do estilo anterior: o preset mostraria
    // uma aparência que não é a dele, e preview e exportação divergiriam.
    t.fontWeight = 400;
    t.color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    t.strokeWidth = 0.0f;
    t.strokeColor = Vec4{0.0f, 0.0f, 0.0f, 1.0f};
    t.tracking = 0.0f;
    t.background = false;
    t.backgroundColor = Vec4{0.0f, 0.0f, 0.0f, 0.6f};
    t.backgroundPadding = 14.0f;
    t.backgroundRadius = 10.0f;
    t.shadow = false;
    t.shadowColor = Vec4{0.0f, 0.0f, 0.0f, 0.6f};
    t.shadowOffset = Vec2{4.0f, 6.0f};
    t.shadowBlur = 6.0f;
    const u32 style = std::min(opt.style, kCaptionStyleCount - 1);
    switch (style) {
        case 6:
        case 7:
            t.fontWeight = 700;
            t.color = style == 7 ? rgb(0xFFC600) : rgb(0xFFFFFF);
            t.strokeColor = style == 7 ? rgb(0xFFFFFF) : rgb(0x000000);
            t.strokeWidth = std::max(1.0f, size * 0.03f);
            t.shadow = true; t.shadowOffset = Vec2{0, 1}; t.shadowBlur = size * 0.3f;
            break;
        case 0:   // Clássico: branco com contorno
            t.fontWeight = 700;
            t.color = rgb(0xFFFFFF);
            t.strokeColor = rgb(0x000000);
            t.strokeWidth = std::max(2.0f, size * 0.08f);
            break;
        case 1:   // Caixa: fundo escuro arredondado
            t.fontWeight = 600;
            t.color = rgb(0xFFFFFF);
            t.background = true;
            t.backgroundColor = rgb(0x000000, 0.7f);
            t.backgroundPadding = size * 0.3f;
            t.backgroundRadius = size * 0.25f;
            break;
        case 2:   // Destaque (viral): pesado, contorno grosso
            t.fontWeight = 900;
            t.color = rgb(0xFFFFFF);
            t.strokeColor = rgb(0x000000);
            t.strokeWidth = std::max(2.0f, size * 0.11f);
            t.shadow = true;
            t.shadowColor = rgb(0x000000, 0.5f);
            t.shadowOffset = Vec2{0, size * 0.06f};
            t.shadowBlur = size * 0.08f;
            break;
        case 3:   // Neon: cor brilhante com brilho da mesma cor
            t.fontWeight = 700;
            t.color = rgb(0x6FF7FF);
            t.shadow = true;
            t.shadowColor = rgb(0x00D5FF, 0.9f);
            t.shadowOffset = Vec2{0, 0};
            t.shadowBlur = size * 0.35f;
            break;
        case 4:   // Karaokê: cinza; o que já foi falado fica branco
            t.fontWeight = 800;
            t.color = rgb(0x9A9A9A);
            t.strokeColor = rgb(0x000000);
            t.strokeWidth = std::max(2.0f, size * 0.07f);
            break;
        default:  // Pop: cada palavra salta quando é falada
            t.fontWeight = 800;
            t.color = rgb(0xFFFFFF);
            t.strokeColor = rgb(0x000000);
            t.strokeWidth = std::max(2.0f, size * 0.08f);
            break;
    }
    apply_caption_animation(opt, t, tr, wf, endFrame);
}

void apply_caption_animation(const CaptionOptions& opt, TextData& t, TrackSet& tr, const std::vector<i64>& wf, i64 endFrame) {
    t.animators.clear();
    tr.remove_if([](const Track& x) { return x.property == TrackProperty::TextAnimParam; });
    const u32 style = std::min(opt.style, kCaptionStyleCount - 1);
    const u32 n = static_cast<u32>(wf.size());
    if (n == 0) return;
    if (style >= 6) {
        const i64 begin = std::max<i64>(0, wf.front());
        const double ratio = style == 6 ? 88.0 / 184.0 : 120.121094 / 236.121094;
        const i64 duration = std::max<i64>(2, static_cast<i64>((endFrame - begin) * ratio));
        (void)apply_text_preset(style == 6 ? 23 : 24, t, tr, begin, duration, 30.0);
        return;
    }
    const f32 step = 100.0f / static_cast<f32>(n);
    if (style == 4) {
        // Seletor 0..fim; o fim anda palavra por palavra na fala.
        TextAnimator a;
        a.name = "Karaokê";
        a.props = kTextPropFill;
        a.fill = rgb(0xFFFFFF);
        a.selector.basedOn = 1;
        t.animators.push_back(a);
        key(tr, kSelEnd, 0, 0.0f, Interpolation::Hold);
        for (u32 i = 0; i < n; ++i) key(tr, kSelEnd, std::max<i64>(wf[i], 0), step * static_cast<f32>(i + 1), Interpolation::Hold);
        return;
    }
    if (style == 5) {
        // Entrada: a palavra i aparece (de 40% e transparente) no instante dela.
        TextAnimator a;
        a.name = "Pop";
        a.props = kTextPropScale | kTextPropOpacity;
        a.scale = Vec2{40, 40};
        a.opacity = 0;
        a.selector.basedOn = 1;
        t.animators.push_back(a);
        for (u32 i = 0; i < n; ++i) {
            const i64 f0 = std::max<i64>(wf[i], 0);
            const i64 next = i + 1 < n ? wf[i + 1] : endFrame;
            const i64 d = std::clamp<i64>(next - f0, 1, 4);
            key(tr, kSelStart, f0, step * static_cast<f32>(i), Interpolation::Linear);
            key(tr, kSelStart, f0 + d, step * static_cast<f32>(i + 1), Interpolation::Linear);
        }
        return;
    }
    if (!opt.highlight || n < 2) return;
    // Destaque: seletor [i, i+1] da palavra falada, troca na hora (Hold).
    TextAnimator a;
    a.name = "Destaque";
    a.props = kTextPropFill | (style == 2 ? kTextPropScale : 0u);
    a.fill = opt.highlightColor;
    a.scale = Vec2{112, 112};
    a.selector.basedOn = 1;
    t.animators.push_back(a);
    for (u32 i = 0; i < n; ++i) {
        const i64 f0 = std::max<i64>(wf[i], 0);
        key(tr, kSelStart, f0, step * static_cast<f32>(i), Interpolation::Hold);
        key(tr, kSelEnd, f0, step * static_cast<f32>(i + 1), Interpolation::Hold);
    }
}

} // namespace aurea::text
