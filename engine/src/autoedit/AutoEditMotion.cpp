// =============================================================================
//  Edição automática: zoom "Reels" (§4), sons e flash (§5), destaque (§9) e a
//  posição da legenda fora dos rostos. Regras puras.
// =============================================================================
#include "aurea/autoedit/AutoEdit.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::autoedit {

u32 Mulberry32::next_u32() noexcept {
    state_ += 0x6D2B79F5u;
    u32 t = state_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
}

f32 easy_ease(f32 x) noexcept {
    x = std::clamp(x, 0.0f, 1.0f);
    // x(u) da cúbica com P1 = (0,65, 0) e P2 = (0,35, 1): bisseção (monótona).
    auto bez = [](f64 p1, f64 p2, f64 u) { const f64 v = 1.0 - u; return 3.0 * v * v * u * p1 + 3.0 * v * u * u * p2 + u * u * u; };
    f64 lo = 0.0, hi = 1.0, u = x;
    for (int i = 0; i < 40; ++i) {
        u = (lo + hi) * 0.5;
        if (bez(kEaseX1, kEaseX2, u) < x) lo = u; else hi = u;
    }
    return static_cast<f32>(bez(kEaseY1, kEaseY2, u));
}

ZoomStrength zoom_strength(u32 level) noexcept {
    switch (level) {
        case 0: return {1.06f, 1.10f, 0.40f};
        case 2: return {1.18f, 1.26f, 0.75f};
        default: return {1.12f, 1.18f, 0.60f};
    }
}

const char* zoom_type_name(ZoomType t) noexcept {
    switch (t) {
        case ZoomType::Intro: return "intro";
        case ZoomType::Hold: return "hold";
        case ZoomType::InOut: return "inout";
        case ZoomType::Push: return "push";
        case ZoomType::Track: return "track";
        default: return "none";
    }
}

namespace {
const Face* largest(const std::vector<Face>& faces, f64 from, f64 to) {
    const Face* best = nullptr;
    for (const auto& f : faces)
        if (f.time >= from && f.time <= to && f.w > 0 && f.h > 0 && (!best || f.w * f.h > best->w * best->h)) best = &f;
    return best;
}
Anchor anchor_of(const Face& f) noexcept {
    return {std::clamp(f.x + f.w * 0.5f, 0.0f, 1.0f), std::clamp(f.y + 0.45f * f.h, 0.0f, 1.0f), true};
}
} // namespace

Anchor face_anchor(const std::vector<Face>& faces, f64 from, f64 to) {
    const Face* f = largest(faces, from, to);
    if (!f) {
        // Sem amostra no trecho: a mais próxima a até 1,5 s.
        f64 best = 1.5;
        for (const auto& c : faces) {
            if (c.w <= 0 || c.h <= 0) continue;
            const f64 d = c.time < from ? from - c.time : (c.time > to ? c.time - to : 0.0);
            if (d <= best) { best = d; f = &c; }
        }
    }
    return f ? anchor_of(*f) : Anchor{};
}

std::vector<Range> zoom_pieces(const std::vector<Range>& clips, const std::vector<f64>& captionStarts) {
    std::vector<Range> out;
    for (const auto& c : clips) {
        if (c.length() <= 0.0) continue;
        std::vector<f64> marks;
        for (f64 s : captionStarts) if (s > c.start + 1.5 && s < c.end - 1.5) marks.push_back(s);
        std::sort(marks.begin(), marks.end());
        if (marks.empty()) {
            for (f64 t = c.start + 2.5; t < c.end - 1.25; t += 2.5) marks.push_back(t);
        }
        f64 a = c.start;
        for (f64 m : marks) { if (m - a > 1e-6) { out.push_back({a, m}); a = m; } }
        out.push_back({a, c.end});
    }
    return out;
}

std::vector<ZoomPiece> plan_zoom(const std::vector<Range>& clips, const std::vector<f64>& captionStarts,
                                 const std::vector<Word>& words, const std::vector<Face>& faces, u32 level) {
    const ZoomStrength k = zoom_strength(level);
    const auto pieces = zoom_pieces(clips, captionStarts);
    f64 total = 0.0;
    for (const auto& p : pieces) total += p.length();
    std::vector<ZoomPiece> out;
    if (pieces.empty() || total <= 0.0) return out;
    auto is_clip_start = [&](f64 t) { for (const auto& c : clips) if (std::fabs(c.start - t) < 1e-6) return true; return false; };
    auto is_clip_end = [&](f64 t) { for (const auto& c : clips) if (std::fabs(c.end - t) < 1e-6) return true; return false; };
    Mulberry32 rng(7);
    f64 zoomed = 0.0;
    bool prevZoom = false;
    ZoomType prevType = ZoomType::None;
    for (usize i = 0; i < pieces.size(); ++i) {
        const Range r = pieces[i];
        const f64 d = r.length();
        const Anchor an = face_anchor(faces, r.start, r.end);
        ZoomPiece z;
        z.range = r;
        const bool cs = is_clip_start(r.start), ce = is_clip_end(r.end);
        auto key = [&](f64 t, f32 s, bool ease = true) { z.keys.push_back({t, s, an.x, an.y, ease}); };
        if (i == 0) {
            // Intro "out": começa em push % e vai a 100 % em min(1,2, d/2).
            z.type = ZoomType::Intro;
            z.peak = k.push;
            z.rampIn = std::min(1.2, d / 2.0);
            key(r.start, k.push);
            key(r.start + z.rampIn, 1.0f);
            out.push_back(std::move(z));
            continue;
        }
        const f64 gate = rng.next();
        const f64 pick = rng.next();
        if (prevZoom || gate >= 0.9 || (zoomed + d) / total > k.share) {
            prevZoom = false;
            out.push_back(std::move(z));
            continue;
        }
        struct Option { ZoomType type; f64 weight; };
        std::vector<Option> opts;
        if (d >= 1.2 && prevType != ZoomType::Hold) opts.push_back({ZoomType::Hold, 1.0});
        if (d >= 1.6 && prevType != ZoomType::InOut) opts.push_back({ZoomType::InOut, 1.0});
        if (d >= 2.6 && prevType != ZoomType::Push) opts.push_back({ZoomType::Push, 0.8});
        if (d >= 2.0 && prevType != ZoomType::Track && an.face) opts.push_back({ZoomType::Track, 0.45});
        if (opts.empty()) { prevZoom = false; out.push_back(std::move(z)); continue; }
        f64 sum = 0.0;
        for (const auto& o : opts) sum += o.weight;
        f64 acc = 0.0;
        ZoomType type = opts.back().type;
        for (const auto& o : opts) { acc += o.weight / sum; if (pick < acc) { type = o.type; break; } }
        z.type = type;
        switch (type) {
            case ZoomType::Hold: {
                z.peak = k.punch;
                const f64 ramp = std::min(0.7, d / 2.5);
                z.rampIn = cs ? 0.0 : ramp;
                if (cs) key(r.start, k.punch);
                else { key(r.start, 1.0f); key(r.start + ramp, k.punch); }
                if (!ce) { key(r.end - ramp, k.punch); key(r.end, 1.0f); }
                break;
            }
            case ZoomType::InOut: {
                z.peak = k.push;
                const f64 ramp = std::min(0.9, d / 2.5);
                z.rampIn = ramp;
                key(r.start, 1.0f); key(r.start + ramp, k.push); key(r.end - ramp, k.push); key(r.end, 1.0f);
                break;
            }
            case ZoomType::Push: {
                z.peak = k.push;
                // Até o acento (a palavra de maior destaque) + 0,35 s.
                f64 accent = r.start + d * 0.5;
                i32 best = -1000;
                for (usize w = 0; w < words.size(); ++w) {
                    if (words[w].start < r.start || words[w].start >= r.end) continue;
                    const bool last = w + 1 >= words.size() || words[w + 1].start >= r.end;
                    const i32 s = highlight_score(words[w].text, last);
                    if (s > best) { best = s; accent = words[w].start; }
                }
                const f64 peakT = std::clamp(accent + 0.35, r.start + 0.3, ce ? r.end : r.end - 0.2);
                z.rampIn = peakT - r.start;
                key(r.start, 1.0f);
                key(peakT, k.push);
                if (r.end - peakT > 0.15 || !ce) key(r.end, 1.0f);
                break;
            }
            case ZoomType::Track: {
                const f32 scale = (k.punch + k.push) * 0.5f;
                z.peak = scale;
                const f64 ramp = std::min(0.7, d / 2.5);
                z.rampIn = cs ? 0.0 : ramp;
                // Posição segue o rosto (média de 3 amostras).
                std::vector<const Face*> samples;
                for (const auto& f : faces) if (f.time >= r.start && f.time <= r.end && f.w > 0) samples.push_back(&f);
                std::sort(samples.begin(), samples.end(), [](const Face* a, const Face* b) { return a->time < b->time; });
                if (!cs) z.keys.push_back({r.start, 1.0f, an.x, an.y, true});
                z.keys.push_back({r.start + z.rampIn, scale, an.x, an.y, true});
                for (usize s = 0; s < samples.size(); ++s) {
                    const usize a = s ? s - 1 : s, b = std::min(samples.size() - 1, s + 1);
                    Anchor m{};
                    m.x = (anchor_of(*samples[a]).x + anchor_of(*samples[s]).x + anchor_of(*samples[b]).x) / 3.0f;
                    m.y = (anchor_of(*samples[a]).y + anchor_of(*samples[s]).y + anchor_of(*samples[b]).y) / 3.0f;
                    const f64 t = samples[s]->time;
                    if (t <= z.keys.back().time + 0.05 || t >= r.end - (ce ? 0.0 : ramp)) continue;
                    z.keys.push_back({t, scale, m.x, m.y, true});
                }
                if (!ce) {
                    const auto lastKey = z.keys.back();
                    z.keys.push_back({std::max(lastKey.time, r.end - ramp), scale, lastKey.ax, lastKey.ay, true});
                    z.keys.push_back({r.end, 1.0f, lastKey.ax, lastKey.ay, true});
                }
                break;
            }
            default: break;
        }
        zoomed += d;
        prevZoom = true;
        prevType = type;
        out.push_back(std::move(z));
    }
    return out;
}

ZoomPose zoom_position(f32 px, f32 py, const f32 e[4], f32 z, f32 fx, f32 fy, f32 w, f32 h) noexcept {
    ZoomPose p{fx + z * (px - fx), fy + z * (py - fy)};
    // Bordas: esquerda = x − z·eL ≤ 0, direita = x + z·eR ≥ w (idem em y).
    const f32 minX = w - z * e[1], maxX = z * e[0];
    if (minX <= maxX) p.x = std::clamp(p.x, minX, maxX);
    const f32 minY = h - z * e[3], maxY = z * e[2];
    if (minY <= maxY) p.y = std::clamp(p.y, minY, maxY);
    return p;
}

i32 highlight_score(std::string_view w, bool last) {
    u32 letters = 0, upper = 0, lower = 0;
    bool digit = false, mark = false;
    for (usize i = 0; i < w.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(w[i]);
        if ((c & 0xC0) == 0x80) continue;   // continuação UTF-8
        if (c >= '0' && c <= '9') { digit = true; ++letters; }
        else if (c >= 'A' && c <= 'Z') { ++letters; ++upper; }
        else if (c >= 'a' && c <= 'z') { ++letters; ++lower; }
        else if (c >= 0xC0) { ++letters; if (c == 0xC3 && i + 1 < w.size()) { ((static_cast<unsigned char>(w[i + 1]) & 0x20) ? lower : upper) += 1; } }
        else if (c == '!' || c == '?') mark = true;
    }
    i32 s = 10 + 2 * static_cast<i32>(std::min<u32>(letters, 11));
    if (digit) s += 30;
    if (mark) s += 8;
    if (letters <= 3) s -= 6;
    if (last) s += 6;
    if (upper >= 2 && lower == 0) s += 6;
    return s;
}

// -----------------------------------------------------------------------------
// §5 Sons e flash
// -----------------------------------------------------------------------------
const char* sfx_role_name(SfxRole r) noexcept {
    switch (r) {
        case SfxRole::Woosh: return "woosh";
        case SfxRole::Shutter: return "shutter";
        case SfxRole::Riser: return "riser";
        case SfxRole::Sub: return "sub";
        case SfxRole::Flash: return "flash";
        case SfxRole::CutFlick: return "cutflick";
        case SfxRole::Flick: return "flick";
        default: return "";
    }
}

f32 sfx_target_peak(SfxRole r) noexcept {
    switch (r) {
        case SfxRole::Woosh: return -21.0f;
        case SfxRole::Shutter: return -24.0f;
        case SfxRole::Riser: return -27.0f;
        case SfxRole::Sub: return -22.0f;
        case SfxRole::Flash: return -24.0f;
        case SfxRole::CutFlick: return -23.0f;
        case SfxRole::Flick: return -26.0f;
        default: return -24.0f;
    }
}

f32 sfx_gain_db(SfxRole r, f32 filePeakDb) noexcept {
    if (!std::isfinite(filePeakDb)) return 0.0f;
    return std::clamp(sfx_target_peak(r) - filePeakDb, -40.0f, 12.0f);
}

u32 sfx_priority(SfxRole r) noexcept {
    switch (r) {
        case SfxRole::Sub: return 7;
        case SfxRole::Riser: return 6;
        case SfxRole::Woosh: return 5;
        case SfxRole::Flash: return 4;
        case SfxRole::Shutter: return 3;
        case SfxRole::CutFlick: return 2;
        default: return 1;
    }
}

std::vector<f64> plan_flashes(const std::vector<f64>& splicesIn, f64 duration) {
    std::vector<f64> splices = splicesIn;
    std::sort(splices.begin(), splices.end());
    std::vector<f64> out;
    auto usable = [&](f64 t) { return t >= 0.6 && t <= duration - 0.5; };
    f64 first = -1.0;
    for (f64 s : splices) if (s >= 0.6 && s <= 4.0 && usable(s)) { first = s; break; }
    if (first < 0.0) for (f64 s : splices) if (usable(s)) { first = s; break; }
    if (first < 0.0) return out;
    out.push_back(first);
    const f64 spacing = std::max(6.0, (duration - first) / 3.0);
    for (int k = 1; k < 3; ++k) {
        const f64 target = first + spacing * k;
        f64 best = -1.0, err = 1e30;
        for (f64 s : splices) {
            if (!usable(s) || s < out.back() + 6.0) continue;
            if (std::fabs(s - target) < err) { err = std::fabs(s - target); best = s; }
        }
        if (best < 0.0) break;
        out.push_back(best);
    }
    return out;
}

std::vector<f64> plan_light_leaks(const std::vector<f64>& splicesIn, f64 duration) {
    std::vector<f64> s = splicesIn;
    std::sort(s.begin(), s.end());
    std::vector<f64> out;
    for (usize i = 0; i < s.size() && out.size() < 6; ++i) {
        const f64 before = i ? s[i] - s[i - 1] : s[i];
        const f64 after = (i + 1 < s.size() ? s[i + 1] : duration) - s[i];
        if (before < 3.0 || after < 3.0) continue;
        if (!out.empty() && s[i] - out.back() < 8.0) continue;
        out.push_back(s[i]);
    }
    return out;
}

std::vector<OpacityKey> flash_keys(f64 splice) {
    return {{splice - 0.13, 0.0f}, {splice, 1.0f}, {splice + 0.17, 1.0f}, {splice + 0.30, 0.0f}};
}

std::vector<SfxEvent> resolve_sfx_clashes(std::vector<SfxEvent> events) {
    std::vector<usize> order(events.size());
    for (usize i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](usize a, usize b) {
        const u32 pa = sfx_priority(events[a].role), pb = sfx_priority(events[b].role);
        return pa != pb ? pa > pb : events[a].time < events[b].time;
    });
    auto exempt = [](SfxRole r) { return r == SfxRole::Sub || r == SfxRole::Riser; };
    std::vector<SfxEvent> kept;
    for (usize i : order) {
        const auto& e = events[i];
        bool clash = false;
        if (!exempt(e.role))
            for (const auto& k : kept) if (!exempt(k.role) && std::fabs(k.time - e.time) < 0.5) { clash = true; break; }
        if (!clash) kept.push_back(e);
    }
    std::stable_sort(kept.begin(), kept.end(), [](const SfxEvent& a, const SfxEvent& b) { return a.time < b.time; });
    return kept;
}

std::vector<SfxEvent> plan_sfx(const SfxInputs& in) {
    std::vector<SfxEvent> ev;
    auto has = [&](SfxRole r) { return in.variants[static_cast<u32>(r)] > 0; };
    auto offset = [&](SfxRole r) { return in.peakOffset[static_cast<u32>(r)]; };
    u32 rotate[kSfxRoleCount]{};
    auto add = [&](SfxRole r, f64 peakTime, f32 extraDb = 0.0f) {
        if (!has(r)) return;
        const u32 role = static_cast<u32>(r);
        ev.push_back({std::max(0.0, peakTime - offset(r)), r, extraDb, rotate[role]++ % in.variants[role]});
    };
    std::vector<f64> covered;   // emendas que já têm som (zoom/flash)
    for (const auto& z : in.zooms) {
        switch (z.type) {
            case ZoomType::Intro:
            case ZoomType::InOut: add(SfxRole::Woosh, z.range.start + 0.75 * z.rampIn); covered.push_back(z.range.start); break;
            case ZoomType::Push: add(SfxRole::Woosh, z.range.start + 0.75 * z.rampIn, -2.0f); covered.push_back(z.range.start); break;
            case ZoomType::Hold: add(SfxRole::Shutter, z.range.start); covered.push_back(z.range.start); break;
            default: break;
        }
    }
    for (f64 f : in.flashes) { add(SfxRole::Flash, f); covered.push_back(f); }
    // Corte seco: probabilidade 0,45, ≥ 2 s entre eles (semente 11).
    if (has(SfxRole::CutFlick)) {
        Mulberry32 rng(11);
        f64 last = -1e9;
        for (f64 s : in.splices) {
            const f64 roll = rng.next();
            const u32 variant = rng.next_u32() % in.variants[static_cast<u32>(SfxRole::CutFlick)];
            bool busy = false;
            for (f64 c : covered) if (std::fabs(c - s) < 0.5) busy = true;
            if (busy || roll >= 0.45 || s - last < 2.0) continue;
            ev.push_back({std::max(0.0, s - offset(SfxRole::CutFlick)), SfxRole::CutFlick, 0.0f, variant});
            last = s;
        }
    }
    // Palavras em blocos: ligado 1–2 legendas, desligado 2–3 (semente 13).
    if (has(SfxRole::Flick)) {
        Mulberry32 rng(13);
        bool on = false;
        u32 left = 2 + rng.next_u32() % 2;
        for (const auto& starts : in.captionWordStarts) {
            if (left == 0) { on = !on; left = on ? 1 + rng.next_u32() % 2 : 2 + rng.next_u32() % 2; }
            --left;
            if (!on) continue;
            for (f64 t : starts) {
                const u32 variant = rng.next_u32() % in.variants[static_cast<u32>(SfxRole::Flick)];
                ev.push_back({std::max(0.0, t - offset(SfxRole::Flick)), SfxRole::Flick, 0.0f, variant});
            }
        }
    }
    return resolve_sfx_clashes(std::move(ev));
}

// -----------------------------------------------------------------------------
// Legenda fora dos rostos
// -----------------------------------------------------------------------------
std::vector<f32> caption_positions(const std::vector<CaptionBlock>& blocks, const std::vector<Face>& faces,
                                   f32 defaultY, f32 boxH, bool vertical) {
    const f32 lo = vertical ? 0.14f : 0.10f, hi = vertical ? 0.70f : 0.90f;
    const f32 half = boxH * 0.5f, margin = 0.02f;
    auto clampY = [&](f32 y) { return std::clamp(y, lo + half, hi - half); };
    const f32 base = clampY(defaultY);
    struct Span { f32 top, bottom; };
    auto spans_for = [&](f64 a, f64 b) {
        std::vector<Span> s;
        for (const auto& f : faces) {
            if (f.time < a - 0.5 || f.time > b + 0.5 || f.w <= 0 || f.h <= 0) continue;
            if (f.x + f.w < 0.08f || f.x > 0.92f) continue;
            s.push_back({f.y - margin, f.y + f.h + margin});
        }
        return s;
    };
    auto overlap = [&](f32 y, const std::vector<Span>& s) {
        f32 o = 0.0f;
        for (const auto& z : s) o += std::max(0.0f, std::min(y + half, z.bottom) - std::max(y - half, z.top));
        return o;
    };
    auto place = [&](const std::vector<Span>& s) {
        if (overlap(base, s) <= 0.0f) return base;
        f32 below = -1.0f, above = 2.0f;
        for (const auto& z : s) { below = std::max(below, z.bottom); above = std::min(above, z.top); }
        std::vector<f32> cands;
        if (below + half <= hi - half + 1e-6f) cands.push_back(below + half);
        if (above - half >= lo + half - 1e-6f) cands.push_back(above - half);
        cands.push_back(hi - half);
        cands.push_back(lo + half);
        f32 best = base, bestO = overlap(base, s), bestD = 0.0f;
        for (f32 c : cands) {
            const f32 y = clampY(c), o = overlap(y, s), d = std::fabs(y - base);
            if (o < bestO - 1e-6f || (std::fabs(o - bestO) <= 1e-6f && d < bestD)) { best = y; bestO = o; bestD = d; }
        }
        return best;
    };
    std::vector<f32> out(blocks.size(), base);
    // Estável no trecho: uma posição para todas as legendas do mesmo trecho.
    for (usize i = 0; i < blocks.size();) {
        usize j = i;
        f64 a = blocks[i].time.start, b = blocks[i].time.end;
        while (j < blocks.size() && blocks[j].shot == blocks[i].shot) { b = std::max(b, blocks[j].time.end); ++j; }
        const f32 y = place(spans_for(a, b));
        bool clear = true;
        for (usize k = i; k < j && clear; ++k) clear = overlap(y, spans_for(blocks[k].time.start, blocks[k].time.end)) <= 0.0f;
        for (usize k = i; k < j; ++k) out[k] = clear ? y : place(spans_for(blocks[k].time.start, blocks[k].time.end));
        i = j;
    }
    return out;
}

} // namespace aurea::autoedit
