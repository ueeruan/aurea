// =============================================================================
//  Saber (Luz) — um traço de energia com núcleo branco-quente e brilho largo.
//
//  Referência de COMPORTAMENTO: o plugin Saber dos editores de movimento (a
//  lista pública de controles: preset, brilho — cor, intensidade, espalhamento,
//  viés —, tamanho do núcleo, tipo de núcleo único/máscaras/texto, núcleo
//  personalizado com deslocamento inicial/final, tamanhos, arredondamento,
//  suavidade e evolução das máscaras, cintilação, distorção e composição).
//  Implementação e shaders próprios (saber_*.frag).
//
//  Passes (todos em px da camada; a prévia em 1/4 e o export desenham igual):
//    1. saber_core.frag    o núcleo na densidade da entrada: G = cobertura,
//                          R = luz (cobertura × brilho do caminho);
//    2. pirâmide de oitavas (glow_octave_down/up, as do Brilho): a luz do
//       núcleo borrada em escalas que dobram, somadas com um peso por oitava —
//       o "viés" inclina os pesos para dentro ou para fora, o "espalhamento"
//       diz quantas oitavas acendem. Somar oitavas de largura dobrada dá a
//       queda longa (quase 1/x) do brilho do Saber, com custo fixo;
//    3. saber_composite.frag  núcleo + brilho em HDR, mapa de tom exponencial
//       por canal (a cor satura no branco onde a luz é forte) e a composição
//       (somar sobre a camada, transparente ou sobre preto).
//
//  Cintilação e distorção são funções do TEMPO do quadro (e da semente): o
//  mesmo quadro dá o mesmo sabre na prévia, no scrubbing e no export.
//  Os índices de parâmetro ficam fixos nos projetos salvos (acrescentar só
//  no fim).
// =============================================================================
#include "CCCommon.hpp"

#include "aurea/render/MaskRaster.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace aurea::builtin {
namespace {
using namespace cc;

/// Espelho de saber_common.glsl (std140, só vec4).
struct SaberUniforms {
    Vec4 dst, src, box, info;
    Vec4 seg, core, window, caps;
    Vec4 dist, misc, affA, affB;
    Vec4 mapRegion, glowColor, coreColor, look;
};
static_assert(sizeof(SaberUniforms) == 256, "layout std140 de saber_common.glsl");

constexpr u32 kOctaves = 8;            ///< oitava mais larga: texel de 512 px
constexpr u32 kMaxSegments = 2048;     ///< trechos das máscaras (achatamento mais grosso acima disto)
constexpr u32 kMaxIndices = 65536;     ///< entradas da grade (célula × trecho)
constexpr u32 kDataWidth = 256;        ///< largura do dado RGBA32F
constexpr f32 kGlowGain = 0.85f;       ///< calibração: brilho a 100%
constexpr f32 kCoreGain = 2.6f;        ///< calibração: núcleo a 100%

enum CoreType : u32 { kSingle = 0, kMasks = 1, kLayer = 2, kNone = 3 };
enum Composite : u32 { kAdd = 0, kTransparent = 1, kOverBlack = 2 };

// --- Ruído determinístico na CPU (cintilação e sorteios por máscara) --------
u32 mix32(u32 x) noexcept {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
f32 hash01(u32 a, u32 b) noexcept {
    return static_cast<f32>(mix32(a * 0x9E3779B9u ^ mix32(b + 0x632BE5ABu)) & 0xFFFFFFu) / 16777216.0f;
}
/// Ruído de valor 1D suave (0..1).
f32 noise1(f64 x, u32 salt) noexcept {
    const f64 i = std::floor(x);
    const f32 f = static_cast<f32>(x - i);
    const u32 k = static_cast<u32>(static_cast<i64>(i));
    const f32 a = hash01(k, salt), b = hash01(k + 1u, salt);
    const f32 s = f * f * (3.0f - 2.0f * f);
    return a + (b - a) * s;
}
/// Brilho da cintilação (1 = parado). `amount` 0..1; `speed` em variações
/// por segundo; `phase` separa máscaras.
f32 flicker(f32 seconds, f32 speed, f32 amount, f32 phase, u32 seed) noexcept {
    if (!(amount > 0.0f)) return 1.0f;
    const f64 x = static_cast<f64>(seconds) * std::max(speed, 0.0f) + static_cast<f64>(phase) * 97.13;
    const f32 n = 0.65f * noise1(x, seed * 3u + 1u) + 0.35f * noise1(x * 2.7 + 13.0, seed * 3u + 2u);
    return std::clamp(1.0f - amount * n * 1.25f, 0.0f, 1.0f);
}

class Saber final : public Effect {
public:
    enum : u32 {
        kGlowEnabled = 0, kGlowColor, kGlowIntensity, kGlowSpread, kGlowBias, kCoreSize,
        kCoreType, kCoreStart, kCoreEnd, kCoreLayer, kUseAlpha,
        kStartOffset, kEndOffset, kOffsetBrightness, kStartSize, kEndSize, kStartRoundness, kEndRoundness,
        kCoreSoftness, kCoreColor, kCoreSaturation, kCoreIntensity, kMaskEvolution, kMaskRandom,
        kFlickerIntensity, kFlickerSpeed, kFlickerMaskRandom,
        kDistortionType, kDistortionAmount, kDistortionSpeed, kDistortionScale,
        kComposite, kSeed,
    };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSaber, "Saber", "Luz", EffectClass::Neighborhood};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kTypes[] = {"Único", "Máscaras da camada", "Camada / texto"};
        static const char* const kDistortions[] = {"Nenhuma", "Suave", "Turbulenta", "Elétrica", "Fumaça"};
        static const char* const kComposites[] = {"Somar sobre a camada", "Transparente", "Sobre preto"};
        p.add_bool("glow_enabled", "Ativar brilho", true);
        p.add_color("glow_color", "Cor do brilho", Vec4{0.10f, 0.45f, 1.0f, 1.0f});
        p.add_float("glow_intensity", "Intensidade do brilho", 100.0f, 0.0f, 300.0f, kAnimPct, "%");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("glow_spread", "Espalhamento do brilho", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.0f, 200.0f);   // a pirâmide para na 8ª oitava
        p.add_float("glow_bias", "Viés do brilho", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("core_size", "Tamanho do núcleo", 8.0f, 0.0f, 100.0f, kAnimPx, "px");
        p.typed_range(0.0f, 2000.0f);
        p.add_enum("core_type", "Tipo de núcleo", kTypes, 3, kSingle);
        p.add_point2("core_start", "Início do núcleo", Vec2{0.2f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_point2("core_end", "Fim do núcleo", Vec2{0.8f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_layer_ref("core_layer", "Camada do núcleo");
        p.add_bool("use_alpha", "Usar alfa da camada", false);
        p.add_float("start_offset", "Deslocamento inicial", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("end_offset", "Deslocamento final", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("offset_brightness", "Brilho do deslocamento", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.0f, 500.0f);
        p.add_float("start_size", "Tamanho inicial", 100.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("end_size", "Tamanho final", 100.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("start_roundness", "Arredondamento inicial", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("end_roundness", "Arredondamento final", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("core_softness", "Suavidade do núcleo", 35.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_color("core_color", "Cor do núcleo", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("core_saturation", "Saturação do núcleo", 15.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("core_intensity", "Intensidade do núcleo", 100.0f, 0.0f, 300.0f, kAnimPct, "%");
        p.typed_range(0.0f, 2000.0f);
        // Evolução: voltas da janela pelo caminho (100% = uma volta).
        p.add_float("mask_evolution", "Evolução das máscaras", 0.0f, -100.0f, 100.0f, kAnimPct, "%");
        p.typed_range(-100000.0f, 100000.0f);
        p.add_float("mask_random", "Aleatoriedade das máscaras", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("flicker_intensity", "Intensidade da cintilação", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("flicker_speed", "Velocidade da cintilação", 10.0f, 0.0f, 60.0f, kAnim, "Hz");
        p.typed_range(0.0f, 240.0f);
        p.add_float("flicker_mask_random", "Aleatoriedade por máscara", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("distortion_type", "Tipo de distorção", kDistortions, 5, 0);
        p.add_float("distortion_amount", "Quantidade da distorção", 12.0f, 0.0f, 200.0f, kAnimPx, "px");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("distortion_speed", "Velocidade da distorção", 1.0f, 0.0f, 10.0f, kAnim);
        p.typed_range(0.0f, 100.0f);
        p.add_float("distortion_scale", "Escala da distorção", 80.0f, 2.0f, 1000.0f, kAnimPx, "px");
        p.typed_range(1.0f, 10000.0f);
        p.add_enum("composite", "Composição", kComposites, 3, kAdd);
        p.add_int("seed", "Semente", 0, 0, 1000);
        // Seções do painel (só apresentação: os índices acima não mudam). Fora
        // de seção ficam o tipo de núcleo e os pontos — o que se mexe sempre.
        p.add_section("glow", "Brilho externo", {kGlowColor, kGlowIntensity, kGlowSpread, kGlowBias}, kGlowEnabled);
        p.add_section("core", "Núcleo", {kCoreSize, kCoreSoftness, kCoreColor, kCoreSaturation, kCoreIntensity});
        p.add_section_range("core_shape", "Forma do núcleo", kStartOffset, kEndRoundness, kInvalidIndex, false);
        p.add_section("masks", "Máscaras", {kMaskEvolution, kMaskRandom}, kInvalidIndex, false);
        p.add_section("flicker", "Cintilação", {kFlickerIntensity, kFlickerSpeed, kFlickerMaskRandom}, kInvalidIndex, false);
        p.add_section("distortion", "Distorção", {kDistortionType, kDistortionAmount, kDistortionSpeed, kDistortionScale},
                      kInvalidIndex, false);
        p.add_section("output", "Saída", {kComposite, kSeed}, kInvalidIndex, false);
        p.set_tip(kCoreLayer, "Usada quando o núcleo é “Camada / texto”.");
        p.set_tip(kMaskEvolution, "Vale para o núcleo “Máscaras da camada”.");
    }

    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // A prévia do catálogo: um sabre na diagonal da foto.
        v[kCoreStart] = ParamValue::vec2(0.18f, 0.78f);
        v[kCoreEnd] = ParamValue::vec2(0.82f, 0.22f);
        v[kCoreSize] = ParamValue::scalar(10.0f);
        return true;
    }

    std::span<const EffectPreset> presets() const noexcept override {
        // O visual inteiro de cada preset (cor, brilho, núcleo, distorção e
        // cintilação); pontos, tipo de núcleo e composição ficam como estão.
#define AUREA_SABER_LOOK(r, g, b, inten, spread, bias, size, soft, sat, core, dt, da, ds, dsc, fi, fs) \
        {kGlowColor, r, 0}, {kGlowColor, g, 1}, {kGlowColor, b, 2}, {kGlowColor, 1.0f, 3},            \
        {kGlowIntensity, inten}, {kGlowSpread, spread}, {kGlowBias, bias}, {kCoreSize, size},          \
        {kCoreSoftness, soft}, {kCoreSaturation, sat}, {kCoreIntensity, core}, {kDistortionType, dt},  \
        {kDistortionAmount, da}, {kDistortionSpeed, ds}, {kDistortionScale, dsc},                      \
        {kFlickerIntensity, fi}, {kFlickerSpeed, fs}, {kGlowEnabled, 1.0f}
        static constexpr EffectPresetValue kDefault[] = {
            AUREA_SABER_LOOK(0.10f, 0.45f, 1.00f, 100.0f, 50.0f, 50.0f, 8.0f, 35.0f, 15.0f, 100.0f, 0.0f, 12.0f, 1.0f, 80.0f, 0.0f, 10.0f)};
        static constexpr EffectPresetValue kLightsaber[] = {
            AUREA_SABER_LOOK(0.15f, 0.55f, 1.00f, 130.0f, 45.0f, 40.0f, 10.0f, 25.0f, 10.0f, 120.0f, 0.0f, 12.0f, 1.0f, 80.0f, 10.0f, 18.0f)};
        static constexpr EffectPresetValue kNeon[] = {
            AUREA_SABER_LOOK(1.00f, 0.15f, 0.75f, 110.0f, 40.0f, 65.0f, 5.0f, 40.0f, 35.0f, 90.0f, 0.0f, 12.0f, 1.0f, 80.0f, 0.0f, 10.0f)};
        static constexpr EffectPresetValue kElectric[] = {
            AUREA_SABER_LOOK(0.45f, 0.60f, 1.00f, 140.0f, 35.0f, 30.0f, 3.0f, 20.0f, 10.0f, 130.0f, 3.0f, 14.0f, 2.0f, 60.0f, 30.0f, 24.0f)};
        static constexpr EffectPresetValue kFire[] = {
            AUREA_SABER_LOOK(1.00f, 0.38f, 0.05f, 150.0f, 55.0f, 55.0f, 9.0f, 60.0f, 40.0f, 100.0f, 4.0f, 18.0f, 1.5f, 50.0f, 15.0f, 8.0f)};
        static constexpr EffectPresetValue kPlasma[] = {
            AUREA_SABER_LOOK(0.65f, 0.20f, 1.00f, 140.0f, 50.0f, 45.0f, 7.0f, 45.0f, 30.0f, 110.0f, 2.0f, 10.0f, 1.2f, 70.0f, 0.0f, 10.0f)};
        static constexpr EffectPresetValue kEnergy[] = {
            AUREA_SABER_LOOK(0.20f, 1.00f, 0.45f, 160.0f, 60.0f, 60.0f, 6.0f, 35.0f, 20.0f, 110.0f, 1.0f, 8.0f, 1.5f, 120.0f, 10.0f, 12.0f)};
        static constexpr EffectPresetValue kSoft[] = {
            AUREA_SABER_LOOK(1.00f, 0.85f, 0.55f, 80.0f, 75.0f, 80.0f, 4.0f, 70.0f, 50.0f, 80.0f, 0.0f, 12.0f, 1.0f, 80.0f, 0.0f, 10.0f)};
        static constexpr EffectPresetValue kMagic[] = {
            AUREA_SABER_LOOK(0.55f, 0.35f, 1.00f, 120.0f, 65.0f, 70.0f, 5.0f, 55.0f, 45.0f, 100.0f, 4.0f, 26.0f, 0.8f, 110.0f, 8.0f, 6.0f)};
#undef AUREA_SABER_LOOK
        static constexpr EffectPreset kPresets[] = {
            {"saber_default", "Padrão", kDefault},
            {"saber_lightsaber", "Sabre de luz", kLightsaber},
            {"saber_neon", "Neon", kNeon},
            {"saber_electric", "Elétrico", kElectric},
            {"saber_fire", "Fogo", kFire},
            {"saber_plasma", "Plasma", kPlasma},
            {"saber_energy", "Energia", kEnergy},
            {"saber_soft", "Brilho suave", kSoft},
            {"saber_magic", "Magia", kMagic},
        };
        return kPresets;
    }

    i32 input_layer_param() const noexcept override { return static_cast<i32>(kCoreLayer); }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_saber_core_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_saber_composite_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_glow_octave_down_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_glow_octave_up_frag, work));
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        // Somar sem luz nenhuma = a camada como está.
        const bool noGlow = !e.b(kGlowEnabled) || !(finite_or(e.f(kGlowIntensity), 100.0f) > 0.0f);
        const bool noCore = !(finite_or(e.f(kCoreIntensity), 100.0f) > 0.0f);
        return std::min<u32>(e.e(kComposite), 2u) == kAdd && noGlow && noCore;
    }

    f32 input_margin(const EffectEval& e) const noexcept override {
        // Só o núcleo de camada LÊ a vizinhança da entrada (o anel do contorno
        // e o brilho de quem está logo fora do quadro).
        if (std::min<u32>(e.e(kCoreType), 2u) != kLayer) return 0.0f;
        return core_reach(e) + glow_reach(e) + distortion_reach(e);
    }

    /// Os caminhos das máscaras no instante, em trechos numa grade (dado
    /// RGBA32F, layout em saber_core.frag). `auxInfo` = caixa dos trechos já
    /// com o alcance do núcleo (px da camada).
    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = TextureHandle{};
        e.auxInfo = Vec4{};
        if (std::min<u32>(e.e(kCoreType), 2u) != kMasks || !e.layer || !e.resources) return;
        struct Seg { Vec4 ab; f32 ta, tb; u32 path; };
        struct PathInfo { f32 ws, wlen, len; bool closed; f32 iv[4]; f32 bright; };
        std::vector<Seg> segs;
        std::vector<PathInfo> paths;
        std::vector<MaskPoint> pts;
        std::vector<Vec4> edges;
        const f32 seconds = cc::seconds(e);
        const u32 seed = static_cast<u32>(std::clamp(e.value(kSeed).as_int(), 0, 1000));
        f32 s0 = clampf(e.f(kStartOffset), 0.0f, 100.0f, 0.0f) / 100.0f;
        f32 s1 = clampf(e.f(kEndOffset), 0.0f, 100.0f, 100.0f) / 100.0f;
        if (s1 < s0) std::swap(s0, s1);
        const f32 evolution = clampf(e.f(kMaskEvolution), -100000.0f, 100000.0f, 0.0f) / 100.0f;
        const f32 maskRandom = clampf(e.f(kMaskRandom), 0.0f, 100.0f, 0.0f) / 100.0f;
        const f32 flickAmount = clampf(e.f(kFlickerIntensity), 0.0f, 100.0f, 0.0f) / 100.0f;
        const f32 flickSpeed = clampf(e.f(kFlickerSpeed), 0.0f, 240.0f, 10.0f);
        const f32 flickRandom = clampf(e.f(kFlickerMaskRandom), 0.0f, 100.0f, 0.0f) / 100.0f;

        // Achatamento: tolerância dobra até caber no teto de trechos.
        for (f32 tolerance = 0.5f; tolerance < 64.0f; tolerance *= 2.0f) {
            segs.clear();
            paths.clear();
            for (const Mask& m : e.layer->masks) {
                pts.clear();
                edges.clear();
                mask::evaluate_path(m, e.time_frames(), pts);
                if (pts.size() < 2) continue;
                mask::flatten(pts, m.closed, tolerance, Vec2{0.0f, 0.0f}, edges);
                f32 total = 0.0f;
                for (const Vec4& ed : edges) total += std::hypot(ed.z - ed.x, ed.w - ed.y);
                if (edges.empty() || !(total > 1e-3f) || !std::isfinite(total)) continue;
                const u32 index = static_cast<u32>(paths.size());
                PathInfo info{};
                info.len = total;
                info.closed = m.closed;
                const f32 phase = evolution + hash01(index, seed + 77u) * maskRandom;
                info.wlen = s1 - s0;
                info.iv[0] = 2.0f; info.iv[1] = 1.0f; info.iv[2] = 2.0f; info.iv[3] = 1.0f;   // vazios
                if (info.closed) {
                    if (info.wlen >= 1.0f - 1e-6f) {
                        info.ws = 0.0f; info.wlen = 1.0f;
                        info.iv[0] = 0.0f; info.iv[1] = 1.0f;
                    } else if (info.wlen > 0.0f) {
                        const f32 a = s0 + phase - std::floor(s0 + phase);
                        const f32 b = a + info.wlen;
                        info.ws = a;
                        if (b <= 1.0f) { info.iv[0] = a; info.iv[1] = b; }
                        else { info.iv[0] = a; info.iv[1] = 1.0f; info.iv[2] = 0.0f; info.iv[3] = b - 1.0f; }
                    }
                } else {
                    info.ws = s0 + phase;
                    if (info.wlen > 0.0f) { info.iv[0] = s0 + phase; info.iv[1] = s1 + phase; }
                }
                info.bright = flicker(seconds, flickSpeed, flickAmount, hash01(index, seed + 131u) * flickRandom, seed);
                f32 run = 0.0f;
                for (const Vec4& ed : edges) {
                    const f32 len = std::hypot(ed.z - ed.x, ed.w - ed.y);
                    if (!(len > 1e-5f)) continue;
                    segs.push_back(Seg{ed, run / total, (run + len) / total, index});
                    run += len;
                }
                // O último trecho termina exatamente em 1 (a soma em float pode faltar um fio).
                if (!segs.empty() && segs.back().path == index) segs.back().tb = 1.0f;
                paths.push_back(info);
            }
            if (segs.size() <= kMaxSegments) break;
        }
        if (segs.empty() || segs.size() > kMaxSegments) return;

        // Grade: caixa dos trechos + alcance do núcleo (com o desvio da distorção).
        const f32 reach = core_reach(e) + distortion_reach(e);
        f32 minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (const Seg& s : segs) {
            minX = std::min({minX, s.ab.x, s.ab.z}); maxX = std::max({maxX, s.ab.x, s.ab.z});
            minY = std::min({minY, s.ab.y, s.ab.w}); maxY = std::max({maxY, s.ab.y, s.ab.w});
        }
        minX -= reach; minY -= reach; maxX += reach; maxY += reach;
        const f32 bw = std::max(maxX - minX, 1.0f), bh = std::max(maxY - minY, 1.0f);
        const f32 target = std::max(reach * 1.5f, std::max(bw, bh) / 48.0f);
        const u32 gw = std::clamp(static_cast<u32>(std::ceil(bw / target)), 1u, 64u);
        const u32 gh = std::clamp(static_cast<u32>(std::ceil(bh / target)), 1u, 64u);
        const f32 cw = bw / static_cast<f32>(gw), ch = bh / static_cast<f32>(gh);
        std::vector<std::vector<u32>> cells(static_cast<usize>(gw) * gh);
        usize totalIdx = 0;
        for (u32 i = 0; i < segs.size() && totalIdx < kMaxIndices; ++i) {
            const Seg& s = segs[i];
            const f32 x0 = std::min(s.ab.x, s.ab.z) - reach - minX, x1 = std::max(s.ab.x, s.ab.z) + reach - minX;
            const f32 y0 = std::min(s.ab.y, s.ab.w) - reach - minY, y1 = std::max(s.ab.y, s.ab.w) + reach - minY;
            const u32 cx0 = std::min(gw - 1, static_cast<u32>(std::max(0.0f, x0 / cw)));
            const u32 cx1 = std::min(gw - 1, static_cast<u32>(std::max(0.0f, x1 / cw)));
            const u32 cy0 = std::min(gh - 1, static_cast<u32>(std::max(0.0f, y0 / ch)));
            const u32 cy1 = std::min(gh - 1, static_cast<u32>(std::max(0.0f, y1 / ch)));
            for (u32 cy = cy0; cy <= cy1; ++cy)
                for (u32 cx = cx0; cx <= cx1 && totalIdx < kMaxIndices; ++cx) { cells[cy * gw + cx].push_back(i); ++totalIdx; }
        }

        const u32 offSeg = 3u;
        const u32 offPath = offSeg + 2u * static_cast<u32>(segs.size());
        const u32 offCell = offPath + 3u * static_cast<u32>(paths.size());
        const u32 offIdx = offCell + gw * gh;
        const u32 total = offIdx + static_cast<u32>((totalIdx + 3) / 4);
        const u32 height = (total + kDataWidth - 1) / kDataWidth;
        std::vector<Vec4> data(static_cast<usize>(height) * kDataWidth, Vec4{});
        data[0] = Vec4{static_cast<f32>(segs.size()), static_cast<f32>(paths.size()), static_cast<f32>(gw), static_cast<f32>(gh)};
        data[1] = Vec4{minX, minY, cw, ch};
        data[2] = Vec4{static_cast<f32>(offSeg), static_cast<f32>(offPath), static_cast<f32>(offCell), static_cast<f32>(offIdx)};
        for (usize i = 0; i < segs.size(); ++i) {
            data[offSeg + 2 * i] = segs[i].ab;
            data[offSeg + 2 * i + 1] = Vec4{segs[i].ta, segs[i].tb, static_cast<f32>(segs[i].path), 0.0f};
        }
        for (usize j = 0; j < paths.size(); ++j) {
            const PathInfo& pi = paths[j];
            data[offPath + 3 * j] = Vec4{pi.ws, pi.wlen, pi.len, pi.closed ? 1.0f : 0.0f};
            data[offPath + 3 * j + 1] = Vec4{pi.iv[0], pi.iv[1], pi.iv[2], pi.iv[3]};
            data[offPath + 3 * j + 2] = Vec4{pi.bright, 0.0f, 0.0f, 0.0f};
        }
        u32 cursor = 0;
        for (usize c = 0; c < cells.size(); ++c) {
            data[offCell + c] = Vec4{static_cast<f32>(cursor), static_cast<f32>(cells[c].size()), 0.0f, 0.0f};
            for (const u32 si : cells[c]) {
                Vec4& slot = data[offIdx + cursor / 4];
                (&slot.x)[cursor % 4] = static_cast<f32>(si);
                ++cursor;
            }
        }
        u64 key = 1469598103934665603ull ^ 0x534142455231ull;
        for (const Vec4& v : data) {
            u32 bits[4];
            std::memcpy(bits, &v, sizeof bits);
            for (const u32 b : bits) key = (key ^ b) * 1099511628211ull;
        }
        e.aux = e.resources->data_texture(key, data.data(), kDataWidth, height);
        if (e.aux.valid()) e.auxInfo = Vec4{minX, minY, bw, bh};
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        u32 type = std::min<u32>(e.e(kCoreType), 2u);
        const u32 composite = std::min<u32>(e.e(kComposite), 2u);
        const Rect box = layer_box(e, input);
        const f32 coreR = core_radius(e);
        const f32 reachCore = core_reach(e);
        const f32 distReach = distortion_reach(e);
        const bool glowOn = e.b(kGlowEnabled) && finite_or(e.f(kGlowIntensity), 100.0f) > 0.0f;
        const f32 reachGlow = glowOn ? glow_reach(e) : 0.0f;

        // Outra camada como núcleo (só no tipo camada): ausente agora = sem núcleo.
        const u64 ref = type == kLayer ? e.value(kCoreLayer).ref : 0;
        const LayerImage* map = ref ? ctx.layer_input(ref) : nullptr;
        if (ref && !map) type = kNone;

        // 1. Onde o núcleo pode estar (px da camada).
        Rect coreBox = input.region;
        bool hasCore = true;
        if (type == kSingle) {
            const Vec2 a = layer_point(e, input, kCoreStart), b = layer_point(e, input, kCoreEnd);
            coreBox = Rect{std::min(a.x, b.x) - reachCore, std::min(a.y, b.y) - reachCore,
                           std::fabs(b.x - a.x) + 2.0f * reachCore, std::fabs(b.y - a.y) + 2.0f * reachCore};
        } else if (type == kMasks) {
            hasCore = e.aux.valid() && e.auxInfo.z > 0.0f && e.auxInfo.w > 0.0f;
            if (hasCore) coreBox = Rect{e.auxInfo.x, e.auxInfo.y, e.auxInfo.z, e.auxInfo.w};
            else type = kNone;
        } else if (type == kLayer && map) {
            coreBox = e.placement && e.placement->compWidth ? visible_layer_rect(*e.placement) : input.region;
            if (coreBox.empty()) coreBox = input.region;
        } else if (type == kNone) {
            hasCore = false;
        }
        const f32 grow = reachGlow + distReach * 2.5f + 2.0f;
        Rect want = hasCore ? Rect{coreBox.x - grow, coreBox.y - grow, coreBox.w + 2.0f * grow, coreBox.h + 2.0f * grow}
                            : input.region;
        if (composite == kAdd) want = Rect::unite(want, input.region);
        if (composite == kOverBlack) want = Rect::unite(want, box);
        if (!hasCore && composite == kTransparent) want = input.region;
        const Rect region = grown_region(e, input.region, want, margin);
        const f32 k = std::max(input.texel_scale_x(), 1e-4f);
        u32 w = 0, h = 0;
        ctx.region_size(region, k, w, h);
        if (!w || !h) { out = input; return OkStatus; }

        // 2. Uniforms (os mesmos nos dois passes).
        SaberUniforms u{};
        u.dst = Vec4{region.x, region.y, region.w, region.h};
        u.src = Vec4{input.region.x, input.region.y, input.region.w, input.region.h};
        u.box = Vec4{box.x, box.y, box.w, box.h};
        const f32 texel = 0.5f * (region.w / static_cast<f32>(w) + region.h / static_cast<f32>(h));
        const f32 secs = cc::seconds(e);
        const u32 seed = static_cast<u32>(std::clamp(e.value(kSeed).as_int(), 0, 1000));
        const f32 flick = type == kMasks ? 1.0f
            : flicker(secs, clampf(e.f(kFlickerSpeed), 0.0f, 240.0f, 10.0f),
                      clampf(e.f(kFlickerIntensity), 0.0f, 100.0f, 0.0f) / 100.0f, 0.0f, seed);
        u.info = Vec4{std::max(texel, 1e-4f), static_cast<f32>(type), e.b(kUseAlpha) ? 1.0f : 0.0f, flick};
        if (type == kSingle) {
            const Vec2 a = layer_point(e, input, kCoreStart), b = layer_point(e, input, kCoreEnd);
            u.seg = Vec4{a.x, a.y, b.x, b.y};
        }
        f32 s0 = clampf(e.f(kStartOffset), 0.0f, 100.0f, 0.0f) / 100.0f;
        f32 s1 = clampf(e.f(kEndOffset), 0.0f, 100.0f, 100.0f) / 100.0f;
        if (s1 < s0) std::swap(s0, s1);
        u.core = Vec4{coreR, clampf(e.f(kCoreSoftness), 0.0f, 100.0f, 35.0f) / 100.0f,
                      clampf(e.f(kStartSize), 0.0f, 1000.0f, 100.0f) / 100.0f, clampf(e.f(kEndSize), 0.0f, 1000.0f, 100.0f) / 100.0f};
        u.window = Vec4{s0, s1, clampf(e.f(kOffsetBrightness), 0.0f, 500.0f, 0.0f) / 100.0f, map ? 1.0f : 0.0f};
        u.caps = Vec4{clampf(e.f(kStartRoundness), 0.0f, 100.0f, 100.0f) / 100.0f,
                      clampf(e.f(kEndRoundness), 0.0f, 100.0f, 100.0f) / 100.0f, 0.0f, 0.0f};
        const u32 distType = std::min<u32>(e.e(kDistortionType), 4u);
        u.dist = Vec4{static_cast<f32>(distType), distType ? clampf(e.f(kDistortionAmount), 0.0f, 2000.0f, 12.0f) : 0.0f,
                      clampf(e.f(kDistortionScale), 1.0f, 10000.0f, 80.0f),
                      secs * clampf(e.f(kDistortionSpeed), 0.0f, 100.0f, 1.0f)};
        u.misc = Vec4{static_cast<f32>(seed), 0.0f, 0.0f, 0.0f};
        if (e.placement) {
            const Mat4& m = e.placement->compFromLayer;
            u.affA = Vec4{m.col[0].x, m.col[0].y, m.col[1].x, m.col[1].y};
            u.affB = Vec4{m.col[3].x, m.col[3].y, 0.0f, 0.0f};
        } else {
            u.affA = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
        }
        if (map) u.mapRegion = Vec4{map->region.x, map->region.y, std::max(map->region.w, 1e-3f), std::max(map->region.h, 1e-3f)};
        const Vec4 glowCol = premultiplied_linear(e.color(kGlowColor));
        u.glowColor = Vec4{glowCol.x, glowCol.y, glowCol.z, clampf(e.f(kGlowIntensity), 0.0f, 2000.0f, 100.0f) / 100.0f * kGlowGain};
        const Vec4 coreCol = premultiplied_linear(e.color(kCoreColor));
        u.coreColor = Vec4{coreCol.x, coreCol.y, coreCol.z, clampf(e.f(kCoreSaturation), 0.0f, 100.0f, 15.0f) / 100.0f};
        u.look = Vec4{static_cast<f32>(composite), glowOn ? 1.0f : 0.0f,
                      clampf(e.f(kCoreIntensity), 0.0f, 2000.0f, 100.0f) / 100.0f * kCoreGain, 0.0f};

        // 3. Núcleo.
        const FGTexture coreTex = ctx.texture("saber-nucleo", w, h);
        const PassTexture data = e.aux.valid() && type == kMasks
            ? PassTexture{{}, e.aux, CommonSampler::NearestClamp}
            : PassTexture{input.texture, {}, CommonSampler::NearestClamp};
        if (ctx.fullscreen_pass("saber-nucleo", PassStage::Effects, coreTex, ShaderId::effects_saber_core_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}, data,
                                 PassTexture{map ? map->texture : input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;

        // 4. Brilho: pirâmide de oitavas da luz do núcleo.
        FGTexture glowTex = coreTex;
        if (glowOn && hasCore) {
            const f32 dens = std::min(k, 1.0f);
            const f32 d0 = dens * 0.5f;
            const f32 first = std::max(0.0f, -std::log2(dens));   // oitava absoluta do nível 0
            const f32 walk = glow_walk(e);
            const f32 bias = clampf(e.f(kGlowBias), 0.0f, 100.0f, 50.0f) / 100.0f;
            const f32 beta = -0.75f + 1.3f * bias;
            auto weight = [&](f32 o) { return std::exp2(beta * o) * std::clamp(walk - o, 0.0f, 1.0f); };
            u32 levels = 1;
            while (levels < kOctaves && first + static_cast<f32>(levels) < static_cast<f32>(kOctaves) &&
                   weight(first + static_cast<f32>(levels)) > 0.0f)
                ++levels;
            // As oitavas finas que esta densidade não tem dobram no nível 0.
            f32 w0 = 0.0f;
            for (u32 o = 0; static_cast<f32>(o) <= first + 0.5f; ++o) w0 += weight(static_cast<f32>(o));
            struct Level { FGTexture tex; u32 w = 0, h = 0; };
            std::array<Level, kOctaves> pyr{};
            const Vec4 same = EffectBuildContext::uv_map(region, region);
            FGTexture prev = coreTex;
            u32 prevW = w, prevH = h;
            for (u32 j = 0; j < levels; ++j) {
                ctx.region_size(region, d0 / static_cast<f32>(1u << j), pyr[j].w, pyr[j].h);
                pyr[j].w = std::max(1u, pyr[j].w);
                pyr[j].h = std::max(1u, pyr[j].h);
                pyr[j].tex = ctx.texture("saber-oitava", pyr[j].w, pyr[j].h);
                EffectUniforms du;
                du.uvMap = same;
                du.texel = Vec4{1.0f / static_cast<f32>(prevW), 1.0f / static_cast<f32>(prevH), 0.0f, 0.0f};
                if (ctx.fullscreen_pass("saber-oitava-desce", PassStage::Effects, pyr[j].tex,
                                        ShaderId::effects_glow_octave_down_frag,
                                        {PassTexture{prev, {}, CommonSampler::LinearBorder}}, &du, sizeof(du)) == kInvalidIndex)
                    return Errc::PipelineCompileFailed;
                prev = pyr[j].tex; prevW = pyr[j].w; prevH = pyr[j].h;
            }
            FGTexture acc = pyr[levels - 1].tex;
            for (u32 j = levels - 1; j-- > 0;) {
                const FGTexture next = ctx.texture("saber-oitava-soma", pyr[j].w, pyr[j].h);
                EffectUniforms uu;
                uu.uvMap = same;
                uu.texel = Vec4{1.0f / static_cast<f32>(pyr[j + 1].w), 1.0f / static_cast<f32>(pyr[j + 1].h), 0.0f, 0.0f};
                const f32 below = j == levels - 2 ? weight(first + static_cast<f32>(levels - 1)) : 1.0f;
                uu.p0 = Vec4{below, j == 0 ? w0 : weight(first + static_cast<f32>(j)), 0.0f, 0.0f};
                if (ctx.fullscreen_pass("saber-oitava-sobe", PassStage::Effects, next, ShaderId::effects_glow_octave_up_frag,
                                        {PassTexture{acc, {}, CommonSampler::LinearBorder},
                                         PassTexture{pyr[j].tex, {}, CommonSampler::LinearClamp}}, &uu, sizeof(uu)) == kInvalidIndex)
                    return Errc::PipelineCompileFailed;
                acc = next;
            }
            if (levels == 1) u.glowColor.w *= w0;
            glowTex = acc;
        } else {
            u.look.y = 0.0f;
        }

        // 5. Luz sobre a camada.
        out = LayerImage{ctx.texture("saber", w, h), region, w, h};
        if (ctx.fullscreen_pass("saber", PassStage::Effects, out.texture, ShaderId::effects_saber_composite_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder},
                                 PassTexture{coreTex, {}, CommonSampler::LinearClamp},
                                 PassTexture{glowTex, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }

private:
    static f32 core_radius(const EffectEval& e) noexcept {
        return 0.5f * clampf(e.f(kCoreSize), 0.0f, 2000.0f, 8.0f);
    }
    /// Até onde o núcleo vai além do caminho (px): raio × maior tamanho × suavidade.
    static f32 core_reach(const EffectEval& e) noexcept {
        const f32 size = std::max(clampf(e.f(kStartSize), 0.0f, 1000.0f, 100.0f), clampf(e.f(kEndSize), 0.0f, 1000.0f, 100.0f)) / 100.0f;
        const f32 soft = clampf(e.f(kCoreSoftness), 0.0f, 100.0f, 35.0f) / 100.0f;
        return core_radius(e) * std::max(size, 1.0f) * (1.0f + soft) + 3.0f;
    }
    static f32 distortion_reach(const EffectEval& e) noexcept {
        if (std::min<u32>(e.e(kDistortionType), 4u) == 0u) return 0.0f;
        return 1.5f * clampf(e.f(kDistortionAmount), 0.0f, 2000.0f, 12.0f);
    }
    /// Espalhamento → quantas oitavas acendem (contínuo, 1..8).
    static f32 glow_walk(const EffectEval& e) noexcept {
        const f32 spread = clampf(e.f(kGlowSpread), 0.0f, 200.0f, 50.0f) / 100.0f;
        return std::clamp(1.5f + 5.0f * spread, 1.0f, static_cast<f32>(kOctaves));
    }
    /// Alcance do brilho (px): a oitava mais larga acesa, como o Brilho.
    static f32 glow_reach(const EffectEval& e) noexcept {
        const f32 coarsest = std::min(static_cast<f32>(kOctaves - 1), std::ceil(glow_walk(e)) - 1.0f);
        return 8.0f * std::exp2(coarsest) + 4.0f;
    }
};

} // namespace

void register_saber_effect(EffectRegistry& r) { (void)r.add(std::make_unique<Saber>()); }

} // namespace aurea::builtin
