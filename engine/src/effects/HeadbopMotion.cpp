// =============================================================================
//  Aurea / effects / HeadbopMotion.cpp — ver o cabeçalho.
//
//  Dinâmica (unidades adimensionais: τ em ACENOS desde o evento; os polos estão
//  em rad/aceno, então a forma não depende do tempo da música):
//
//    quadro      F(s) = ω_F² / (s² + 2ζ_F ω_F s + ω_F²),  ω_d = π  (meio período =
//                1 aceno → o oscilador cruza zero exatamente no aceno seguinte, e a
//                soma periódica de todos os acenos tem a MESMA forma de h(τ) em
//                [0,1): desce em atan(√(1-ζ²)/ζ)/π = attack, sobe no resto).
//    tronco      F·S_T          (mola rápida: segue o quadro com ~0.02 aceno)
//    cabeça      F·S_T·S_H      (mola com atraso ~0.13 aceno ≈ 2 quadros a 30 fps)
//    extremid.   F·S_T·S_E      (atraso ~0.12 aceno depois do tronco)
//    cabelo      F·S_T·S_H·S_C  (0.5.2: mola ~3,5 Hz, ζ 0,25 — passa do ponto e
//                balança; só entra na cadeia com `hair` > 0)
//    roupa       F·S_T·S_R      (0.5.2: mola ~2,5 Hz, ζ 0,35; só com `cloth` > 0)
//    Cabelo/roupa também respondem ao quadro sozinho (F·S_C, F·S_R: o atraso ao
//    solavanco) e ao balanço senoidal pelo ganho/fase da mola na frequência dele.
//
//  Cada cadeia é uma função de transferência racional com polos simples; a
//  resposta ao impulso é Σ_i R_i e^{p_i τ}, com R_i = ganho / Π_{j≠i}(p_i − p_j)
//  (resíduos). Cada batida é um impulso; a pose em t é a soma fechada das
//  respostas das batidas dentro da janela (exp(−decaimento·W) < 1e-4).
//  A normalização (pico do regime periódico = 1) usa a soma geométrica
//  Σ_m e^{p(τ+m)} = e^{pτ}/(1−e^{p}).
// =============================================================================
#include "aurea/effects/HeadbopMotion.hpp"
#include "aurea/effects/ShakeMotion.hpp"
#include "aurea/audio/Beats.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace aurea::headbop {

namespace {

constexpr double kPi = 3.14159265358979323846;
using cd = std::complex<double>;

// --- Tabelas de preset -------------------------------------------------------
// Bop afinado no vídeo de referência (576x1024, 30 fps, 117.9 BPM, casamento de
// modelo por quadro): quadro desce 5.5% e vai 2.4% para a direita (diagonal,
// dx ≈ 0.44·dy) em ~4 quadros (0.27 batida) e volta em ~10; a cabeça inclina
// 15°/8°/13°/13° num sentido só, 2–3 quadros depois do fundo do quadro, e volta a
// ~0–2°; o tronco balança ±2.3% com período ~1.6 batida e roll ±1°.
//   Jolt   TorJ   TorSw  SwPer TorRo HPitch HRoll HCnt  HYaw  Cycle Attack Zeta  HLag  ELag  JAng   HDrop  HRollSw
constexpr float kTable[static_cast<int>(Preset::Count)][ChCount] = {
    /* Bop    */ {0.055f, 0.025f, 0.023f, 1.6f, 1.0f, 5.0f, 14.0f, 0.85f, 0.0f, 1.0f, 0.27f, 0.70f, 0.15f, 0.12f, 23.6f, 0.020f, 0.0f},
    /* Nod    */ {0.000f, 0.000f, 0.000f, 1.6f, 0.0f, 6.0f, 14.0f, 0.00f, 0.0f, 1.0f, 0.27f, 0.70f, 0.15f, 0.12f, 0.0f, 0.020f, 0.0f},
    /* Sway   */ {0.000f, 0.000f, 0.050f, 2.0f, 2.5f, 0.0f, 0.0f, 0.85f, 0.0f, 1.0f, 0.27f, 0.70f, 0.15f, 0.12f, 0.0f, 0.000f, 5.0f},
    /* Bounce */ {0.055f, 0.025f, 0.000f, 1.6f, 0.0f, 0.0f, 0.0f, 0.00f, 0.0f, 1.0f, 0.27f, 0.70f, 0.15f, 0.12f, 23.6f, 0.000f, 0.0f},
    /* Groove */ {0.040f, 0.020f, 0.035f, 2.0f, 2.0f, 6.0f, 10.0f, 0.60f, 14.0f, 2.0f, 0.30f, 0.70f, 0.15f, 0.12f, 15.0f, 0.015f, 0.0f},
};

/// Mola do tronco: rápida, fixa (rad/aceno), dividida pelo `delay`.
constexpr double kTorsoOmega = 80.0;
/// ω da mola seguidora para um atraso de pico `lag` (acenos), ajustado com ζ=0.7
/// sobre o quadro ζ=0.5: lag 0.13 → ω≈12.5; 0.12 → ω≈13.4.
constexpr double kLagToOmega = 1.62;
/// Resposta abaixo disto (relativa ao pico 1) é ignorada.
constexpr double kEps = 1e-4;
constexpr int kMaxSlots = 12;  // quadro, tronco, cabeça, extremidades, cabelo, roupa: 2 polos cada

float finite_or(float v, float def) noexcept { return std::isfinite(v) ? v : def; }
double finite_or(double v, double def) noexcept { return std::isfinite(v) ? v : def; }

// --- Cadeias de molas ----------------------------------------------------------

struct Chains {
    cd p[kMaxSlots];
    bool conjOfPrev[kMaxSlots] = {};   // p[i] == conj(p[i-1]) (par complexo)
    // Resíduos por cadeia, indexados por slot (0 = slot fora da cadeia).
    cd rF[kMaxSlots], rT[kMaxSlots], rH[kMaxSlots], rE[kMaxSlots];
    // 0.5.2: cabelo (sobre a cabeça e sobre o quadro) e roupa (sobre o tronco e o quadro).
    cd rHair[kMaxSlots], rFHair[kMaxSlots], rCloth[kMaxSlots], rFCloth[kMaxSlots];
    bool used[kMaxSlots] = {};
    double nF = 1, nT = 1, nH = 1, nE = 1;  // 1/pico do regime periódico
    double window = 8;                       // acenos
    double cutoff[kMaxSlots] = {};           // τ (acenos) a partir do qual o polo some (< 1e-7)
};

struct ChainKey {
    double zF, zS, wT, wH, wE;
    double wHair = 0, zHair = 0, wCloth = 0, zCloth = 0;   // 0 = seção fora (a pose de antes, bit a bit)
    bool operator==(const ChainKey& o) const noexcept {
        return zF == o.zF && zS == o.zS && wT == o.wT && wH == o.wH && wE == o.wE && wHair == o.wHair &&
               zHair == o.zHair && wCloth == o.wCloth && zCloth == o.zCloth;
    }
};

void section_poles(double z, double w, cd& a, cd& b) noexcept {
    if (z < 1.0) {
        const double wd = w * std::sqrt(1.0 - z * z);
        a = cd(-z * w, wd);
        b = cd(-z * w, -wd);
    } else {
        const double s = w * std::sqrt(z * z - 1.0);
        a = cd(-z * w + s, 0.0);
        b = cd(-z * w - s, 0.0);
    }
}

void residues(const Chains& c, const int* slots, int count, double gain, cd* out) noexcept {
    for (int i = 0; i < kMaxSlots; ++i) out[i] = cd(0.0, 0.0);
    for (int a = 0; a < count; ++a) {
        cd den(1.0, 0.0);
        for (int b = 0; b < count; ++b) if (b != a) den *= (c.p[slots[a]] - c.p[slots[b]]);
        out[slots[a]] = gain / den;
    }
}

/// Pico do regime periódico (um impulso por aceno, desde sempre) de uma cadeia.
double steady_peak(const Chains& c, const cd* r) noexcept {
    cd k[kMaxSlots];
    for (int i = 0; i < kMaxSlots; ++i) k[i] = r[i] / (1.0 - std::exp(c.p[i]));
    double best = 0.0;
    constexpr int kN = 600;
    for (int s = 0; s < kN; ++s) {
        const double tau = static_cast<double>(s) / kN;
        cd acc(0.0, 0.0);
        for (int i = 0; i < kMaxSlots; ++i) if (r[i] != cd(0.0, 0.0)) acc += k[i] * std::exp(c.p[i] * tau);
        best = std::max(best, acc.real());
    }
    return best;
}

Chains build_chains(const ChainKey& key) noexcept {
    Chains c;
    // ω do quadro: meio período amortecido = 1 aceno.
    const double wF = kPi / std::sqrt(1.0 - key.zF * key.zF);
    double ws[6] = {wF, key.wT, key.wH, key.wE, key.wHair, key.wCloth};
    double zs[6] = {key.zF, key.zS, key.zS, key.zS, key.zHair, key.zCloth};
    const bool hairOn = key.wHair > 0.0, clothOn = key.wCloth > 0.0;
    const int sections = 4 + (hairOn || clothOn ? 2 : 0);
    // Polos de seções diferentes de uma mesma cadeia não podem coincidir (os
    // resíduos explodiriam); afasta ω em 3% até separar.
    for (int sec = 1; sec < sections; ++sec) {
        if (ws[sec] <= 0.0) continue;
        for (int guard = 0; guard < 16; ++guard) {
            bool close = false;
            for (int prev = 0; prev < sec; ++prev) {
                if (ws[prev] > 0.0 && std::fabs(ws[sec] - ws[prev]) < 0.02 * std::max(ws[sec], ws[prev])) close = true;
            }
            if (!close) break;
            ws[sec] *= 1.03;
        }
    }
    for (int sec = 0; sec < sections; ++sec) {
        if (ws[sec] <= 0.0) continue;
        c.used[2 * sec] = c.used[2 * sec + 1] = true;
        double z = zs[sec];
        if (std::fabs(z - 1.0) < 1e-3) z = 1.0 + 1e-3;  // polo duplo
        section_poles(z, ws[sec], c.p[2 * sec], c.p[2 * sec + 1]);
        c.conjOfPrev[2 * sec + 1] = z < 1.0;
    }
    const double g0 = ws[0] * ws[0], g1 = ws[1] * ws[1], g2 = ws[2] * ws[2], g3 = ws[3] * ws[3];
    const int sF[] = {0, 1}, sT[] = {0, 1, 2, 3}, sH[] = {0, 1, 2, 3, 4, 5}, sE[] = {0, 1, 2, 3, 6, 7};
    residues(c, sF, 2, g0, c.rF);
    residues(c, sT, 4, g0 * g1, c.rT);
    residues(c, sH, 6, g0 * g1 * g2, c.rH);
    residues(c, sE, 6, g0 * g1 * g3, c.rE);
    for (int i = 0; i < kMaxSlots; ++i) c.rHair[i] = c.rFHair[i] = c.rCloth[i] = c.rFCloth[i] = cd(0.0, 0.0);
    if (hairOn) {
        const double g4 = ws[4] * ws[4];
        const int sHa[] = {0, 1, 2, 3, 4, 5, 8, 9}, sFh[] = {0, 1, 8, 9};
        residues(c, sHa, 8, g0 * g1 * g2 * g4, c.rHair);
        residues(c, sFh, 4, g0 * g4, c.rFHair);
    }
    if (clothOn) {
        const double g5 = ws[5] * ws[5];
        const int sCl[] = {0, 1, 2, 3, 10, 11}, sFc[] = {0, 1, 10, 11};
        residues(c, sCl, 6, g0 * g1 * g5, c.rCloth);
        residues(c, sFc, 4, g0 * g5, c.rFCloth);
    }
    auto inv = [](double v) { return v > 1e-9 ? 1.0 / v : 0.0; };
    c.nF = inv(steady_peak(c, c.rF));
    c.nT = inv(steady_peak(c, c.rT));
    c.nH = inv(steady_peak(c, c.rH));
    c.nE = inv(steady_peak(c, c.rE));
    // Janela: o polo mais lento domina a cauda.
    double decay = 1e9, mag = 0.0;
    for (int i = 0; i < kMaxSlots; ++i) if (c.used[i]) decay = std::min(decay, -c.p[i].real());
    const cd* rs[] = {c.rF, c.rT, c.rH, c.rE, c.rHair, c.rFHair, c.rCloth, c.rFCloth};
    const double ns[] = {c.nF, c.nT, c.nH, c.nE, c.nH, c.nF, c.nT, c.nF};
    const int chains = hairOn || clothOn ? 8 : 4;
    for (int ch = 0; ch < chains; ++ch) {
        double m = 0.0;
        for (int i = 0; i < kMaxSlots; ++i) m += std::abs(rs[ch][i]);
        mag = std::max(mag, m * ns[ch]);
    }
    for (int i = 0; i < kMaxSlots; ++i) {
        double m = 0.0;
        for (int chn = 0; chn < chains; ++chn) m = std::max(m, std::abs(rs[chn][i]) * ns[chn]);
        const double di = std::max(-c.p[i].real(), 1e-3);
        c.cutoff[i] = m > 0.0 ? std::max(0.0, std::log(m / 1e-7) / di) : 0.0;
    }
    decay = std::max(decay, 1e-3);
    c.window = std::clamp(std::log(std::max(mag, 1.0) / kEps) / decay, 2.0, 64.0);
    return c;
}

/// Memo por thread da última cadeia (depende só de parâmetros; mesmo resultado
/// com ou sem memo). Animar suavidade/atraso só recalcula quando eles mudam.
const Chains& chains_for(const ChainKey& key) noexcept {
    thread_local ChainKey last{-1, -1, -1, -1, -1, -1, -1, -1, -1};
    thread_local Chains cached;
    if (!(last == key)) {
        cached = build_chains(key);
        last = key;
    }
    return cached;
}

// --- Mapa tempo → índice de batida -------------------------------------------

struct BeatMap {
    bool ok = false;
    bool explicitGrid = false;
    double bpm = 0, phase = 0;
    const double* b = nullptr;
    size_t n = 0;
    double firstP = 0.5, lastP = 0.5;

    /// Índice contínuo de batida (batida k da grade em u = k).
    double u(double t) const noexcept {
        if (!explicitGrid) return (t - phase) * bpm / 60.0;
        if (n == 1) return (t - b[0]) / firstP;
        if (t < b[0]) return (t - b[0]) / firstP;
        if (t >= b[n - 1]) return static_cast<double>(n - 1) + (t - b[n - 1]) / lastP;
        const size_t k = static_cast<size_t>(std::upper_bound(b, b + n, t) - b) - 1;
        const double span = b[k + 1] - b[k];
        return static_cast<double>(k) + (span > 1e-6 ? (t - b[k]) / span : 0.0);
    }
};

BeatMap make_map(const Params& p, const BeatGrid& g) noexcept {
    BeatMap m;
    const double manual = finite_or(p.manualBpm, 0.0);
    if (manual > 0.0) {
        m.ok = true;
        m.bpm = std::clamp(manual, 1.0, 1000.0);
        m.phase = finite_or(g.phaseSec, 0.0);
        return m;
    }
    const double gb = finite_or(g.bpm, 0.0);
    const double fallbackP = gb > 0.0 ? 60.0 / std::clamp(gb, 1.0, 1000.0) : 0.0;
    if (g.beats.empty()) {
        if (gb <= 0.0) return m;
        m.ok = true;
        m.bpm = std::clamp(gb, 1.0, 1000.0);
        m.phase = finite_or(g.phaseSec, 0.0);
        return m;
    }
    m.explicitGrid = true;
    m.b = g.beats.data();
    m.n = g.beats.size();
    if (!std::isfinite(m.b[0]) || !std::isfinite(m.b[m.n - 1])) return m;
    if (m.n == 1) {
        if (fallbackP <= 0.0) return m;
        m.firstP = m.lastP = fallbackP;
    } else {
        const double f = m.b[1] - m.b[0], l = m.b[m.n - 1] - m.b[m.n - 2];
        m.firstP = f > 1e-3 ? f : (fallbackP > 0.0 ? fallbackP : 0.5);
        m.lastP = l > 1e-3 ? l : (fallbackP > 0.0 ? fallbackP : 0.5);
    }
    m.ok = true;
    return m;
}

float smooth01(double x) noexcept {
    const double c = std::clamp(x, 0.0, 1.0);
    return static_cast<float>(c * c * (3.0 - 2.0 * c));
}

} // namespace

// =============================================================================

float preset_value(Preset preset, int channel) {
    const int p = static_cast<int>(preset);
    if (p < 0 || p >= static_cast<int>(Preset::Count) || channel < 0 || channel >= ChCount) return 0.0f;
    return kTable[p][channel];
}

Pose evaluate(const Params& prm, const BeatGrid& grid, double tSeconds) {
    Pose out;
    if (!std::isfinite(tSeconds)) return out;
    const BeatMap map = make_map(prm, grid);
    if (!map.ok) return out;

    const Preset preset = static_cast<int>(prm.preset) < static_cast<int>(Preset::Count) ? prm.preset : Preset::Bop;
    auto tv = [&](int ch) { return static_cast<double>(preset_value(preset, ch)); };
    const double intensity = std::max(0.0f, finite_or(prm.intensity, 1.0f));
    const double head = std::max(0.0f, finite_or(prm.head, 1.0f));
    const double body = std::max(0.0f, finite_or(prm.body, 1.0f));
    const double jolt = std::max(0.0f, finite_or(prm.jolt, 1.0f));
    const double sway = std::max(0.0f, finite_or(prm.sway, 1.0f));
    const double turn = std::clamp(finite_or(prm.turn3d, 0.0f), -4.0f, 4.0f);
    const double delay = std::clamp(finite_or(prm.delay, 1.0f), 0.0f, 8.0f);
    const double smooth = std::clamp(finite_or(prm.smoothness, 1.0f), 0.05f, 8.0f);
    const double var = std::clamp(finite_or(prm.variation, 1.0f), 0.0f, 4.0f);
    const double sub = std::clamp(finite_or(prm.subdivision, 1.0f), 0.25f, 4.0f);
    const double zoomAmt = finite_or(prm.beatZoom, 0.0f);
    const double phaseBeats = std::clamp(finite_or(prm.phaseBeats, 0.0), -1e6, 1e6);
    const double tiltSign = std::clamp(finite_or(prm.tiltSign, 1.0f), -2.0f, 2.0f);
    const double joltAngle = std::clamp(tv(ChJoltAngle) + finite_or(prm.joltAngle, 0.0f), -80.0, 80.0);

    // Dinâmica.
    const double attack = std::clamp(tv(ChAttack), 0.05, 0.45);
    ChainKey key;
    key.zF = std::clamp(std::cos(kPi * attack) * smooth, 0.05, 0.95);
    key.zS = std::clamp(tv(ChZeta) * smooth, 0.1, 3.0);
    const double d = std::max(delay, 1e-3);
    key.wT = kTorsoOmega / d;
    key.wH = kLagToOmega / (std::max(tv(ChHeadLagBeats), 0.01) * d);
    key.wE = kLagToOmega / (std::max(tv(ChExtremityLagBeats), 0.01) * d);
    // Atraso ~0 → seguidoras quase instantâneas (limite de ω para não estourar a janela).
    key.wT = std::min(key.wT, 4000.0);
    key.wH = std::min(key.wH, 4000.0);
    key.wE = std::min(key.wE, 4000.0);
    // 0.5.2: cabelo/roupa — molas em Hz (física de verdade), levadas a rad/aceno
    // pelo tempo do aceno. Fora (0) com hair/cloth = 0: a cadeia é a de antes.
    const double hairAmt = std::clamp(finite_or(prm.hair, 0.0f), 0.0f, 4.0f);
    const double clothAmt = std::clamp(finite_or(prm.cloth, 0.0f), 0.0f, 4.0f);
    const double phys = std::clamp(finite_or(prm.physics, 1.0f), 0.0f, 2.0f);
    double bpmEff = map.bpm;
    if (map.explicitGrid) bpmEff = map.n > 1 && map.b[map.n - 1] > map.b[0] ? 60.0 * static_cast<double>(map.n - 1) / (map.b[map.n - 1] - map.b[0])
                                                                       : 60.0 / std::max(map.firstP, 1e-3);
    const double nodSec = sub * 60.0 / std::clamp(bpmEff, 1.0, 1000.0);
    const double looser = 1.5 - 0.5 * phys;   // física 0 → 1.5 (firme), 1 → 1, 2 → 0.5 (solto)
    if (hairAmt > 0.0) {
        key.wHair = std::clamp(2.0 * kPi * 3.5 * nodSec * looser, 1.0, 400.0);
        key.zHair = std::clamp(0.25 * looser, 0.08, 0.6);
    }
    if (clothAmt > 0.0) {
        key.wCloth = std::clamp(2.0 * kPi * 2.5 * nodSec * looser, 1.0, 400.0);
        key.zCloth = std::clamp(0.35 * looser, 0.1, 0.7);
    }
    const Chains& ch = chains_for(key);

    const double uRaw = map.u(tSeconds);
    if (!std::isfinite(uRaw)) return out;
    const double u = uRaw - phaseBeats;   // índice com fase aplicada
    const double cycle = tv(ChCycleBeats);

    // --- Soma das respostas dos acenos -------------------------------------------
    double F = 0, T = 0, H = 0, E = 0, Halt = 0, Z = 0;
    double Hh = 0, Fh = 0, Cc = 0, Fc = 0, HhAlt = 0;   // 0.5.2: cabelo/roupa (cadeias absolutas)
    const bool secondary = key.wHair > 0.0 || key.wCloth > 0.0;
    const double windowBeats = ch.window * sub;
    const double mHi = std::floor(u / sub + 1e-9);
    const double mLo = std::ceil((u - windowBeats) / sub - 1e-9);
    const double zoomT = 0.06;   // pico do pulso de zoom, em batidas
    const size_t nStrength = grid.strength.size() == map.n ? map.n : 0;
    for (double m = std::max(mLo, mHi - 128.0); m <= mHi; m += 1.0) {
        const double ue = m * sub;                 // em u (com fase)
        const double uAbs = ue + phaseBeats;       // em índice da grade
        // Aceno "na batida" = inteiro no índice com fase (a fase desloca tudo junto).
        const double ueFrac = ue - std::floor(ue);
        const bool onBeat = ueFrac < 1e-6 || ueFrac > 1.0 - 1e-6;
        if (map.explicitGrid) {
            const double lo = onBeat ? -0.5 : 0.0, hi = static_cast<double>(map.n - 1) + (onBeat ? 0.5 : 0.0);
            if (uAbs < lo - 1e-6 || uAbs > hi + 1e-6) continue;
        }
        const double tauBeats = u - ue;
        if (tauBeats < 0.0) continue;
        const double tau = tauBeats / sub;         // em acenos

        // Força do ataque: batida da grade, ou contratempo (subdivisão) mais fraco.
        double st = 1.0;
        if (nStrength) {
            const double last = static_cast<double>(map.n - 1);
            if (onBeat) {
                const size_t k = static_cast<size_t>(std::clamp(std::floor(uAbs + 0.5), 0.0, last));
                st = finite_or(static_cast<double>(grid.strength[k]), 1.0);
            } else {
                const double kf = std::clamp(std::floor(uAbs), 0.0, last), frac = std::clamp(uAbs - kf, 0.0, 1.0);
                const size_t k0 = static_cast<size_t>(kf), k1 = std::min(k0 + 1, map.n - 1);
                const double s0 = finite_or(static_cast<double>(grid.strength[k0]), 1.0);
                const double s1 = finite_or(static_cast<double>(grid.strength[k1]), 1.0);
                st = s0 + (s1 - s0) * frac;
            }
        }
        if (!onBeat) st *= 0.6;
        const auto cell = static_cast<std::int64_t>(m);
        double a = std::sqrt(std::clamp(st, 0.0, 4.0)) * (1.0 + 0.3 * var * shake::random(prm.seed, cell, 0));
        if (cycle >= 2.0 && (cell & 1)) a *= 0.65;   // Groove: tempo fraco do ciclo de 2
        a = std::max(a, 0.0);
        const double aHead = a * std::max(0.0, 1.0 + 0.12 * var * shake::random(prm.seed, cell, 1));

        cd e[kMaxSlots];
        for (int i = 0; i < kMaxSlots; ++i) {
            if (tau > ch.cutoff[i]) e[i] = cd(0.0, 0.0);          // cauda já abaixo de 1e-7
            else if (ch.conjOfPrev[i]) e[i] = std::conj(e[i - 1]);
            else {
                const double mag = std::exp(ch.p[i].real() * tau), ang = ch.p[i].imag() * tau;
                e[i] = ang == 0.0 ? cd(mag, 0.0) : cd(mag * std::cos(ang), mag * std::sin(ang));
            }
        }
        double f = 0, to = 0, h = 0, ex = 0;
        for (int i = 0; i < kMaxSlots; ++i) {
            f += (ch.rF[i] * e[i]).real();
            to += (ch.rT[i] * e[i]).real();
            h += (ch.rH[i] * e[i]).real();
            ex += (ch.rE[i] * e[i]).real();
        }
        F += a * f;
        T += a * to;
        H += aHead * h;
        E += a * ex;
        Halt += ((cell & 1) ? -1.0 : 1.0) * aHead * h;
        if (secondary) {
            double hh = 0, fh = 0, cc = 0, fc = 0;
            for (int i = 0; i < kMaxSlots; ++i) {
                hh += (ch.rHair[i] * e[i]).real();
                fh += (ch.rFHair[i] * e[i]).real();
                cc += (ch.rCloth[i] * e[i]).real();
                fc += (ch.rFCloth[i] * e[i]).real();
            }
            Hh += aHead * hh;
            HhAlt += ((cell & 1) ? -1.0 : 1.0) * aHead * hh;
            Fh += a * fh;
            Cc += a * cc;
            Fc += a * fc;
        }
        if (zoomAmt != 0.0) {
            const double x = tauBeats / zoomT;
            if (x < 40.0) Z += a * x * std::exp(1.0 - x);
        }
    }
    F *= ch.nF; T *= ch.nT; H *= ch.nH; E *= ch.nE; Halt *= ch.nH;
    Hh *= ch.nH; HhAlt *= ch.nH; Fh *= ch.nF; Cc *= ch.nT; Fc *= ch.nF;

    // --- Balanço lateral (contínuo, período fora da grade) -----------------------
    double env = 1.0;
    if (map.explicitGrid) env = smooth01(uRaw + 1.0) * smooth01(static_cast<double>(map.n) - uRaw);
    const double period = std::max(0.25, tv(ChSwayPeriod));
    const double phi0 = kPi * shake::random(prm.seed, 0x5157, 3);
    const double headLagBeats = tv(ChHeadLagBeats) * delay * sub;
    auto swayAt = [&](double uu) {
        const double mod = 1.0 + 0.1 * var * shake::noise(uu / (2.0 * period), prm.seed, 11, 1.0f);
        return std::sin(2.0 * kPi * uu / period + phi0) * mod;
    };
    const double sNow = swayAt(u) * env;
    const double sLag = swayAt(u - headLagBeats) * env;
    // A cabeça se estabiliza rápido (reflexo): contra-desliza com 1/4 do atraso do aceno.
    const double sCounter = swayAt(u - 0.25 * headLagBeats) * env;

    const double I = intensity;
    const double frameDy = I * jolt * tv(ChJolt) * F;
    out.frameDy = static_cast<float>(frameDy);
    out.frameDx = static_cast<float>(frameDy * std::tan(joltAngle * kPi / 180.0));
    out.torsoDy = static_cast<float>(I * body * tv(ChTorsoJolt) * T);
    out.extremityLag = static_cast<float>(I * body * (tv(ChJolt) + tv(ChTorsoJolt)) * E);
    out.torsoDx = static_cast<float>(I * body * sway * tv(ChTorsoSway) * sNow);
    out.torsoRoll = static_cast<float>(I * body * sway * tv(ChTorsoRoll) * sNow);
    const double torsoDxLag = I * body * sway * tv(ChTorsoSway) * sCounter;
    out.headDx = static_cast<float>(-tv(ChHeadCounter) * std::min(head, 1.0) * torsoDxLag);
    out.headPitch = static_cast<float>(I * head * tv(ChHeadPitch) * H);
    out.headDy = static_cast<float>(I * head * tv(ChHeadDrop) * H);
    if (cycle >= 2.0) {
        // Groove: inclinação alterna a cada batida do ciclo de 2.
        out.headRoll = static_cast<float>(I * head * tiltSign * tv(ChHeadRoll) * Halt);
        out.headYaw = static_cast<float>(I * head * tv(ChHeadYaw) * std::sin(kPi * (u - headLagBeats)) * env);
    } else {
        // Um sentido só: a inclinação desce no fundo de cada batida e volta a ~0.
        out.headRoll = static_cast<float>(I * head * (tiltSign * tv(ChHeadRoll) * H - sway * tv(ChHeadRollSway) * sLag));
    }
    out.headYaw += static_cast<float>(I * turn * 25.0 * sLag);
    out.frameZoom = static_cast<float>(I * zoomAmt * Z);

    // --- 0.5.2: movimento secundário (diferenças em relação à cabeça/tronco) -----
    if (secondary) {
        // Resposta de uma mola (ω rad/aceno, ζ) a um seno de ω_s rad/aceno: ganho e atraso.
        auto springSine = [](double w, double z, double ws, double& gain, double& lagNods) {
            const cd G = (w * w) / cd(w * w - ws * ws, 2.0 * z * w * ws);
            gain = std::abs(G);
            lagNods = ws > 1e-9 ? -std::arg(G) / ws : 0.0;
        };
        const double wSway = 2.0 * kPi * sub / period;   // rad/aceno
        const double torsoDxAmp = I * body * sway * tv(ChTorsoSway);
        const double torsoRollAmp = I * body * sway * tv(ChTorsoRoll);
        const double frameDyNow = I * jolt * tv(ChJolt);
        const double tanJ = std::tan(joltAngle * kPi / 180.0);
        if (hairAmt > 0.0) {
            const double dH = Hh - H, dHalt = HhAlt - Halt, dF = Fh - F;
            double g = 1, lag = 0;
            springSine(key.wHair, key.zHair, wSway, g, lag);
            const double lagBeats = lag * sub;
            // A cabeça segue o tronco (menos o contra-deslize): o cabelo segue a cabeça com o atraso da mola.
            const double headSwayDx = static_cast<double>(out.torsoDx) + out.headDx;
            const double hairSwayDx = torsoDxAmp * (1.0 - tv(ChHeadCounter) * std::min(head, 1.0)) * g * swayAt(u - lagBeats) * env;
            const double k = hairAmt;
            out.hairRoll = static_cast<float>(k * I * head * (cycle >= 2.0 ? tiltSign * tv(ChHeadRoll) * dHalt : tiltSign * tv(ChHeadRoll) * dH));
            out.hairPitch = static_cast<float>(k * I * head * tv(ChHeadPitch) * dH);
            // Vertical: a queda da cabeça e o solavanco do quadro (o cabelo fica para cima e depois cai).
            out.hairDy = static_cast<float>(k * (I * head * tv(ChHeadDrop) * dH + 0.35 * frameDyNow * dF));
            out.hairDx = static_cast<float>(k * (0.35 * frameDyNow * dF * tanJ + (hairSwayDx - headSwayDx)));
            // Tremulação: ruído suave determinístico no tempo absoluto (sem histórico).
            out.flutterX = static_cast<float>(hairAmt * shake::noise(tSeconds * 1.7, prm.seed, 21, 1.0f));
            out.flutterY = static_cast<float>(hairAmt * shake::noise(tSeconds * 1.3, prm.seed, 22, 1.0f));
        }
        if (clothAmt > 0.0) {
            const double dT = Cc - T, dF = Fc - F;
            double g = 1, lag = 0;
            springSine(key.wCloth, key.zCloth, wSway, g, lag);
            const double lagBeats = lag * sub;
            const double clothSway = g * swayAt(u - lagBeats) * env - sNow;
            const double k = clothAmt;
            out.clothDx = static_cast<float>(k * (torsoDxAmp * clothSway + 0.5 * frameDyNow * dF * tanJ));
            // Assentamento: a roupa fica para trás no solavanco e quica de volta.
            out.clothDy = static_cast<float>(k * (I * body * tv(ChTorsoJolt) * dT + 0.6 * frameDyNow * dF));
            out.clothRoll = static_cast<float>(k * torsoRollAmp * clothSway);
            // Dobra que corre pela camisa: fase segue o tempo e o balanço; amplitude
            // pequena, maior com o balanço e logo depois do solavanco.
            out.wavePhase = static_cast<float>(std::fmod(2.0 * kPi * (0.9 * tSeconds) + 2.5 * sNow, 2.0 * kPi));
            out.waveAmp = static_cast<float>(k * (0.0015 * I + 0.25 * std::fabs(torsoDxAmp * clothSway) + 0.15 * std::fabs(frameDyNow * dF)));
        }
    }

    // Defesa final: nunca NaN/inf.
    float* f[] = {&out.frameDy, &out.frameZoom, &out.torsoDx, &out.torsoDy, &out.torsoRoll, &out.headDx,
                  &out.headDy, &out.headPitch, &out.headRoll, &out.headYaw, &out.extremityLag, &out.frameDx,
                  &out.hairDx, &out.hairDy, &out.hairRoll, &out.hairPitch, &out.clothDx, &out.clothDy, &out.clothRoll,
                  &out.flutterX, &out.flutterY, &out.wavePhase, &out.waveAmp};
    for (float* v : f) if (!std::isfinite(*v)) *v = 0.0f;
    return out;
}

// =============================================================================
//  Grades
// =============================================================================

BeatGrid grid_from_bpm(double bpm, double phaseSec, double durationSec) {
    BeatGrid g;
    if (!std::isfinite(bpm) || bpm <= 0.0 || !std::isfinite(phaseSec) || !std::isfinite(durationSec)) return g;
    bpm = std::clamp(bpm, 1.0, 1000.0);
    const double P = 60.0 / bpm;
    g.bpm = bpm;
    g.phaseSec = phaseSec;
    if (durationSec <= 0.0) return g;
    const double kLo = std::ceil((0.0 - phaseSec) / P - 1e-9);
    const double kHi = std::floor((durationSec - phaseSec) / P + 1e-9);
    if (kHi < kLo || kHi - kLo > 2e6) return g;
    g.beats.reserve(static_cast<size_t>(kHi - kLo + 1));
    for (double k = kLo; k <= kHi; k += 1.0) g.beats.push_back(phaseSec + k * P);
    g.strength.assign(g.beats.size(), 1.0f);
    return g;
}

BeatGrid grid_from_taps(const std::vector<double>& tapTimes, double durationSec) {
    std::vector<double> taps;
    taps.reserve(tapTimes.size());
    for (double t : tapTimes) if (std::isfinite(t)) taps.push_back(t);
    std::sort(taps.begin(), taps.end());
    // Toques duplos (< 80 ms) contam como um.
    taps.erase(std::unique(taps.begin(), taps.end(), [](double a, double b) { return b - a < 0.08; }), taps.end());
    if (taps.size() < 2) return {};
    std::vector<double> iv;
    for (size_t i = 1; i < taps.size(); ++i) iv.push_back(taps[i] - taps[i - 1]);
    std::vector<double> sorted = iv;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
    const double med = sorted[sorted.size() / 2];
    if (med < 0.2 || med > 3.0) return {};   // 20..300 BPM
    // Índice de batida de cada toque (um toque esquecido pula um índice) e
    // mínimos quadrados t = fase + k·P.
    std::vector<double> k(taps.size());
    for (size_t i = 0; i < taps.size(); ++i) k[i] = std::round((taps[i] - taps[0]) / med);
    double sk = 0, st = 0, skk = 0, skt = 0;
    const double n = static_cast<double>(taps.size());
    for (size_t i = 0; i < taps.size(); ++i) { sk += k[i]; st += taps[i]; skk += k[i] * k[i]; skt += k[i] * taps[i]; }
    const double den = n * skk - sk * sk;
    if (std::fabs(den) < 1e-12) return {};
    const double P = (n * skt - sk * st) / den;
    const double phase = (st - P * sk) / n;
    if (!(P > 0.0)) return {};
    double ph = std::fmod(phase, P);
    if (ph < 0.0) ph += P;
    BeatGrid g = grid_from_bpm(60.0 / P, ph, durationSec);
    return g;
}

BeatGrid analyze_audio(const float* mono48k, size_t count) {
    BeatGrid g;
    if (!mono48k || count == 0) return g;
    audio::BeatAnalysis a = audio::detect_beats_ex(mono48k, count);
    if (a.bpm <= 0.0 || a.beats.empty()) return g;
    g.bpm = a.bpm;
    g.phaseSec = a.phaseSec;
    g.beats = std::move(a.beats);
    g.strength = std::move(a.strength);
    return g;
}

// =============================================================================
//  Cache e blob
// =============================================================================

uint64_t content_hash(const float* mono48k, size_t count) {
    uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&h](uint64_t v) { h ^= v; h *= 0x100000001b3ull; };
    mix(static_cast<uint64_t>(count));
    if (!mono48k) return h;
    for (size_t i = 0; i < count; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &mono48k[i], 4);
        mix(bits);
    }
    return h;
}

AnalysisCache& AnalysisCache::shared() {
    static AnalysisCache cache;
    return cache;
}

bool AnalysisCache::get(uint64_t hash, BeatGrid& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = map_.find(hash);
    if (it == map_.end()) return false;
    out = it->second;
    return true;
}

void AnalysisCache::put(uint64_t hash, const BeatGrid& g) {
    std::lock_guard<std::mutex> lock(mutex_);
    map_[hash] = g;
}

BeatGrid AnalysisCache::get_or_analyze(const float* mono48k, size_t count) {
    const uint64_t h = content_hash(mono48k, count);
    BeatGrid g;
    if (get(h, g)) return g;
    g = analyze_audio(mono48k, count);   // fora do lock: análise é lenta
    put(h, g);
    return g;
}

void AnalysisCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    map_.clear();
}

size_t AnalysisCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.size();
}

namespace {
constexpr char kMagic[4] = {'H', 'B', 'G', '1'};
constexpr uint32_t kBlobVersion = 1;

template <typename T>
void put_raw(std::string& s, const T& v) {
    char b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));
    s.append(b, sizeof(T));
}
template <typename T>
bool get_raw(const std::string& s, size_t& at, T& v) {
    if (s.size() < at || s.size() - at < sizeof(T)) return false;
    std::memcpy(&v, s.data() + at, sizeof(T));
    at += sizeof(T);
    return true;
}
} // namespace

std::string serialize(const BeatGrid& g, uint64_t hash) {
    std::string s;
    const uint32_t n = static_cast<uint32_t>(g.beats.size());
    s.reserve(4 + 4 + 8 + 8 + 8 + 4 + n * 12u);
    s.append(kMagic, 4);
    put_raw(s, kBlobVersion);
    put_raw(s, hash);
    put_raw(s, g.bpm);
    put_raw(s, g.phaseSec);
    put_raw(s, n);
    for (double b : g.beats) put_raw(s, b);
    for (uint32_t i = 0; i < n; ++i) put_raw(s, i < g.strength.size() ? g.strength[i] : 1.0f);
    return s;
}

bool deserialize(const std::string& blob, BeatGrid& out, uint64_t* hash) {
    if (blob.size() < 4 || std::memcmp(blob.data(), kMagic, 4) != 0) return false;
    size_t at = 4;
    uint32_t version = 0, n = 0;
    uint64_t h = 0;
    BeatGrid g;
    if (!get_raw(blob, at, version) || version != kBlobVersion) return false;
    if (!get_raw(blob, at, h) || !get_raw(blob, at, g.bpm) || !get_raw(blob, at, g.phaseSec) || !get_raw(blob, at, n)) return false;
    if (blob.size() - at != static_cast<size_t>(n) * 12u) return false;
    g.beats.resize(n);
    g.strength.resize(n);
    for (uint32_t i = 0; i < n; ++i) if (!get_raw(blob, at, g.beats[i])) return false;
    for (uint32_t i = 0; i < n; ++i) if (!get_raw(blob, at, g.strength[i])) return false;
    if (!std::isfinite(g.bpm) || !std::isfinite(g.phaseSec)) return false;
    out = std::move(g);
    if (hash) *hash = h;
    return true;
}

} // namespace aurea::headbop
