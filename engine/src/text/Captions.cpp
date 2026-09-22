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
#include <cstdlib>

namespace aurea::text {

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

} // namespace

const char* caption_style_name(u32 id) noexcept {
    static const char* const names[kCaptionStyleCount] = {"Clássico", "Caixa", "Destaque", "Neon", "Karaokê", "Pop"};
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
    std::vector<CaptionGroup> out;
    const u32 maxWords = opt.mode == 1 ? 1u : std::clamp<u32>(opt.maxWords, 1, 20);
    const u32 maxChars = std::clamp<u32>(opt.maxChars, 4, 80);
    const u32 maxLines = std::clamp<u32>(opt.maxLines, 1, 4);
    CaptionGroup g;
    std::vector<std::string> lines;
    auto flush = [&]() {
        if (g.count == 0) return;
        g.text.clear();
        for (usize i = 0; i < lines.size(); ++i) g.text += (i ? "\n" : "") + lines[i];
        out.push_back(g);
        g = CaptionGroup{};
        lines.clear();
    };
    for (u32 i = 0; i < words.size(); ++i) {
        std::string w = words[i].text;
        // Espaços das pontas (o Whisper devolve " palavra").
        while (!w.empty() && std::isspace(static_cast<unsigned char>(w.front()))) w.erase(w.begin());
        while (!w.empty() && std::isspace(static_cast<unsigned char>(w.back()))) w.pop_back();
        if (w.empty()) continue;
        if (opt.uppercase) w = upper(w);
        if (g.count > 0) {
            const bool pause = opt.breakOnPause && words[i].start - g.end > static_cast<f64>(std::max(0.05f, opt.pauseSec));
            const bool full = g.count >= maxWords;
            const bool fits = cp_len(lines.back()) + 1 + cp_len(w) <= maxChars;
            const bool noLine = !fits && lines.size() >= maxLines;
            if (pause || full || noLine) flush();
        }
        if (g.count == 0) {
            g.first = i;
            g.start = words[i].start;
            lines.push_back(w);
        } else if (cp_len(lines.back()) + 1 + cp_len(w) <= maxChars) {
            lines.back() += " " + w;
        } else {
            lines.push_back(w);
        }
        ++g.count;
        g.end = std::max(words[i].end, words[i].start);
        // Fim de frase fecha a legenda (não junta duas frases).
        const char last = w.back();
        if (opt.mode == 0 && (last == '.' || last == '?' || last == '!')) flush();
    }
    flush();
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

void apply_caption_style(const CaptionOptions& opt, u32 shortSide, TextData& t, TrackSet& tr, const std::vector<i64>& wf, i64 endFrame) {
    const f32 size = std::round(std::clamp(opt.sizeFrac, 0.02f, 0.25f) * static_cast<f32>(std::max(1u, shortSide)));
    t.size = size;
    t.alignment = 1;
    t.lineHeight = 1.15f;
    t.animators.clear();
    tr.remove_if([](const Track& x) { return x.property == TrackProperty::TextAnimParam; });
    const u32 style = std::min(opt.style, kCaptionStyleCount - 1);
    switch (style) {
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
    const u32 n = static_cast<u32>(wf.size());
    if (n == 0) return;
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
