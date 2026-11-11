// =============================================================================
//  Aurea / effects / builtin / AeEffects2026Extra.cpp
//
//  Lote "AE 2026", segunda leva: os efeitos de tamanho S que ainda faltavam no
//  levantamento (build/reference/ae2026/gap_effects.md).
//
//    ae_extra.frag   Combinador de canais, Cores de transmissão, Limitador de
//                    vídeo, Neutralizador de cor, Excessos de brilho, Fios
//    ae_vr.frag      Girar esfera VR, Esfera para plano VR, Plano para esfera
//                    VR, Conversor VR
//    ae_layers2.frag Aritmética composta, Cálculos, Óculos 3D, Limpar com
//                    vidro, WarpoMatic, Ambiente (leem até duas outras camadas)
//    áudio           Distorção e Portão de ruído (DSP em audio/AudioFx.cpp)
//
//  Especificados pelo COMPORTAMENTO e pelos parâmetros públicos dos efeitos
//  do After Effects; nenhum código, shader ou asset da Adobe. Rótulos em
//  pt-BR; a ordem dos parâmetros é contrato (só acrescentar no fim).
// =============================================================================
#include "AeEffects2026.hpp"

#include "aurea/audio/AudioEffects.hpp"

namespace aurea::builtin::ae26 {
namespace {

constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
constexpr u16 kAnimPx = kParamAnimatable | kParamPixels;
constexpr u16 kAnimRel = kParamAnimatable | kParamRelative;

// =============================================================================
// ae_extra.frag
// =============================================================================
/// Operações de ae_extra.frag (contrato com o shader).
enum class ExtraOp : u32 { ChannelCombiner = 0, BroadcastColors, VideoLimiter, Neutralizer, Overbrights, Threads };

class AeExtraEffect : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_extra_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        AeUniforms u = ae_uniforms(e, input);
        fill(e, input, u);
        u.p[11].w = static_cast<f32>(op());
        return ae_pass(ctx, input, u, ShaderId::effects_ae_extra_frag, "ae-extra", CommonSampler::LinearClamp, out);
    }

protected:
    [[nodiscard]] virtual ExtraOp op() const noexcept = 0;
    virtual void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept = 0;
};

// --- Combinador de canais --------------------------------------------------------------
// Um valor da cor (matiz, luminosidade, um canal...) vai para um canal; ou o
// trio inteiro troca de espaço (RGB ↔ HLS, RGB ↔ YUV).
class ChannelCombiner final : public AeExtraEffect {
public:
    enum : u32 { kFrom = 0, kTo, kInvert, kSolidAlpha };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.channel_combiner", "Combinador de canais", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kFromNames[] = {"Matiz", "Luminosidade", "Saturação", "Luminância", "Vermelho",
                                                 "Verde", "Azul", "Alfa", "Mínimo do RGB", "Máximo do RGB",
                                                 "RGB para HLS", "HLS para RGB", "RGB para YUV", "YUV para RGB"};
        p.add_enum("from", "De", kFromNames, 14, 1);
        // O valor ENTRA no canal escolhido; os outros ficam (o "Red" do AE; o
        // "Red only" do AE importa como este).
        static const char* const kToNames[] = {"Vermelho", "Verde", "Azul", "Alfa", "Matiz", "Luminosidade", "Saturação"};
        p.add_enum("to", "Para", kToNames, 7, 5);
        p.add_bool("invert", "Inverter", false);
        p.add_bool("solid_alpha", "Alfa sólido", false);
        // Com uma conversão (RGB para HLS...), "Para" não vale.
        p.set_enabled_by(kTo, kFrom, (1u << 10) - 1u);   // opções 0..9
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        // Luminosidade → só a luminosidade, sem inverter: a mesma cor.
        return e.e(kFrom) == 1u && e.e(kTo) == 5u && !e.b(kInvert) && !e.b(kSolidAlpha);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kFrom] = ParamValue::scalar(0.0f);   // matiz
        v[kTo] = ParamValue::scalar(0.0f);     // no vermelho
        return true;
    }

protected:
    ExtraOp op() const noexcept override { return ExtraOp::ChannelCombiner; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kFrom, 14), en(e, kTo, 7), flag(e.b(kInvert)), flag(e.b(kSolidAlpha))};
    }
};

// --- Cores de transmissão ----------------------------------------------------------------
// O sinal composto (luma + amplitude do croma, em IRE) passa do limite: reduzir
// a luminância, reduzir a saturação, ou recortar o que não é (ou o que é) seguro.
class BroadcastColors final : public AeExtraEffect {
public:
    enum : u32 { kLocale = 0, kMethod, kMaxIre };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.broadcast_colors", "Cores de transmissão", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kLocales[] = {"NTSC", "PAL"};
        p.add_enum("locale", "Padrão de transmissão", kLocales, 2, 0);
        static const char* const kMethods[] = {"Reduzir luminância", "Reduzir saturação", "Recortar o inseguro",
                                               "Recortar o seguro"};
        p.add_enum("method", "Como tornar a cor segura", kMethods, 4, 0);
        p.add_float("max_amplitude", "Amplitude máxima do sinal (IRE)", 110.0f, 90.0f, 120.0f, kParamAnimatable, "IRE");
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kMaxIre] = ParamValue::scalar(90.0f);
        return true;
    }

protected:
    ExtraOp op() const noexcept override { return ExtraOp::BroadcastColors; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kLocale, 2), en(e, kMethod, 4), clampf(e.f(kMaxIre), 50.0f, 150.0f, 110.0f) / 100.0f, 0.0f};
    }
};

// --- Limitador de vídeo --------------------------------------------------------------------
class VideoLimiter final : public AeExtraEffect {
public:
    enum : u32 { kAxis = 0, kMin, kMax, kChroma };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.video_limiter", "Limitador de vídeo", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kAxes[] = {"Luma", "Croma", "Luma e croma", "Limite inteligente"};
        p.add_enum("axis", "Eixo da redução", kAxes, 4, 3);
        p.add_float("signal_min", "Sinal mínimo", 0.0f, -10.0f, 50.0f, kAnimPct, "%");
        p.add_float("signal_max", "Sinal máximo", 100.0f, 50.0f, 110.0f, kAnimPct, "%");
        p.add_float("chroma_max", "Croma máxima", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        // Faixa cheia e croma inteira: nada passa do limite.
        return fin(e.f(kMin), 0.0f) <= 0.0f && fin(e.f(kMax), 100.0f) >= 100.0f && fin(e.f(kChroma), 100.0f) >= 100.0f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kMax] = ParamValue::scalar(70.0f);
        return true;
    }

protected:
    ExtraOp op() const noexcept override { return ExtraOp::VideoLimiter; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kAxis, 4), clampf(e.f(kMin), -10.0f, 50.0f, 0.0f) / 100.0f,
                      clampf(e.f(kMax), 50.0f, 110.0f, 100.0f) / 100.0f, pct(e, kChroma)};
    }
};

// --- Neutralizador de cor ---------------------------------------------------------------------
// A cor que deveria ser neutra (conta-gotas) em cada faixa de tom é levada ao
// cinza dela; a correção se espalha pelos tons com pesos suaves.
class ColorNeutralizer final : public AeExtraEffect {
public:
    enum : u32 { kShadows = 0, kMidtones, kHighlights, kPinning, kBlend };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.color_neutralizer", "Neutralizador de cor", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("shadows_unbalance", "Desequilíbrio das sombras", Vec4{0.0f, 0.0f, 0.0f, 1.0f});
        p.add_color("midtones_unbalance", "Desequilíbrio dos tons médios", Vec4{0.214f, 0.214f, 0.214f, 1.0f});
        p.add_color("highlights_unbalance", "Desequilíbrio dos realces", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("pinning", "Fixação", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("blend", "Misturar com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    static bool neutral(Vec4 c) noexcept { return std::fabs(c.x - c.y) < 1e-4f && std::fabs(c.y - c.z) < 1e-4f; }
    bool is_identity(const EffectEval& e) const noexcept override {
        return (neutral(e.color(kShadows)) && neutral(e.color(kMidtones)) && neutral(e.color(kHighlights)))
            || pct(e, kBlend) >= 0.999f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kMidtones] = ParamValue::color(0.35f, 0.21f, 0.12f, 1.0f);   // tons médios puxando para o laranja
        return true;
    }

protected:
    ExtraOp op() const noexcept override { return ExtraOp::Neutralizer; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.c[0] = e.color(kShadows);
        u.c[1] = e.color(kMidtones);
        u.c[2] = e.color(kHighlights);
        u.p[0] = Vec4{pct(e, kPinning), pct(e, kBlend), 0.0f, 0.0f};
    }
};

// --- Excessos de brilho ------------------------------------------------------------------------
// Mostra onde o valor (linear, do espaço de trabalho) passa de 1 ou fica
// abaixo de 0: a cor do recorte (ou a complementar, para os negativos). Com um
// canal escolhido, a vista é a daquele canal em cinza.
class Overbrights final : public AeExtraEffect {
public:
    enum : u32 { kChannel = 0, kClipColor };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.overbrights", "Excessos de brilho", "Utilitário", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kChannels[] = {"RGB", "Luminância", "Vermelho", "Verde", "Azul", "Alfa"};
        p.add_enum("channel", "Canal", kChannels, 6, 0);
        p.add_color("clip_color", "Cor do recorte", Vec4{1.0f, 0.0f, 1.0f, 1.0f});
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kChannel] = ParamValue::scalar(1.0f);   // a luminância em cinza
        return true;
    }

protected:
    ExtraOp op() const noexcept override { return ExtraOp::Overbrights; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kChannel, 6), 0.0f, 0.0f, 0.0f};
        u.c[0] = e.color(kClipColor);
    }
};

// --- Fios (trama) ----------------------------------------------------------------------------------
// A imagem vira um tecido: fios horizontais e verticais que se cruzam por cima
// e por baixo, cada um com a cor da imagem ao longo dele.
class Threads final : public AeExtraEffect {
public:
    enum : u32 { kWidth = 0, kHeight, kOverlaps, kDirection, kCenter, kCoverage, kShadowing, kTexture };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.threads", "Fios", "Estilizar", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("width", "Largura", 50.0f, 2.0f, 1000.0f, kAnimPx, "px");
        p.add_float("height", "Altura", 50.0f, 2.0f, 1000.0f, kAnimPx, "px");
        p.add_int("overlaps", "Sobreposições", 3, 1, 10);
        p.add_angle("direction", "Direção", 0.0f);
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.add_float("coverage", "Cobertura", 90.0f, 5.0f, 100.0f, kAnimPct, "%");
        p.add_float("shadowing", "Sombreamento", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("texture", "Textura", 30.0f, 0.0f, 100.0f, kAnimPct, "%");
    }

protected:
    ExtraOp op() const noexcept override { return ExtraOp::Threads; }
    void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept override {
        const Vec2 c = layer_point(e, input, kCenter);
        u.p[0] = Vec4{clampf(e.f(kWidth), 2.0f, 1000.0f, 50.0f), clampf(e.f(kHeight), 2.0f, 1000.0f, 50.0f),
                      clampf(e.f(kOverlaps), 1.0f, 10.0f, 3.0f), fin(e.f(kDirection), 0.0f) * kDeg2Rad};
        u.p[1] = Vec4{c.x, c.y, pct(e, kCoverage, 0.05f, 1.0f), pct(e, kShadowing)};
        u.p[2] = Vec4{pct(e, kTexture), 0.0f, 0.0f, 0.0f};
    }
};

// =============================================================================
// ae_vr.frag
// =============================================================================
/// Projeções de ae_vr.frag (contrato com o shader).
enum : u32 { kProjEquirect = 0, kProjCube = 1, kProjFisheye = 2, kProjPlane = 3 };

/// Linhas da rotação que leva a direção da saída à da entrada: inclinação
/// (x), panorâmica (y), rolagem (z), em graus. `inverse` = a transposta.
void vr_rotation(f32 tiltDeg, f32 panDeg, f32 rollDeg, bool inverse, AeUniforms& u) noexcept {
    const f32 a = fin(tiltDeg, 0.0f) * kDeg2Rad, b = fin(panDeg, 0.0f) * kDeg2Rad, c = fin(rollDeg, 0.0f) * kDeg2Rad;
    const f32 ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b), cc = std::cos(c), sc = std::sin(c);
    // R = Ry(pan) · Rx(tilt) · Rz(roll)
    const f32 rx[3][3] = {{1, 0, 0}, {0, ca, -sa}, {0, sa, ca}};
    const f32 ry[3][3] = {{cb, 0, sb}, {0, 1, 0}, {-sb, 0, cb}};
    const f32 rz[3][3] = {{cc, -sc, 0}, {sc, cc, 0}, {0, 0, 1}};
    f32 t[3][3]{}, r[3][3]{};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) t[i][j] += ry[i][k] * rx[k][j];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) r[i][j] += t[i][k] * rz[k][j];
    for (int i = 0; i < 3; ++i) {
        u.p[1 + i] = inverse ? Vec4{r[0][i], r[1][i], r[2][i], 0.0f} : Vec4{r[i][0], r[i][1], r[i][2], 0.0f};
    }
}

class AeVrEffect : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_vr_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        AeUniforms u = ae_uniforms(e, input);
        fill(e, u);
        return ae_pass(ctx, input, u, ShaderId::effects_ae_vr_frag, "ae-vr", CommonSampler::LinearClamp, out);
    }

protected:
    virtual void fill(const EffectEval& e, AeUniforms& u) const noexcept = 0;
    static f32 tan_half(const EffectEval& e, u32 fov) noexcept {
        return std::tan(clampf(e.f(fov), 1.0f, 179.0f, 90.0f) * 0.5f * kDeg2Rad);
    }
    static bool still(const EffectEval& e, u32 first) noexcept {
        for (u32 k = first; k < first + 3; ++k) if (std::fabs(fin(e.f(k), 0.0f)) > 1e-3f) return false;
        return true;
    }
};

class VrRotateSphere final : public AeVrEffect {
public:
    enum : u32 { kTilt = 0, kPan, kRoll };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.distort.vr_rotate_sphere", "Girar esfera VR", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_angle("tilt", "Inclinação (X)", 0.0f);
        p.add_angle("pan", "Panorâmica (Y)", 0.0f);
        p.add_angle("roll", "Rolagem (Z)", 0.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return still(e, kTilt); }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kPan] = ParamValue::scalar(90.0f);
        v[kTilt] = ParamValue::scalar(20.0f);
        return true;
    }

protected:
    void fill(const EffectEval& e, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{static_cast<f32>(kProjEquirect), static_cast<f32>(kProjEquirect), 1.0f, 1.0f};
        vr_rotation(e.f(kTilt), e.f(kPan), e.f(kRoll), false, u);
    }
};

class VrSphereToPlane final : public AeVrEffect {
public:
    enum : u32 { kFov = 0, kTilt, kPan, kRoll };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.distort.vr_sphere_to_plane", "Esfera para plano VR", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("fov", "Campo de visão (FOV)", 90.0f, 10.0f, 170.0f, kParamAnimatable, "°");
        p.add_angle("tilt", "Inclinação (X)", 0.0f);
        p.add_angle("pan", "Panorâmica (Y)", 0.0f);
        p.add_angle("roll", "Rolagem (Z)", 0.0f);
    }

protected:
    void fill(const EffectEval& e, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{static_cast<f32>(kProjEquirect), static_cast<f32>(kProjPlane), 1.0f, tan_half(e, kFov)};
        vr_rotation(e.f(kTilt), e.f(kPan), e.f(kRoll), false, u);
    }
};

class VrPlaneToSphere final : public AeVrEffect {
public:
    enum : u32 { kFov = 0, kTilt, kPan, kRoll };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.distort.vr_plane_to_sphere", "Plano para esfera VR", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("fov", "Campo de visão (FOV)", 90.0f, 10.0f, 170.0f, kParamAnimatable, "°");
        p.add_angle("tilt", "Inclinação (X)", 0.0f);
        p.add_angle("pan", "Panorâmica (Y)", 0.0f);
        p.add_angle("roll", "Rolagem (Z)", 0.0f);
    }

protected:
    void fill(const EffectEval& e, AeUniforms& u) const noexcept override {
        // A saída é a esfera: o plano é posto na direção pedida (rotação inversa).
        u.p[0] = Vec4{static_cast<f32>(kProjPlane), static_cast<f32>(kProjEquirect), tan_half(e, kFov), 1.0f};
        vr_rotation(e.f(kTilt), e.f(kPan), e.f(kRoll), true, u);
    }
};

class VrConverter final : public AeVrEffect {
public:
    enum : u32 { kInput = 0, kOutput, kTilt, kPan, kRoll };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.distort.vr_converter", "Conversor VR", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kProjections[] = {"Equiretangular", "Cubo 3x2", "Olho de peixe 180°"};
        p.add_enum("input_projection", "Projeção de entrada", kProjections, 3, 0);
        p.add_enum("output_projection", "Projeção de saída", kProjections, 3, 1);
        p.add_angle("tilt", "Inclinação (X)", 0.0f);
        p.add_angle("pan", "Panorâmica (Y)", 0.0f);
        p.add_angle("roll", "Rolagem (Z)", 0.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::min(e.e(kInput), 2u) == std::min(e.e(kOutput), 2u) && still(e, kTilt);
    }

protected:
    void fill(const EffectEval& e, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kInput, 3), en(e, kOutput, 3), 1.0f, 1.0f};
        vr_rotation(e.f(kTilt), e.f(kPan), e.f(kRoll), false, u);
    }
};

// =============================================================================
// ae_layers2.frag — até duas outras camadas
// =============================================================================
enum class LayerOp : u32 { CompoundArithmetic = 0, Calculations, Glasses3D, GlassWipe, WarpoMatic, Environment };

class AeLayersEffect : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_layers2_frag, work));
    }
    u32 input_layer_count() const noexcept override { return second() >= 0 ? 2u : 1u; }
    i32 input_layer_param_at(u32 slot) const noexcept override {
        return slot == 0 ? first() : slot == 1 ? second() : -1;
    }
    i32 input_layer_param() const noexcept override { return first(); }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        const LayerImage* layers[2] = {nullptr, nullptr};
        const i32 params[2] = {first(), second()};
        for (u32 k = 0; k < 2; ++k) {
            if (params[k] < 0) continue;
            const u64 ref = e.value(static_cast<u32>(params[k])).ref;
            if (!ref) continue;
            layers[k] = ctx.layer_input(ref);
            // A camada pedida ainda não está desenhada (ou aponta de volta): sem efeito.
            if (!layers[k]) { out = input; return OkStatus; }
        }
        AeUniforms u = ae_uniforms(e, input);
        fill(e, input, u);
        u.p[11].w = static_cast<f32>(op());
        const Vec2 ls = layer_size(e, input);
        u.p[10] = Vec4{layers[0] ? 1.0f : 0.0f, layers[1] ? 1.0f : 0.0f, flag(stretch(e, 0)), flag(stretch(e, 1))};
        u.p[9] = Vec4{layers[0] ? std::max(layers[0]->region.w, 1.0f) : ls.x, layers[0] ? std::max(layers[0]->region.h, 1.0f) : ls.y,
                      layers[1] ? std::max(layers[1]->region.w, 1.0f) : ls.x, layers[1] ? std::max(layers[1]->region.h, 1.0f) : ls.y};
        const PassTexture tex[2] = {
            PassTexture{layers[0] ? layers[0]->texture : input.texture, {}, CommonSampler::LinearBorder},
            PassTexture{layers[1] ? layers[1]->texture : input.texture, {}, CommonSampler::LinearBorder},
        };
        return ae_pass(ctx, input, u, ShaderId::effects_ae_layers2_frag, "ae-camadas", CommonSampler::LinearClamp, out,
                       tex, 2);
    }

protected:
    [[nodiscard]] virtual LayerOp op() const noexcept = 0;
    [[nodiscard]] virtual i32 first() const noexcept = 0;
    [[nodiscard]] virtual i32 second() const noexcept { return -1; }
    /// A camada `slot` esticada na caixa desta (falso = centralizada 1:1).
    [[nodiscard]] virtual bool stretch(const EffectEval&, u32) const noexcept { return true; }
    virtual void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept = 0;
};

// --- Aritmética composta -------------------------------------------------------------------------
class CompoundArithmetic final : public AeLayersEffect {
public:
    enum : u32 { kLayer = 0, kOperator, kChannels, kOverflow, kStretch, kBlend };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.compound_arithmetic", "Aritmética composta", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_layer_ref("second_layer", "Segunda camada de origem");
        static const char* const kOps[] = {"Copiar", "Somar", "Subtrair", "Multiplicar", "Diferença", "E", "Ou",
                                           "Ou exclusivo", "Clarear", "Escurecer", "Mínimo", "Máximo", "Tela",
                                           "Sobrepor", "Luz direta"};
        p.add_enum("operator", "Operador", kOps, 15, 0);
        static const char* const kChannelNames[] = {"RGB", "ARGB", "Alfa"};
        p.add_enum("channels", "Operar nos canais", kChannelNames, 3, 0);
        static const char* const kOverflows[] = {"Recortar", "Envolver", "Escalar"};
        p.add_enum("overflow", "Comportamento do excesso", kOverflows, 3, 0);
        p.add_bool("stretch", "Esticar a segunda camada para caber", true);
        p.add_float("blend", "Misturar com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        // Sem a segunda camada, a própria entra no lugar: "Copiar" a devolve igual.
        return pct(e, kBlend) >= 0.999f || (e.value(kLayer).ref == 0 && e.e(kOperator) == 0u);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kOperator] = ParamValue::scalar(3.0f);   // multiplicar (a própria camada)
        return true;
    }

protected:
    LayerOp op() const noexcept override { return LayerOp::CompoundArithmetic; }
    i32 first() const noexcept override { return static_cast<i32>(kLayer); }
    bool stretch(const EffectEval& e, u32) const noexcept override { return e.b(kStretch); }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kOperator, 15), en(e, kChannels, 3), en(e, kOverflow, 3), pct(e, kBlend)};
    }
};

// --- Cálculos ------------------------------------------------------------------------------------------
class Calculations final : public AeLayersEffect {
public:
    enum : u32 { kInputChannel = 0, kInvertInput, kLayer, kSecondChannel, kSecondOpacity, kInvertSecond, kStretch,
                 kPreserve, kMode };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.color.calculations", "Cálculos", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kChannels[] = {"RGBA", "Cinza", "Vermelho", "Verde", "Azul", "Alfa"};
        p.add_enum("input_channel", "Canal de entrada", kChannels, 6, 0);
        p.add_bool("invert_input", "Inverter a entrada", false);
        p.add_layer_ref("second_layer", "Segunda camada");
        p.add_enum("second_channel", "Canal da segunda camada", kChannels, 6, 0);
        p.add_float("second_opacity", "Opacidade da segunda camada", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("invert_second", "Inverter a segunda camada", false);
        p.add_bool("stretch", "Esticar a segunda camada para caber", true);
        p.add_bool("preserve_transparency", "Preservar transparência", false);
        static const char* const kModes[] = {"Normal", "Somar", "Multiplicar", "Tela", "Sobrepor", "Diferença",
                                             "Escurecer", "Clarear", "Subtrair"};
        p.add_enum("blend_mode", "Modo de mesclagem", kModes, 9, 0);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.e(kInputChannel) == 0u && !e.b(kInvertInput) && e.value(kLayer).ref == 0;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kInputChannel] = ParamValue::scalar(1.0f);   // cinza
        return true;
    }

protected:
    LayerOp op() const noexcept override { return LayerOp::Calculations; }
    i32 first() const noexcept override { return static_cast<i32>(kLayer); }
    bool stretch(const EffectEval& e, u32) const noexcept override { return e.b(kStretch); }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kInputChannel, 6), flag(e.b(kInvertInput)), en(e, kSecondChannel, 6), flag(e.b(kInvertSecond))};
        // Modos do lote (ae_blend_mode): 0 normal, 1 somar, 2 multiplicar, 3 tela,
        // 4 sobrepor, 5 diferença, 6 escurecer, 7 clarear, 8 subtrair.
        u.p[1] = Vec4{pct(e, kSecondOpacity), flag(e.b(kPreserve)), en(e, kMode, 9), 0.0f};
    }
};

// --- Óculos 3D -------------------------------------------------------------------------------------------
// Esta camada é a vista esquerda; a direita vem de outra camada (sem ela, a
// própria, deslocada pela convergência).
class Glasses3D final : public AeLayersEffect {
public:
    enum : u32 { kRight = 0, kConvergence, kVertical, kView, kBalance, kSwap };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.glasses_3d", "Óculos 3D", "Estilizar", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_layer_ref("right_view", "Vista direita");
        p.add_float("convergence", "Convergência da cena", 0.0f, -200.0f, 200.0f, kAnimPx, "px");
        p.add_float("vertical_alignment", "Alinhamento vertical", 0.0f, -200.0f, 200.0f, kAnimPx, "px");
        static const char* const kViews[] = {"Par estéreo", "Entrelaçado", "Vermelho e verde", "Vermelho e azul",
                                             "Vermelho e azul balanceado", "Vermelho e azul colorido"};
        p.add_enum("view", "Vista 3D", kViews, 6, 3);
        p.add_float("balance", "Equilíbrio", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("swap", "Trocar esquerda e direita", false);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kConvergence] = ParamValue::scalar(8.0f);
        return true;
    }

protected:
    LayerOp op() const noexcept override { return LayerOp::Glasses3D; }
    i32 first() const noexcept override { return static_cast<i32>(kRight); }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kView, 6), clampf(e.f(kConvergence), -1000.0f, 1000.0f, 0.0f),
                      clampf(e.f(kVertical), -1000.0f, 1000.0f, 0.0f), pct(e, kBalance)};
        u.p[1] = Vec4{flag(e.b(kSwap)), 0.0f, 0.0f, 0.0f};
    }
};

// --- Limpar com vidro -----------------------------------------------------------------------------------
class GlassWipe final : public AeLayersEffect {
public:
    enum : u32 { kCompletion = 0, kReveal, kGradient, kSoftness, kDisplacement };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.transition.glass_wipe", "Limpar com vidro", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_layer_ref("reveal_layer", "Camada a revelar");
        p.add_layer_ref("gradient_layer", "Camada do gradiente");
        p.add_float("softness", "Suavidade", 10.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("displacement", "Quantidade de deslocamento", 10.0f, 0.0f, 200.0f, kAnimPx, "px");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kCompletion) < 1e-4f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kCompletion] = ParamValue::scalar(50.0f);
        return true;
    }

protected:
    LayerOp op() const noexcept override { return LayerOp::GlassWipe; }
    i32 first() const noexcept override { return static_cast<i32>(kReveal); }
    i32 second() const noexcept override { return static_cast<i32>(kGradient); }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{pct(e, kCompletion), std::max(pct(e, kSoftness), 1e-3f), clampf(e.f(kDisplacement), 0.0f, 1000.0f, 10.0f),
                      0.0f};
    }
};

// --- WarpoMatic -------------------------------------------------------------------------------------------
class WarpoMatic final : public AeLayersEffect {
public:
    enum : u32 { kCompletion = 0, kReveal, kReactor, kSmoothness, kWarp, kDirection, kSpan };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.transition.warpomatic", "WarpoMatic", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_layer_ref("reveal_layer", "Camada a revelar");
        static const char* const kReactors[] = {"Brilho", "Diferenças de contraste", "Diferenças de brilho",
                                                "Diferenças locais"};
        p.add_enum("reactor", "Reator", kReactors, 4, 0);
        p.add_float("smoothness", "Suavidade", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("warp_amount", "Quantidade de deformação", 30.0f, -500.0f, 500.0f, kAnimPx, "px");
        static const char* const kDirections[] = {"Junta", "Oposta", "Torcendo"};
        p.add_enum("warp_direction", "Direção da deformação", kDirections, 3, 0);
        p.add_float("blend_span", "Faixa de mistura", 20.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kCompletion) < 1e-4f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kCompletion] = ParamValue::scalar(50.0f);
        return true;
    }

protected:
    LayerOp op() const noexcept override { return LayerOp::WarpoMatic; }
    i32 first() const noexcept override { return static_cast<i32>(kReveal); }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{pct(e, kCompletion), en(e, kReactor, 4), pct(e, kSmoothness), clampf(e.f(kWarp), -1000.0f, 1000.0f, 30.0f)};
        u.p[1] = Vec4{en(e, kDirection, 3), std::max(pct(e, kSpan), 1e-3f), 0.0f, 0.0f};
    }
};

// --- Ambiente ----------------------------------------------------------------------------------------------
// A camada vira uma superfície espelhada (relevo pela luminância) que reflete
// a camada de ambiente (sem ela, a própria).
class Environment final : public AeLayersEffect {
public:
    enum : u32 { kEnvLayer = 0, kMapping, kFilter, kRelief, kAmount };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.environment", "Ambiente", "Estilizar", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_layer_ref("environment_layer", "Camada do ambiente");
        static const char* const kMappings[] = {"Esférico", "Sonda", "Cruz vertical"};
        p.add_enum("mapping", "Mapeamento", kMappings, 3, 0);
        p.add_bool("filter_environment", "Filtrar o ambiente", true);
        p.add_float("relief", "Relevo", 20.0f, 0.0f, 100.0f, kParamAnimatable);
        p.add_float("amount", "Quantidade", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kAmount) < 1e-4f; }

protected:
    LayerOp op() const noexcept override { return LayerOp::Environment; }
    i32 first() const noexcept override { return static_cast<i32>(kEnvLayer); }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{en(e, kMapping, 3), flag(e.b(kFilter)), clampf(e.f(kRelief), 0.0f, 100.0f, 20.0f), pct(e, kAmount)};
    }
};

// =============================================================================
// Áudio (no vídeo são identidade; o DSP mora em audio/AudioFx.cpp e lê os
// índices de audio::fxp::Distortion / audio::fxp::Gate)
// =============================================================================
class AudioDistortion final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kDistortion, "Distorção", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kTypes[] = {"Corte suave", "Corte duro", "Saturação 1", "Saturação 2", "Válvula", "Fuzz"};
        p.add_enum("type", "Tipo", kTypes, 6, 0);
        p.add_float("drive", "Drive", 12.0f, -24.0f, 48.0f, kParamAnimatable, "dB");
        p.add_float("gain", "Ganho", 0.0f, -48.0f, 24.0f, kParamAnimatable, "dB");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("volume", "Volume", 0.0f, -48.0f, 24.0f, kParamAnimatable, "dB");
        p.add_float("resolution", "Resolução (bits)", 24.0f, 1.0f, 24.0f, kParamAnimatable, "bit");
        p.add_int("downsample", "Redução de amostragem (x)", 1, 1, 64);
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

class AudioGate final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kGate, "Portão de ruído", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("threshold", "Limiar", -40.0f, -100.0f, 0.0f, kParamAnimatable, "dB");
        p.add_float("attack", "Ataque", 1.0f, 0.01f, 200.0f, kParamAnimatable, "ms");
        p.add_float("hold", "Retenção", 50.0f, 0.0f, 2000.0f, kParamAnimatable, "ms");
        p.add_float("release", "Soltura", 100.0f, 1.0f, 3000.0f, kParamAnimatable, "ms");
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

} // namespace

void register_extra(EffectRegistry& r) {
    (void)r.add(std::make_unique<ChannelCombiner>());
    (void)r.add(std::make_unique<BroadcastColors>());
    (void)r.add(std::make_unique<VideoLimiter>());
    (void)r.add(std::make_unique<ColorNeutralizer>());
    (void)r.add(std::make_unique<Overbrights>());
    (void)r.add(std::make_unique<Threads>());
    (void)r.add(std::make_unique<VrRotateSphere>());
    (void)r.add(std::make_unique<VrSphereToPlane>());
    (void)r.add(std::make_unique<VrPlaneToSphere>());
    (void)r.add(std::make_unique<VrConverter>());
    (void)r.add(std::make_unique<CompoundArithmetic>());
    (void)r.add(std::make_unique<Calculations>());
    (void)r.add(std::make_unique<Glasses3D>());
    (void)r.add(std::make_unique<GlassWipe>());
    (void)r.add(std::make_unique<WarpoMatic>());
    (void)r.add(std::make_unique<Environment>());
    (void)r.add(std::make_unique<AudioDistortion>());
    (void)r.add(std::make_unique<AudioGate>());
}

} // namespace aurea::builtin::ae26
