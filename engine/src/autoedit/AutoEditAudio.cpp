// =============================================================================
//  Edição automática: áudio da fala (§1), alinhamento (§2), voz (§7),
//  música/ducking (§6) e cor (§8). Regras puras — ver AutoEdit.hpp.
// =============================================================================
#include "aurea/autoedit/AutoEdit.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace aurea::autoedit {

Language language_of(std::string_view code) noexcept {
    if (code.size() >= 2) {
        const char a = static_cast<char>(code[0] | 0x20), b = static_cast<char>(code[1] | 0x20);
        if (a == 'e' && b == 'n') return Language::En;
        if (a == 'e' && b == 's') return Language::Es;
    }
    return Language::Pt;
}

f32 percentile(std::vector<f32> values, f32 p) {
    if (values.empty()) return -120.0f;
    std::sort(values.begin(), values.end());
    const f64 pos = std::clamp(static_cast<f64>(p), 0.0, 100.0) / 100.0 * static_cast<f64>(values.size() - 1);
    const usize lo = static_cast<usize>(std::floor(pos));
    const usize hi = std::min(values.size() - 1, lo + 1);
    const f64 t = pos - static_cast<f64>(lo);
    return static_cast<f32>(values[lo] * (1.0 - t) + values[hi] * t);
}

f32 edge_sensitivity_db(u32 sensitivity) noexcept {
    static constexpr f32 kTable[7] = {3.0f, 4.5f, 6.0f, 8.0f, 10.0f, 12.5f, 15.0f};
    return kTable[std::clamp<u32>(sensitivity, 1, 7) - 1];
}

Envelope analyze_envelope(const f32* x, usize count) {
    Envelope e;
    if (!x || count < kFrameSamples) return e;
    const usize frames = count / kFrameSamples;
    e.db.resize(frames); e.hfDb.resize(frames); e.zcr.resize(frames); e.peakDb.resize(frames);
    for (usize f = 0; f < frames; ++f) {
        const f32* s = x + f * kFrameSamples;
        f64 energy = 0.0, diff = 0.0;
        f32 peak = 0.0f;
        u32 crossings = 0;
        for (u32 i = 0; i < kFrameSamples; ++i) {
            energy += static_cast<f64>(s[i]) * s[i];
            peak = std::max(peak, std::fabs(s[i]));
            const f32 prev = i ? s[i - 1] : (f ? s[-1] : s[0]);
            const f64 d = static_cast<f64>(s[i]) - prev;
            diff += d * d;
            if (i && ((s[i] >= 0.0f) != (prev >= 0.0f))) ++crossings;
        }
        e.db[f] = static_cast<f32>(10.0 * std::log10(energy / kFrameSamples + 1e-18));
        e.hfDb[f] = static_cast<f32>(10.0 * std::log10(diff / kFrameSamples + 1e-18));
        e.zcr[f] = static_cast<f32>(crossings) / static_cast<f32>(kFrameSamples - 1);
        e.peakDb[f] = 20.0f * std::log10(peak + 1e-9f);
    }
    return e;
}

namespace {
std::vector<f32> audible(const std::vector<f32>& db) {
    std::vector<f32> v;
    v.reserve(db.size());
    for (f32 d : db) if (d > -120.0f) v.push_back(d);
    return v;
}
f64 sec(usize frame) noexcept { return static_cast<f64>(frame) * kFrameSec; }
} // namespace

Speech detect_speech(const Envelope& env, u32 sensitivity) {
    Speech out;
    const usize n = env.size();
    if (n == 0) return out;
    // Suavização de 3 quadros.
    std::vector<f32> s(n);
    for (usize i = 0; i < n; ++i) {
        const usize a = i ? i - 1 : i, b = std::min(n - 1, i + 1);
        s[i] = (env.db[a] + env.db[i] + env.db[b]) / 3.0f;
    }
    const auto loud = audible(s);
    out.noiseDb = percentile(loud, 10.0f);
    out.levelDb = percentile(loud, 95.0f);
    const f32 range = out.levelDb - out.noiseDb;
    if (range < 6.0f) out.thresholdDb = out.levelDb > -50.0f ? out.noiseDb - 1.0f : out.levelDb + 1.0f;
    else out.thresholdDb = out.noiseDb + std::max(6.0f, 0.3f * range);
    // Histerese ±2 dB e segura 120 ms.
    constexpr u32 kHold = 12, kMergeGap = 12, kClick = 6;
    std::vector<u8> on(n, 0);
    bool active = false;
    u32 hold = 0;
    for (usize i = 0; i < n; ++i) {
        if (!active && s[i] >= out.thresholdDb + 2.0f) { active = true; hold = kHold; }
        else if (active) {
            if (s[i] >= out.thresholdDb - 2.0f) hold = kHold;
            else if (hold == 0) active = false;
            else --hold;
        }
        on[i] = active ? 1 : 0;
    }
    // Regiões; buracos < 120 ms juntam; ilhas < 60 ms = estalo.
    std::vector<std::pair<usize, usize>> runs;
    for (usize i = 0; i < n;) {
        if (!on[i]) { ++i; continue; }
        usize j = i;
        while (j < n && on[j]) ++j;
        if (!runs.empty() && i - runs.back().second < kMergeGap) runs.back().second = j;
        else runs.emplace_back(i, j);
        i = j;
    }
    std::vector<std::pair<usize, usize>> kept;
    for (const auto& r : runs) {
        if (r.second - r.first < kClick) out.clicks.push_back({sec(r.first), sec(r.second)});
        else kept.push_back(r);
    }
    // Borda pelo ruído do vão (p20 do vão + sensibilidade); fricativa colada estende.
    const f32 sens = edge_sensitivity_db(sensitivity);
    for (usize k = 0; k < kept.size(); ++k) {
        const usize gapA = k ? kept[k - 1].second : 0;
        const usize gapB = k + 1 < kept.size() ? kept[k + 1].first : n;
        auto gap_noise = [&](usize a, usize b) {
            std::vector<f32> g(env.db.begin() + static_cast<std::ptrdiff_t>(a), env.db.begin() + static_cast<std::ptrdiff_t>(b));
            return g.empty() ? out.noiseDb : percentile(audible(g), 20.0f);
        };
        const f32 before = gap_noise(gapA, kept[k].first) + sens;
        const f32 after = gap_noise(kept[k].second, gapB) + sens;
        usize a = kept[k].first, b = kept[k].second;
        while (a > gapA && env.db[a - 1] > before) --a;
        for (u32 f = 0; f < 15 && a > gapA && env.zcr[a - 1] > 0.3f && env.db[a - 1] > before - sens + 3.0f; ++f) --a;
        while (b < gapB && env.db[b] > after) ++b;
        for (u32 f = 0; f < 15 && b < gapB && env.zcr[b] > 0.3f && env.db[b] > after - sens + 3.0f; ++f) ++b;
        kept[k] = {a, b};
    }
    out.voiced.assign(n, 0);
    const f32 voicedFloor = std::max(out.noiseDb + 12.0f, out.levelDb - 40.0f);
    for (usize i = 0; i < n; ++i) out.voiced[i] = env.zcr[i] < 0.1f && env.db[i] > voicedFloor ? 1 : 0;
    // Respiração (0,12–1,0 s, média < nível − 10 dB, zcr > 0,15, ≤ 20 % vozeado):
    // uma região assim não é fala, mesmo acima do limiar.
    auto breath_like = [&](usize a, usize b) {
        const usize len = b - a;
        if (len < 12 || len > 100) return false;
        f64 zsum = 0.0, power = 0.0;
        u32 voiced = 0;
        for (usize i = a; i < b; ++i) { zsum += env.zcr[i]; power += std::pow(10.0, env.db[i] / 10.0); voiced += out.voiced[i]; }
        const f64 meanDb = 10.0 * std::log10(power / len + 1e-18);
        return zsum / len > 0.15 && meanDb < out.levelDb - 10.0 && voiced * 5 <= len;
    };
    out.active.assign(n, 0);
    for (const auto& r : kept) {
        if (breath_like(r.first, r.second)) { out.breaths.push_back({sec(r.first), sec(r.second)}); continue; }
        out.regions.push_back({sec(r.first), sec(r.second)});
        std::fill(out.active.begin() + static_cast<std::ptrdiff_t>(r.first), out.active.begin() + static_cast<std::ptrdiff_t>(r.second), u8{1});
    }
    // Respiração: fora da fala, piso + 8 < energia < nível − 10, 0,12–1,0 s,
    // zcr médio > 0,15 e ≤ 20 % de quadros vozeados.
    for (usize i = 0; i < n;) {
        const bool cand = !out.active[i] && env.db[i] > out.noiseDb + 8.0f && env.db[i] < out.levelDb - 10.0f;
        if (!cand) { ++i; continue; }
        usize j = i;
        f64 zsum = 0.0, dsum = 0.0;
        u32 voiced = 0;
        while (j < n && !out.active[j] && env.db[j] > out.noiseDb + 8.0f && env.db[j] < out.levelDb - 10.0f) {
            zsum += env.zcr[j]; dsum += env.db[j]; voiced += out.voiced[j]; ++j;
        }
        const usize len = j - i;
        bool known = false;
        for (const auto& b : out.breaths) known = known || (sec(i) < b.end && sec(j) > b.start);
        if (!known && len >= 12 && len <= 100 && zsum / len > 0.15 && dsum / len < out.levelDb - 10.0f && voiced * 5 <= len)
            out.breaths.push_back({sec(i), sec(j)});
        i = j;
    }
    std::sort(out.breaths.begin(), out.breaths.end(), [](const Range& a, const Range& b) { return a.start < b.start; });
    return out;
}

std::vector<Word> align_words(const std::vector<Word>& words, const Envelope& env) {
    std::vector<Word> out;
    const usize n = env.size();
    const auto loud = audible(env.db);
    const f32 p10 = percentile(loud, 10.0f), p95 = percentile(loud, 95.0f);
    const f32 range = p95 - p10;
    const bool dynamic = n > 0 && range >= 10.0f;
    const f32 thr = p10 + 0.3f * range;
    const f32 ghost = std::min(p10 + 0.3f * range, p95 - 6.0f);
    auto frame_of = [&](f64 t) { return static_cast<i64>(std::floor(t / kFrameSec)); };
    auto db_at = [&](i64 f) { return f >= 0 && static_cast<usize>(f) < n ? env.db[static_cast<usize>(f)] : -200.0f; };
    f64 prevEnd = 0.0;
    for (usize w = 0; w < words.size(); ++w) {
        Word word = words[w];
        if (!std::isfinite(word.start) || !std::isfinite(word.end) || word.end < word.start) continue;
        const f64 nextStart = w + 1 < words.size() ? words[w + 1].start : 1e18;
        // Fantasma: nenhum quadro acima do limiar.
        if (dynamic) {
            bool heard = false;
            for (i64 f = frame_of(word.start); f <= frame_of(word.end) && !heard; ++f) heard = db_at(f) > ghost;
            if (!heard) continue;
        }
        u32 letters = 0;
        for (unsigned char c : word.text) if (std::isalnum(c) || c >= 0xC0) ++letters;
        const f64 maxDur = std::min(2.0, std::max(0.6, 0.12 * letters));
        if (dynamic) {
            // Início: o ataque em −0,25..+0,15 s, menos 20 ms.
            for (i64 f = frame_of(word.start - 0.25); f <= frame_of(word.start + 0.15); ++f) {
                if (db_at(f) >= thr) { word.start = std::max(0.0, sec(static_cast<usize>(std::max<i64>(f, 0))) - 0.02); break; }
            }
            // Fim: até a voz cair por 80 ms, mais 30 ms.
            i64 f = frame_of(word.start + 0.02);
            const i64 limit = frame_of(std::min(nextStart, word.start + maxDur));
            u32 quiet = 0;
            i64 quietStart = -1;
            for (; f <= limit; ++f) {
                if (db_at(f) < thr) { if (quiet++ == 0) quietStart = f; if (quiet >= 8) break; }
                else quiet = 0;
            }
            if (quiet >= 8 && quietStart >= 0) word.end = sec(static_cast<usize>(quietStart)) + 0.03;
            else word.end = std::max(word.end, sec(static_cast<usize>(std::max<i64>(limit, 0))));
        }
        word.start = std::max(word.start, prevEnd);
        word.end = std::min(word.end, word.start + maxDur);
        if (nextStart > word.start + 0.08) word.end = std::min(word.end, nextStart);
        word.end = std::max(word.end, word.start + 0.08);
        prevEnd = word.end;
        out.push_back(std::move(word));
    }
    return out;
}

// -----------------------------------------------------------------------------
// §7 Voz
// -----------------------------------------------------------------------------
namespace {
struct Bq {
    f64 b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    f64 run(f64 x) noexcept {
        const f64 y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
} // namespace

f64 integrated_lufs(const f32* mono, usize count, u32 rate) {
    if (!mono || count == 0 || rate < 8000) return -70.0;
    const f64 fs = static_cast<f64>(rate);
    constexpr f64 kPi = 3.14159265358979323846;
    // K-weighting (BS.1770) para qualquer taxa: prateleira alta + passa-alta RLB.
    Bq shelf{}, rlb{};
    {
        const f64 f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
        const f64 k = std::tan(kPi * f0 / fs), vh = std::pow(10.0, g / 20.0), vb = std::pow(vh, 0.4996667741545416);
        const f64 a0 = 1.0 + k / q + k * k;
        shelf = Bq{(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                   2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    {
        const f64 f0 = 38.13547087602444, q = 0.5003270373238773;
        const f64 k = std::tan(kPi * f0 / fs);
        const f64 a0 = 1.0 + k / q + k * k;
        rlb = Bq{1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }
    const usize step = rate / 10, block = step * 4;
    if (count < block) return -70.0;
    std::vector<f64> sq(count / step + 1, 0.0);
    for (usize i = 0; i < count; ++i) {
        const f64 y = rlb.run(shelf.run(mono[i]));
        sq[i / step] += y * y;
    }
    std::vector<f64> z;
    for (usize b = 0; (b + 4) * step <= count; ++b)
        z.push_back((sq[b] + sq[b + 1] + sq[b + 2] + sq[b + 3]) / static_cast<f64>(block));
    auto loudness = [](f64 ms) { return -0.691 + 10.0 * std::log10(ms + 1e-30); };
    f64 sum = 0.0; usize cnt = 0;
    for (f64 v : z) if (loudness(v) > -70.0) { sum += v; ++cnt; }
    if (!cnt) return -70.0;
    const f64 relative = loudness(sum / cnt) - 10.0;
    sum = 0.0; cnt = 0;
    for (f64 v : z) if (loudness(v) > -70.0 && loudness(v) > relative) { sum += v; ++cnt; }
    return cnt ? loudness(sum / cnt) : -70.0;
}

f32 voice_gain_db(f64 lufs) noexcept {
    if (!std::isfinite(lufs)) return 0.0f;
    const f64 g = std::clamp(-12.5 - lufs, -6.0, 15.0);
    return static_cast<f32>(std::round(g * 2.0) / 2.0);
}

f32 compressor_threshold_db(f32 p95, f32 gain, bool measured) noexcept {
    return measured && std::isfinite(p95) ? p95 + gain - 8.0f : -26.0f;
}

std::vector<f32> phrase_gains(const std::vector<f32>& levels) {
    std::vector<f32> out(levels.size(), 0.0f);
    if (levels.empty()) return out;
    const f32 target = percentile(levels, 50.0f);
    for (usize i = 0; i < levels.size(); ++i) {
        const f32 g = std::clamp(target - levels[i], -6.0f, 6.0f);
        out[i] = std::fabs(g) >= 1.0f ? g : 0.0f;
    }
    return out;
}

namespace {
/// Envoltória somada de rampas trapezoidais (dB).
struct Trapezoid { f64 a, b, ramp; f32 db; };
f32 trapezoid_at(const Trapezoid& z, f64 t) noexcept {
    if (t <= z.a - z.ramp || t >= z.b + z.ramp) return 0.0f;
    if (t < z.a) return z.ramp > 0 ? static_cast<f32>(z.db * (t - (z.a - z.ramp)) / z.ramp) : 0.0f;
    if (t > z.b) return z.ramp > 0 ? static_cast<f32>(z.db * ((z.b + z.ramp) - t) / z.ramp) : 0.0f;
    return z.db;
}
std::vector<GainKey> sum_keys(const std::vector<Trapezoid>& parts) {
    std::vector<f64> times;
    for (const auto& z : parts) {
        if (z.db == 0.0f || z.b <= z.a) continue;
        times.insert(times.end(), {std::max(0.0, z.a - z.ramp), z.a, z.b, z.b + z.ramp});
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](f64 x, f64 y) { return std::fabs(x - y) < 1e-6; }), times.end());
    std::vector<GainKey> keys;
    for (f64 t : times) {
        f32 db = 0.0f;
        for (const auto& z : parts) db += trapezoid_at(z, t);
        if (!keys.empty() && std::fabs(keys.back().db - db) < 1e-4f && keys.size() >= 2
            && std::fabs(keys[keys.size() - 2].db - db) < 1e-4f) keys.back().time = t;
        else keys.push_back({t, db});
    }
    return keys;
}
} // namespace

std::vector<GainKey> voice_keys(const std::vector<Range>& phrases, const std::vector<f32>& gains,
                                const std::vector<Range>& breaths) {
    std::vector<Trapezoid> parts;
    for (usize i = 0; i < phrases.size() && i < gains.size(); ++i)
        if (gains[i] != 0.0f) parts.push_back({phrases[i].start, phrases[i].end, kBreathRamp, gains[i]});
    for (const auto& b : breaths) {
        if (b.length() < 0.12) continue;
        parts.push_back({b.start + kBreathRamp, b.end - kBreathRamp, kBreathRamp, kBreathDb});
    }
    return sum_keys(parts);
}

Dynamics voice_dynamics(const Envelope& env, const std::vector<u8>& speech, f32 inputDb, f32 thr, f32 ratio, f32 ceiling) {
    Dynamics d;
    const usize n = env.size();
    d.gainDb.assign(n, 0.0f);
    if (!n || ratio < 1.0f) return d;
    // Compressor: redução = excesso × (1 − 1/razão), suavizada (ataque 10 ms, soltura 150 ms).
    const f32 up = 1.0f - std::exp(-1.0f), down = 1.0f - std::exp(-10.0f / 150.0f);
    std::vector<f32> gr(n, 0.0f);
    f32 state = 0.0f;
    for (usize i = 0; i < n; ++i) {
        const f32 over = env.db[i] + inputDb - thr;
        const f32 target = over > 0.0f ? over * (1.0f - 1.0f / ratio) : 0.0f;
        state += (target - state) * (target > state ? up : down);
        gr[i] = state;
    }
    // Make-up automático: a redução mediana nos quadros de fala.
    std::vector<f32> inSpeech;
    for (usize i = 0; i < n; ++i) if (i < speech.size() && speech[i]) inSpeech.push_back(gr[i]);
    d.makeupDb = inSpeech.empty() ? 0.0f : percentile(inSpeech, 50.0f);
    // Limitador: o pico do quadro (e do vizinho, para chegar antes) não passa do teto; soltura 50 ms.
    const f32 release = 1.0f - std::exp(-10.0f / 50.0f);
    f32 lim = 0.0f;
    for (usize i = 0; i < n; ++i) {
        f32 need = 0.0f;
        for (usize k = i; k <= std::min(n - 1, i + 1); ++k) {
            const f32 peak = (env.peakDb.size() > k ? env.peakDb[k] : env.db[k] + 3.0f) + inputDb - gr[k] + d.makeupDb;
            need = std::max(need, peak - ceiling);
        }
        lim = need > lim ? need : lim + (need - lim) * release;
        d.gainDb[i] = -gr[i] + d.makeupDb - lim;
        d.maxReductionDb = std::max(d.maxReductionDb, gr[i] + lim);
    }
    return d;
}

std::vector<GainKey> simplify_gain(const std::vector<f32>& v, f64 frameSec, f32 tol) {
    std::vector<GainKey> keys;
    const usize n = v.size();
    if (!n) return keys;
    std::vector<u8> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    std::vector<std::pair<usize, usize>> stack{{0, n - 1}};
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1) continue;
        f32 worst = 0.0f;
        usize at = a;
        for (usize i = a + 1; i < b; ++i) {
            const f32 line = v[a] + (v[b] - v[a]) * static_cast<f32>(i - a) / static_cast<f32>(b - a);
            const f32 err = std::fabs(v[i] - line);
            if (err > worst) { worst = err; at = i; }
        }
        if (worst > tol) { keep[at] = 1; stack.push_back({a, at}); stack.push_back({at, b}); }
    }
    for (usize i = 0; i < n; ++i) if (keep[i]) keys.push_back({static_cast<f64>(i) * frameSec, v[i]});
    return keys;
}

// -----------------------------------------------------------------------------
// §6 Música e ducking
// -----------------------------------------------------------------------------
f32 music_target_bpm(f64 wps) noexcept {
    return static_cast<f32>(std::clamp(85.0 + (wps - 2.0) * 15.0, 80.0, 120.0));
}

f64 music_score(const MusicTrack& t, f64 wps, f64 videoDuration) noexcept {
    f64 score = 0.0;
    if (t.bpm > 0.0f) score -= std::fabs(static_cast<f64>(t.bpm) - music_target_bpm(wps)) / 8.0;
    else if (t.music) score += 1.0;   // sem BPM: a categoria de música vem antes
    if (t.duration >= videoDuration + 2.0) score += 3.0;
    if (t.recent) score -= 2.0;
    if (t.energyDb < -30.0f) score -= 1.5;
    return score;
}

i32 choose_music(const std::vector<MusicTrack>& tracks, f64 wps, f64 videoDuration) {
    i32 best = -1;
    f64 bestScore = -1e30;
    for (usize i = 0; i < tracks.size(); ++i) {
        if (!(tracks[i].duration > 1.0)) continue;
        const f64 s = music_score(tracks[i], wps, videoDuration);
        if (s > bestScore + 1e-9) { bestScore = s; best = static_cast<i32>(i); }
    }
    return best;
}

f64 music_entry(const Envelope& env) {
    const auto loud = audible(env.db);
    if (loud.empty()) return 0.0;
    // Energia média (em potência) do arquivo.
    f64 power = 0.0;
    for (f32 d : loud) power += std::pow(10.0, d / 10.0);
    const f32 energy = static_cast<f32>(10.0 * std::log10(power / loud.size() + 1e-18));
    u32 run = 0;
    for (usize i = 0; i < env.size(); ++i) {
        if (env.db[i] >= energy - 6.0f) { if (++run >= 100) return sec(i + 1 - 100); }
        else run = 0;
    }
    return 0.0;
}

std::vector<GainKey> ducking_keys(const std::vector<Range>& speech, f64 duration, f32 reduction) {
    std::vector<Range> merged;
    for (const auto& r : speech) {
        if (r.end <= r.start) continue;
        if (!merged.empty() && r.start - merged.back().end < 1.15) merged.back().end = std::max(merged.back().end, r.end);
        else merged.push_back(r);
    }
    std::vector<GainKey> keys;
    constexpr f64 kAttack = 0.25, kRelease = 0.6;
    for (const auto& r : merged) {
        const f64 a = std::max(0.0, r.start - kAttack), b = std::min(duration, r.end + kRelease);
        if (!keys.empty() && keys.back().time >= a) {
            // A soltura da anterior encontra o ataque desta: fica abaixada.
            keys.pop_back();
        } else {
            keys.push_back({a, 0.0f});
        }
        keys.push_back({std::max(a, r.start), -reduction});
        keys.push_back({std::max(r.start, std::min(r.end, duration)), -reduction});
        keys.push_back({b, 0.0f});
    }
    return keys;
}

f32 gain_at(const std::vector<GainKey>& keys, f64 t) noexcept {
    if (keys.empty()) return 0.0f;
    if (t <= keys.front().time) return keys.front().db;
    if (t >= keys.back().time) return keys.back().db;
    for (usize i = 0; i + 1 < keys.size(); ++i) {
        if (t < keys[i + 1].time) {
            const f64 span = keys[i + 1].time - keys[i].time;
            const f64 u = span > 0 ? (t - keys[i].time) / span : 1.0;
            return static_cast<f32>(keys[i].db + (keys[i + 1].db - keys[i].db) * u);
        }
    }
    return keys.back().db;
}

f32 duck_reduction_for_peak(const std::vector<f32>& voice, f32 voiceGainDb, const std::vector<f32>& music,
                            const std::vector<Range>& speech, f64 duration, f32 reduction) {
    const f32 vg = std::pow(10.0f, voiceGainDb / 20.0f);
    for (int pass = 0; pass < 3; ++pass) {
        const auto keys = ducking_keys(speech, duration, reduction);
        f32 peak = 0.0f;
        const usize n = std::max(voice.size(), music.size());
        usize k = 0;
        for (usize i = 0; i < n; ++i) {
            const f64 t = static_cast<f64>(i) / kAnalysisRate;
            while (k + 1 < keys.size() && keys[k + 1].time <= t) ++k;
            f32 duck = 0.0f;
            if (!keys.empty() && t >= keys.front().time) {
                if (k + 1 < keys.size()) {
                    const f64 span = keys[k + 1].time - keys[k].time;
                    duck = static_cast<f32>(keys[k].db + (keys[k + 1].db - keys[k].db) * (span > 0 ? (t - keys[k].time) / span : 1.0));
                } else duck = keys.back().db;
            }
            const f32 mg = std::pow(10.0f, (kMusicBaseDb + duck) / 20.0f);
            const f32 v = (i < voice.size() ? voice[i] * vg : 0.0f) + (i < music.size() ? music[i] * mg : 0.0f);
            peak = std::max(peak, std::fabs(v));
        }
        const f32 peakDb = 20.0f * std::log10(peak + 1e-12f);
        if (peakDb <= -1.5f) break;
        reduction += std::ceil(peakDb + 2.5f);
    }
    return reduction;
}

// -----------------------------------------------------------------------------
// §8 Cor
// -----------------------------------------------------------------------------
FrameColor frame_color_stats(const u8* rgba, u32 w, u32 h, u32 stride) {
    FrameColor c;
    if (!rgba || !w || !h) return c;
    u32 hist[256]{};
    f64 sat = 0.0;
    u64 count = 0;
    for (u32 y = 0; y < h; ++y) {
        const u8* row = rgba + static_cast<usize>(y) * stride;
        for (u32 x = 0; x < w; ++x) {
            const u8 r = row[x * 4], g = row[x * 4 + 1], b = row[x * 4 + 2];
            const u32 luma = (2126u * r + 7152u * g + 722u * b + 5000u) / 10000u;
            ++hist[std::min(255u, luma)];
            const u8 mx = std::max({r, g, b}), mn = std::min({r, g, b});
            sat += mx ? static_cast<f64>(mx - mn) / mx : 0.0;
            ++count;
        }
    }
    auto pct = [&](f64 p) {
        const u64 target = static_cast<u64>(std::ceil(p * static_cast<f64>(count)));
        u64 acc = 0;
        for (u32 i = 0; i < 256; ++i) { acc += hist[i]; if (acc >= std::max<u64>(1, target)) return static_cast<f32>(i) / 255.0f; }
        return 1.0f;
    };
    c.p2 = pct(0.02);
    c.p98 = pct(0.98);
    c.saturation = static_cast<f32>(sat / static_cast<f64>(count));
    return c;
}

bool already_graded(const std::vector<FrameColor>& frames) noexcept {
    if (frames.empty()) return false;
    f32 p2 = 0, p98 = 0, sat = 0;
    for (const auto& f : frames) { p2 += f.p2; p98 += f.p98; sat += f.saturation; }
    const f32 n = static_cast<f32>(frames.size());
    p2 /= n; p98 /= n; sat /= n;
    return p2 <= 0.08f && (sat >= 0.42f || p98 - p2 >= 0.6f);
}

} // namespace aurea::autoedit
