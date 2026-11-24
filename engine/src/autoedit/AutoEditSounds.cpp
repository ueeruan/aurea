// =============================================================================
//  Edição automática: a biblioteca de sons do próprio Aurea (sintetizada) e a
//  leitura dos nomes da biblioteca do usuário. Nenhum arquivo de terceiros:
//  cada som é feito aqui de ruído e senos, sempre igual (semente fixa).
// =============================================================================
#include "aurea/autoedit/AutoEdit.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::autoedit {

namespace {
constexpr f64 kTau = 6.283185307179586;

struct Noise {
    u32 s;
    f32 next() noexcept { s = s * 1664525u + 1013904223u; return static_cast<f32>(s >> 8) / 8388608.0f - 1.0f; }
};

/// Filtro de estado variável (Chamberlin): passa-banda com corte que anda.
struct Svf {
    f32 low = 0, band = 0;
    f32 run(f32 x, f32 hz, f32 q, f32 rate) noexcept {
        const f32 f = 2.0f * std::sin(static_cast<f32>(kTau * 0.5) * std::min(hz, rate * 0.2f) / rate);
        low += f * band;
        const f32 high = x - low - q * band;
        band += f * high;
        return band;
    }
};

void normalize(std::vector<f32>& x, f32 peakDb) {
    f32 peak = 0.0f;
    for (f32 v : x) peak = std::max(peak, std::fabs(v));
    if (peak <= 0.0f) return;
    const f32 g = std::pow(10.0f, peakDb / 20.0f) / peak;
    for (f32& v : x) v *= g;
}
} // namespace

std::vector<f32> synth_sfx(SfxRole role, u32 variant, u32 rate) {
    const f32 r = static_cast<f32>(rate);
    const f32 tone = variant % 2 ? 1.18f : 1.0f;   // a 2ª variante é mais aguda
    Noise noise{0x5EEDu + static_cast<u32>(role) * 977u + variant * 131u};
    std::vector<f32> x;
    auto make = [&](f64 seconds) { x.assign(static_cast<usize>(seconds * rate), 0.0f); };
    switch (role) {
        case SfxRole::Woosh: {
            // Ruído com banda que sobe e desce, pico aos 60 % (0,7 s).
            make(0.7);
            Svf a, b;
            for (usize i = 0; i < x.size(); ++i) {
                const f32 t = static_cast<f32>(i) / static_cast<f32>(x.size());
                const f32 env = t < 0.6f ? std::pow(t / 0.6f, 2.2f) : std::pow(1.0f - (t - 0.6f) / 0.4f, 1.6f);
                const f32 hz = (350.0f + 2600.0f * std::sin(t * 3.14159f)) * tone;
                x[i] = env * (a.run(noise.next(), hz, 0.6f, r) + 0.5f * b.run(noise.next(), hz * 1.9f, 0.9f, r));
            }
            break;
        }
        case SfxRole::Shutter: {
            // Dois estalos de obturador (abre/fecha) com um baque grave.
            make(0.16);
            for (f64 at : {0.0, 0.065}) {
                const usize s = static_cast<usize>(at * rate);
                for (usize i = 0; i + s < x.size() && i < static_cast<usize>(0.03 * rate); ++i) {
                    const f32 t = static_cast<f32>(i) / r;
                    x[s + i] += std::exp(-t * 260.0f) * noise.next()
                              + 0.6f * std::exp(-t * 90.0f) * static_cast<f32>(std::sin(kTau * 180.0 * tone * t));
                }
            }
            break;
        }
        case SfxRole::Riser: {
            // 2 s: seno que sobe 200 → 1200 Hz com ruído, crescendo até o fim.
            make(2.0);
            f64 phase = 0.0;
            Svf f;
            for (usize i = 0; i < x.size(); ++i) {
                const f32 t = static_cast<f32>(i) / static_cast<f32>(x.size());
                phase += kTau * (200.0 + 1000.0 * t * t) * tone / rate;
                const f32 env = std::pow(t, 1.8f) * (t > 0.97f ? (1.0f - t) / 0.03f : 1.0f);
                x[i] = env * (0.6f * static_cast<f32>(std::sin(phase)) + 0.5f * f.run(noise.next(), 800.0f + 4000.0f * t, 0.7f, r));
            }
            break;
        }
        case SfxRole::Sub: {
            // Queda grave: 55 → 35 Hz, decaindo em ~0,9 s.
            make(1.0);
            f64 phase = 0.0;
            for (usize i = 0; i < x.size(); ++i) {
                const f32 t = static_cast<f32>(i) / r;
                phase += kTau * (35.0 + 20.0 * std::exp(-t * 6.0)) * tone / rate;
                const f32 attack = std::min(1.0f, t / 0.004f);
                x[i] = attack * std::exp(-t * 3.2f) * static_cast<f32>(std::sin(phase));
            }
            break;
        }
        case SfxRole::Flash: {
            // Brilho: ruído agudo + dois senos altos, ataque rápido, 0,45 s.
            make(0.45);
            Svf f;
            for (usize i = 0; i < x.size(); ++i) {
                const f32 t = static_cast<f32>(i) / r;
                const f32 env = std::min(1.0f, t / 0.01f) * std::exp(-t * 9.0f);
                x[i] = env * (0.7f * f.run(noise.next(), 6500.0f * tone, 0.5f, r)
                            + 0.35f * static_cast<f32>(std::sin(kTau * 2637.0 * tone * t) + std::sin(kTau * 3951.0 * tone * t)));
            }
            break;
        }
        case SfxRole::CutFlick: {
            make(0.06);
            for (usize i = 0; i < x.size(); ++i) {
                const f32 t = static_cast<f32>(i) / r;
                x[i] = std::exp(-t * 500.0f) * noise.next() * 0.6f
                     + std::exp(-t * 120.0f) * static_cast<f32>(std::sin(kTau * 1800.0 * tone * t));
            }
            break;
        }
        case SfxRole::Flick:
        default: {
            make(0.04);
            for (usize i = 0; i < x.size(); ++i) {
                const f32 t = static_cast<f32>(i) / r;
                x[i] = std::exp(-t * 160.0f) * static_cast<f32>(std::sin(kTau * 1250.0 * tone * t))
                     + std::exp(-t * 900.0f) * noise.next() * 0.3f;
            }
            break;
        }
    }
    normalize(x, -3.0f);
    return x;
}

namespace {
bool contains_any(const std::string& s, std::initializer_list<const char*> keys) {
    for (const char* k : keys) if (s.find(k) != std::string::npos) return true;
    return false;
}
std::string folded(std::string_view a, std::string_view b) {
    // fold_token tira espaços e pontuação: junta as duas partes com um separador neutro.
    return fold_token(a) + "|" + fold_token(b);
}
} // namespace

SfxRole sfx_role_of(std::string_view category, std::string_view name) {
    const std::string s = folded(category, name);
    if (contains_any(s, {"whoosh", "woosh", "swoosh", "swish", "transica", "transition", "swipe"})) return SfxRole::Woosh;
    if (contains_any(s, {"shutter", "obturador", "camera", "camara", "foto", "photo", "snapshot"})) return SfxRole::Shutter;
    if (contains_any(s, {"riser", "subida", "rise", "buildup", "tensao", "uplifter"})) return SfxRole::Riser;
    if (contains_any(s, {"sub", "drop", "boom", "impact", "impacto", "hit", "bass", "grave"})) return SfxRole::Sub;
    if (contains_any(s, {"flash", "brilho", "shine", "sparkle", "glint", "ding", "bling"})) return SfxRole::Flash;
    if (contains_any(s, {"flick", "tap", "blip", "typing", "digit", "tecla", "keyboard"})) return SfxRole::Flick;
    if (contains_any(s, {"click", "clique", "clic", "tick", "pop", "cut", "corte"})) return SfxRole::CutFlick;
    return SfxRole::Count;
}

bool music_like(std::string_view category, std::string_view name) {
    const std::string s = folded(category, name);
    return contains_any(s, {"musica", "music", "trilha", "bgm", "song", "cancao", "beat", "instrumental", "lofi", "soundtrack"});
}

} // namespace aurea::autoedit
