// =============================================================================
//  Edição automática: cortes (§3) e mapa do tempo. Regras puras.
// =============================================================================
#include "aurea/autoedit/AutoEdit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace aurea::autoedit {

PauseRule pause_rule(Intensity i) noexcept {
    switch (i) {
        case Intensity::Light: return {0.45, 0.10, 0.16};
        case Intensity::Strong: return {0.22, 0.05, 0.08};
        default: return {0.30, 0.07, 0.12};
    }
}

const char* cut_reason_name(CutReason r) noexcept {
    switch (r) {
        case CutReason::Silence: return "pausa";
        case CutReason::Breath: return "respiro";
        case CutReason::Hesitation: return "hesitacao";
        case CutReason::Slate: return "claquete";
        case CutReason::Filler: return "muleta";
        case CutReason::Backstage: return "bastidor";
        case CutReason::Stutter: return "gaguejo";
        case CutReason::FalseStart: return "falso_comeco";
        case CutReason::Retake: return "retomada";
        case CutReason::Cleanup: return "faxina";
        default: return "";
    }
}

// -----------------------------------------------------------------------------
// Tokens
// -----------------------------------------------------------------------------
std::string fold_token(std::string_view w) {
    std::string out;
    out.reserve(w.size());
    for (usize i = 0; i < w.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(w[i]);
        if (c < 0x80) {
            if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
            continue;
        }
        if (c == 0xC3 && i + 1 < w.size()) {
            const unsigned char d = static_cast<unsigned char>(w[++i]) | 0x20;   // maiúscula → minúscula
            char base = 0;
            if (d >= 0xA0 && d <= 0xA6) base = 'a';
            else if (d == 0xA7) base = 'c';
            else if (d >= 0xA8 && d <= 0xAB) base = 'e';
            else if (d >= 0xAC && d <= 0xAF) base = 'i';
            else if (d == 0xB1) base = 'n';
            else if ((d >= 0xB2 && d <= 0xB6) || d == 0xB8) base = 'o';
            else if (d >= 0xB9 && d <= 0xBC) base = 'u';
            else if (d == 0xBD || d == 0xBF) base = 'y';
            if (base) out.push_back(base);
            continue;
        }
        // Outras sequências (pontuação tipográfica, outros alfabetos): pula os bytes de continuação.
        while (i + 1 < w.size() && (static_cast<unsigned char>(w[i + 1]) & 0xC0) == 0x80) ++i;
    }
    return out;
}

namespace {
const char* number_digits(const std::string& t, Language lang) {
    struct Row { const char* word; const char* digits; };
    static const Row pt[] = {{"zero","0"},{"um","1"},{"uma","1"},{"dois","2"},{"duas","2"},{"tres","3"},{"quatro","4"},
        {"cinco","5"},{"seis","6"},{"sete","7"},{"oito","8"},{"nove","9"},{"dez","10"},{"onze","11"},{"doze","12"},
        {"treze","13"},{"catorze","14"},{"quatorze","14"},{"quinze","15"},{"dezesseis","16"},{"dezessete","17"},
        {"dezoito","18"},{"dezenove","19"},{"vinte","20"},{"trinta","30"},{"quarenta","40"},{"cinquenta","50"},
        {"sessenta","60"},{"setenta","70"},{"oitenta","80"},{"noventa","90"},{"cem","100"},{"cento","100"},{"mil","1000"}};
    static const Row en[] = {{"zero","0"},{"one","1"},{"two","2"},{"three","3"},{"four","4"},{"five","5"},{"six","6"},
        {"seven","7"},{"eight","8"},{"nine","9"},{"ten","10"},{"eleven","11"},{"twelve","12"},{"thirteen","13"},
        {"fourteen","14"},{"fifteen","15"},{"sixteen","16"},{"seventeen","17"},{"eighteen","18"},{"nineteen","19"},
        {"twenty","20"},{"thirty","30"},{"forty","40"},{"fifty","50"},{"sixty","60"},{"seventy","70"},{"eighty","80"},
        {"ninety","90"},{"hundred","100"},{"thousand","1000"}};
    static const Row es[] = {{"cero","0"},{"uno","1"},{"una","1"},{"un","1"},{"dos","2"},{"tres","3"},{"cuatro","4"},
        {"cinco","5"},{"seis","6"},{"siete","7"},{"ocho","8"},{"nueve","9"},{"diez","10"},{"once","11"},{"doce","12"},
        {"trece","13"},{"catorce","14"},{"quince","15"},{"dieciseis","16"},{"diecisiete","17"},{"dieciocho","18"},
        {"diecinueve","19"},{"veinte","20"},{"treinta","30"},{"cuarenta","40"},{"cincuenta","50"},{"sesenta","60"},
        {"setenta","70"},{"ochenta","80"},{"noventa","90"},{"cien","100"},{"mil","1000"}};
    auto look = [&](const Row* rows, usize n) -> const char* {
        for (usize i = 0; i < n; ++i) if (t == rows[i].word) return rows[i].digits;
        return nullptr;
    };
    switch (lang) {
        case Language::En: return look(en, std::size(en));
        case Language::Es: return look(es, std::size(es));
        default: return look(pt, std::size(pt));
    }
}

bool is_number(const std::string& t) noexcept {
    return !t.empty() && std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; });
}

template <usize N>
bool in(const std::string& t, const char* const (&list)[N]) noexcept {
    for (const char* s : list) if (t == s) return true;
    return false;
}

/// Último caractere "de verdade" do texto cru: termina frase?
bool ends_sentence(const std::string& raw) noexcept {
    usize n = raw.size();
    while (n && (raw[n - 1] == ' ' || raw[n - 1] == '"' || raw[n - 1] == '\'' || raw[n - 1] == ')')) --n;
    if (!n) return false;
    const char c = raw[n - 1];
    if (c == '.' || c == '!' || c == '?') return true;
    return n >= 3 && static_cast<unsigned char>(raw[n - 3]) == 0xE2 && static_cast<unsigned char>(raw[n - 2]) == 0x80
        && static_cast<unsigned char>(raw[n - 1]) == 0xA6;   // …
}
bool ends_with_question(const std::string& raw) noexcept {
    return raw.find('?') != std::string::npos;
}
/// Termina em "-" ou "…"/"...": palavra cortada.
bool cut_off(const std::string& raw) noexcept {
    usize n = raw.size();
    while (n && raw[n - 1] == ' ') --n;
    if (!n) return false;
    if (raw[n - 1] == '-') return true;
    if (n >= 3 && raw.compare(n - 3, 3, "...") == 0) return true;
    return n >= 3 && static_cast<unsigned char>(raw[n - 3]) == 0xE2 && static_cast<unsigned char>(raw[n - 2]) == 0x80
        && static_cast<unsigned char>(raw[n - 1]) == 0xA6;
}

std::string collapse_runs(const std::string& t) {
    std::string c;
    for (char ch : t) if (c.empty() || c.back() != ch) c.push_back(ch);
    return c;
}

bool weak_word(const std::string& t) noexcept {
    static const char* const kWeak[] = {"a","o","e","de","do","da","dos","das","em","no","na","nos","nas","um","uma",
        "que","se","por","pra","para","com","os","as","eu","ele","ela","ta","me","te","ja","so","mas","mais","foi",
        "the","an","of","to","in","on","and","or","is","it","i","you","we","el","la","los","las","y","en","con",
        "por","del","al","lo","le","es","un","una","ne","ai","ah","eh","tipo","entao"};
    return in(t, kWeak) || t.size() <= 1;
}

bool negation(const std::string& t, Language lang) noexcept {
    static const char* const pt[] = {"nao","nem","nunca","jamais","ninguem","nada"};
    static const char* const en[] = {"not","no","nor","never","dont","doesnt","didnt","cant","wont","isnt","arent","nothing"};
    static const char* const es[] = {"no","ni","nunca","jamas","nada","nadie"};
    switch (lang) {
        case Language::En: return in(t, en);
        case Language::Es: return in(t, es);
        default: return in(t, pt);
    }
}

bool emphasis(const std::string& t) noexcept {
    static const char* const kEmphasis[] = {"muito","mto","bem","super","mega","tao","mais","nunca","nao","sim","isso",
        "very","so","really","no","yes","much","more","never","too","muy","mucho","mas","si","nada","ya","vai","bora",
        "corre","olha","pronto"};
    return in(t, kEmphasis) || is_number(t);
}
} // namespace

std::string normalize_token(std::string_view w, Language lang) {
    std::string t = fold_token(w);
    if (const char* d = number_digits(t, lang)) return d;
    return t;
}

bool is_hesitation(std::string_view raw, Language lang) {
    const std::string f = fold_token(raw);
    if (f.empty()) return false;
    const std::string r(raw);
    const bool tilde = r.find("\xC3\xA3") != std::string::npos || r.find("\xC3\x83") != std::string::npos;
    if (tilde && (f == "a" || f == "ah")) return true;     // ã, ãh
    const std::string c = collapse_runs(f);
    const bool repeated = c.size() < f.size();
    if (c == "a" || c == "e" || c == "m") return repeated;  // aa, ee, mm, mmm (mas "a" e "e" são palavras)
    if (c == "an") return lang != Language::En;            // "an" é artigo em inglês
    if (c == "um") return lang == Language::En;            // "um" é artigo em pt
    static const char* const kBase[] = {"ah","ahn","eh","er","erm","hm","hum","uh","uhm","ahm","ehm","hmn","uhn","ehn"};
    return in(c, kBase);
}

u32 edit_distance(std::string_view a, std::string_view b, u32 cap) noexcept {
    if (a.size() > 64 || b.size() > 64) return a == b ? 0 : cap;
    const usize n = a.size(), m = b.size();
    if ((n > m ? n - m : m - n) >= cap) return cap;
    u32 prev[65], cur[65];
    for (usize j = 0; j <= m; ++j) prev[j] = static_cast<u32>(j);
    for (usize i = 1; i <= n; ++i) {
        cur[0] = static_cast<u32>(i);
        u32 best = cur[0];
        for (usize j = 1; j <= m; ++j) {
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0u : 1u)});
            best = std::min(best, cur[j]);
        }
        if (best >= cap) return cap;
        std::memcpy(prev, cur, sizeof(u32) * (m + 1));
    }
    return std::min(prev[m], cap);
}

bool tokens_match(const std::string& a, const std::string& b) noexcept {
    if (a == b) return true;
    if (a.size() < 4 || b.size() < 4) return false;
    return edit_distance(a, b, 2) <= 1;
}

u32 token_lcs(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.empty() || b.empty()) return 0;
    std::vector<u32> prev(b.size() + 1, 0), cur(b.size() + 1, 0);
    for (usize i = 1; i <= a.size(); ++i) {
        for (usize j = 1; j <= b.size(); ++j)
            cur[j] = tokens_match(a[i - 1], b[j - 1]) ? prev[j - 1] + 1 : std::max(prev[j], cur[j - 1]);
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

bool contrast_veto(const std::vector<std::string>& a, const std::vector<std::string>& b, Language lang) {
    if (a.size() > 10 || b.size() > 10) return false;
    auto negs = [&](const std::vector<std::string>& v) { u32 n = 0; for (const auto& t : v) n += negation(t, lang); return n; };
    if (negs(a) != negs(b)) return true;
    auto nums = [](const std::vector<std::string>& v) {
        std::vector<std::string> n;
        for (const auto& t : v) if (is_number(t)) n.push_back(t);
        std::sort(n.begin(), n.end());
        return n;
    };
    const auto na = nums(a), nb = nums(b);
    if (!na.empty() && !nb.empty() && na != nb) return true;
    if (a.size() == b.size()) {
        u32 diff = 0;
        for (usize i = 0; i < a.size(); ++i) diff += tokens_match(a[i], b[i]) ? 0 : 1;
        if (diff >= 1 && diff <= 2) return true;   // 1–2 termos trocados
        if (diff > 2) {
            auto sa = a, sb = b;
            std::sort(sa.begin(), sa.end()); std::sort(sb.begin(), sb.end());
            if (sa == sb) return true;              // as mesmas palavras em outra ordem
        }
    }
    return false;
}

std::vector<Sentence> split_sentences(const std::vector<Word>& words) {
    std::vector<Sentence> out;
    for (u32 i = 0; i < words.size(); ++i) {
        if (out.empty() || (i > 0 && (ends_sentence(words[i - 1].text) || words[i].start - words[i - 1].end > 0.7))) {
            if (!out.empty()) out.back().complete = ends_sentence(words[i - 1].text);
            out.push_back({i, 0, false});
        }
        ++out.back().count;
    }
    if (!out.empty()) out.back().complete = ends_sentence(words.back().text);
    return out;
}

// -----------------------------------------------------------------------------
// Marcas por palavra
// -----------------------------------------------------------------------------
namespace {

struct Lists {
    std::vector<const char*> tags, tagq, vocatives, slateWords;
    std::vector<std::vector<const char*>> slatePhrases;
    std::vector<const char*> backstage, triggers;
};

const Lists& lists(Language lang) {
    static const Lists pt{
        {"entendeu","ne","beleza"},
        {"ta","ok","certo","tranquilo","viu"},
        {"galera","pessoal","rapaziada"},
        {"take","cena","tomada"},
        {{"mais","uma"},{"de","novo"},{"valendo"},{"gravando"},{"rodando"}},
        {"corta","corte","de","novo","pera","perai","espera","calma","errei","desculpa","droga","merda","porra","caramba",
         "putz","pqp","cacete","ops","opa","vai","valendo","gravando","rodando","ai","nossa","cara","recomeca","recomecar",
         "vamo","vamos","la","comecar","comeca","denovo","ah","eh","hum","ahn","nao","isso","ta","ok"},
        {"corta","errei","droga","merda","porra","caramba","putz","pqp","cacete","denovo","recomeca","recomecar"}};
    static const Lists en{
        {"right","yeah"},
        {"ok","okay","right","yeah","alright"},
        {"guys","folks","everyone","everybody"},
        {"take","scene"},
        {{"one","more"},{"again"},{"rolling"},{"action"}},
        {"cut","again","sorry","wait","hold","on","damn","shit","fuck","oops","okay","ok","start","over","one","more",
         "let","me","redo","from","the","top","uh","um","no","so"},
        {"cut","damn","shit","fuck","oops","redo"}};
    static const Lists es{
        {"vale","sabes","verdad"},
        {"no","vale","ok","verdad","sabes"},
        {"chicos","gente","amigos","chavales"},
        {"toma","escena"},
        {{"otra","vez"},{"de","nuevo"},{"grabando"},{"accion"}},
        {"corta","corten","otra","vez","perdon","espera","mierda","joder","ostia","vale","de","nuevo","vamos","venga",
         "ay","uf","eh","no","ok"},
        {"corta","corten","mierda","joder","ostia","perdon"}};
    switch (lang) {
        case Language::En: return en;
        case Language::Es: return es;
        default: return pt;
    }
}

bool listed(const std::string& t, const std::vector<const char*>& list) noexcept {
    for (const char* s : list) if (t == s) return true;
    return false;
}

} // namespace

std::vector<CutReason> mark_words(const std::vector<Word>& words, const Options& opt) {
    const usize n = words.size();
    std::vector<CutReason> marks(n, CutReason::None);
    if (!n) return marks;
    const Lists& L = lists(opt.language);
    std::vector<std::string> tok(n);
    for (usize i = 0; i < n; ++i) tok[i] = normalize_token(words[i].text, opt.language);
    auto gap_before = [&](usize i) { return i == 0 ? 1e9 : words[i].start - words[i - 1].end; };
    auto gap_after = [&](usize i) { return i + 1 >= n ? 1e9 : words[i + 1].start - words[i].end; };
    const f64 total = words.back().end;

    // §3.2 Hesitações: saem sempre.
    for (usize i = 0; i < n; ++i) if (is_hesitation(words[i].text, opt.language)) marks[i] = CutReason::Hesitation;

    // Claquete: "take/cena/tomada + número" e frases de set isoladas (≥ 0,3 s antes, ≥ 0,4 s depois).
    for (usize i = 0; i < n; ++i) {
        if (marks[i] != CutReason::None) continue;
        usize len = 0;
        if (listed(tok[i], L.slateWords) && i + 1 < n && is_number(tok[i + 1])) len = 2;
        for (const auto& phrase : L.slatePhrases) {
            if (len || i + phrase.size() > n) continue;
            bool ok = true;
            for (usize k = 0; k < phrase.size() && ok; ++k) ok = tok[i + k] == phrase[k];
            if (ok) len = phrase.size();
        }
        if (len && gap_before(i) >= 0.3 && gap_after(i + len - 1) >= 0.4)
            for (usize k = 0; k < len; ++k) marks[i + k] = CutReason::Slate;
    }

    const auto sentences = split_sentences(words);
    // Bastidor: frase curta (≤ 7 palavras) só de comando, com um gatilho
    // (ou contagem regressiva / palavrão isolado).
    for (const auto& s : sentences) {
        if (s.count == 0 || s.count > 7) continue;
        bool all = true, trigger = false;
        i64 lastNumber = -1; u32 countdown = 0;
        for (u32 k = s.first; k < s.first + s.count; ++k) {
            const auto& t = tok[k];
            if (is_number(t)) {
                const i64 v = std::atoll(t.c_str());
                if (lastNumber >= 0 && v == lastNumber - 1) ++countdown;
                lastNumber = v;
                continue;
            }
            if (listed(t, L.triggers)) trigger = true;
            if (k + 1 < s.first + s.count && ((t == "de" && (tok[k + 1] == "novo" || tok[k + 1] == "nuevo")) || (t == "again"))) trigger = true;
            if (!listed(t, L.backstage) && !is_hesitation(words[k].text, opt.language)) all = false;
        }
        if (countdown >= 2) trigger = true;
        if (all && trigger) for (u32 k = s.first; k < s.first + s.count; ++k) if (marks[k] == CutReason::None) marks[k] = CutReason::Backstage;
    }

    // Muletas: TAGS fechando frase (a partir da 2ª palavra), TAGQ com "?" que
    // aparecem ≥ 3 vezes, vocativos ≥ 5 vezes no início da frase, a > 15 s das pontas.
    std::map<std::string, u32> tagqCount, vocCount;
    for (usize i = 0; i < n; ++i) {
        if (listed(tok[i], L.tagq) && ends_with_question(words[i].text)) ++tagqCount[tok[i]];
        if (listed(tok[i], L.vocatives)) ++vocCount[tok[i]];
    }
    std::vector<u8> filler(n, 0);
    for (const auto& s : sentences) {
        const u32 last = s.first + s.count - 1;
        if (s.count >= 2 && listed(tok[last], L.tags) && ends_sentence(words[last].text)) filler[last] = 1;
        if (s.count >= 2 && listed(tok[last], L.tags) && !ends_sentence(words[last].text) && gap_after(last) > 0.7) filler[last] = 1;
        const u32 first = s.first;
        if (listed(tok[first], L.vocatives) && vocCount[tok[first]] >= 5 && words[first].start > 15.0
            && words[first].end < total - 15.0) filler[first] = 1;
    }
    for (usize i = 0; i < n; ++i)
        if (listed(tok[i], L.tagq) && ends_with_question(words[i].text) && tagqCount[tok[i]] >= 3) filler[i] = 1;
    for (usize i = 0; i < n; ++i) {
        if (!filler[i] || marks[i] != CutReason::None) continue;
        // Guarda do "não": muleta a ≤ 0,12 s de "não/nem" volta.
        const bool nearNeg = (i > 0 && negation(tok[i - 1], opt.language) && gap_before(i) <= 0.12)
                          || (i + 1 < n && negation(tok[i + 1], opt.language) && gap_after(i) <= 0.12);
        if (!nearNeg) marks[i] = CutReason::Filler;
    }

    // §3.3 Gaguejo: mesma palavra dobrada com vão ≤ 0,5 s (sai a primeira).
    for (usize i = 0; i + 1 < n; ++i) {
        if (marks[i] != CutReason::None || marks[i + 1] != CutReason::None) continue;
        if (!tok[i].empty() && tok[i] == tok[i + 1] && gap_after(i) <= 0.5 && !emphasis(tok[i])) marks[i] = CutReason::Stutter;
    }
    // Par dobrado (a b a b) com vãos ≤ 0,4 s: sai o primeiro par.
    for (usize i = 0; i + 3 < n; ++i) {
        if (marks[i] != CutReason::None || marks[i + 1] != CutReason::None) continue;
        if (tok[i].empty() || tok[i + 1].empty() || tok[i] == tok[i + 1]) continue;
        if (tok[i] == tok[i + 2] && tok[i + 1] == tok[i + 3] && gap_after(i) <= 0.4 && gap_after(i + 1) <= 0.4
            && gap_after(i + 2) <= 0.4 && !(emphasis(tok[i]) && emphasis(tok[i + 1])))
            marks[i] = marks[i + 1] = CutReason::Stutter;
    }
    // Falso começo: "-"/"…" no fim, ou toco ≤ 4 letras, ≤ 0,3 s, prefixo de ≤ 70 % da seguinte.
    for (usize i = 0; i + 1 < n; ++i) {
        if (marks[i] != CutReason::None) continue;
        const std::string& t = tok[i];
        if (t.empty()) continue;
        bool falseStart = cut_off(words[i].text);
        if (!falseStart && t.size() >= 2 && t.size() <= 4 && !weak_word(t) && words[i].end - words[i].start <= 0.3) {
            const std::string& next = tok[i + 1];
            falseStart = next.size() > t.size() && next.compare(0, t.size(), t) == 0
                      && static_cast<f64>(t.size()) <= 0.7 * static_cast<f64>(next.size()) && gap_after(i) <= 0.5;
        }
        if (falseStart) marks[i] = CutReason::FalseStart;
    }

    // §3.4 Tomadas repetidas (só "retomadas").
    if (opt.recording == Recording::Retakes) {
        for (int pass = 0; pass < 3; ++pass) {
            bool changed = false;
            // Frases com as palavras que ainda ficam.
            std::vector<Sentence> ss;
            std::vector<std::vector<u32>> idx;
            for (const auto& s : sentences) {
                std::vector<u32> ids;
                for (u32 k = s.first; k < s.first + s.count; ++k) if (marks[k] == CutReason::None && !tok[k].empty()) ids.push_back(k);
                if (ids.empty()) continue;
                ss.push_back(s); idx.push_back(std::move(ids));
            }
            std::vector<u8> gone(ss.size(), 0);
            for (usize a = 0; a < ss.size(); ++a) {
                if (gone[a] || idx[a].size() < 3) continue;
                std::vector<std::string> ta;
                for (u32 k : idx[a]) ta.push_back(tok[k]);
                const f64 aEnd = words[idx[a].back()].end;
                for (usize b = a + 1; b < ss.size(); ++b) {
                    if (gone[b]) continue;
                    if (words[idx[b].front()].start - aEnd > 60.0) break;
                    if (idx[b].size() < 3) continue;
                    std::vector<std::string> tb;
                    for (u32 k : idx[b]) tb.push_back(tok[k]);
                    const f64 lcs = token_lcs(ta, tb);
                    const f64 mx = static_cast<f64>(std::max(ta.size(), tb.size())), mn = static_cast<f64>(std::min(ta.size(), tb.size()));
                    const bool first3 = tokens_match(ta[0], tb[0]) && tokens_match(ta[1], tb[1]) && tokens_match(ta[2], tb[2]);
                    const bool repeat = lcs / mx >= (first3 ? 0.45 : 0.55);
                    const bool restart = ta.size() != tb.size() && lcs / mn >= 0.80;
                    if (!repeat && !restart) continue;
                    if (contrast_veto(ta, tb, opt.language)) continue;
                    // Fica a ÚLTIMA tentativa, exceto se ela termina sem ponto e a
                    // anterior está completa e é maior.
                    const bool laterIncomplete = !ss[b].complete && !ends_sentence(words[idx[b].back()].text);
                    const bool earlierComplete = ss[a].complete || ends_sentence(words[idx[a].back()].text);
                    const usize drop = laterIncomplete && earlierComplete && ta.size() > tb.size() ? b : a;
                    for (u32 k : idx[drop]) marks[k] = CutReason::Retake;
                    gone[drop] = 1;
                    changed = true;
                    if (drop == a) break;
                }
            }
            if (!changed) break;
        }
        // Sequência de 5 palavras que volta em ≤ 20 s no início de frase: sai da 1ª até a 2ª.
        std::vector<u32> live;
        for (u32 k = 0; k < n; ++k) if (marks[k] == CutReason::None && !tok[k].empty()) live.push_back(k);
        std::vector<u8> sentenceStart(n, 0);
        for (const auto& s : sentences) sentenceStart[s.first] = 1;
        for (usize j = 5; j + 5 <= live.size(); ++j) {
            const u32 wj = live[j];
            bool startHere = sentenceStart[wj];
            if (!startHere) {   // a 1ª palavra que fica da frase também conta como início
                for (const auto& s : sentences) if (wj >= s.first && wj < s.first + s.count) {
                    startHere = true;
                    for (u32 k = s.first; k < wj; ++k) if (marks[k] == CutReason::None && !tok[k].empty()) startHere = false;
                    break;
                }
            }
            if (!startHere) continue;
            for (usize i = 0; i + 5 <= j; ++i) {
                if (words[wj].start - words[live[i]].start > 20.0) continue;
                bool same = true;
                for (usize k = 0; k < 5 && same; ++k) same = tokens_match(tok[live[i + k]], tok[live[j + k]]);
                if (!same) continue;
                for (u32 k = live[i]; k < wj; ++k) if (marks[k] == CutReason::None) marks[k] = CutReason::Retake;
                break;
            }
        }
    }
    return marks;
}

// -----------------------------------------------------------------------------
// Plano de cortes
// -----------------------------------------------------------------------------
namespace {

struct Interval { f64 a, b; CutReason reason; };

void add_interval(std::vector<Interval>& out, f64 a, f64 b, CutReason reason, f64 minLength = 0.04) {
    if (b - a > minLength) out.push_back({a, b, reason});
}

} // namespace

CutPlan plan_cuts(const std::vector<Word>& words, f64 duration, const Options& opt, const Envelope* env, const Speech* speech) {
    CutPlan plan;
    plan.duration = duration;
    plan.wordMarks = opt.cuts ? mark_words(words, opt) : std::vector<CutReason>(words.size(), CutReason::None);
    auto whole = [&] { plan.keep = {{0.0, duration}}; plan.cuts.clear(); plan.kept = duration; };
    if (!opt.cuts || duration <= 0.0 || words.empty()) { whole(); return plan; }
    const PauseRule rule = pause_rule(opt.intensity);
    const bool energy = env && speech && env->size() > 0;
    auto loud = [&](f64 t) {
        if (!energy) return false;
        const i64 f = static_cast<i64>(std::floor(t / kFrameSec));
        return f >= 0 && static_cast<usize>(f) < env->size() && env->db[static_cast<usize>(f)] >= speech->thresholdDb;
    };
    // Borda pela energia: início recua até 0,15 s, fim avança até 0,25 s.
    auto extend_back = [&](f64 t, f64 floor) {
        f64 x = t;
        while (x - kFrameSec >= std::max(floor, t - 0.15) && loud(x - kFrameSec)) x -= kFrameSec;
        return x;
    };
    auto extend_forward = [&](f64 t, f64 ceiling) {
        f64 x = t;
        while (x + kFrameSec <= std::min(ceiling, t + 0.25) && loud(x)) x += kFrameSec;
        return x;
    };
    auto voiced_in = [&](f64 a, f64 b) {
        if (!speech || speech->voiced.empty()) return false;
        for (i64 f = static_cast<i64>(a / kFrameSec); f < static_cast<i64>(b / kFrameSec); ++f)
            if (f >= 0 && static_cast<usize>(f) < speech->voiced.size() && speech->voiced[static_cast<usize>(f)]) return true;
        return false;
    };
    auto breath_in = [&](f64 a, f64 b) {
        f64 total = 0.0;
        if (speech) for (const auto& r : speech->breaths) total += std::max(0.0, std::min(b, r.end) - std::max(a, r.start));
        return total;
    };

    std::vector<usize> keptIdx;
    for (usize i = 0; i < words.size(); ++i) if (plan.wordMarks[i] == CutReason::None) keptIdx.push_back(i);
    if (keptIdx.empty()) { whole(); plan.aborted = true; return plan; }

    std::vector<Interval> cuts;
    auto first_reason = [&](usize from, usize to) {   // motivo da 1ª palavra marcada em (from, to)
        for (usize k = from + 1; k < to; ++k) if (plan.wordMarks[k] != CutReason::None) return plan.wordMarks[k];
        return CutReason::Silence;
    };
    // Corte de palavra: max(anterior + 0,02, início − 0,05) .. min(próxima − 0,03, fim + 0,05).
    auto cut_words = [&](usize from, usize to) {   // palavras marcadas em (from, to)
        for (usize k = from + 1; k < to; ++k) {
            if (plan.wordMarks[k] == CutReason::None) continue;
            const f64 prevEnd = k ? words[k - 1].end : 0.0;
            const f64 nextStart = k + 1 < words.size() ? words[k + 1].start : duration;
            const f64 a = std::max(prevEnd + 0.02, words[k].start - 0.05);
            const f64 b = std::min(nextStart - 0.03, words[k].end + 0.05);
            add_interval(cuts, std::max(a, words[from].end), std::min(b, to < words.size() ? words[to].start : duration),
                         plan.wordMarks[k]);
        }
    };
    // Antes da primeira e depois da última palavra que fica.
    {
        const Word& w = words[keptIdx.front()];
        const f64 s = extend_back(w.start, 0.0) - rule.before;
        if (s > 0.04) cuts.push_back({0.0, s, first_reason(static_cast<usize>(-1), keptIdx.front())});
        const Word& z = words[keptIdx.back()];
        const f64 e = extend_forward(z.end, duration) + rule.after;
        if (duration - e > 0.04) cuts.push_back({e, duration, CutReason::Silence});
    }
    for (usize k = 0; k + 1 < keptIdx.size(); ++k) {
        const usize ia = keptIdx[k], ib = keptIdx[k + 1];
        const Word& a = words[ia];
        const Word& b = words[ib];
        const f64 gap = b.start - a.end;
        const bool removedBetween = ib > ia + 1;
        if (gap > rule.gap) {
            const f64 from = extend_forward(a.end, b.start) + rule.after;
            const f64 to = extend_back(b.start, a.end) - rule.before;
            if (to - from > 0.04) { cuts.push_back({from, to, removedBetween ? first_reason(ia, ib) : CutReason::Silence}); continue; }
        }
        if (removedBetween) { cut_words(ia, ib); continue; }
        // Respiro entre palavras: vão sem vogal ≥ 0,3 s com ≥ 0,18 s de respiro.
        if (gap >= 0.3 && breath_in(a.end, b.start) >= 0.18 && !voiced_in(a.end, b.start))
            add_interval(cuts, a.end + 0.06, b.start - 0.06, CutReason::Breath, 0.08);
    }
    std::sort(cuts.begin(), cuts.end(), [](const Interval& x, const Interval& y) { return x.a < y.a; });
    // União; trechos a ≤ 20 ms se fundem (o corte some).
    std::vector<Interval> merged;
    for (const auto& c : cuts) {
        const f64 a = std::clamp(c.a, 0.0, duration), b = std::clamp(c.b, 0.0, duration);
        if (b - a <= 0.02) continue;
        if (!merged.empty() && a <= merged.back().b + 0.02) merged.back().b = std::max(merged.back().b, b);
        else merged.push_back({a, b, c.reason});
    }
    // Mantidos = complemento.
    auto keeps_of = [&](const std::vector<Interval>& cs) {
        std::vector<Range> k;
        f64 t = 0.0;
        for (const auto& c : cs) { if (c.a > t) k.push_back({t, c.a}); t = std::max(t, c.b); }
        if (t < duration) k.push_back({t, duration});
        return k;
    };
    // Faxina: fiapo < 0,15 s entre cortes vira um corte; mantido ≤ 0,12 s sai;
    // < 0,2 s sem palavra sai; ≤ 0,25 s só com palavras fracas sai.
    auto has_word = [&](const Range& r, bool strong) {
        for (usize i : keptIdx) {
            const Word& w = words[i];
            const f64 mid = (w.start + w.end) * 0.5;
            if (mid < r.start || mid >= r.end) continue;
            if (!strong || !weak_word(normalize_token(w.text, opt.language))) return true;
        }
        return false;
    };
    for (bool again = true; again;) {
        again = false;
        const auto k = keeps_of(merged);
        for (const auto& r : k) {
            const bool inner = r.start > 0.0 && r.end < duration;
            const bool sliver = inner && r.length() < 0.15;
            const bool tiny = r.length() <= 0.12 || (r.length() < 0.2 && !has_word(r, false))
                           || (r.length() <= 0.25 && !has_word(r, true));
            if (!(sliver || tiny)) continue;
            merged.push_back({r.start, r.end, CutReason::Cleanup});
            std::sort(merged.begin(), merged.end(), [](const Interval& x, const Interval& y) { return x.a < y.a; });
            std::vector<Interval> joined;
            for (const auto& c : merged) {
                if (!joined.empty() && c.a <= joined.back().b + 1e-9) {
                    joined.back().b = std::max(joined.back().b, c.b);
                    if (joined.back().reason == CutReason::Cleanup) joined.back().reason = c.reason;
                } else joined.push_back(c);
            }
            merged = std::move(joined);
            again = true;
            break;
        }
    }
    // Bordas no quadro de vídeo.
    const f64 fps = opt.fps > 0.0 ? opt.fps : 30.0;
    std::vector<Range> keep;
    for (const auto& r : keeps_of(merged)) {
        const f64 a = std::round(r.start * fps) / fps, b = std::min(duration, std::round(r.end * fps) / fps);
        if (b - a >= 1.0 / fps) keep.push_back({a, b});
    }
    f64 kept = 0.0;
    for (const auto& r : keep) kept += r.length();
    // Trava: aborta se sobrar < 8 %.
    if (keep.empty() || kept < 0.08 * duration) { whole(); plan.aborted = true; return plan; }
    plan.keep = std::move(keep);
    plan.kept = kept;
    for (const auto& c : merged) plan.cuts.push_back({{c.a, c.b}, c.reason});
    return plan;
}

// -----------------------------------------------------------------------------
// Mapa do tempo
// -----------------------------------------------------------------------------
TimeMap::TimeMap(std::vector<Range> keep) : keep_(std::move(keep)) {
    for (const auto& r : keep_) { starts_.push_back(total_); total_ += std::max(0.0, r.length()); }
}

f64 TimeMap::to_edit(f64 s) const noexcept {
    for (usize i = 0; i < keep_.size(); ++i) {
        if (s >= keep_[i].start && (s < keep_[i].end || (i + 1 == keep_.size() && s <= keep_[i].end)))
            return starts_[i] + (s - keep_[i].start);
    }
    return -1.0;
}

f64 TimeMap::to_source(f64 e) const noexcept {
    if (keep_.empty()) return e;
    for (usize i = 0; i < keep_.size(); ++i)
        if (e < starts_[i] + keep_[i].length() || i + 1 == keep_.size())
            return keep_[i].start + std::clamp(e - starts_[i], 0.0, keep_[i].length());
    return keep_.back().end;
}

std::vector<f64> TimeMap::splices() const {
    return starts_.size() > 1 ? std::vector<f64>(starts_.begin() + 1, starts_.end()) : std::vector<f64>{};
}

std::vector<Word> map_words(const std::vector<Word>& words, const TimeMap& map) {
    std::vector<Word> out;
    const auto& keep = map.keep();
    for (const auto& w : words) {
        const f64 len = std::max(1e-3, w.end - w.start);
        f64 inside = 0.0;
        i64 first = -1, last = -1;
        for (usize i = 0; i < keep.size(); ++i) {
            const f64 o = std::min(w.end, keep[i].end) - std::max(w.start, keep[i].start);
            if (o > 0.0 || (w.end <= w.start && w.start >= keep[i].start && w.start < keep[i].end)) {
                inside += std::max(0.0, o);
                if (first < 0) first = static_cast<i64>(i);
                last = static_cast<i64>(i);
            }
        }
        if (first < 0 || (w.end > w.start && (len - inside) / len >= 0.7)) continue;
        const auto& a = keep[static_cast<usize>(first)];
        const auto& b = keep[static_cast<usize>(last)];
        Word m = w;
        m.start = map.starts()[static_cast<usize>(first)] + (std::max(w.start, a.start) - a.start);
        m.end = map.starts()[static_cast<usize>(last)] + (std::min(w.end, b.end) - b.start);
        if (m.end < m.start) m.end = m.start;
        if (!out.empty() && m.start < out.back().end) m.start = out.back().end;
        m.end = std::max(m.end, m.start);
        out.push_back(std::move(m));
    }
    return out;
}

} // namespace aurea::autoedit
