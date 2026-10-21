// =============================================================================
//  Aurea / effects / builtin / AuroraEffect.cpp
//
//  Aurora (Gerar, 0.2.7): aurora boreal animada sobre um céu com estrelas e o
//  reflexo numa lâmina d'água. Os controles seguem os de um gerador de aurora
//  do After Effects (comportamento especificado pela tela de controles; ver
//  build/reference/ae2026/NOTAS.md): Distorção, Quantidade, Escala,
//  Comprimento, Exposição, Detalhe, Curvatura, Brilho do céu, as três cores,
//  Deslocamento de tempo e Velocidade, mais o grupo Ambiente.
//
//  Implementação própria (nenhum código, shader ou asset de terceiros): cada
//  pixel é um raio de uma câmera virtual na altura da água; as cortinas são
//  lâminas horizontais empilhadas no céu, e em cada lâmina uma faixa sinuosa
//  de ruído com raios verticais finos. Abaixo do horizonte o raio é espelhado
//  (o reflexo), com a ondulação da água.
//
//  0.3.0 — efeito 3D e rápido:
//   - Com câmera na composição, o céu acompanha a câmera (orientação e campo
//     de visão): a aurora fica no lugar dela no mundo, como um céu 3D.
//     Inclinação e giro do grupo Ambiente somam por cima da câmera.
//   - A aurora (a parte cara: dezenas de lâminas de ruído) sai de uma CÚPULA
//     por direção (azimute × elevação), calculada só na faixa que a câmera vê
//     e em resolução reduzida (aurora é macia). O passe final, em resolução
//     cheia, só lê a cúpula — para o céu e para o reflexo — e desenha as
//     estrelas. Antes, cada pixel do reflexo refazia o céu inteiro.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

struct AuroraUniforms {
    EffectUniforms base{};
    Vec4 q0{};   ///< rgb = cor secundária (linear), w = estrelas 0..1
    Vec4 q1{};   ///< rgb = cor do céu (linear), w = foco da câmera (1/tan(fov/2))
    Vec4 q2{};   ///< x = lâminas, y = 0, zw = tamanho da camada (px)
    Vec4 q3{};   ///< x = opacidade 0..1, y = altura do céu, z = cúpula ligada, w = 0
    Vec4 q4{};   ///< cúpula: x = azimute do centro, y = meia faixa (rad), z = elevação máx (rad), w = 0
    Vec4 right{}, up{}, forward{};   ///< eixos da câmera no espaço da aurora (y para cima)
};
static_assert(sizeof(AuroraUniforms) == 240, "layout std140 dos uniforms da aurora");

struct AuroraBasis { Vec3 right, up, forward; f32 focal = 1.0f; };

Vec3 rot_x(Vec3 v, f32 a) noexcept {
    const f32 c = std::cos(a), s = std::sin(a);
    return Vec3{v.x, v.y * c + v.z * s, -v.y * s + v.z * c};
}
Vec3 rot_y(Vec3 v, f32 a) noexcept {
    const f32 c = std::cos(a), s = std::sin(a);
    return Vec3{v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}
Vec3 unit(Vec3 v) noexcept {
    const f32 n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return n > 1e-12f ? Vec3{v.x / n, v.y / n, v.z / n} : Vec3{0, 0, 1};
}

class Aurora final : public Effect {
public:
    enum : u32 {
        kWarp = 0, kAmount, kScale, kLength, kExposure, kDetail, kCurvature, kSkyGlow,
        kColor, kSecondary, kSkyColor, kTimeOffset, kSpeed,
        // Ambiente
        kHorizon, kReflection, kRipple, kStars, kTilt, kPan, kFov, kSkyHeight,
        kOpacity,
        // 0.3.0
        kFollowCamera, kQuality,
    };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kAurora, "Aurora", "3D", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("warp", "Distorção", 0.0f, -100.0f, 100.0f);
        p.typed_range(-1000.0f, 1000.0f);
        p.add_float("amount", "Quantidade", 100.0f, 0.0f, 300.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("scale", "Escala", 5.0f, 0.5f, 30.0f);
        p.typed_range(0.05f, 300.0f);
        p.add_float("length", "Comprimento", 50.0f, 0.0f, 200.0f);
        p.typed_range(0.0f, 1000.0f);
        p.add_float("exposure", "Exposição", 40.0f, 0.0f, 100.0f);
        p.typed_range(-100.0f, 300.0f);
        p.add_float("detail", "Detalhe", 60.0f, 0.0f, 100.0f);
        p.add_float("curvature", "Curvatura", 4.0f, -30.0f, 30.0f);
        p.typed_range(-300.0f, 300.0f);
        p.add_float("sky_glow", "Brilho do céu", 20.0f, 0.0f, 100.0f);
        p.typed_range(0.0f, 1000.0f);
        p.add_color("color", "Cor", Vec4{0.0f, 0.62f, 0.55f, 1.0f});
        p.add_color("secondary_color", "Cor secundária", Vec4{0.10f, 0.48f, 0.05f, 1.0f});
        p.add_color("sky_color", "Cor do céu", Vec4{0.004f, 0.006f, 0.016f, 1.0f});
        p.add_float("time_offset", "Deslocamento de tempo", 0.0f, -1000.0f, 1000.0f, kParamAnimatable, "s");
        p.typed_range(-100000.0f, 100000.0f);
        p.add_float("speed", "Velocidade", 40.0f, 0.0f, 200.0f);
        p.typed_range(-1000.0f, 1000.0f);
        // Ambiente
        p.add_float("horizon", "Horizonte", 0.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("reflection", "Reflexo", 80.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("ripple", "Ondulação da água", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("stars", "Estrelas", 35.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("tilt", "Inclinação da câmera", 10.0f);
        p.add_angle("pan", "Giro da câmera", 0.0f);
        p.add_float("fov", "Campo de visão", 75.0f, 20.0f, 140.0f, kParamAnimatable, "°");
        p.add_float("sky_height", "Altura das cortinas", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("follow_camera", "Seguir a câmera 3D", true);
        static const char* const kQualities[] = {"Rápida", "Normal", "Alta"};
        p.add_enum("quality", "Qualidade", kQualities, 3, 1);
        p.add_section("environment", "Ambiente", {kHorizon, kReflection, kRipple, kStars, kTilt, kPan, kFov, kSkyHeight,
                                                  kFollowCamera});
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAmount] = ParamValue::scalar(140.0f);
        v[kExposure] = ParamValue::scalar(45.0f);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(finite_or(e.f(kOpacity), 100.0f) > 0.01f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_aurora_dome_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_aurora_frag, work));
    }

    /// Os eixos da câmera virtual no espaço da aurora (x direita, y para
    /// cima, z para a frente): a câmera 3D da composição, se seguir, e por
    /// cima a inclinação e o giro do grupo Ambiente — a mesma conta do
    /// shader antigo (inclinar em X, depois girar em Y).
    static AuroraBasis basis(const EffectEval& e) noexcept {
        AuroraBasis b;
        const f32 tilt = std::clamp(finite_or(e.f(kTilt), 10.0f), -89.0f, 89.0f) * kDeg2Rad;
        const f32 pan = finite_or(e.f(kPan), 0.0f) * kDeg2Rad;
        Vec3 r = rot_y(rot_x(Vec3{1, 0, 0}, tilt), pan);
        Vec3 u = rot_y(rot_x(Vec3{0, 1, 0}, tilt), pan);
        Vec3 f = rot_y(rot_x(Vec3{0, 0, 1}, tilt), pan);
        f32 fovY = std::clamp(finite_or(e.f(kFov), 75.0f), 20.0f, 140.0f) * kDeg2Rad;
        const bool follow = e.count > kFollowCamera && e.b(kFollowCamera);
        if (follow && e.placement && e.placement->cameraActive) {
            // Linhas da vista = eixos da câmera no mundo (y para baixo); no
            // espaço da aurora y sobe. Câmera parada padrão = identidade.
            const Mat4& v = e.placement->cameraView;
            const Vec3 cr = unit(Vec3{v.col[0].x, -v.col[1].x, v.col[2].x});
            const Vec3 cu = unit(Vec3{-v.col[0].y, v.col[1].y, -v.col[2].y});
            const Vec3 cf = unit(Vec3{v.col[0].z, -v.col[1].z, v.col[2].z});
            auto toWorld = [&](Vec3 a) { return Vec3{cr.x * a.x + cu.x * a.y + cf.x * a.z,
                                                     cr.y * a.x + cu.y * a.y + cf.y * a.z,
                                                     cr.z * a.x + cu.z * a.y + cf.z * a.z}; };
            r = toWorld(r); u = toWorld(u); f = toWorld(f);
            if (e.placement->cameraFovY > 0.01f) fovY = std::clamp(e.placement->cameraFovY, 0.05f, 3.0f);
        }
        b.right = r; b.up = u; b.forward = f;
        b.focal = 1.0f / std::tan(fovY * 0.5f);
        return b;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const f32 w = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 h = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
        // Relógio da aurora: segundos da camada mais o deslocamento, na velocidade
        // (40 = o ritmo calmo de referência). Guardado só com a parte útil.
        const f64 seconds = e.time_frames() / fps + static_cast<f64>(finite_or(e.f(kTimeOffset), 0.0f));
        const f64 clock = seconds * static_cast<f64>(finite_or(e.f(kSpeed), 40.0f)) * 0.025;
        const f32 time = static_cast<f32>(clock - std::floor(clock / 4096.0) * 4096.0);
        AuroraUniforms u;
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{finite_or(e.f(kWarp), 0.0f) * 0.01f,
                         std::max(0.0f, finite_or(e.f(kAmount), 100.0f)) * 0.01f,
                         std::clamp(finite_or(e.f(kScale), 5.0f), 0.05f, 300.0f),
                         std::max(0.0f, finite_or(e.f(kLength), 50.0f)) * 0.01f};
        u.base.p1 = Vec4{finite_or(e.f(kExposure), 40.0f),
                         std::clamp(finite_or(e.f(kDetail), 60.0f), 0.0f, 100.0f) * 0.01f,
                         finite_or(e.f(kCurvature), 4.0f) * 0.01f,
                         std::max(0.0f, finite_or(e.f(kSkyGlow), 20.0f)) * 0.01f};
        u.base.p2 = Vec4{input.region.x, input.region.y, input.region.w, input.region.h};
        u.base.p3 = Vec4{time,
                         std::clamp(finite_or(e.f(kHorizon), 0.0f), -100.0f, 100.0f) * 0.01f,
                         std::clamp(finite_or(e.f(kReflection), 80.0f), 0.0f, 100.0f) * 0.01f,
                         std::clamp(finite_or(e.f(kRipple), 25.0f), 0.0f, 100.0f) * 0.01f};
        u.base.color = e.color(kColor);
        const Vec4 secondary = e.color(kSecondary), sky = e.color(kSkyColor);
        const AuroraBasis cam = basis(e);
        const f32 detail = std::clamp(finite_or(e.f(kDetail), 60.0f), 0.0f, 100.0f) * 0.01f;
        const u32 quality = e.count > kQuality ? std::min<u32>(e.e(kQuality), 2u) : 1u;
        // Lâminas por qualidade (Alta = as 12..40 de antes). O tremido estático
        // por texel do shader esconde as faixas que a contagem menor deixaria.
        static constexpr f32 kSheetBase[3] = {8.0f, 10.0f, 12.0f}, kSheetSpan[3] = {8.0f, 18.0f, 28.0f};
        const f32 sheets = std::round(kSheetBase[quality] + detail * kSheetSpan[quality]);
        u.q0 = Vec4{secondary.x, secondary.y, secondary.z, std::clamp(finite_or(e.f(kStars), 35.0f), 0.0f, 100.0f) * 0.01f};
        u.q1 = Vec4{sky.x, sky.y, sky.z, cam.focal};
        u.q2 = Vec4{sheets, 0.0f, w, h};
        u.q3 = Vec4{std::clamp(finite_or(e.f(kOpacity), 100.0f), 0.0f, 100.0f) * 0.01f,
                    std::clamp(finite_or(e.f(kSkyHeight), 50.0f), 0.0f, 100.0f) * 0.01f, 0.0f, 0.0f};
        u.right = Vec4{cam.right.x, cam.right.y, cam.right.z, 0.0f};
        u.up = Vec4{cam.up.x, cam.up.y, cam.up.z, 0.0f};
        u.forward = Vec4{cam.forward.x, cam.forward.y, cam.forward.z, 0.0f};

        // CÚPULA: as direções que a imagem vê (e as do reflexo, espelhadas),
        // numa grade da região de saída — o mesmo raio do shader.
        const f32 horizon = u.base.p3.y, ripple = u.base.p3.w, reflection = u.base.p3.z;
        f32 azLo = 1e9f, azHi = -1e9f, elMax = 0.0f;
        bool zenith = false;
        const Vec3 center = unit(Vec3{cam.forward.x * cam.focal + cam.up.x * horizon,
                                      cam.forward.y * cam.focal + cam.up.y * horizon,
                                      cam.forward.z * cam.focal + cam.up.z * horizon});
        const f32 azC = std::atan2(center.x, center.z);
        constexpr u32 kGrid = 12;
        for (u32 gy = 0; gy <= kGrid; ++gy) {
            for (u32 gx = 0; gx <= kGrid; ++gx) {
                const f32 px = input.region.x + input.region.w * static_cast<f32>(gx) / kGrid;
                const f32 py = input.region.y + input.region.h * static_cast<f32>(gy) / kGrid;
                const f32 nx = (px - 0.5f * w) / (0.5f * std::max(h, 1.0f));
                const f32 ny = -(py - 0.5f * h) / (0.5f * std::max(h, 1.0f)) + horizon;
                const Vec3 d = unit(Vec3{cam.right.x * nx + cam.up.x * ny + cam.forward.x * cam.focal,
                                         cam.right.y * nx + cam.up.y * ny + cam.forward.y * cam.focal,
                                         cam.right.z * nx + cam.up.z * ny + cam.forward.z * cam.focal});
                if (d.y < 0.0f && reflection <= 0.0f) continue;   // só água, sem reflexo
                const f32 el = std::asin(std::clamp(std::fabs(d.y), 0.0f, 1.0f));
                elMax = std::max(elMax, el);
                if (std::fabs(d.y) > 0.985f) zenith = true;
                f32 da = std::atan2(d.x, d.z) - azC;
                da -= kTwoPi * std::floor((da + kPi) / kTwoPi);
                azLo = std::min(azLo, da);
                azHi = std::max(azHi, da);
            }
        }
        const bool dome = elMax > 0.0f && u.base.p0.y > 0.0f;
        out = input;
        if (dome) {
            elMax = std::min(elMax + 0.04f + ripple * 0.06f, kPi * 0.5f);
            f32 half = 0.5f * (azHi - azLo) + 0.04f + ripple * 0.06f;
            f32 mid = azC + 0.5f * (azHi + azLo);
            if (zenith || half > kPi * 0.95f) { half = kPi; mid = 0.0f; }
            // Densidade: os texels da saída por radiano no centro, vezes a
            // fração da qualidade (e a do preview adaptativo).
            static constexpr f32 kDensity[3] = {0.30f, 0.45f, 0.70f};
            const f32 perRad = static_cast<f32>(input.height) / std::max(0.05f, 2.0f * std::atan(1.0f / cam.focal));
            const f32 scale = kDensity[quality] * std::clamp(ctx.resources().effect_quality(), 0.25f, 1.0f);
            f32 dw = std::ceil(2.0f * half * perRad * scale), dh = std::ceil(elMax * perRad * scale * 1.3f);
            const f32 cap = 1.2e6f;
            if (dw * dh > cap) { const f32 k = std::sqrt(cap / (dw * dh)); dw *= k; dh *= k; }
            const u32 domeW = static_cast<u32>(std::clamp(dw, 16.0f, 2048.0f));
            const u32 domeH = static_cast<u32>(std::clamp(dh, 8.0f, 1024.0f));
            u.q3.z = 1.0f;
            u.q4 = Vec4{mid, half, elMax, 0.0f};
            const FGTexture domeTex = ctx.texture("aurora-cupula", domeW, domeH);
            if (ctx.fullscreen_pass("aurora-cupula", PassStage::Effects, domeTex, ShaderId::effects_aurora_dome_frag,
                                    {}, &u, sizeof(u)) == kInvalidIndex) {
                return Errc::PipelineCompileFailed;
            }
            out.texture = ctx.texture("aurora", input.width, input.height);
            if (ctx.fullscreen_pass("aurora", PassStage::Effects, out.texture, ShaderId::effects_aurora_frag,
                                    {PassTexture{input.texture, {}, CommonSampler::LinearClamp},
                                     PassTexture{domeTex, {}, CommonSampler::LinearClamp}},
                                    &u, sizeof(u)) == kInvalidIndex) {
                return Errc::PipelineCompileFailed;
            }
            return OkStatus;
        }
        // Só água sem reflexo, ou quantidade 0: nada caro a fazer — o mesmo
        // passe sem cúpula (o shader não lê a aurora).
        out.texture = ctx.texture("aurora", input.width, input.height);
        if (ctx.fullscreen_pass("aurora", PassStage::Effects, out.texture, ShaderId::effects_aurora_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp},
                                 PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_aurora_effect(EffectRegistry& r) { (void)r.add(std::make_unique<Aurora>()); }

} // namespace aurea::builtin
