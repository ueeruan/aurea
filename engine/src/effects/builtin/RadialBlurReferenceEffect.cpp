// Desfoque radial (referência do AE): reconstrução independente do
// comportamento observado no Radial Blur do After Effects (ADBE Radial Blur).
// Evidência em build/reference/effects-20261115/batch-a/radial-blur.md.
//
// O que o efeito faz, medido contra os quadros do AE:
//  - o centro vai para a grade [0, largura-2] x [0, altura-2] em 16.16;
//  - cada pixel escolhe o número de amostras pelo raio (quartos de pixel) e
//    pelo valor inteiro do controle, entre 4 e o teto da qualidade
//    (rascunho 4, baixa 16, alta 255);
//  - as amostras cobrem a faixa inteira de ângulo (giro) ou de escala (zoom)
//    em 1024 passos, começando numa fase pseudoaleatória por pixel que segue
//    a sequência do "Random Seed" na ordem de varredura da camada;
//  - cada amostra é bilinear com frações de 1/32 e média simples.
// A sequência de fases depende só da semente e do tamanho da camada: ela é
// calculada uma vez na CPU e viaja como textura de dados (8 pixels por texel).
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

namespace aurea::builtin {
namespace {

constexpr const char* kRadialBlurReference = "aurea.blur.radial_blur_reference";
constexpr u32 kPhasesPerTexel = 8;

f64 finite_value(f32 value, f64 fallback) noexcept {
    return std::isfinite(value) ? static_cast<f64>(value) : fallback;
}

u32 seed_of(const EffectEval& e) noexcept {
    return static_cast<u32>(std::clamp(finite_value(e.f(4), 0), 0., 10000.));
}

/// Um passo do gerador inteiro de 32 bits observado no efeito (multiplicador
/// 48271, quociente 44488). Zero vira 0x7fffffff.
u32 radial_step(u32 state) noexcept {
    const u32 next = state * 48271u + (state / 44488u) * 0x80001a8fu;
    return next ? next : 0x7fffffffu;
}

struct PhaseTable {
    u32 seed = 0, width = 0, height = 0;
    std::shared_ptr<const std::vector<Vec4>> texels;
};

/// Fases (10 bits) de todos os pixels, em ordem de varredura, depois dos 32
/// passos que sorteiam a tabela de jitter. Dois valores por canal (exatos em
/// float: < 2^20), oito pixels por texel.
std::shared_ptr<const std::vector<Vec4>> phase_texels(u32 seed, u32 width, u32 height) {
    static std::mutex mutex;
    static std::vector<PhaseTable> cache;
    std::lock_guard<std::mutex> lock(mutex);
    for (const PhaseTable& t : cache)
        if (t.seed == seed && t.width == width && t.height == height) return t.texels;
    const u32 columns = (width + kPhasesPerTexel - 1) / kPhasesPerTexel;
    auto texels = std::make_shared<std::vector<Vec4>>(static_cast<usize>(columns) * height, Vec4{});
    u32 state = seed;
    for (u32 i = 0; i < 32; ++i) state = radial_step(state);
    for (u32 y = 0; y < height; ++y) {
        Vec4* row = texels->data() + static_cast<usize>(y) * columns;
        for (u32 x = 0; x < width; ++x) {
            state = radial_step(state);
            const u32 phase = static_cast<u32>(static_cast<i32>(state) >> 7) & 1023u;
            Vec4& t = row[x / kPhasesPerTexel];
            f32* channel = &t.x + (x % kPhasesPerTexel) / 2;
            *channel += static_cast<f32>(x % 2 ? phase * 1024u : phase);
        }
    }
    if (cache.size() >= 2) cache.erase(cache.begin());
    cache.push_back({seed, width, height, texels});
    return texels;
}

struct RadialBlurUniforms {
    std::array<i32, 4> world{};    ///< largura, altura, centro x e y (16.16 na grade W-2/H-2)
    std::array<i32, 4> control{};  ///< tipo (0 giro, 1 zoom), valor inteiro, teto de amostras, deslocamento do zoom
    std::array<i32, 4> zoom{};     ///< escala 16.16 na ponta -1/2 e +1/2, semente, textura de fases válida
    std::array<u32, 4> jitter{};   ///< 32 valores de 4 bits
    Vec4 raster{};                 ///< origem da região (px da camada) e texels por px
};
static_assert(sizeof(RadialBlurUniforms) == 80);

class RadialBlurReference final : public Effect {
public:
    enum : u32 { kAmount = 0, kCenter, kType, kAntialias, kSeed };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{kRadialBlurReference, "Desfoque radial", "Desfoque", EffectClass::Neighborhood};
        return info;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("amount", "Quantidade", 10, 0, 100, kParamAnimatable);
        p.typed_range(0, 1000);
        p.add_point2("center", "Centro", {.5f, .5f}, -1, 2, kParamAnimatable | kParamRelative);
        p.typed_range(-10, 10);
        static const char* const types[] = {"Giro", "Zoom"};
        p.add_enum("type", "Tipo", types, 2, 0);
        static const char* const antialias[] = {"Baixa", "Alta"};
        p.add_enum("antialias", "Antisserrilhado", antialias, 2, 0);
        p.add_int("seed", "Semente aleatória", 0, 0, 10000);
    }

    bool demo_values(EffectInstance&, std::vector<ParamValue>& values) const noexcept override {
        values[kAmount] = ParamValue::scalar(30);
        values[kType] = ParamValue::scalar(1);
        return true;
    }

    /// O efeito usa o valor INTEIRO do controle no ângulo/escala: abaixo de 1
    /// cada amostra cai no próprio pixel.
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::clamp(finite_value(e.f(kAmount), 0), 0., 1000.) < 1;
    }

    bool needs_full_input() const noexcept override { return true; }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_radial_blur_reference_frag, format));
    }

    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = {};
        if (!e.resources || !e.placement) return;
        const u32 width = e.placement->layerWidth, height = e.placement->layerHeight;
        const u32 columns = (width + kPhasesPerTexel - 1) / kPhasesPerTexel;
        if (!width || !height || columns > 4096 || height > 4096) return;
        const u32 seed = seed_of(e);
        // Sem exceções no motor (-fno-exceptions): a tabela tem no máximo
        // 512 × 4096 texels (8 MiB), limitada pelo teste acima.
        const auto texels = phase_texels(seed, width, height);
        const u64 key = 0x5244424c52000000ull ^ (static_cast<u64>(seed) << 40) ^
                        (static_cast<u64>(width) << 20) ^ height;
        e.aux = e.resources->data_texture(key, texels->data(), columns, height);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                 f32, LayerImage& out) const override {
        const f64 densityX = input.texel_scale_x(), densityY = input.texel_scale_y();
        const i64 width = e.placement && e.placement->layerWidth ? e.placement->layerWidth
                                                                 : static_cast<i64>(std::lround(input.region.w));
        const i64 height = e.placement && e.placement->layerHeight ? e.placement->layerHeight
                                                                   : static_cast<i64>(std::lround(input.region.h));
        if (width < 3 || height < 3 || width > 32000 || height > 32000) {
            out = input;
            return OkStatus;
        }
        const f64 amount = std::clamp(finite_value(e.f(kAmount), 10), 0., 1000.);
        const i64 amountFixed = std::llround(amount * 65536.);
        const Vec2 center = e.p2(kCenter);
        const auto fixed_center = [](f64 relative, i64 size) {
            const i64 point = std::llround(std::clamp(relative * static_cast<f64>(size), -1e6, 1e6) * 65536.);
            const i64 fraction = point / size;                       // 16.16 / inteiro (C trunca)
            return static_cast<i32>((fraction * ((size - 2) << 16)) >> 16);   // FixMul
        };
        RadialBlurUniforms u;
        u.world = {static_cast<i32>(width), static_cast<i32>(height),
                   fixed_center(finite_value(center.x, .5), width), fixed_center(finite_value(center.y, .5), height)};
        const u32 quality = std::min(e.e(kAntialias), 1u) + 1;   // 1 baixa, 2 alta
        // Rascunho do AE (4 amostras) só quando o preview adaptativo baixa muito.
        const bool draft = ctx.resources().effect_quality() <= .5f;
        const i32 maxSamples = draft ? 4 : (quality == 1 ? 16 : 255);
        const i32 shift = !draft && quality == 2 ? 2 : 3;
        u.control = {e.e(kType) == 1 ? 1 : 0, static_cast<i32>(amountFixed >> 16), maxSamples, shift};
        const i64 zoomStep = amountFixed / 200;
        const auto fix_mul = [](i64 a, i64 b) { return (a * b) >> 16; };
        u.zoom = {static_cast<i32>(65536 + fix_mul(-32768, zoomStep)), static_cast<i32>(65536 + fix_mul(32768, zoomStep)),
                  static_cast<i32>(seed_of(e)), e.aux.valid() ? 1 : 0};
        u32 state = static_cast<u32>(u.zoom[2]);
        for (u32 i = 0; i < 32; ++i) {
            state = radial_step(state);
            const u32 value = (static_cast<u32>(static_cast<i32>(state) >> 7) >> 11) & 15u;
            u.jitter[i / 8] |= value << ((i % 8) * 4);
        }
        u.raster = {input.region.x, input.region.y, static_cast<f32>(densityX), static_cast<f32>(densityY)};
        out = input;
        out.texture = ctx.texture("radial-blur-reference", input.width, input.height);
        PassTexture phases = e.aux.valid() ? PassTexture{{}, e.aux, CommonSampler::NearestClamp}
                                           : PassTexture{input.texture, {}, CommonSampler::NearestClamp};
        if (ctx.fullscreen_pass("radial-blur-reference", PassStage::Effects, out.texture,
                ShaderId::effects_radial_blur_reference_frag,
                {{input.texture, {}, CommonSampler::NearestClamp}, phases}, &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

} // namespace

void register_radial_blur_reference(EffectRegistry& registry) {
    (void)registry.add(std::make_unique<RadialBlurReference>());
}

} // namespace aurea::builtin
