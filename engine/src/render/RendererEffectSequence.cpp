// =============================================================================
//  Aurea / render / RendererEffectSequence.cpp
//
//  A PRÉVIA ANIMADA DE UM EFEITO (docs/diagnostico/efeitos-imagem-feed.md §3.2).
//
//  O mesmo `render_effect_preview` de sempre (o efeito REAL, com os valores de
//  demonstração, antes/depois), num laço de N quadros (12..16) em que variam:
//    - o tempo local (2 quadros da linha do tempo por quadro da prévia);
//    - o progresso do clipe (0..1 ao longo da sequência);
//    - o parâmetro principal (do padrão até o de demonstração e de volta);
//    - a CENA-FONTE, quando o efeito é de tempo ou compara com o passado.
//
//  As cenas são nossas: a foto de amostra do app, a cartela procedural do
//  shader e duas cenas geradas aqui na CPU (formas coloridas em movimento e um
//  ambiente escuro de baixa luz). Nenhum asset novo, nada de terceiros.
//
//  EFEITOS DE TEMPO. No projeto, o eco, o RGB no tempo, o posterizar, o
//  TimeSlice e os de optical flow são DECLARATIVOS: quem age é o renderer, que
//  busca a camada em outros instantes (Renderer.cpp, bloco "Eco e RGB no
//  tempo"). A prévia não tem camada nem decoder; a cena dela é uma função do
//  tempo, então os instantes são gerados aqui e somados com as MESMAS regras
//  de peso do renderer (soma aditiva com queda no eco, um canal por instante
//  no RGB). Isto é uma reprodução da regra para a prévia, não o caminho do
//  projeto: Flow Retime e Flow Motion Blur são mostrados pela fonte amostrada
//  no instante fracionário (a cena é contínua, não há vetor a estimar). Quem
//  compara com o passado pela cadeia de efeitos (Detectar movimento, Datamosh,
//  Video Glitch) roda o efeito real com o quadro passado como histórico.
// =============================================================================
#include "aurea/render/Renderer.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Math.hpp"
#include "aurea/effects/EffectRegistry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string_view>

namespace aurea {
namespace {

constexpr i64 kSequenceStartFrame = 18;   // o mesmo instante da prévia estática

f32 srgb_to_linear_f(f32 c) noexcept {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

const std::array<f32, 256>& srgb_lut() noexcept {
    static const std::array<f32, 256> lut = [] {
        std::array<f32, 256> t{};
        for (u32 i = 0; i < 256; ++i) t[i] = srgb_to_linear_f(static_cast<f32>(i) / 255.0f);
        return t;
    }();
    return lut;
}

u8 linear_to_srgb8(f32 v) noexcept {
    const f32 c = std::clamp(v, 0.0f, 1.0f);
    const f32 e = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return static_cast<u8>(std::lround(std::clamp(e, 0.0f, 1.0f) * 255.0f));
}

u8 to8(f32 v) noexcept { return static_cast<u8>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

f32 fract(f64 x) noexcept { return static_cast<f32>(x - std::floor(x)); }

f32 hash2(u32 x, u32 y) noexcept {
    u32 h = 0x811C9DC5u;
    h = (h ^ x) * 16777619u;
    h = (h ^ y) * 16777619u;
    h ^= h >> 15;
    return static_cast<f32>(h & 0xFFFFu) / 65536.0f;
}

/// Cobertura antisserrilhada de uma distância com sinal (px; < 0 = dentro).
f32 cover(f32 sd) noexcept { return std::clamp(0.5f - sd, 0.0f, 1.0f); }

struct Rgb { f32 r, g, b; };
Rgb mix(Rgb a, Rgb b, f32 t) noexcept { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

// --- Cena 2: formas coloridas em movimento ----------------------------------
// Fundo em degradê com uma faixa de listras finas (o que desfoque e nitidez
// mostram), um disco em órbita, um quadrado que gira e vai e volta, um
// triângulo que sobe e desce e um ponto branco forte que atravessa o quadro
// (brilho, rastro, raios). Tudo periódico em `period`: a sequência fecha o laço.
void scene_shapes(u32 w, u32 h, f64 t, f64 period, std::vector<u8>& out) noexcept {
    const f32 ph = fract(t / period) * kTwoPi;
    const f32 W = static_cast<f32>(w), H = static_cast<f32>(h), S = std::min(W, H);
    const f32 cx = W * (0.50f + 0.26f * std::cos(ph)), cy = H * (0.52f + 0.22f * std::sin(ph)), cr = S * 0.17f;
    const f32 sx = W * (0.22f + 0.20f * (0.5f - 0.5f * std::cos(ph))), sy = H * 0.30f, sh = S * 0.13f;
    const f32 sa = ph, sc = std::cos(sa), ss = std::sin(sa);
    const f32 tx = W * 0.76f, ty = H * (0.62f + 0.12f * std::sin(2.0f * ph)), ts = S * 0.16f;
    const f32 dx = W * fract(t / period * 2.0), dy = H * 0.16f, dr = S * 0.035f;
    out.resize(static_cast<usize>(w) * h * 4);
    for (u32 y = 0; y < h; ++y) {
        const f32 fy = (static_cast<f32>(y) + 0.5f);
        for (u32 x = 0; x < w; ++x) {
            const f32 fx = (static_cast<f32>(x) + 0.5f);
            Rgb c = mix(Rgb{0.06f, 0.08f, 0.18f}, Rgb{0.26f, 0.12f, 0.32f}, fy / H * 0.8f + fx / W * 0.2f);
            // Listras finas embaixo (3 px), paradas: referência para o movimento.
            if (fy > H * 0.86f) c = ((x / 3u) & 1u) ? Rgb{0.88f, 0.88f, 0.86f} : Rgb{0.08f, 0.08f, 0.10f};
            // Quadrado girando (ciano).
            {
                const f32 lx = (fx - sx) * sc + (fy - sy) * ss, ly = -(fx - sx) * ss + (fy - sy) * sc;
                const f32 sd = std::max(std::fabs(lx), std::fabs(ly)) - sh;
                c = mix(c, Rgb{0.10f, 0.78f, 0.92f}, cover(sd));
            }
            // Triângulo (amarelo), apontado para cima.
            {
                const f32 lx = std::fabs(fx - tx), ly = fy - ty;
                const f32 edge = (lx * 0.866f + ly * 0.5f) - ts * 0.5f;   // lados
                const f32 base = ly - ts * 0.5f;                            // base
                c = mix(c, Rgb{0.98f, 0.84f, 0.18f}, cover(std::max(edge, base)));
            }
            // Disco em órbita (laranja), por cima.
            {
                const f32 sd = std::hypot(fx - cx, fy - cy) - cr;
                c = mix(c, Rgb{0.96f, 0.36f, 0.14f}, cover(sd));
            }
            // Ponto branco forte com halo.
            {
                const f32 d = std::hypot(fx - dx, fy - dy);
                const f32 halo = std::exp(-(d * d) / (dr * dr * 6.0f)) * 0.6f;
                c = mix(c, Rgb{1.0f, 1.0f, 1.0f}, std::clamp(cover(d - dr) + halo, 0.0f, 1.0f));
            }
            u8* p = &out[(static_cast<usize>(y) * w + x) * 4];
            p[0] = to8(c.r); p[1] = to8(c.g); p[2] = to8(c.b); p[3] = 255;
        }
    }
}

// --- Cena 3: baixa luz --------------------------------------------------------
// Um ambiente quase preto (ruído de sensor fixo), uma janela azulada fraca, um
// chão um pouco mais claro e UMA lâmpada quente que passeia de um lado a outro:
// é onde exposição, níveis, redução de ruído e brilho mostram o que fazem com
// pouca luz.
void scene_low_light(u32 w, u32 h, f64 t, f64 period, std::vector<u8>& out) noexcept {
    const f32 ph = fract(t / period) * kTwoPi;
    const f32 W = static_cast<f32>(w), H = static_cast<f32>(h), S = std::min(W, H);
    const f32 lx = W * (0.30f + 0.40f * (0.5f - 0.5f * std::cos(ph))), ly = H * 0.38f, core = S * 0.045f;
    out.resize(static_cast<usize>(w) * h * 4);
    for (u32 y = 0; y < h; ++y) {
        const f32 fy = static_cast<f32>(y) + 0.5f;
        for (u32 x = 0; x < w; ++x) {
            const f32 fx = static_cast<f32>(x) + 0.5f;
            Rgb c = mix(Rgb{0.035f, 0.04f, 0.06f}, Rgb{0.015f, 0.018f, 0.03f}, fy / H);
            // Janela fraca à esquerda, com caixilho.
            if (fx > W * 0.08f && fx < W * 0.30f && fy > H * 0.14f && fy < H * 0.56f) {
                const bool frame = std::fabs(fx - W * 0.19f) < 1.0f || std::fabs(fy - H * 0.35f) < 1.0f;
                c = frame ? Rgb{0.03f, 0.03f, 0.04f} : Rgb{0.10f, 0.13f, 0.21f};
            }
            // Chão.
            if (fy > H * 0.74f) c = Rgb{0.07f, 0.06f, 0.055f};
            // Silhueta de um móvel no chão.
            if (fx > W * 0.58f && fx < W * 0.86f && fy > H * 0.58f && fy < H * 0.80f) c = Rgb{0.02f, 0.02f, 0.025f};
            // Lâmpada quente: núcleo e halo largo.
            const f32 d = std::hypot(fx - lx, fy - ly);
            const f32 halo = std::exp(-(d * d) / (S * S * 0.03f)) * 0.35f;
            c.r += halo * 1.0f; c.g += halo * 0.72f; c.b += halo * 0.42f;
            c = mix(c, Rgb{1.0f, 0.86f, 0.62f}, cover(d - core));
            // Ruído de sensor (fixo: a sequência é determinística).
            const f32 n = (hash2(x, y) - 0.5f) * 0.03f;
            u8* p = &out[(static_cast<usize>(y) * w + x) * 4];
            p[0] = to8(c.r + n); p[1] = to8(c.g + n); p[2] = to8(c.b + n); p[3] = 255;
        }
    }
}

/// A foto/cartela capturada com folga, recortada numa janela que passeia
/// (amostragem bilinear: o movimento é contínuo, sem degrau de 1 px).
void crop_moving(const EffectPreviewSequence& s, f64 t, f64 period, std::vector<u8>& out) noexcept {
    const f32 ph = fract(t / period) * kTwoPi;
    const f32 padX = static_cast<f32>(s.baseW - s.width), padY = static_cast<f32>(s.baseH - s.height);
    const f32 ox = padX * (0.5f + 0.5f * std::sin(ph));
    const f32 oy = padY * (0.5f + 0.5f * std::sin(2.0f * ph + 0.7f));
    out.resize(static_cast<usize>(s.width) * s.height * 4);
    const u32 bw = s.baseW, bh = s.baseH;
    for (u32 y = 0; y < s.height; ++y) {
        const f32 sy = std::clamp(static_cast<f32>(y) + oy, 0.0f, static_cast<f32>(bh - 1));
        const u32 y0 = static_cast<u32>(sy), y1 = std::min(y0 + 1, bh - 1);
        const f32 wy = sy - static_cast<f32>(y0);
        for (u32 x = 0; x < s.width; ++x) {
            const f32 sx = std::clamp(static_cast<f32>(x) + ox, 0.0f, static_cast<f32>(bw - 1));
            const u32 x0 = static_cast<u32>(sx), x1 = std::min(x0 + 1, bw - 1);
            const f32 wx = sx - static_cast<f32>(x0);
            const u8* a = &s.base[(static_cast<usize>(y0) * bw + x0) * 4];
            const u8* b = &s.base[(static_cast<usize>(y0) * bw + x1) * 4];
            const u8* c = &s.base[(static_cast<usize>(y1) * bw + x0) * 4];
            const u8* d = &s.base[(static_cast<usize>(y1) * bw + x1) * 4];
            u8* p = &out[(static_cast<usize>(y) * s.width + x) * 4];
            for (u32 k = 0; k < 4; ++k) {
                const f32 top = a[k] + (b[k] - a[k]) * wx, bottom = c[k] + (d[k] - c[k]) * wx;
                p[k] = static_cast<u8>(std::lround(std::clamp(top + (bottom - top) * wy, 0.0f, 255.0f)));
            }
        }
    }
}

f64 sequence_period(const EffectPreviewSequence& s) noexcept {
    return static_cast<f64>(s.frames) * static_cast<f64>(kEffectPreviewSequenceStep);
}

/// A cena-fonte no instante `t` (quadros). Vazio = fonte parada (o caminho da
/// foto/cartela de sempre).
void scene_at(const EffectPreviewSequence& s, f64 t, std::vector<u8>& out) noexcept {
    const f64 period = sequence_period(s);
    if (s.scene == EffectPreviewScene::Shapes || s.scene == EffectPreviewScene::LowLight) {
        effect_preview_procedural_scene(s.scene, s.width, s.height, t, period, out);
    } else if (!s.base.empty()) {
        crop_moving(s, t, period, out);
    } else {
        out.clear();
    }
}

enum class TemporalKind : u8 { None, Echo, RgbTime, Posterize, Slice, Remap, Retime, FlowBlur };

TemporalKind temporal_kind(std::string_view key) noexcept {
    if (key == effect_keys::kEchoTrail) return TemporalKind::Echo;
    if (key == effect_keys::kTimeWarpRgb) return TemporalKind::RgbTime;
    if (key == effect_keys::kPosterizeTime) return TemporalKind::Posterize;
    if (key == effect_keys::kTimeSlice) return TemporalKind::Slice;
    if (key == effect_keys::kTimeRemap) return TemporalKind::Remap;
    if (key == effect_keys::kFlowRetime) return TemporalKind::Retime;
    if (key == effect_keys::kFlowMotionBlur) return TemporalKind::FlowBlur;
    return TemporalKind::None;
}

/// Imagem linear (RGB) para somar instantes.
struct Linear {
    std::vector<f32> px;   // 3 por pixel
};

class SceneSampler {
public:
    explicit SceneSampler(const EffectPreviewSequence& s) noexcept : s_(s) {}
    const Linear& at(f64 t) noexcept {
        const f64 key = std::round(t * 64.0) / 64.0;
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        scene_at(s_, key, bytes_);
        Linear lin;
        const auto& lut = srgb_lut();
        const usize n = static_cast<usize>(s_.width) * s_.height;
        lin.px.resize(n * 3);
        for (usize i = 0; i < n && bytes_.size() >= n * 4; ++i) {
            lin.px[i * 3 + 0] = lut[bytes_[i * 4 + 0]];
            lin.px[i * 3 + 1] = lut[bytes_[i * 4 + 1]];
            lin.px[i * 3 + 2] = lut[bytes_[i * 4 + 2]];
        }
        return cache_.emplace(key, std::move(lin)).first->second;
    }
private:
    const EffectPreviewSequence& s_;
    std::vector<u8> bytes_;
    std::map<f64, Linear> cache_;
};

void encode(const std::vector<f32>& acc, std::vector<u8>& out, usize n) noexcept {
    out.resize(n * 4);
    for (usize i = 0; i < n; ++i) {
        out[i * 4 + 0] = linear_to_srgb8(acc[i * 3 + 0]);
        out[i * 4 + 1] = linear_to_srgb8(acc[i * 3 + 1]);
        out[i * 4 + 2] = linear_to_srgb8(acc[i * 3 + 2]);
        out[i * 4 + 3] = 255;
    }
}

f32 val(const std::vector<ParamValue>& v, u32 i, f32 fallback) noexcept {
    return i < v.size() && std::isfinite(v[i].v[0]) ? v[i].v[0] : fallback;
}

/// O resultado de um efeito de tempo DECLARATIVO no instante `t`, com as regras
/// de peso do renderer (ver o comentário do topo).
bool compose_temporal(TemporalKind kind, const std::vector<ParamValue>& v, SceneSampler& scene, f64 t,
                      u32 w, u32 h, std::vector<u8>& out) noexcept {
    const usize n = static_cast<usize>(w) * h;
    std::vector<f32> acc(n * 3, 0.0f);
    auto add = [&](const Linear& img, f32 weight, f32 mr, f32 mg, f32 mb) noexcept {
        for (usize i = 0; i < n; ++i) {
            acc[i * 3 + 0] += img.px[i * 3 + 0] * weight * mr;
            acc[i * 3 + 1] += img.px[i * 3 + 1] * weight * mg;
            acc[i * 3 + 2] += img.px[i * 3 + 2] * weight * mb;
        }
    };
    switch (kind) {
        case TemporalKind::Echo: {
            // Renderer: RGB atrasado (um canal por instante) ou a camada agora;
            // depois as cópias com peso que cai `decay` a cada uma (soma aditiva).
            const u32 copies = static_cast<u32>(std::clamp(val(v, 0, 4.0f), 0.0f, 16.0f) + 0.5f);
            const f64 delay = std::clamp(static_cast<f64>(val(v, 1, 2.0f)), 0.25, 120.0);
            const f32 decay = std::clamp(val(v, 2, 0.6f), 0.0f, 1.0f);
            const f32 rgb = val(v, 3, 0.0f);
            if (rgb > 0.0f) {
                const f64 d = std::clamp(static_cast<f64>(rgb), 0.0, 60.0);
                add(scene.at(t), 1.0f, 1, 0, 0);
                add(scene.at(t - d), 1.0f, 0, 1, 0);
                add(scene.at(t - 2.0 * d), 1.0f, 0, 0, 1);
            } else {
                add(scene.at(t), 1.0f, 1, 1, 1);
            }
            f32 wgt = 1.0f;
            for (u32 i = 1; i <= copies; ++i) {
                wgt *= decay;
                if (wgt < 0.01f) break;
                add(scene.at(t - static_cast<f64>(i) * delay), wgt, 1, 1, 1);
            }
            break;
        }
        case TemporalKind::RgbTime: {
            const bool seconds = v.size() > 3 && v[3].as_enum() == 1;
            const f64 unit = seconds ? 30.0 : 1.0;
            const f32 amount = std::clamp(val(v, 4, 100.0f) / 100.0f, 0.0f, 1.0f);
            const Linear& now = scene.at(t);
            const Linear& r = scene.at(t + val(v, 0, 3.0f) * unit);
            const Linear& g = scene.at(t + val(v, 1, 0.0f) * unit);
            const Linear& b = scene.at(t + val(v, 2, -3.0f) * unit);
            for (usize i = 0; i < n; ++i) {
                acc[i * 3 + 0] = now.px[i * 3 + 0] + (r.px[i * 3 + 0] - now.px[i * 3 + 0]) * amount;
                acc[i * 3 + 1] = now.px[i * 3 + 1] + (g.px[i * 3 + 1] - now.px[i * 3 + 1]) * amount;
                acc[i * 3 + 2] = now.px[i * 3 + 2] + (b.px[i * 3 + 2] - now.px[i * 3 + 2]) * amount;
            }
            break;
        }
        case TemporalKind::Posterize: {
            // PosterizeTime::quantize (TemporalEffects.cpp), a 30 qps.
            const f64 target = std::max(0.1, static_cast<f64>(val(v, 0, 8.0f)));
            const bool hold = v.size() <= 1 || v[1].as_bool();
            const f64 step = 30.0 / target;
            f64 q = t;
            if (step > 1.0) q = (hold ? std::floor(t / step) : std::round(t / step)) * step;
            add(scene.at(q), 1.0f, 1, 1, 1);
            break;
        }
        case TemporalKind::Slice: {
            // N faixas na direção pedida, a faixa s em t + deslocamento + rel(s)
            // (rel de −N/2 a +N/2), como o renderer.
            const f32 dir = val(v, 0, 90.0f) * kDeg2Rad;
            const i32 slices = std::clamp(v.size() > 1 ? v[1].as_int() : 12, 1, 32);
            const f64 base = t + static_cast<f64>(val(v, 2, 0.0f));
            const f32 dx = std::cos(dir), dy = std::sin(dir);
            const f32 span = std::fabs(dx) * 0.5f + std::fabs(dy) * 0.5f;
            for (u32 y = 0; y < h; ++y) {
                for (u32 x = 0; x < w; ++x) {
                    const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(w) - 0.5f;
                    const f32 q = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(h) - 0.5f;
                    const f32 proj = span > 1e-4f ? (u * dx + q * dy) / span : 0.0f;   // −1..1
                    const i32 s = std::clamp(static_cast<i32>(std::floor((proj * 0.5f + 0.5f) * static_cast<f32>(slices))),
                                             0, slices - 1);
                    const f64 rel = slices > 1 ? -0.5 * slices + static_cast<f64>(s) * slices / (slices - 1) : 0.0;
                    const Linear& img = scene.at(std::floor(base + rel + 0.5));
                    const usize i = static_cast<usize>(y) * w + x;
                    acc[i * 3 + 0] = img.px[i * 3 + 0];
                    acc[i * 3 + 1] = img.px[i * 3 + 1];
                    acc[i * 3 + 2] = img.px[i * 3 + 2];
                }
            }
            break;
        }
        case TemporalKind::Remap:
            // O remapeamento padrão de uma camada nova anda a meia velocidade
            // aqui: mostra que o tempo da camada é outro, sem congelar a prévia.
            add(scene.at(static_cast<f64>(kSequenceStartFrame) + (t - kSequenceStartFrame) * 0.5), 1.0f, 1, 1, 1);
            break;
        case TemporalKind::Retime: {
            const f64 speed = std::clamp(static_cast<f64>(val(v, 0, 50.0f)) / 100.0, 0.05, 4.0);
            add(scene.at(static_cast<f64>(kSequenceStartFrame) + (t - kSequenceStartFrame) * speed), 1.0f, 1, 1, 1);
            break;
        }
        case TemporalKind::FlowBlur: {
            // Obturador: média de 8 instantes na janela aberta antes do quadro.
            const f64 amount = std::clamp(static_cast<f64>(val(v, 0, 100.0f)) / 100.0, 0.0, 4.0);
            const f64 window = std::clamp(static_cast<f64>(val(v, 1, 180.0f)), 0.0, 720.0) / 360.0 * amount;
            constexpr u32 K = 8;
            for (u32 k = 0; k < K; ++k)
                add(scene.at(t - window * static_cast<f64>(k) / static_cast<f64>(K - 1)), 1.0f / K, 1, 1, 1);
            break;
        }
        case TemporalKind::None:
            return false;
    }
    encode(acc, out, n);
    return true;
}

/// De quantos quadros atrás vem o histórico de um efeito que compara com o
/// passado (as mesmas regras do renderer, bloco "DETECTAR MOVIMENTO").
f64 history_delay(const Effect& effect, const ParameterRegistry& params, const EffectInstance& instance,
                  const std::vector<ParamValue>& values, i64 local) noexcept {
    const std::string_view key = effect.info().key;
    if (key == effect_keys::kMotionDetect) {
        return static_cast<f64>(std::clamp<i64>(std::llround(val(values, 0, 1.0f)), 1, kMotionDetectMaxDelay));
    }
    if (key == effect_keys::kDatamosh) {
        const u32 hold = params.find("hold_frames");
        const i64 q = hold < values.size() ? std::clamp<i64>(std::llround(val(values, hold, 12.0f)), 1, kDatamoshMaxHold) : 12;
        return static_cast<f64>(((local % q) + q) % q + 1);
    }
    EffectEval e;
    e.effect = &effect;
    e.instance = &instance;
    e.values = values.data();
    e.count = static_cast<u32>(values.size());
    e.localTime = FrameIndex{local};
    const f64 d = effect.history_delay_frames(e);
    return std::isfinite(d) && d >= 1.0 ? std::clamp(std::floor(d), 1.0, 1200.0) : 1.0;
}

} // namespace

namespace {
/// Um parâmetro que a varredura move: numérico, animável, visível, que não é
/// semente (semente só cintila) e cujo valor de demonstração difere do padrão.
bool sweepable(const ParamSpec& p, const ParamValue& neutral, const ParamValue& demo) noexcept {
    switch (p.type) {
        case ParamType::Float: case ParamType::Angle: case ParamType::Int:
        case ParamType::Point2D: case ParamType::Color: break;
        default: return false;
    }
    if (!p.animatable() || (p.flags & kParamHidden)) return false;
    const std::string_view id = p.id ? p.id : "";
    if (id.find("seed") != std::string_view::npos) return false;
    const u32 n = component_count(p.type);
    for (u32 c = 0; c < n; ++c) {
        if (std::isfinite(neutral.v[c]) && std::isfinite(demo.v[c]) && std::fabs(demo.v[c] - neutral.v[c]) > 1e-4f) return true;
    }
    return false;
}
} // namespace

i32 effect_preview_sweep_param(const ParameterRegistry& params, const EffectInstance& instance,
                               const std::vector<ParamValue>& demo) noexcept {
    for (u32 i = 0; i < params.count() && i < demo.size() && i < instance.params.size(); ++i) {
        if (sweepable(params.at(i), instance.params[i].constant, demo[i])) return static_cast<i32>(i);
    }
    return -1;
}

void effect_preview_apply_sweep(const ParameterRegistry& params, const EffectInstance& instance,
                                std::vector<ParamValue>& values, f32 s) noexcept {
    const f32 k = std::clamp(s, 0.0f, 1.0f);
    for (u32 i = 0; i < params.count() && i < values.size() && i < instance.params.size(); ++i) {
        const ParamSpec& p = params.at(i);
        const ParamValue& neutral = instance.params[i].constant;
        if (!sweepable(p, neutral, values[i])) continue;
        const u32 n = component_count(p.type);
        for (u32 c = 0; c < n; ++c) values[i].v[c] = neutral.v[c] + (values[i].v[c] - neutral.v[c]) * k;
        if (p.type == ParamType::Int) values[i].v[0] = std::round(values[i].v[0]);
    }
}

void effect_preview_procedural_scene(EffectPreviewScene scene, u32 w, u32 h, f64 t, f64 period,
                                     std::vector<u8>& out) noexcept {
    if (w == 0 || h == 0) { out.clear(); return; }
    if (!(period > 0.0)) period = 32.0;
    if (scene == EffectPreviewScene::LowLight) scene_low_light(w, h, t, period, out);
    else scene_shapes(w, h, t, period, out);
}

Status Renderer::prepare_effect_preview_sequence(const EffectRegistry& effects, EffectTypeId type, u32 width,
                                                 u32 height, u32 frames, EffectPreviewScene scene,
                                                 EffectPreviewSequence& out) noexcept {
    if (width == 0 || height == 0) return Status{Errc::InvalidArgument, "previa sem tamanho"};
    if (static_cast<u32>(scene) >= kEffectPreviewSceneCount) return Status{Errc::InvalidArgument, "cena desconhecida"};
    const Effect* effect = effects.find(type);
    if (!effect) return Status{Errc::NotFound, "efeito desconhecido"};
    const ParameterRegistry* params = effects.params(type);
    if (!params) return Status{Errc::NotFound, "efeito sem parametros"};
    out = EffectPreviewSequence{};
    out.type = type;
    out.width = width;
    out.height = height;
    out.frames = std::clamp(frames, kEffectPreviewSequenceMinFrames, kEffectPreviewSequenceMaxFrames);
    out.scene = scene;
    out.info.frames = out.frames;
    out.info.temporal = effect->effect_class() == EffectClass::Temporal;
    const TemporalKind kind = temporal_kind(effect->info().key);
    const bool procedural = scene == EffectPreviewScene::Shapes || scene == EffectPreviewScene::LowLight;

    EffectInstance instance;
    initialize_instance(instance, *params);
    out.values.resize(params->count());
    for (u32 i = 0; i < params->count(); ++i) out.values[i] = instance.params[i].constant;
    (void)effect->demo_values(instance, out.values);
    // Efeito de tempo declarativo: o movimento É a prévia; o parâmetro fica parado.
    out.info.sweepParam = kind != TemporalKind::None ? -1 : effect_preview_sweep_param(*params, instance, out.values);
    // A fonte anda para o efeito de tempo e para quem compara com o passado; e,
    // na foto/cartela, também para o efeito que já aparece com os padrões (sem
    // nada a varrer): o laço mostra o efeito sobre imagem em movimento, como
    // num vídeo. Exceção: quem lê o mapa de profundidade da FOTO parada.
    bool usesDepth = false;
    {
        struct DepthProbe final : EffectResources {
            bool asked = false;
            TextureHandle curve_lut(const CurveData&) noexcept override { return {}; }
            DepthMapResult depth_map(const DepthMapRequest&) noexcept override { asked = true; return {}; }
        } probe;
        EffectEval e;
        e.effect = effect;
        e.instance = &instance;
        e.resources = &probe;
        e.values = out.values.data();
        e.count = static_cast<u32>(out.values.size());
        e.localTime = FrameIndex{kSequenceStartFrame};
        effect->resolve_resources(e);
        usesDepth = probe.asked;
    }
    const bool needsMotion = out.info.temporal || effect->wants_history()
                          || (out.info.sweepParam < 0 && !usesDepth);
    out.info.moving = needsMotion || procedural;

    // Foto/cartela que precisam andar: captura com folga (25 %) uma vez.
    if (needsMotion && !procedural) {
        const u32 bw = width + std::max<u32>(4, width / 4), bh = height + std::max<u32>(4, height / 4);
        PreviewFrameSpec spec;
        spec.sourceOnly = true;
        spec.allowTemporal = true;
        spec.forcePlate = scene == EffectPreviewScene::Plate;
        const Status s = render_effect_preview_impl(effects, type, bw, bh, spec, out.base);
        if (!s.ok()) return s;
        out.baseW = bw;
        out.baseH = bh;
    }
    return OkStatus;
}

Status Renderer::render_effect_preview_sequence_frame(const EffectRegistry& effects, const EffectPreviewSequence& seq,
                                                      u32 index, std::vector<u8>& outRgba) noexcept {
    if (index >= seq.frames) return Status{Errc::OutOfRange, "quadro fora da sequencia"};
    const Effect* effect = effects.find(seq.type);
    const ParameterRegistry* params = effects.params(seq.type);
    if (!effect || !params) return Status{Errc::NotFound, "efeito desconhecido"};
    const i64 local = kSequenceStartFrame + static_cast<i64>(index) * kEffectPreviewSequenceStep;
    PreviewFrameSpec spec;
    spec.comparison = true;
    spec.allowTemporal = true;
    spec.forcePlate = seq.scene == EffectPreviewScene::Plate;
    spec.localTime = local;
    spec.clipProgress = (static_cast<f32>(index) + 0.5f) / static_cast<f32>(seq.frames);
    if (seq.info.sweepParam >= 0) {
        const f32 phase = kTwoPi * static_cast<f32>(index) / static_cast<f32>(seq.frames);
        spec.sweep = 0.25f + 0.75f * (0.5f - 0.5f * std::cos(phase));
    }
    SceneSampler sampler(seq);
    std::vector<u8> source, history, result;
    const f64 t = static_cast<f64>(local);
    scene_at(seq, t, source);
    if (!source.empty()) spec.source = source.data();
    const TemporalKind kind = temporal_kind(effect->info().key);
    if (kind != TemporalKind::None) {
        if (!compose_temporal(kind, seq.values, sampler, t, seq.width, seq.height, result) || source.empty())
            return Status{Errc::NotImplemented, "efeito de tempo sem fonte"};
        spec.result = result.data();
    } else if (effect->wants_history() && !source.empty()) {
        EffectInstance instance;
        initialize_instance(instance, *params);
        scene_at(seq, t - history_delay(*effect, *params, instance, seq.values, local), history);
        if (!history.empty()) spec.history = history.data();
    } else if (seq.info.temporal) {
        return Status{Errc::NotImplemented, "efeito de tempo sem previa"};
    }
    return render_effect_preview_impl(effects, seq.type, seq.width, seq.height, spec, outRgba);
}

Status Renderer::render_effect_preview_sequence(const EffectRegistry& effects, EffectTypeId type, u32 width,
                                                u32 height, u32 frames, EffectPreviewScene scene,
                                                std::vector<u8>& outRgba, EffectPreviewSequenceInfo* info) noexcept {
    EffectPreviewSequence seq;
    if (const Status s = prepare_effect_preview_sequence(effects, type, width, height, frames, scene, seq); !s.ok())
        return s;
    const usize frameBytes = static_cast<usize>(width) * height * 4;
    outRgba.clear();
    outRgba.reserve(frameBytes * seq.frames);
    std::vector<u8> one;
    for (u32 i = 0; i < seq.frames; ++i) {
        one.clear();
        const Status s = render_effect_preview_sequence_frame(effects, seq, i, one);
        if (!s.ok()) return s;
        if (one.size() != frameBytes) return Status{Errc::InvalidState, "quadro da sequencia sem tamanho"};
        outRgba.insert(outRgba.end(), one.begin(), one.end());
    }
    if (info) *info = seq.info;
    return OkStatus;
}

} // namespace aurea
