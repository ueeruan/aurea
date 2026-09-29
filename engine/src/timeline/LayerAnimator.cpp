// =============================================================================
//  Aurea / timeline / LayerAnimator.cpp
// =============================================================================
#include "aurea/timeline/LayerAnimator.hpp"

#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::layeranim {

namespace {

u32 mix_bits(u32 a, u32 b, u32 c, u32 d) noexcept {
    u32 h = 0x811C9DC5u;
    for (u32 v : {a, b, c, d}) {
        h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
        h *= 0x01000193u;
        h ^= h >> 13;
    }
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

/// −1..1 determinístico por (semente, unidade, canal, passo).
f32 lattice(u32 seed, u32 unit, u32 channel, i64 step) noexcept {
    const u32 h = mix_bits(seed, unit, channel, static_cast<u32>(step) ^ static_cast<u32>(step >> 32));
    return static_cast<f32>(h & 0xFFFFFFu) / 8388607.5f - 1.0f;
}

/// Curva do progresso de cada unidade (0..1 → 0..1).
f32 shape(u8 ease, f32 p) noexcept {
    p = std::clamp(p, 0.0f, 1.0f);
    switch (ease) {
        case 1: { const f32 q = 1.0f - p; return 1.0f - q * q * q; }                       // sai rápido, pousa suave
        case 2: return p < 0.5f ? 4.0f * p * p * p : 1.0f - std::pow(-2.0f * p + 2.0f, 3.0f) * 0.5f;
        case 3: { const f32 c = 1.70158f, q = p - 1.0f; return 1.0f + (c + 1.0f) * q * q * q + c * q * q; }   // passa do ponto
        default: return p;
    }
}

/// Quantas unidades o animador conta e qual é a do glifo.
u32 unit_count(u8 unit, u32 chars, u32 words, u32 lines) noexcept {
    return unit == 2 ? std::max(1u, words) : unit == 3 ? std::max(1u, lines) : std::max(1u, chars);
}
u32 unit_of(u8 unit, const text::GlyphUnits& g) noexcept {
    return unit == 2 ? g.wordIndex : unit == 3 ? g.lineIndex : g.charIndex;
}

/// Parâmetros do animador no instante (a unidade não muda nenhum deles).
struct Frame {
    f32 strength, delay;
    f32 op, px, py, sx, sy, rz, rx, ry, trk;
    f32 wx, wy, ws, wr, wspeed, whold;
};

Frame frame_of(const TrackSet& tracks, u32 ai, const LayerAnimator& a, f64 local) noexcept {
    auto P = [&](u32 p) { return clamp_param(p, param_at(tracks, ai, p, local, param_static(a, p))); };
    Frame f{};
    f.strength = P(kStrength) / 100.0f;
    f.delay = P(kDelay);
    f.op = P(kFromOpacity); f.px = P(kFromPosX); f.py = P(kFromPosY);
    f.sx = P(kFromScale); f.sy = a.scaleSeparated ? P(kFromScaleY) : f.sx;
    f.rz = P(kFromRotation); f.rx = P(kFromRotX); f.ry = P(kFromRotY); f.trk = P(kFromTracking);
    f.wx = P(kWigglePosX); f.wy = P(kWigglePosY); f.ws = P(kWiggleScale); f.wr = P(kWiggleRotation);
    f.wspeed = P(kWiggleSpeed); f.whold = P(kWiggleHold) / 100.0f;
    return f;
}

/// Soma de um animador numa unidade (índice já na ordem de entrada).
struct Accum {
    f32 op = 0, px = 0, py = 0, sx = 0, sy = 0, rz = 0, rx = 0, ry = 0, trk = 0;
};

/// Loop (vai e volta): o relógio da unidade dobra dentro do trecho dos
/// keyframes do progresso. Antes do primeiro keyframe fica como está.
f64 loop_time(const TrackSet& tracks, u32 ai, f64 t) noexcept {
    const Track* tr = tracks.find(TrackProperty::LayerAnimParam, ai, kProgress);
    if (!tr || tr->keys.size() < 2 || !std::isfinite(t)) return t;
    const f64 a0 = static_cast<f64>(tr->keys.front().time.value);
    const f64 span = static_cast<f64>(tr->keys.back().time.value) - a0;
    if (span <= 0.0 || t <= a0) return t;
    const f64 m = std::fmod(t - a0, 2.0 * span);
    return a0 + (m <= span ? m : 2.0 * span - m);
}

void add_unit(const TrackSet& tracks, u32 ai, const LayerAnimator& a, const Frame& f, f64 local, f64 fps, u32 order,
              u32 noiseUnit, Accum& s) noexcept {
    const f64 delayFrames = static_cast<f64>(f.delay) * 0.001 * fps;
    f64 t = local - static_cast<f64>(order) * delayFrames;
    if (a.loop) t = loop_time(tracks, ai, t);
    const f32 p = clamp_param(kProgress, param_at(tracks, ai, kProgress, t, a.progress)) / 100.0f;
    const f32 w = f.strength * (1.0f - shape(a.ease, p));
    if (w != 0.0f) {
        s.op += (f.op - 100.0f) * w;
        s.px += f.px * w; s.py += f.py * w;
        s.sx += (f.sx - 100.0f) * w; s.sy += (f.sy - 100.0f) * w;
        s.rz += f.rz * w; s.rx += f.rx * w; s.ry += f.ry * w;
        s.trk += f.trk * w;
    }
    if ((f.wx != 0.0f || f.wy != 0.0f || f.ws != 0.0f || f.wr != 0.0f) && f.wspeed > 0.0f) {
        const f64 wt = local / (fps > 0.0 ? fps : 30.0) * static_cast<f64>(f.wspeed);
        if (f.wx != 0.0f) s.px += f.wx * wiggle_noise(a.wiggleSeed, noiseUnit, 0, wt, f.whold);
        if (f.wy != 0.0f) s.py += f.wy * wiggle_noise(a.wiggleSeed, noiseUnit, 1, wt, f.whold);
        if (f.ws != 0.0f) {
            const f32 n = f.ws * wiggle_noise(a.wiggleSeed, noiseUnit, 2, wt, f.whold);
            s.sx += n; s.sy += n;
        }
        if (f.wr != 0.0f) s.rz += f.wr * wiggle_noise(a.wiggleSeed, noiseUnit, 3, wt, f.whold);
    }
}

} // namespace

f32* param_ref(LayerAnimator& a, u32 p) noexcept {
    switch (p) {
        case kProgress: return &a.progress;
        case kStrength: return &a.strength;
        case kDelay: return &a.delayMs;
        case kFromOpacity: return &a.fromOpacity;
        case kFromPosX: return &a.fromPosX;
        case kFromPosY: return &a.fromPosY;
        case kFromScale: return &a.fromScale;
        case kFromScaleY: return &a.fromScaleY;
        case kFromRotation: return &a.fromRotation;
        case kFromRotX: return &a.fromRotX;
        case kFromRotY: return &a.fromRotY;
        case kFromTracking: return &a.fromTracking;
        case kWigglePosX: return &a.wigglePosX;
        case kWigglePosY: return &a.wigglePosY;
        case kWiggleScale: return &a.wiggleScale;
        case kWiggleRotation: return &a.wiggleRotation;
        case kWiggleSpeed: return &a.wiggleSpeed;
        case kWiggleHold: return &a.wiggleHold;
        default: return nullptr;
    }
}

f32 param_static(const LayerAnimator& a, u32 p) noexcept {
    const f32* r = param_ref(const_cast<LayerAnimator&>(a), p);
    return r ? *r : 0.0f;
}

f32 clamp_param(u32 p, f32 v) noexcept {
    if (!std::isfinite(v)) v = 0.0f;
    switch (p) {
        case kProgress: case kStrength: case kFromOpacity: case kWiggleHold: return std::clamp(v, 0.0f, 100.0f);
        case kDelay: return std::clamp(v, 0.0f, 10000.0f);
        case kFromScale: case kFromScaleY: return std::clamp(v, 0.0f, 5000.0f);
        case kWiggleSpeed: return std::clamp(v, 0.0f, 60.0f);
        case kWiggleScale: return std::clamp(v, 0.0f, 1000.0f);
        case kFromRotation: case kFromRotX: case kFromRotY: case kWiggleRotation: return std::clamp(v, -3600.0f, 3600.0f);
        default: return std::clamp(v, -20000.0f, 20000.0f);
    }
}

f32 param_at(const TrackSet& tracks, u32 animator, u32 param, f64 local, f32 fallback) noexcept {
    const Track* tr = tracks.find(TrackProperty::LayerAnimParam, animator, param);
    if (!tr || !tr->driven() || !std::isfinite(local)) return fallback;
    const f64 f = std::floor(local);
    const f32 a = tr->value_or(FrameIndex{static_cast<i64>(f)}, fallback);
    const f32 k = static_cast<f32>(local - f);
    if (k <= 0.0f) return a;
    return a + (tr->value_or(FrameIndex{static_cast<i64>(f) + 1}, fallback) - a) * k;
}

f32 wiggle_noise(u32 seed, u32 unit, u32 channel, f64 t, f32 hold) noexcept {
    if (!std::isfinite(t)) return 0.0f;
    const f64 k = std::floor(t);
    const i64 step = static_cast<i64>(k);
    const f32 a = lattice(seed, unit, channel, step);
    const f32 b = lattice(seed, unit, channel, step + 1);
    const f32 h = std::clamp(hold, 0.0f, 0.99f);
    const f32 x = static_cast<f32>(t - k);
    if (x <= h) return a;   // parado no começo de cada passo
    const f32 u = (x - h) / (1.0f - h);
    return a + (b - a) * (u * u * (3.0f - 2.0f * u));
}

// Texto 3D (Model3D com letras separadas) também tem unidades: letra, palavra
// e linha viram os nós da malha (scene3d::apply_text3d_animators).
bool acts_on_whole(const Layer& l, const LayerAnimator& a) noexcept {
    return a.enabled && ((l.kind != LayerKind::Text && l.kind != LayerKind::Model3D) || a.unit == 0);
}

bool has_whole(const Layer& l) noexcept {
    for (const LayerAnimator& a : l.layerAnimators) if (acts_on_whole(l, a)) return true;
    return false;
}

bool has_units(const Layer& l) noexcept {
    if (l.kind != LayerKind::Text && l.kind != LayerKind::Model3D) return false;
    for (const LayerAnimator& a : l.layerAnimators) if (a.enabled && a.unit != 0) return true;
    return false;
}

Offset whole_offset(const Layer& l, f64 local, f64 fps) noexcept {
    Offset o;
    if (l.layerAnimators.empty()) return o;
    Accum s;
    bool any = false;
    for (u32 ai = 0; ai < l.layerAnimators.size(); ++ai) {
        const LayerAnimator& a = l.layerAnimators[ai];
        if (!acts_on_whole(l, a)) continue;
        any = true;
        add_unit(l.tracks, ai, a, frame_of(l.tracks, ai, a, local), local, fps, 0, 0, s);
    }
    if (!any) return o;
    o.translate = Vec3{s.px, s.py, 0.0f};
    o.rotation = Vec3{s.rx, s.ry, s.rz};
    o.scale = Vec2{std::max(0.0f, 1.0f + s.sx / 100.0f), std::max(0.0f, 1.0f + s.sy / 100.0f)};
    o.opacity = std::clamp(1.0f + s.op / 100.0f, 0.0f, 1.0f);
    return o;
}

void apply_to_glyphs(const Layer& l, f64 local, f64 fps, const std::vector<text::GlyphUnits>& units, u32 chars, u32 words,
                     u32 lines, f32 align, std::vector<text::GlyphAnim>& out) {
    if (!has_units(l) || units.empty()) return;
    if (out.size() < units.size()) out.resize(units.size());
    std::vector<Accum> acc(units.size());
    for (u32 ai = 0; ai < l.layerAnimators.size(); ++ai) {
        const LayerAnimator& a = l.layerAnimators[ai];
        if (!a.enabled || a.unit == 0) continue;
        const Frame f = frame_of(l.tracks, ai, a, local);
        const u32 count = unit_count(a.unit, chars, words, lines);
        for (usize g = 0; g < units.size(); ++g) {
            const u32 unit = std::min(unit_of(a.unit, units[g]), count - 1);
            const u32 order = a.exit ? count - 1 - unit : unit;
            add_unit(l.tracks, ai, a, f, local, fps, order, unit, acc[g]);
        }
    }
    std::vector<f32> lineRun(std::max<u32>(1, lines) + 1, 0.0f);
    std::vector<f32> lineSpan(lineRun.size(), 0.0f);   ///< deslocamento da última letra da linha
    std::vector<f32> shift(units.size(), 0.0f);
    bool tracking = false;
    for (usize g = 0; g < units.size(); ++g) {
        const Accum& s = acc[g];
        text::GlyphAnim& o = out[g];
        o.translate = o.translate + Vec3{s.px, s.py, 0.0f};
        o.rotation = o.rotation + Vec3{s.rx, s.ry, s.rz};
        o.scale.x *= std::max(0.0f, 1.0f + s.sx / 100.0f);
        o.scale.y *= std::max(0.0f, 1.0f + s.sy / 100.0f);
        o.opacity *= std::clamp(1.0f + s.op / 100.0f, 0.0f, 1.0f);
        // Tracking: cada glifo empurra os seguintes da mesma linha.
        const u32 li = std::min<u32>(units[g].lineIndex, static_cast<u32>(lineRun.size() - 1));
        shift[g] = lineRun[li];
        lineSpan[li] = shift[g];
        lineRun[li] += s.trk;
        tracking |= s.trk != 0.0f;
    }
    if (!tracking) return;
    // A linha abre a partir do alinhamento (centro: metade para cada lado; o
    // espaço depois da última letra não conta).
    for (usize g = 0; g < units.size(); ++g) {
        const u32 li = std::min<u32>(units[g].lineIndex, static_cast<u32>(lineRun.size() - 1));
        out[g].trackingShift += shift[g] - lineSpan[li] * std::clamp(align, 0.0f, 1.0f);
    }
}

f32 glyph_padding(const Layer& l, f32 textSize, usize glyphs) noexcept {
    if (!has_units(l)) return 0.0f;
    auto maxAbs = [&](u32 ai, u32 p, f32 v) {
        f32 m = std::fabs(v);
        if (const Track* tr = l.tracks.find(TrackProperty::LayerAnimParam, ai, p)) {
            for (const Keyframe& k : tr->keys) m = std::max(m, std::fabs(k.value));
        }
        return m;
    };
    f32 extra = 0.0f;
    for (u32 ai = 0; ai < l.layerAnimators.size(); ++ai) {
        const LayerAnimator& a = l.layerAnimators[ai];
        if (!a.enabled || a.unit == 0) continue;
        extra += std::max(maxAbs(ai, kFromPosX, a.fromPosX), maxAbs(ai, kFromPosY, a.fromPosY));
        extra += std::max(maxAbs(ai, kWigglePosX, a.wigglePosX), maxAbs(ai, kWigglePosY, a.wigglePosY));
        const f32 sc = std::max({maxAbs(ai, kFromScale, a.fromScale), maxAbs(ai, kFromScaleY, a.fromScaleY)}) + maxAbs(ai, kWiggleScale, a.wiggleScale);
        extra += textSize * std::max(0.0f, sc / 100.0f - 1.0f);
        if (a.fromRotation != 0.0f || a.fromRotX != 0.0f || a.fromRotY != 0.0f || a.wiggleRotation != 0.0f
            || l.tracks.find(TrackProperty::LayerAnimParam, ai, kFromRotation)) extra += textSize * 0.5f;
        extra += maxAbs(ai, kFromTracking, a.fromTracking) * static_cast<f32>(std::min<usize>(glyphs, 200));
    }
    return std::min(extra, 4000.0f);
}

} // namespace aurea::layeranim
