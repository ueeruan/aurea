// =============================================================================
//  Ladrilhos que se mexem (0.4.5, pedido de usuários): Tile Shift,
//  Hexagon Tile Rotate e Hexagon Tile Shift.
//
//  Referência de COMPORTAMENTO: os efeitos de mesmo nome do Alight Motion
//  (guia público: "divide a camada numa grade de ladrilhos — quadrados ou
//  hexagonais — e desloca/gira o conteúdo de cada ladrilho de forma
//  independente"). O guia não publica os parâmetros; os daqui são nossos:
//  tamanho, quantidade, padrão (aleatório ou fileiras/colunas alternadas),
//  aleatoriedade, evolução, semente, centro da grade, bordas e mistura. A
//  matemática e o shader (shaders/effects/tile_shift.frag) são nossos.
//
//  Um passe de tela cheia, UMA amostra por pixel: o custo não cresce com os
//  valores. A saída fica na região da entrada (o conteúdo anda dentro dela).
//  Os índices de parâmetro ficam fixos nos projetos salvos (acrescentar só no
//  fim).
// =============================================================================
#include "CCCommon.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

using namespace cc;

/// As três variações dividem parâmetros e shader: a grade (quadrada ou
/// hexagonal) e o que acontece em cada célula (deslocar ou girar).
struct TileShiftKind {
    const char* key;
    const char* name;
    bool hex;
    bool rotate;
};

constexpr TileShiftKind kSquareShift{"aurea.distort.tile_shift", "Tile Shift", false, false};
constexpr TileShiftKind kHexRotate{"aurea.distort.hex_tile_rotate", "Hexagon Tile Rotate", true, true};
constexpr TileShiftKind kHexShift{"aurea.distort.hex_tile_shift", "Hexagon Tile Shift", true, false};

class TileShift final : public Effect {
public:
    enum : u32 { kSize = 0, kAmount, kPattern, kRandomness, kEvolution, kSeed, kCenter, kEdges, kBlend };

    explicit TileShift(const TileShiftKind& kind) noexcept
        : kind_(kind), info_{kind.key, kind.name, "Distorcer", EffectClass::Domain} {}

    const EffectInfo& info() const noexcept override { return info_; }
    void declare_parameters(ParameterRegistry& p) const override {
        // Tamanho em % do lado menor da camada: o mesmo mosaico na prévia e no
        // export, em qualquer resolução.
        p.add_float("size", kind_.hex ? "Tamanho da célula" : "Tamanho do ladrilho", 20.0f, 1.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.5f, 1000.0f);
        if (kind_.rotate) {
            p.add_angle("rotation", "Rotação", 45.0f);
        } else {
            // % do tamanho da célula; acima de 100% o conteúdo dá a volta.
            p.add_float("amount", "Deslocamento", 35.0f, -100.0f, 100.0f, kAnimPct, "%");
            p.typed_range(-1000.0f, 1000.0f);
        }
        static const char* const kPatterns[] = {"Aleatório", "Fileiras alternadas", "Colunas alternadas"};
        p.add_enum("pattern", "Padrão", kPatterns, 3, 0);
        p.add_float("randomness", "Aleatoriedade", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        // 360° = um sorteio novo inteiro; entre um e outro, a mudança é suave.
        p.add_angle("evolution", "Evolução", 0.0f);
        p.add_int("seed", "Semente", 1, 0, 9999);
        p.add_point2("grid_center", "Centro da grade", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        static const char* const kEdgeModes[] = {"Repetir dentro da célula", "Imagem contínua"};
        p.add_enum("edges", "Bordas da célula", kEdgeModes, 2, 0);
        p.add_float("blend", "Mistura com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.set_tip(kRandomness, kind_.rotate
            ? "0%: todas as células giram igual. 100%: cada uma gira um valor sorteado."
            : "0%: todas as células andam a mesma distância. 100%: cada uma anda uma distância sorteada.");
        p.add_section("grid", "Grade", {kSeed, kCenter, kEdges}, kInvalidIndex, false);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return finite_or(e.f(kBlend), 0.0f) >= 99.999f || std::fabs(finite_or(e.f(kAmount), 0.0f)) < 1e-4f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kSize] = ParamValue::scalar(25.0f);
        v[kAmount] = ParamValue::scalar(kind_.rotate ? 60.0f : 45.0f);
        v[kRandomness] = ParamValue::scalar(30.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_tile_shift_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        // Célula menor que ~2 texels vira ruído: o tamanho para lá.
        const f32 minCell = 2.0f / std::max(in.texel_scale_x(), 1e-3f);
        const f32 size = std::max(clampf(e.f(kSize), 0.5f, 1000.0f, 20.0f) / 100.0f * min_side(box), minCell);
        const Vec2 c = layer_point(e, in, kCenter);
        const f32 amount = kind_.rotate ? clampf(e.f(kAmount), -36000.0f, 36000.0f, 45.0f) * kDeg
                                        : clampf(e.f(kAmount), -1000.0f, 1000.0f, 35.0f) / 100.0f * size;
        Uniforms u{};
        u.p0 = Vec4{c.x, c.y, size, kind_.hex ? 1.0f : 0.0f};
        u.p1 = Vec4{amount, clampf(e.f(kRandomness), 0.0f, 100.0f, 50.0f) / 100.0f,
                    static_cast<f32>(std::fmod(static_cast<f64>(finite_or(e.f(kEvolution), 0.0f)) / 360.0, 4096.0)),
                    static_cast<f32>(std::min<u32>(e.e(kSeed), 9999u))};
        u.p2 = Vec4{kind_.rotate ? 1.0f : 0.0f, static_cast<f32>(std::min(e.e(kPattern), 2u)),
                    static_cast<f32>(std::min(e.e(kEdges), 1u)), clampf(e.f(kBlend), 0.0f, 100.0f, 0.0f) / 100.0f};
        return run_pass(ctx, e, ShaderId::effects_tile_shift_frag, in, in.region, u,
                        kind_.rotate ? "colmeia-girar" : "ladrilhos-deslocar", out);
    }

private:
    TileShiftKind kind_;
    EffectInfo info_;
};

} // namespace

void register_tile_shift_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<TileShift>(kSquareShift));
    (void)r.add(std::make_unique<TileShift>(kHexRotate));
    (void)r.add(std::make_unique<TileShift>(kHexShift));
}

} // namespace aurea::builtin
