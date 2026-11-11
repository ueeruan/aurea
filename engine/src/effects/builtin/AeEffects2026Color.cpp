// =============================================================================
//  Aurea / effects / builtin / AeEffects2026Color.cpp
//
//  Lote "AE 2026", cor e canais: Unmult, Manter cor, Mudar cor, Mudar para
//  cor, Filtro de foto, Preto e branco, Cor seletiva, Níveis do alfa, Remover
//  fosco de cor, Aritmética, Faixa de cor, Luz estroboscópica, Ruído HLS (e o
//  automático) e Ruído alfa — um shader só (`ae_color.frag`, operação em
//  p[11].w) — e os dois que leem OUTRA camada: Mesclar e Fosco por diferença
//  (`ae_two_layer.frag`).
//
//  Parâmetros: os do efeito equivalente do After Effects (documentação
//  pública), com rótulos em pt-BR; a ordem é contrato (só acrescentar no fim).
// =============================================================================
#include "AeEffects2026.hpp"

namespace aurea::builtin::ae26 {
namespace {

constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
constexpr u16 kAnimPx = kParamAnimatable | kParamPixels;

/// Operações de ae_color.frag (contrato com o shader).
enum class AeOp : u32 {
    Unmult = 0, LeaveColor, ChangeColor, ChangeToColor, PhotoFilter, BlackWhite, SelectiveColor,
    AlphaLevels, RemoveMatting, Arithmetic, ColorRange, Strobe, NoiseHls, NoiseAlpha,
};

/// Base dos efeitos de `ae_color.frag`: um passe na região da entrada.
class AeColorEffect : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_color_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        AeUniforms u = ae_uniforms(e, input);
        fill(e, input, u);
        u.p[11].w = static_cast<f32>(op());
        return ae_pass(ctx, input, u, ShaderId::effects_ae_color_frag, "ae-cor", CommonSampler::LinearClamp, out);
    }

protected:
    [[nodiscard]] virtual AeOp op() const noexcept = 0;
    virtual void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept = 0;
};

// --- Unmult ----------------------------------------------------------------------
// A luz sobre fundo preto (ou branco) vira transparência: o alfa sai do canal
// mais forte e a cor é desmultiplicada, de modo que a camada sobre o mesmo
// fundo volta a ser a imagem original (a conta é em linear, como a composição).
class Unmult final : public AeColorEffect {
public:
    enum : u32 { kBackground = 0, kBlackLevel, kWhiteLevel, kSoftness, kRemoveMatting, kClipHdr };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.key.unmult", "Unmult", "Recorte", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kBg[] = {"Preto", "Branco"};
        p.add_enum("background", "Cor do fundo", kBg, 2, 0);
        p.add_float("black_level", "Nível do preto", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("white_level", "Nível do branco", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("softness", "Suavidade", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("remove_matting", "Remover fosco de cor", true);
        p.add_bool("clip_hdr", "Recortar resultados HDR", true);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kBlackLevel] = ParamValue::scalar(25.0f);
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::Unmult; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        const f32 black = pct(e, kBlackLevel), white = std::max(pct(e, kWhiteLevel), black + 1e-3f);
        u.p[0] = Vec4{en(e, kBackground, 2), black, white, pct(e, kSoftness)};
        u.p[1] = Vec4{flag(e.b(kRemoveMatting)), flag(e.b(kClipHdr)), 0.0f, 0.0f};
    }
};

// --- Manter cor --------------------------------------------------------------------
class LeaveColor final : public AeColorEffect {
public:
    enum : u32 { kAmount = 0, kColor, kTolerance, kSoftness, kMatch };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.leave_color", "Manter cor", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("amount", "Quantidade de descoloração", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_color("color", "Cor a manter", Vec4{0.8f, 0.03f, 0.03f, 1.0f});
        p.add_float("tolerance", "Tolerância", 15.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("softness", "Suavidade da borda", 10.0f, 0.0f, 100.0f, kAnimPct, "%");
        static const char* const kMatch[] = {"Usando RGB", "Usando matiz"};
        p.add_enum("match", "Comparar cores", kMatch, 2, 1);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(fin(e.f(kAmount), 0.0f) > 0.01f); }

protected:
    AeOp op() const noexcept override { return AeOp::LeaveColor; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{pct(e, kAmount), pct(e, kTolerance), pct(e, kSoftness), en(e, kMatch, 2)};
        u.c[0] = e.color(kColor);
    }
};

// --- Mudar cor ----------------------------------------------------------------------
class ChangeColor final : public AeColorEffect {
public:
    enum : u32 { kView = 0, kHue, kLightness, kSaturation, kColor, kTolerance, kSoftness, kMatch, kInvert };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.change_color", "Mudar cor", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kViews[] = {"Camada corrigida", "Máscara de correção"};
        p.add_enum("view", "Visualizar", kViews, 2, 0);
        p.add_angle("hue_transform", "Transformar matiz", 0.0f);
        p.add_float("lightness_transform", "Transformar luminosidade", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("saturation_transform", "Transformar saturação", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_color("color_to_change", "Cor a mudar", Vec4{0.8f, 0.03f, 0.03f, 1.0f});
        p.add_float("tolerance", "Tolerância", 15.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("softness", "Suavidade", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        static const char* const kMatch[] = {"Usando RGB", "Usando matiz", "Usando croma"};
        p.add_enum("match", "Comparar cores", kMatch, 3, 1);
        p.add_bool("invert_mask", "Inverter máscara de correção", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.e(kView) == 0 && std::fabs(fin(e.f(kHue), 0.0f)) < 0.01f && std::fabs(fin(e.f(kLightness), 0.0f)) < 0.01f
            && std::fabs(fin(e.f(kSaturation), 0.0f)) < 0.01f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kHue] = ParamValue::scalar(140.0f);
        v[kTolerance] = ParamValue::scalar(30.0f);
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::ChangeColor; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kView, 2), fin(e.f(kHue), 0.0f) / 360.0f, clampf(e.f(kLightness), -100, 100, 0) / 100.0f,
                      clampf(e.f(kSaturation), -100, 100, 0) / 100.0f};
        u.p[1] = Vec4{pct(e, kTolerance), pct(e, kSoftness), en(e, kMatch, 3), flag(e.b(kInvert))};
        u.c[0] = e.color(kColor);
    }
};

// --- Mudar para cor -------------------------------------------------------------------
class ChangeToColor final : public AeColorEffect {
public:
    enum : u32 { kFrom = 0, kTo, kChange, kChangeBy, kHueTol, kLightTol, kSatTol, kSoftness, kViewMatte };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.change_to_color", "Mudar para cor", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("from", "De", Vec4{0.8f, 0.03f, 0.03f, 1.0f});
        p.add_color("to", "Para", Vec4{0.02f, 0.25f, 0.9f, 1.0f});
        static const char* const kChanges[] = {"Matiz", "Matiz e luminosidade", "Matiz, luminosidade e saturação",
                                               "Matiz e saturação"};
        p.add_enum("change", "Mudar", kChanges, 4, 0);
        static const char* const kBy[] = {"Definir para a cor", "Transformar para a cor"};
        p.add_enum("change_by", "Mudar por", kBy, 2, 0);
        p.add_float("hue_tolerance", "Tolerância de matiz", 5.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("lightness_tolerance", "Tolerância de luminosidade", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("saturation_tolerance", "Tolerância de saturação", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("softness", "Suavidade", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("view_matte", "Ver fosco de correção", false);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kHueTol] = ParamValue::scalar(20.0f);
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::ChangeToColor; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kChange, 4), en(e, kChangeBy, 2), pct(e, kHueTol), pct(e, kLightTol)};
        u.p[1] = Vec4{pct(e, kSatTol), pct(e, kSoftness), flag(e.b(kViewMatte)), 0.0f};
        u.c[0] = e.color(kFrom);
        u.c[1] = e.color(kTo);
    }
};

// --- Filtro de foto ---------------------------------------------------------------------
// As cores dos filtros são aproximações nossas dos filtros de lente clássicos
// (âmbar 85/81, azul 80/82, LBA/LBB) e de cores puras, em sRGB.
class PhotoFilter final : public AeColorEffect {
public:
    enum : u32 { kFilter = 0, kColor, kDensity, kPreserve };
    static constexpr u32 kCustom = 20;
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.photo_filter", "Filtro de foto", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kFilters[] = {
            "Aquecimento (85)", "Aquecimento (LBA)", "Aquecimento (81)", "Resfriamento (80)", "Resfriamento (LBB)",
            "Resfriamento (82)", "Vermelho", "Laranja", "Amarelo", "Verde", "Ciano", "Azul", "Violeta", "Magenta",
            "Sépia", "Vermelho profundo", "Azul profundo", "Esmeralda profunda", "Amarelo profundo", "Subaquático",
            "Personalizado"};
        p.add_enum("filter", "Filtro", kFilters, 21, 0);
        p.add_color("color", "Cor", Vec4{0.92f, 0.36f, 0.05f, 1.0f});
        p.add_float("density", "Densidade", 25.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("preserve_luminosity", "Preservar luminosidade", true);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(fin(e.f(kDensity), 0.0f) > 0.01f); }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kDensity] = ParamValue::scalar(60.0f);
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::PhotoFilter; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        // sRGB 0..255 (aproximações próprias).
        static const u8 kRgb[20][3] = {
            {236, 138, 0},  {250, 150, 0},  {235, 177, 19}, {0, 109, 255},  {0, 93, 255},   {0, 181, 255},
            {234, 26, 26},  {243, 132, 23}, {249, 227, 28}, {25, 201, 25},  {29, 203, 234}, {29, 53, 234},
            {155, 29, 234}, {227, 24, 227}, {172, 122, 51}, {255, 0, 0},    {0, 34, 205},   {0, 140, 0},
            {255, 213, 0},  {0, 194, 177}};
        const u32 f = std::min<u32>(e.e(kFilter), kCustom);
        Vec4 c = e.color(kColor);
        if (f < kCustom) {
            auto lin = [](u8 v) {
                const f32 x = static_cast<f32>(v) / 255.0f;
                return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
            };
            c = Vec4{lin(kRgb[f][0]), lin(kRgb[f][1]), lin(kRgb[f][2]), 1.0f};
        }
        u.p[0] = Vec4{pct(e, kDensity), flag(e.b(kPreserve)), 0.0f, 0.0f};
        u.c[0] = c;
    }
};

// --- Preto e branco -------------------------------------------------------------------------
// Cinza = mín + (meio − mín)·peso da secundária + (máx − meio)·peso da primária:
// cada pixel pesa pelas duas faixas de cor entre as quais o matiz cai.
class BlackWhite final : public AeColorEffect {
public:
    enum : u32 { kReds = 0, kYellows, kGreens, kCyans, kBlues, kMagentas, kTint, kTintColor };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.black_white", "Preto e branco", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("reds", "Vermelhos", 40.0f, -200.0f, 300.0f, kAnimPct, "%");
        p.add_float("yellows", "Amarelos", 60.0f, -200.0f, 300.0f, kAnimPct, "%");
        p.add_float("greens", "Verdes", 40.0f, -200.0f, 300.0f, kAnimPct, "%");
        p.add_float("cyans", "Cianos", 60.0f, -200.0f, 300.0f, kAnimPct, "%");
        p.add_float("blues", "Azuis", 20.0f, -200.0f, 300.0f, kAnimPct, "%");
        p.add_float("magentas", "Magentas", 80.0f, -200.0f, 300.0f, kAnimPct, "%");
        p.add_bool("tint", "Tingir", false);
        p.add_color("tint_color", "Cor do tingimento", Vec4{0.69f, 0.52f, 0.33f, 1.0f});
    }

protected:
    AeOp op() const noexcept override { return AeOp::BlackWhite; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{pct(e, kReds, -2, 3), pct(e, kYellows, -2, 3), pct(e, kGreens, -2, 3), pct(e, kCyans, -2, 3)};
        u.p[1] = Vec4{pct(e, kBlues, -2, 3), pct(e, kMagentas, -2, 3), flag(e.b(kTint)), 0.0f};
        u.c[0] = e.color(kTintColor);
    }
};

// --- Cor seletiva -----------------------------------------------------------------------------
// Nove faixas (vermelhos ... pretos) × tintas CMYK. A faixa pesa o pixel pela
// distância entre os canais (vermelhos: máx − meio com R no topo; brancos: o
// quanto o menor canal passa de 50%...). Relativo: o ajuste é proporcional à
// tinta que o pixel já tem; absoluto: soma direto.
class SelectiveColor final : public AeColorEffect {
public:
    static constexpr u32 kRanges = 9;
    enum : u32 { kMethod = 0, kFirst = 1 };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.selective_color", "Cor seletiva", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kMethods[] = {"Relativo", "Absoluto"};
        p.add_enum("method", "Método", kMethods, 2, 0);
        static const char* const kIds[kRanges * 4] = {
            "reds_cyan", "reds_magenta", "reds_yellow", "reds_black",
            "yellows_cyan", "yellows_magenta", "yellows_yellow", "yellows_black",
            "greens_cyan", "greens_magenta", "greens_yellow", "greens_black",
            "cyans_cyan", "cyans_magenta", "cyans_yellow", "cyans_black",
            "blues_cyan", "blues_magenta", "blues_yellow", "blues_black",
            "magentas_cyan", "magentas_magenta", "magentas_yellow", "magentas_black",
            "whites_cyan", "whites_magenta", "whites_yellow", "whites_black",
            "neutrals_cyan", "neutrals_magenta", "neutrals_yellow", "neutrals_black",
            "blacks_cyan", "blacks_magenta", "blacks_yellow", "blacks_black"};
        static const char* const kLabels[kRanges * 4] = {
            "Vermelhos: ciano", "Vermelhos: magenta", "Vermelhos: amarelo", "Vermelhos: preto",
            "Amarelos: ciano", "Amarelos: magenta", "Amarelos: amarelo", "Amarelos: preto",
            "Verdes: ciano", "Verdes: magenta", "Verdes: amarelo", "Verdes: preto",
            "Cianos: ciano", "Cianos: magenta", "Cianos: amarelo", "Cianos: preto",
            "Azuis: ciano", "Azuis: magenta", "Azuis: amarelo", "Azuis: preto",
            "Magentas: ciano", "Magentas: magenta", "Magentas: amarelo", "Magentas: preto",
            "Brancos: ciano", "Brancos: magenta", "Brancos: amarelo", "Brancos: preto",
            "Neutros: ciano", "Neutros: magenta", "Neutros: amarelo", "Neutros: preto",
            "Pretos: ciano", "Pretos: magenta", "Pretos: amarelo", "Pretos: preto"};
        for (u32 k = 0; k < kRanges * 4; ++k) p.add_float(kIds[k], kLabels[k], 0.0f, -100.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        for (u32 k = 0; k < kRanges * 4; ++k) if (std::fabs(fin(e.f(kFirst + k), 0.0f)) > 0.01f) return false;
        return true;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kFirst + 0] = ParamValue::scalar(-80.0f);   // vermelhos: menos ciano
        v[kFirst + 8 + 1] = ParamValue::scalar(60.0f); // verdes: mais magenta
        v[kFirst + 7 * 4 + 2] = ParamValue::scalar(30.0f); // neutros: mais amarelo
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::SelectiveColor; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        for (u32 r = 0; r < kRanges; ++r) {
            u.p[r] = Vec4{pct(e, kFirst + r * 4, -1, 1), pct(e, kFirst + r * 4 + 1, -1, 1), pct(e, kFirst + r * 4 + 2, -1, 1),
                          pct(e, kFirst + r * 4 + 3, -1, 1)};
        }
        u.p[9] = Vec4{en(e, kMethod, 2), 0.0f, 0.0f, 0.0f};
    }
};

// --- Níveis do alfa ------------------------------------------------------------------------------
class AlphaLevels final : public AeColorEffect {
public:
    enum : u32 { kInBlack = 0, kInWhite, kGamma, kOutBlack, kOutWhite };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.alpha_levels", "Níveis do alfa", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("input_black", "Preto de entrada", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("input_white", "Branco de entrada", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("gamma", "Gama", 1.0f, 0.1f, 10.0f, kParamAnimatable);
        p.add_float("output_black", "Preto de saída", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("output_white", "Branco de saída", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(fin(e.f(kInBlack), 0)) < 0.01f && std::fabs(fin(e.f(kInWhite), 100) - 100.0f) < 0.01f
            && std::fabs(fin(e.f(kGamma), 1) - 1.0f) < 1e-4f && std::fabs(fin(e.f(kOutBlack), 0)) < 0.01f
            && std::fabs(fin(e.f(kOutWhite), 100) - 100.0f) < 0.01f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kOutWhite] = ParamValue::scalar(50.0f);
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::AlphaLevels; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{pct(e, kInBlack), pct(e, kInWhite), clampf(e.f(kGamma), 0.1f, 10.0f, 1.0f), pct(e, kOutBlack)};
        u.p[1] = Vec4{pct(e, kOutWhite), 0.0f, 0.0f, 0.0f};
    }
};

// --- Remover fosco de cor ---------------------------------------------------------------------------
// A imagem foi pré-multiplicada sobre uma cor (C = a·F + (1 − a)·fundo): tira
// o fundo das bordas semitransparentes (F = (C − (1 − a)·fundo) / a).
class RemoveColorMatting final : public AeColorEffect {
public:
    enum : u32 { kBackground = 0, kClipHdr };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.remove_color_matting", "Remover fosco de cor", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("background", "Cor do fundo", Vec4{0.0f, 0.0f, 0.0f, 1.0f});
        p.add_bool("clip_hdr", "Recortar resultados HDR", true);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kBackground] = ParamValue::color(1.0f, 1.0f, 1.0f, 1.0f);
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::RemoveMatting; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{flag(e.b(kClipHdr)), 0.0f, 0.0f, 0.0f};
        u.c[0] = e.color(kBackground);
    }
};

// --- Aritmética -------------------------------------------------------------------------------------
// Opera nos valores de 8 bits de cada canal (codificados) com um valor por canal.
class Arithmetic final : public AeColorEffect {
public:
    enum : u32 { kOperator = 0, kRed, kGreen, kBlue, kClip };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.arithmetic", "Aritmética", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kOps[] = {"E", "Ou", "Ou exclusivo", "Somar", "Subtrair", "Diferença", "Mínimo",
                                           "Máximo", "Bloquear acima", "Bloquear abaixo", "Fatiar", "Multiplicar",
                                           "Tela"};
        p.add_enum("operator", "Operador", kOps, 13, 3);
        p.add_float("red", "Valor do vermelho", 0.0f, 0.0f, 255.0f, kParamAnimatable);
        p.add_float("green", "Valor do verde", 0.0f, 0.0f, 255.0f, kParamAnimatable);
        p.add_float("blue", "Valor do azul", 0.0f, 0.0f, 255.0f, kParamAnimatable);
        p.add_bool("clip", "Recortar resultados", true);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        const u32 o = e.e(kOperator);
        const bool zero = fin(e.f(kRed), 0) < 0.5f && fin(e.f(kGreen), 0) < 0.5f && fin(e.f(kBlue), 0) < 0.5f;
        // Somar/subtrair/ou/ou exclusivo/diferença/máximo com zero não mudam nada.
        return zero && (o == 1 || o == 2 || o == 3 || o == 4 || o == 5 || o == 7);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kOperator] = ParamValue::scalar(5.0f);
        v[kRed] = ParamValue::scalar(200.0f);
        v[kGreen] = ParamValue::scalar(60.0f);
        v[kBlue] = ParamValue::scalar(128.0f);
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::Arithmetic; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kOperator, 13), clampf(e.f(kRed), 0, 255, 0), clampf(e.f(kGreen), 0, 255, 0),
                      clampf(e.f(kBlue), 0, 255, 0)};
        u.p[1] = Vec4{flag(e.b(kClip)), 0.0f, 0.0f, 0.0f};
    }
};

// --- Faixa de cor -------------------------------------------------------------------------------------
// Recorta (torna transparente) o que cai dentro da caixa mín..máx de cada eixo
// do espaço escolhido; a difusão amolece a borda da caixa. Padrão: verde de tela.
class ColorRange final : public AeColorEffect {
public:
    enum : u32 { kFuzziness = 0, kSpace, kMin0, kMax0, kMin1, kMax1, kMin2, kMax2 };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.key.color_range", "Faixa de cor", "Recorte", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("fuzziness", "Difusão", 10.0f, 0.0f, 100.0f, kParamAnimatable);
        static const char* const kSpaces[] = {"Lab", "YUV", "RGB"};
        p.add_enum("color_space", "Espaço de cor", kSpaces, 3, 2);
        p.add_float("min_1", "Mín. (L, Y, R)", 0.0f, 0.0f, 255.0f, kParamAnimatable);
        p.add_float("max_1", "Máx. (L, Y, R)", 110.0f, 0.0f, 255.0f, kParamAnimatable);
        p.add_float("min_2", "Mín. (a, U, G)", 140.0f, 0.0f, 255.0f, kParamAnimatable);
        p.add_float("max_2", "Máx. (a, U, G)", 255.0f, 0.0f, 255.0f, kParamAnimatable);
        p.add_float("min_3", "Mín. (b, V, B)", 0.0f, 0.0f, 255.0f, kParamAnimatable);
        p.add_float("max_3", "Máx. (b, V, B)", 110.0f, 0.0f, 255.0f, kParamAnimatable);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // Prévia: recorta os tons quentes (pouco azul), que quase toda foto tem.
        v[kMin0] = ParamValue::scalar(0.0f);
        v[kMax0] = ParamValue::scalar(255.0f);
        v[kMin1] = ParamValue::scalar(0.0f);
        v[kMax1] = ParamValue::scalar(255.0f);
        v[kMin2] = ParamValue::scalar(0.0f);
        v[kMax2] = ParamValue::scalar(90.0f);
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::ColorRange; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        auto v = [&](u32 i, f32 d) { return clampf(e.f(i), 0.0f, 255.0f, d) / 255.0f; };
        u.p[0] = Vec4{en(e, kSpace, 3), clampf(e.f(kFuzziness), 0.0f, 100.0f, 10.0f) / 255.0f, 0.0f, 0.0f};
        u.p[1] = Vec4{v(kMin0, 0), v(kMax0, 110), v(kMin1, 140), v(kMax1, 255)};
        u.p[2] = Vec4{v(kMin2, 0), v(kMax2, 110), 0.0f, 0.0f};
    }
};

// --- Luz estroboscópica ----------------------------------------------------------------------------------
// Pisca a cada período por `duração` segundos (e, com probabilidade, em
// quadros sorteados pela semente). A decisão é na CPU: fora do flash, o efeito
// sai da cadeia.
class StrobeLight final : public AeColorEffect {
public:
    enum : u32 { kColor = 0, kBlend, kDuration, kPeriod, kProbability, kStrobe, kOperator, kSeed };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.strobe_light", "Luz estroboscópica", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("color", "Cor do flash", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("blend", "Misturar com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("duration", "Duração do flash", 0.05f, 0.0f, 2.0f, kParamAnimatable, "s");
        p.typed_range(0.0f, 60.0f);
        p.add_float("period", "Período do flash", 0.5f, 0.01f, 5.0f, kParamAnimatable, "s");
        p.typed_range(0.01f, 600.0f);
        p.add_float("probability", "Probabilidade aleatória", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        static const char* const kModes[] = {"Só a cor", "Torna a camada transparente"};
        p.add_enum("strobe", "Flash", kModes, 2, 0);
        static const char* const kOps[] = {"Copiar", "Somar", "Subtrair", "Multiplicar", "Diferença", "Mínimo",
                                           "Máximo", "Tela"};
        p.add_enum("operator", "Operador do flash", kOps, 8, 0);
        p.add_int("seed", "Semente aleatória", 0, 0, 10000);
    }
    /// O flash está aceso neste instante?
    static bool lit(const EffectEval& e) noexcept {
        const f64 t = seconds(e);
        const f64 period = std::max(0.01, static_cast<f64>(clampf(e.f(kPeriod), 0.01f, 600.0f, 0.5f)));
        const f64 duration = static_cast<f64>(clampf(e.f(kDuration), 0.0f, 60.0f, 0.05f));
        const f64 phase = t - std::floor(t / period) * period;
        if (phase < duration) return true;
        const f32 prob = pct(e, kProbability);
        if (prob <= 0.0f) return false;
        // Sorteio por quadro (determinístico: preview == export).
        const u64 frame = static_cast<u64>(static_cast<i64>(std::floor(e.time_frames())));
        u64 h = frame * 0x9E3779B97F4A7C15ull ^ (static_cast<u64>(std::max(0.0f, fin(e.f(kSeed), 0.0f))) + 0x632BE59BD9B4E019ull);
        h ^= h >> 31; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 29;
        return static_cast<f32>(h & 0xFFFFFF) / 16777216.0f < prob;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (!lit(e)) return true;
        return e.e(kStrobe) == 0 && pct(e, kBlend) >= 0.999f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // Prévia (e quadros adiante): meio segundo aceso a cada segundo.
        v[kDuration] = ParamValue::scalar(0.5f);
        v[kPeriod] = ParamValue::scalar(1.0f);
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::Strobe; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kStrobe, 2), en(e, kOperator, 8), pct(e, kBlend), flag(lit(e))};
        u.c[0] = e.color(kColor);
    }
};

// --- Ruído HLS (e o automático) -----------------------------------------------------------------------------
class NoiseHls final : public AeColorEffect {
public:
    enum : u32 { kNoise = 0, kHue, kLightness, kSaturation, kGrainSize, kPhase };
    explicit NoiseHls(bool automatic) noexcept : automatic_(automatic) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo a{"aurea.stylize.noise_hls_auto", "Ruído HLS automático", "Estilizar", EffectClass::Neighborhood};
        static const EffectInfo m{"aurea.stylize.noise_hls", "Ruído HLS", "Estilizar", EffectClass::Neighborhood};
        return automatic_ ? a : m;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kTypes[] = {"Uniforme", "Ao quadrado", "Grão"};
        p.add_enum("noise", "Ruído", kTypes, 3, 0);
        p.add_float("hue", "Matiz", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("lightness", "Luminosidade", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("saturation", "Saturação", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("grain_size", "Tamanho do grão", 1.0f, 0.5f, 50.0f, kAnimPx, "px");
        if (automatic_) p.add_float("speed", "Velocidade da animação", 10.0f, 0.0f, 100.0f, kParamAnimatable);
        else p.add_angle("phase", "Fase do ruído", 0.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return pct(e, kHue) < 1e-4f && pct(e, kLightness) < 1e-4f && pct(e, kSaturation) < 1e-4f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kLightness] = ParamValue::scalar(40.0f);
        v[kHue] = ParamValue::scalar(20.0f);
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::NoiseHls; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        // Fase em "fatias" do ruído: 1 fatia por 90° (manual) ou por (10/velocidade) s (automático).
        const f64 slice = automatic_ ? seconds(e) * static_cast<f64>(clampf(e.f(kPhase), 0.0f, 1000.0f, 10.0f)) * 0.5
                                     : static_cast<f64>(fin(e.f(kPhase), 0.0f)) / 90.0;
        u.p[0] = Vec4{en(e, kNoise, 3), pct(e, kHue), pct(e, kLightness), pct(e, kSaturation)};
        u.p[1] = Vec4{clampf(e.f(kGrainSize), 0.25f, 500.0f, 1.0f), static_cast<f32>(std::fmod(slice, 4096.0)), 0.0f, 0.0f};
    }

private:
    bool automatic_;
};

// --- Ruído alfa ------------------------------------------------------------------------------------------------
class NoiseAlpha final : public AeColorEffect {
public:
    enum : u32 { kNoise = 0, kAmount, kOriginal, kOverflow, kSeed, kPhase, kCycle, kCycles };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.noise_alpha", "Ruído alfa", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kTypes[] = {"Aleatório uniforme", "Aleatório ao quadrado", "Animação uniforme",
                                             "Animação ao quadrado"};
        p.add_enum("noise", "Ruído", kTypes, 4, 0);
        p.add_float("amount", "Quantidade", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        static const char* const kOriginal[] = {"Somar", "Prender", "Escalar", "Bordas"};
        p.add_enum("original_alpha", "Alfa original", kOriginal, 4, 1);
        static const char* const kOverflow[] = {"Recortar", "Rebater", "Envolver"};
        p.add_enum("overflow", "Excesso", kOverflow, 3, 0);
        p.add_int("seed", "Semente aleatória", 0, 0, 10000);
        p.add_angle("phase", "Fase do ruído", 0.0f);
        p.add_bool("cycle", "Ciclar o ruído", false);
        p.add_int("cycles", "Ciclo (em voltas)", 1, 1, 100);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kAmount) < 1e-4f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAmount] = ParamValue::scalar(60.0f);
        return true;
    }

protected:
    AeOp op() const noexcept override { return AeOp::NoiseAlpha; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        f64 turns = static_cast<f64>(fin(e.f(kPhase), 0.0f)) / 360.0;
        if (e.b(kCycle)) {
            const f64 cycles = std::max(1.0, static_cast<f64>(fin(e.f(kCycles), 1.0f)));
            turns = turns - std::floor(turns / cycles) * cycles;
        }
        u.p[0] = Vec4{en(e, kNoise, 4), pct(e, kAmount), en(e, kOriginal, 4), en(e, kOverflow, 3)};
        u.p[1] = Vec4{std::clamp(fin(e.f(kSeed), 0.0f), 0.0f, 10000.0f), static_cast<f32>(std::fmod(turns * 4.0, 4096.0)), 0.0f, 0.0f};
    }
};

// =============================================================================
// Duas camadas: Mesclar e Fosco por diferença (ae_two_layer.frag)
// =============================================================================
/// A outra camada (px da composição) posta sobre esta: centralizada 1:1 ou
/// esticada na caixa da camada. Sem camada, o slot recebe a própria entrada.
class AeTwoLayerEffect : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_two_layer_frag, work));
    }

protected:
    Status two_layer_pass(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, u32 layerParam,
                          u32 sizeMode, AeUniforms& u, LayerImage& out) const {
        const u64 ref = e.value(layerParam).ref;
        const LayerImage* other = ref ? ctx.layer_input(ref) : nullptr;
        const Vec2 ls = layer_size(e, input);
        // p[10]: x = 1 há outra camada, y = 0 centralizar / 1 esticar, zw = tamanho dela (px).
        u.p[10] = Vec4{other ? 1.0f : 0.0f, static_cast<f32>(std::min<u32>(sizeMode, 1u)),
                       other ? std::max(other->region.w, 1.0f) : ls.x, other ? std::max(other->region.h, 1.0f) : ls.y};
        const PassTexture tex1{other ? other->texture : input.texture, {}, CommonSampler::LinearBorder};
        return ae_pass(ctx, input, u, ShaderId::effects_ae_two_layer_frag, "ae-duas-camadas", CommonSampler::LinearClamp,
                       out, &tex1, 1);
    }
};

class BlendLayers final : public AeTwoLayerEffect {
public:
    enum : u32 { kLayer = 0, kMode, kOriginal, kSize };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.blend_layers", "Mesclar", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_layer_ref("blend_layer", "Mesclar com a camada");
        static const char* const kModes[] = {"Transição cruzada", "Só a cor", "Só o tom", "Só escurecer", "Só clarear"};
        p.add_enum("mode", "Modo", kModes, 5, 0);
        p.add_float("blend_with_original", "Misturar com o original", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        static const char* const kSizes[] = {"Centralizar", "Esticar para caber"};
        p.add_enum("size_mode", "Se os tamanhos diferirem", kSizes, 2, 1);
    }
    i32 input_layer_param() const noexcept override { return static_cast<i32>(kLayer); }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.value(kLayer).ref == 0 || pct(e, kOriginal) >= 0.999f;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        const u64 ref = e.value(kLayer).ref;
        if (ref && !ctx.layer_input(ref)) { out = input; return OkStatus; }
        AeUniforms u = ae_uniforms(e, input);
        u.p[0] = Vec4{0.0f, en(e, kMode, 5), pct(e, kOriginal), 0.0f};
        return two_layer_pass(ctx, e, input, kLayer, e.e(kSize), u, out);
    }
};

class DifferenceMatte final : public AeTwoLayerEffect {
public:
    enum : u32 { kView = 0, kLayer, kSize, kTolerance, kSoftness, kBlur };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.key.difference_matte", "Fosco por diferença", "Recorte", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kViews[] = {"Saída final", "Só a origem", "Só o fosco"};
        p.add_enum("view", "Visualizar", kViews, 3, 0);
        p.add_layer_ref("difference_layer", "Camada de diferença");
        static const char* const kSizes[] = {"Centralizar", "Esticar para caber"};
        p.add_enum("size_mode", "Se os tamanhos diferirem", kSizes, 2, 1);
        p.add_float("tolerance", "Tolerância", 15.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("softness", "Suavidade", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("blur", "Desfocar antes da diferença", 0.0f, 0.0f, 50.0f, kAnimPx, "px");
    }
    i32 input_layer_param() const noexcept override { return static_cast<i32>(kLayer); }
    bool is_identity(const EffectEval& e) const noexcept override { return e.e(kView) == 1; }
    f32 input_margin(const EffectEval& e) const noexcept override { return clampf(e.f(kBlur), 0.0f, 50.0f, 0.0f); }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        const u64 ref = e.value(kLayer).ref;
        if (ref && !ctx.layer_input(ref)) { out = input; return OkStatus; }
        AeUniforms u = ae_uniforms(e, input);
        u.p[0] = Vec4{1.0f, en(e, kView, 3), pct(e, kTolerance), pct(e, kSoftness)};
        u.p[1] = Vec4{clampf(e.f(kBlur), 0.0f, 50.0f, 0.0f), 0.0f, 0.0f, 0.0f};
        return two_layer_pass(ctx, e, input, kLayer, e.e(kSize), u, out);
    }
};

} // namespace

void register_color(EffectRegistry& r) {
    (void)r.add(std::make_unique<Unmult>());
    (void)r.add(std::make_unique<LeaveColor>());
    (void)r.add(std::make_unique<ChangeColor>());
    (void)r.add(std::make_unique<ChangeToColor>());
    (void)r.add(std::make_unique<PhotoFilter>());
    (void)r.add(std::make_unique<BlackWhite>());
    (void)r.add(std::make_unique<SelectiveColor>());
    (void)r.add(std::make_unique<AlphaLevels>());
    (void)r.add(std::make_unique<RemoveColorMatting>());
    (void)r.add(std::make_unique<Arithmetic>());
    (void)r.add(std::make_unique<ColorRange>());
    (void)r.add(std::make_unique<StrobeLight>());
    (void)r.add(std::make_unique<NoiseHls>(false));
    (void)r.add(std::make_unique<NoiseHls>(true));
    (void)r.add(std::make_unique<NoiseAlpha>());
    (void)r.add(std::make_unique<BlendLayers>());
    (void)r.add(std::make_unique<DifferenceMatte>());
}

} // namespace aurea::builtin::ae26
