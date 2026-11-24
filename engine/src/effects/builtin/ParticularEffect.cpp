// =============================================================================
//  Partículas 3D AUREA: emissor em caixa/esfera, direção em cone, forças,
//  tamanho e cor ao longo da vida. A câmera e o espaço de emissão vêm da
//  composição; uma camada 2D sem câmera mantém sua projeção local.
//
//  Como tudo aqui é FECHADO (particular.vert), o quadro depende só de
//  (parâmetros, tempo, semente): prévia = export, determinístico.
//
//  Passes:
//    1. geometria: `slots` × 6 vértices, cada slot um quadrado, misturados no
//       alvo (soma na aditiva, "sobre" na normal);
//    2. composição: partículas presas em 0..1, por cima da camada quando
//       "Mostrar camada" está ligado.
//
//  Unidades guardadas: percentuais em %, tempos em ms, ângulos em
//  graus, distâncias em px da composição.
// =============================================================================
#include "BuiltinEffects.hpp"

#include "aurea/effects/Particular.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace aurea::builtin {
namespace {

using namespace particular;

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }
f32 pct(const EffectEval& e, u32 i, f32 lo, f32 hi) noexcept {
    return std::clamp(finite_or(e.f(i), 0.0f), lo, hi) * 0.01f;
}

constexpr u16 kAnim = kParamAnimatable;
constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
constexpr u16 kAnimPx = kParamAnimatable | kParamPixels;
/// Teto de slots (6 vértices cada) e de partículas por segundo.
constexpr f32 kMaxSlots = 4000.0f;
constexpr f32 kMaxRate = 1000.0f;
constexpr u32 kLifeSamples = 8;
constexpr u32 kPathSamples = 16;
constexpr u32 kAppearanceOffset = 1;
constexpr u32 kOpacityOffset = kAppearanceOffset + kLifeSamples;
constexpr u32 kPathOffset = kOpacityOffset + 2;

enum Behavior : u32 { Continuous, FormGrid, Burst, AlongPath };

bool samples_sprite(u32 shape) noexcept {
    return shape == kShapeTexturedTriangle || shape == kShapeTexturedQuad ||
           (shape >= kShapeLensConvex && shape <= kShapeTexturedFadedDisc) || shape == kShapeSprite;
}

struct ParticularUniforms {
    Vec4 region, view, clock, frame, emitPos, emitSize, launch, cone, forces, motion, lifeSize, look, color0, color1;
    Mat4 worldFromLayer, compFromWorld;   // camada do Particular (LayerPlacement::particleSpace)
    Vec4 camRight, camUp;                 // camRight.w = 1 liga o espaço 3D
    Mat4 previousParticleProjection;
    Mat4 layerFromComp;
    Vec4 plexus;                          // distância (px; 0 = sem rede), espessura (px), opacidade, pontos
    Vec4 plexusSurface;                   // ligada (0/1), opacidade, pontos S, -
    Vec4 shape;
    Vec4 behavior, options;               // modo, dimensões da grade; dispersão, caminho, mapa de vida, sprite
    Vec4 life[kLifeSamples];              // cor linear RGB, tamanho relativo à idade
    Vec4 opacity[2];                      // 8 amostras do multiplicador de opacidade
    Vec4 path[kPathSamples];              // posição em pixels no plano da camada
};
static_assert(sizeof(ParticularUniforms) == 1008, "layout std140 do particular.vert");
/// Teto de pontos ligados na rede: M(M−1)/2 pares × 6 vértices (300 → ~270 mil).
constexpr u32 kMaxPlexusPoints = 300;
/// Teto da superfície: S(S−1)(S−2)/6 trios × 3 vértices (80 → ~246 mil).
constexpr u32 kMaxSurfacePoints = 80;

Vec4 linear(Vec4 c) noexcept {
    return Vec4{Color::srgb_to_linear(std::clamp(c.x, 0.0f, 1.0f)), Color::srgb_to_linear(std::clamp(c.y, 0.0f, 1.0f)),
                Color::srgb_to_linear(std::clamp(c.z, 0.0f, 1.0f)), 1.0f};
}

class Particular final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kParticular, "Partículas 3D", "Gerar", EffectClass::Domain};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        // Emissor
        p.add_float("rate", "Partículas/seg", 60.0f, 0.0f, kMaxRate, kAnim, "/s");
        p.add_float("pre_roll", "Pré-rolagem", 1000.0f, 0.0f, 30000.0f, kAnim, "ms");
        p.add_float("position_x", "Posição X", 0.0f, -400.0f, 400.0f, kAnimPct, "%");
        p.add_float("position_y", "Posição Y", 0.0f, -400.0f, 400.0f, kAnimPct, "%");
        p.add_float("position_z", "Posição Z", 0.0f, -400.0f, 400.0f, kAnimPct, "%");
        p.add_float("emitter_size_x", "Tamanho do emissor X", 0.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.add_float("emitter_size_y", "Tamanho do emissor Y", 0.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.add_float("emitter_size_z", "Tamanho do emissor Z", 0.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.add_bool("emitter_sphere", "Emissor esférico", false);
        // Velocidade e direção
        p.add_float("velocity", "Velocidade", 320.0f, 0.0f, 6000.0f, kAnimPx, "px/s");
        p.add_float("velocity_random", "Variação da velocidade", 45.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("direction_tilt", "Inclinação da direção", 0.0f, 0.0f, 180.0f, kAnim, "°");
        p.add_float("direction_spin", "Giro da direção", 0.0f, 0.0f, 360.0f, kAnim, "°");
        p.add_float("spread", "Abertura", 32.0f, 0.0f, 180.0f, kAnim, "°");
        p.add_float("outwards", "Para fora", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        // Física
        p.add_float("gravity", "Gravidade", 260.0f, -8000.0f, 8000.0f, kAnimPx, "px/s²");
        p.add_float("wind_x", "Vento X", 0.0f, -4000.0f, 4000.0f, kAnimPx, "px/s");
        p.add_float("wind_y", "Vento Y", 0.0f, -4000.0f, 4000.0f, kAnimPx, "px/s");
        p.add_float("wind_z", "Vento Z", 0.0f, -4000.0f, 4000.0f, kAnimPx, "px/s");
        p.add_float("air_drag", "Resistência do ar", 0.0f, 0.0f, 800.0f, kAnimPct, "%");
        p.add_float("turbulence", "Turbulência", 0.0f, 0.0f, 4000.0f, kAnimPx, "px");
        p.add_float("turbulence_speed", "Velocidade da turbulência", 100.0f, 0.0f, 1000.0f, kAnimPct, "%");
        // Partícula
        p.add_float("life", "Vida", 2000.0f, 50.0f, 30000.0f, kAnim, "ms");
        p.add_float("life_random", "Variação da vida", 30.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("size", "Tamanho", 26.0f, 0.0f, 2000.0f, kAnimPx, "px");
        p.add_float("size_random", "Variação do tamanho", 40.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("size_end", "Tamanho final", 20.0f, 0.0f, 800.0f, kAnimPct, "%");
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("fade_in", "Surgir", 8.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("fade_out", "Sumir", 45.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("feather", "Suavidade da borda", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("stretch", "Esticar no movimento", 0.0f, 0.0f, 600.0f, kAnimPct, "%");
        p.add_color("color", "Cor da partícula", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_color("color_end", "Cor final", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("color_random", "Cor aleatória", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        // Render
        p.add_bool("add_mode", "Mistura aditiva", true);
        p.add_bool("show_source", "Mostrar camada", false);
        p.add_float("seed", "Semente", 0.0f, 0.0f, 9999.0f, kAnim);
        // Desfoque de movimento por partícula: rastro na
        // direção do movimento visível, do tamanho do trajeto no obturador.
        // Também liga sozinho com a chave de desfoque de movimento da camada
        // (aí vale o obturador da composição).
        p.add_bool("motion_blur", "Desfoque de movimento", false);
        p.add_float("shutter_angle", "Ângulo do obturador", 180.0f, 0.0f, 720.0f, kAnim, "°");
        // Rede (plexus): pontos ligados por linhas que somem com a distância.
        p.add_bool("plexus", "Rede (plexus)", false);
        p.add_float("plexus_distance", "Distância da rede", 180.0f, 5.0f, 3000.0f, kAnimPx, "px");
        p.add_float("plexus_width", "Espessura da linha", 1.2f, 0.2f, 12.0f, kAnimPx, "px");
        p.add_float("plexus_opacity", "Opacidade da rede", 70.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_int("plexus_points", "Pontos ligados", 150, 2, static_cast<i32>(kMaxPlexusPoints));
        // Superfície da rede: um triângulo transparente em cada trio de pontos
        // ligados entre si (as faces da malha do plexus).
        p.add_bool("plexus_surface", "Superfície da rede", false);
        p.add_float("plexus_surface_opacity", "Opacidade da superfície", 25.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_int("plexus_surface_points", "Pontos da superfície", 40, 3, static_cast<i32>(kMaxSurfacePoints));
        // Forma (1.0.8): o desenho de cada partícula. Borboleta bate as asas,
        // folha/pétala/confete giram e tombam, estrela/brilho cintilam.
        static const char* const kShapes[kShapeCount] = {
            "Esfera", "Estrela", "Coração", "Folha", "Pétala", "Borboleta", "Confete", "Brilho", "Floco de neve",
            "Linha", "Esfera sombreada", "Esfera suave", "Esfera suave escura", "Bolha",
            "Polígono em movimento", "Quadrado em movimento", "Triângulo", "Quadrilátero",
            "Triângulo texturizado", "Quadrilátero texturizado", "Tetraedro", "Cubo",
            "Lente convexa", "Lente côncava", "Lente suave", "Lente suave escura", "Lente bolha",
            "Quadrado texturizado", "Disco texturizado", "Disco texturizado suave",
            "Esfera luminosa", "Estrela luminosa", "Nuvem", "Rastro luminoso", "Sprite",
        };
        p.add_enum("shape", "Forma da partícula", kShapes, kShapeCount, kShapeDisc);
        p.add_float("spin", "Giro", 0.0f, 0.0f, 1440.0f, kAnim, "°/s");
        p.add_float("flutter", "Bater de asas e balanço", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("twinkle", "Cintilar", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        static const char* const kBehaviors[] = {"Contínua", "Forma (grade 3D)", "Rajada", "Trajetória"};
        p.add_enum("behavior", "Emissão", kBehaviors, 4, Continuous);
        p.add_layer_ref("path_layer", "Camada do caminho");
        p.add_layer_ref("sprite_layer", "Camada da textura");
        p.add_int("form_x", "Pontos X", 16, 1, 64);
        p.add_int("form_y", "Pontos Y", 12, 1, 64);
        p.add_int("form_z", "Pontos Z", 1, 1, 16);
        p.add_float("form_disperse", "Dispersão", 0.0f, 0.0f, 1000.0f, kAnimPx, "px");
        p.add_curve("size_over_life", "Tamanho ao longo da vida");
        p.add_curve("opacity_over_life", "Opacidade ao longo da vida");
        p.add_bool("use_life_gradient", "Gradiente ao longo da vida", false);
        p.add_gradient("life_gradient", "Gradiente de cor");
        p.set_enabled_by(kPathLayer, kBehavior, 1u << AlongPath);
        for (u32 index : {kGridX, kGridY, kGridZ, kDisperse}) p.set_enabled_by(index, kBehavior, 1u << FormGrid);
        p.set_enabled_by(kLifeGradient, kUseLifeGradient, 1);
        p.add_section("particle_emission", "Emissão",
            {kRate, kPreRoll, kPositionX, kPositionY, kPositionZ, kEmitterW, kEmitterH, kEmitterD,
             kEmitterSphere, kPathLayer, kGridX, kGridY, kGridZ, kDisperse});
        p.add_section("particle_life", "Vida e aparência",
            {kLife, kLifeRandom, kSize, kSizeRandom, kSizeEnd, kOpacity, kFadeIn, kFadeOut, kColor, kColorEnd,
             kColorRandom, kFeather, kSizeCurve, kOpacityCurve, kUseLifeGradient, kLifeGradient}, kInvalidIndex, false);
        p.add_section("particle_air", "Física do ar",
            {kVelocity, kVelocityRandom, kDirectionTilt, kDirectionSpin, kSpread, kOutwards, kGravity,
             kWindX, kWindY, kWindZ, kAirDrag, kTurbulence, kTurbulenceSpeed}, kInvalidIndex, false);
        p.add_section("particle_detail", "Detalhes da partícula",
            {kStretch, kSpin, kFlutter, kTwinkle, kAddMode, kShowSource, kSeed, kMotionBlur, kShutterAngle}, kInvalidIndex, false);
        p.add_section("particle_connections", "Conexões",
            {kPlexus, kPlexusDistance, kPlexusWidth, kPlexusOpacity, kPlexusPoints,
             kPlexusSurface, kPlexusSurfaceOpacity, kPlexusSurfacePoints}, kInvalidIndex, false);
    }

    bool is_identity(const EffectEval&) const noexcept override { return false; }

    // Prévia do catálogo: emissão suave distribuída no volume.
    bool demo_values(EffectInstance& instance, std::vector<ParamValue>& values) const noexcept override {
        (void)instance;
        if (values.size() < kParamCount) return false;
        auto set = [&](u32 i, f32 v) { values[i] = ParamValue::scalar(v); };
        set(kRate, 70.0f); set(kPreRoll, 9000.0f);
        set(kEmitterW, 160.0f); set(kEmitterH, 240.0f); set(kEmitterD, 200.0f);
        set(kVelocity, 24.0f); set(kVelocityRandom, 60.0f); set(kDirectionTilt, 180.0f); set(kSpread, 30.0f);
        set(kGravity, 12.0f); set(kWindX, 9.0f); set(kAirDrag, 60.0f);
        set(kTurbulence, 20.0f); set(kTurbulenceSpeed, 35.0f);
        set(kLife, 7000.0f); set(kLifeRandom, 45.0f);
        set(kSize, 6.0f); set(kSizeRandom, 70.0f); set(kSizeEnd, 100.0f);
        set(kOpacity, 90.0f); set(kFadeIn, 6.0f); set(kFadeOut, 12.0f);
        values[kAddMode] = ParamValue::boolean(false);
        return true;
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        for (BlendMode m : {BlendMode::Add, BlendMode::Normal}) {
            out.push_back(PipelineKey::graphics(ShaderId::effects_particular_vert, ShaderId::effects_particular_frag,
                                                work, true, m));
        }
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_particular_compose_frag, work));
    }

    /// Os uniforms do quadro: os mesmos na folha 2D e na cena 3D.
    i32 input_layer_param() const noexcept override { return kSpriteLayer; }

    void resolve_resources(EffectEval& e) const noexcept override {
        if (e.count < kParamCount) return;
        const auto* size = e.curve(kSizeCurve);
        const auto* opacity = e.curve(kOpacityCurve);
        const bool sizeEdited = size && !size->is_identity();
        const bool opacityEdited = opacity && !opacity->is_identity();
        const u64 ref = e.value(kLifeGradient).ref;
        const GradientData* gradient = e.b(kUseLifeGradient) && e.instance && ref < e.instance->gradients.size()
            ? &e.instance->gradients[ref] : nullptr;
        const u64 guide = e.value(kPathLayer).ref;
        const bool path = e.e(kBehavior) == AlongPath && guide && e.resources && e.layer;
        if (!sizeEdited && !opacityEdited && !gradient && !path) return;

        auto data = std::make_shared<std::vector<Vec4>>(kPathOffset);
        (*data)[0] = Vec4{sizeEdited ? 1.0f : 0.0f, opacityEdited ? 1.0f : 0.0f, gradient ? 1.0f : 0.0f, 0.0f};
        const auto sample_gradient = [](const GradientData& g, f32 t) -> Vec4 {
            const auto hi = std::lower_bound(g.stops.begin(), g.stops.end(), t, [](const GradientStop& stop, f32 pos) {
                return stop.position < pos;
            });
            if (hi == g.stops.begin()) return linear(hi->color);
            if (hi == g.stops.end()) return linear(g.stops.back().color);
            const auto lo = hi - 1;
            const f32 u = std::clamp((t - lo->position) / std::max(hi->position - lo->position, 1e-5f), 0.0f, 1.0f);
            const Vec4 a = linear(lo->color), b = linear(hi->color);
            return Vec4{a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u, 1.0f};
        };
        if (gradient && gradient->stops.empty()) { gradient = nullptr; (*data)[0].z = 0.0f; }
        for (u32 j = 0; j < kLifeSamples; ++j) {
            const f32 t = static_cast<f32>(j) / (kLifeSamples - 1);
            Vec4 sample = gradient ? sample_gradient(*gradient, t) : Vec4{};
            sample.w = sizeEdited ? std::clamp(size->evaluate(0, t), 0.0f, 1.0f) : t;
            (*data)[kAppearanceOffset + j] = sample;
        }
        const auto alpha = [&](u32 j) -> f32 {
            return opacityEdited ? std::clamp(opacity->evaluate(0, static_cast<f32>(j) / (kLifeSamples - 1)), 0.0f, 1.0f) : 1.0f;
        };
        (*data)[kOpacityOffset] = Vec4{alpha(0), alpha(1), alpha(2), alpha(3)};
        (*data)[kOpacityOffset + 1] = Vec4{alpha(4), alpha(5), alpha(6), alpha(7)};
        if (path) {
            const auto points = e.resources->repeat_path(e.layer, kPathSamples, 0.0f, guide);
            data->insert(data->end(), points.begin(), points.end());
        }
        e.pathSamples = std::move(data);
    }

    void make_uniforms(const EffectEval& e, Rect region, Vec2 comp, Vec2 layerSize, f32 texel, ParticularUniforms& u,
                       u32& slots, u32& plexusVerts, u32& surfaceVerts, bool& draw) const noexcept {
        // --- Grade determinística de nascimentos -----------------------------
        const f32 rate = std::clamp(finite_or(e.f(kRate), 0.0f), 0.0f, kMaxRate);
        const f32 lifeMs = std::clamp(finite_or(e.f(kLife), 2000.0f), 50.0f, 30000.0f);
        const f32 lifeRandom = pct(e, kLifeRandom, 0.0f, 100.0f);
        const f32 preRoll = std::clamp(finite_or(e.f(kPreRoll), 0.0f), 0.0f, 30000.0f) * 0.001f;
        const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
        const f32 now = static_cast<f32>(e.time_frames() / fps) + preRoll;
        const f32 span = std::max((lifeRandom + 1.0f) * lifeMs * 0.001f * 1.05f, 1e-3f);
        const bool plexus = e.b(kPlexus) && rate > 0.0f;
        // Rede: todo slot dispara (grade = taxa) — os primeiros M slots são
        // partículas vivas de verdade, não um sorteio esparso da grade.
        const f32 grid = plexus ? std::clamp(rate, 1e-3f, std::min(kMaxRate, kMaxSlots / span))
                                : std::min(kMaxRate, kMaxSlots / span);
        slots = static_cast<u32>(std::clamp(std::ceil(span * grid), 1.0f, kMaxSlots));
        const u32 behavior = e.count > kBehavior ? e.e(kBehavior) : Continuous;
        u32 nx = 1, ny = 1, nz = 1;
        if (behavior == FormGrid) {
            nx = static_cast<u32>(std::clamp(e.value(kGridX).as_int(), 1, 64));
            ny = static_cast<u32>(std::clamp(e.value(kGridY).as_int(), 1, 64));
            nz = static_cast<u32>(std::clamp(e.value(kGridZ).as_int(), 1, 16));
            slots = std::min(nx * ny * nz, static_cast<u32>(kMaxSlots));
        } else if (behavior == Burst) {
            slots = static_cast<u32>(std::clamp(std::round(rate), 1.0f, kMaxSlots));
        }
        u.behavior = Vec4{static_cast<f32>(behavior), static_cast<f32>(nx), static_cast<f32>(ny), static_cast<f32>(nz)};
        u.options.x = e.count > kDisperse ? std::clamp(finite_or(e.f(kDisperse), 0.0f), 0.0f, 1000.0f) : 0.0f;
        if (e.pathSamples && e.pathSamples->size() >= kPathOffset) {
            const auto& data = *e.pathSamples;
            u.options.y = static_cast<f32>(std::min<usize>(data.size() - kPathOffset, kPathSamples));
            u.options.z = data.front().x + data.front().y * 2.0f + data.front().z * 4.0f;
            std::copy_n(data.begin() + kAppearanceOffset, kLifeSamples, u.life);
            std::copy_n(data.begin() + kOpacityOffset, 2, u.opacity);
            std::copy_n(data.begin() + kPathOffset, static_cast<u32>(u.options.y), u.path);
        }
        draw = now >= 0.0f && (behavior == FormGrid || rate > 0.0f)
            && (behavior != AlongPath || u.options.y >= 2.0f);

        u.region = Vec4{region.x, region.y, region.w, region.h};
        // Câmera padrão da composição: 40° na vertical, olhando o centro.
        const f32 focal = comp.y * 0.5f / std::tan(20.0f * 0.01745329252f);
        u.view = Vec4{layerSize.x * 0.5f, layerSize.y * 0.5f, focal, 0.0f};
        u.clock = Vec4{now, grid, static_cast<f32>(slots), std::min(rate, grid)};
        u.frame = Vec4{comp.x, comp.y, layerSize.x * 0.5f, layerSize.y * 0.5f};
        u.emitPos = Vec4{pct(e, kPositionX, -400.0f, 400.0f), pct(e, kPositionY, -400.0f, 400.0f),
                         pct(e, kPositionZ, -400.0f, 400.0f), e.b(kEmitterSphere) ? 1.0f : 0.0f};
        u.emitSize = Vec4{pct(e, kEmitterW, 0.0f, 400.0f), pct(e, kEmitterH, 0.0f, 400.0f),
                          pct(e, kEmitterD, 0.0f, 400.0f), std::clamp(finite_or(e.f(kSeed), 0.0f), 0.0f, 9999.0f)};
        constexpr f32 kDeg = 0.01745329252f;
        u.launch = Vec4{std::clamp(finite_or(e.f(kVelocity), 0.0f), 0.0f, 60000.0f), pct(e, kVelocityRandom, 0.0f, 100.0f),
                        std::clamp(finite_or(e.f(kDirectionTilt), 0.0f), 0.0f, 180.0f) * kDeg,
                        std::clamp(finite_or(e.f(kDirectionSpin), 0.0f), 0.0f, 360.0f) * kDeg};
        u.cone = Vec4{std::clamp(finite_or(e.f(kSpread), 0.0f), 0.0f, 180.0f), pct(e, kOutwards, 0.0f, 100.0f),
                      pct(e, kStretch, 0.0f, 600.0f), pct(e, kFeather, 0.0f, 100.0f)};
        u.forces = Vec4{finite_or(e.f(kGravity), 0.0f), finite_or(e.f(kWindX), 0.0f), finite_or(e.f(kWindY), 0.0f),
                        finite_or(e.f(kWindZ), 0.0f)};
        // Obturador: o do efeito, ou o da composição quando a camada está com
        // a chave de desfoque de movimento ligada. Meia janela, em segundos.
        f32 shutterDeg = 0.0f;
        if (e.b(kMotionBlur)) shutterDeg = std::clamp(finite_or(e.f(kShutterAngle), 0.0f), 0.0f, 720.0f);
        else if (e.placement) shutterDeg = std::clamp(finite_or(e.placement->shutterAngle, 0.0f), 0.0f, 2880.0f);
        const f32 halfShutter = shutterDeg / 360.0f / static_cast<f32>(fps) * 0.5f;
        if (!e.b(kMotionBlur) && e.placement && shutterDeg > 0.f)
            u.clock.x += (finite_or(e.placement->shutterPhase, -shutterDeg * .5f) + shutterDeg * .5f)
                / (360.f * static_cast<f32>(fps));
        u.motion = Vec4{pct(e, kAirDrag, 0.0f, 800.0f), std::clamp(finite_or(e.f(kTurbulence), 0.0f), 0.0f, 40000.0f),
                        pct(e, kTurbulenceSpeed, 0.0f, 1000.0f), halfShutter};
        u.lifeSize = Vec4{lifeMs, lifeRandom, std::clamp(finite_or(e.f(kSize), 0.0f), 0.0f, 20000.0f),
                          pct(e, kSizeRandom, 0.0f, 100.0f)};
        u.look = Vec4{pct(e, kSizeEnd, 0.0f, 800.0f), pct(e, kOpacity, 0.0f, 100.0f), pct(e, kFadeIn, 0.0f, 100.0f),
                      pct(e, kFadeOut, 0.0f, 100.0f)};
        u.color0 = linear(e.color(kColor));
        u.color0.w = pct(e, kColorRandom, 0.0f, 100.0f);
        u.color1 = linear(e.color(kColorEnd));
        u.color1.w = 0.0f;
        u.shape = Vec4{static_cast<f32>(std::min<u32>(e.e(kShape), kShapeCount - 1)),
                       std::clamp(finite_or(e.f(kSpin), 0.0f), 0.0f, 1440.0f) * 0.01745329252f,
                       pct(e, kFlutter, 0.0f, 100.0f), pct(e, kTwinkle, 0.0f, 100.0f)};
        if (e.placement && e.placement->particleSpace) {
            // Camada do Particular: a rotação da camada gira o espaço das
            // partículas; a câmera é a da composição.
            u.worldFromLayer = e.placement->worldFromLayer;
            u.compFromWorld = e.placement->compFromWorld;
            u.camRight = Vec4{e.placement->camRight.x, e.placement->camRight.y, e.placement->camRight.z, 1.0f};
            u.camUp = Vec4{e.placement->camUp.x, e.placement->camUp.y, e.placement->camUp.z, static_cast<f32>(fps)};
            u.previousParticleProjection = e.placement->previousParticleProjection;
            u.layerFromComp = e.placement->particleLayerFromComp;
        }
        plexusVerts = 0;
        if (plexus && behavior == Continuous) {
            const u32 points = std::min(slots, static_cast<u32>(std::clamp(e.e(kPlexusPoints), 2u, kMaxPlexusPoints)));
            u.plexus = Vec4{std::clamp(finite_or(e.f(kPlexusDistance), 0.0f), 0.0f, 3000.0f),
                            std::clamp(finite_or(e.f(kPlexusWidth), 1.0f), 0.2f, 12.0f) / texel,
                            pct(e, kPlexusOpacity, 0.0f, 100.0f), static_cast<f32>(points)};
            if (u.plexus.x > 0.0f) plexusVerts = points * (points - 1) / 2 * 6;
            else u.plexus = Vec4{};
        }
        surfaceVerts = 0;
        if (plexusVerts && e.b(kPlexusSurface)) {
            const f32 opacity = pct(e, kPlexusSurfaceOpacity, 0.0f, 100.0f);
            const u32 sp = std::min(static_cast<u32>(u.plexus.w),
                                    static_cast<u32>(std::clamp(e.e(kPlexusSurfacePoints), 3u, kMaxSurfacePoints)));
            if (opacity > 0.0f && sp >= 3) {
                u.plexusSurface = Vec4{1.0f, opacity, static_cast<f32>(sp), 0.0f};
                surfaceVerts = sp * (sp - 1) * (sp - 2) / 6 * 3;
            }
        }

    }

    // Com camadas 3D ao lado, as partículas vão para o passe da cena: a profundidade
    // decide quem aparece, não a ordem na timeline. A rede (plexus) é desenhada em
    // px da tela e "Mostrar origem" precisa da camada: esses ficam na folha.
    bool scene_capable(const EffectEval& e) const noexcept override {
        const bool sprite = e.count > kSpriteLayer && samples_sprite(e.e(kShape)) && e.value(kSpriteLayer).ref != 0;
        return e.placement && e.placement->particleSpace && !e.b(kPlexus) && !e.b(kShowSource) && !sprite;
    }

    bool scene_draw(const EffectEval& e, const Mat4& clip, f32 opacity, void* uniforms,
                    EffectSceneDraw& out) const noexcept override {
        if (!uniforms || !scene_capable(e)) return false;
        static_assert(sizeof(ParticularUniforms) <= kEffectSceneUniformBytes, "uniforms do Particular na cena");
        const Vec2 comp{static_cast<f32>(e.placement->compWidth), static_cast<f32>(e.placement->compHeight)};
        const Vec2 layerSize{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
        ParticularUniforms u{};
        u32 slots = 0, plexusVerts = 0, surfaceVerts = 0;
        bool draw = false;
        make_uniforms(e, Rect{0.0f, 0.0f, layerSize.x, layerSize.y}, comp, layerSize, 1.0f, u, slots, plexusVerts, surfaceVerts, draw);
        if (!draw) return false;
        u.look.y *= std::clamp(opacity, 0.0f, 1.0f);
        u.view.w = 1.0f;          // particular.vert: `layerFromComp` é o clip da cena
        u.layerFromComp = clip;
        std::memcpy(uniforms, &u, sizeof(u));
        out.vertex = ShaderId::effects_particular_vert;
        out.fragment = ShaderId::effects_particular_frag;
        out.vertices = slots * 6u;
        out.uniformBytes = static_cast<u32>(sizeof(u));
        out.additive = e.b(kAddMode);
        return true;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        // A saída cobre o que o quadro mostra: as partículas voam para fora
        // da caixa da camada.
        Rect region = input.region;
        Vec2 comp{input.region.w, input.region.h};
        Vec2 layerSize{input.region.w, input.region.h};
        if (e.placement) {
            if (e.placement->compWidth && e.placement->compHeight) {
                comp = Vec2{static_cast<f32>(e.placement->compWidth), static_cast<f32>(e.placement->compHeight)};
            }
            if (e.placement->layerWidth && e.placement->layerHeight) {
                layerSize = Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
            }
            const Rect vis = visible_layer_rect(*e.placement);
            if (vis.w > 0.5f && vis.h > 0.5f) {
                region = Rect{vis.x - margin - 1.0f, vis.y - margin - 1.0f,
                              vis.w + 2.0f * margin + 2.0f, vis.h + 2.0f * margin + 2.0f};
            }
        }
        const f32 texel = std::max(input.texel_scale_x(), 1e-4f);
        u32 w = 0, h = 0;
        ctx.region_size(region, texel, w, h);
        if (!w || !h) return Status{Errc::InvalidArgument, "particular: regiao vazia"};

        ParticularUniforms u{};
        u32 slots = 0, plexusVerts = 0, surfaceVerts = 0;
        bool draw = false;
        make_uniforms(e, region, comp, layerSize, texel, u, slots, plexusVerts, surfaceVerts, draw);

        const bool additive = e.b(kAddMode);
        const FGTexture sprites = ctx.texture("particular-particulas", w, h);
        const u64 spriteRef = e.count > kSpriteLayer && samples_sprite(e.e(kShape)) ? e.value(kSpriteLayer).ref : 0;
        const LayerImage* sprite = spriteRef ? ctx.layer_input(spriteRef) : nullptr;
        u.options.w = sprite && sprite->valid() ? 1.0f : 0.0f;
        if (ctx.geometry_pass("particular", PassStage::Effects, sprites, ShaderId::effects_particular_vert,
                              ShaderId::effects_particular_frag,
                              u.options.w > 0.5f
                                  ? std::initializer_list<PassTexture>{{sprite->texture, {}, CommonSampler::LinearBorder}}
                                  : std::initializer_list<PassTexture>{},
                              &u, sizeof(u), draw ? slots * 6u + plexusVerts + surfaceVerts : 0u, false,
                              true, additive ? BlendMode::Add : BlendMode::Normal) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }

        struct ComposeUniforms {
            Vec4 uvMap;
            Vec4 mode;
        } cu{};
        cu.uvMap = EffectBuildContext::uv_map(region, input.region);
        cu.mode = Vec4{additive ? 1.0f : 0.0f, e.b(kShowSource) ? 1.0f : 0.0f, 0.0f, 0.0f};
        out.region = region;
        out.width = w;
        out.height = h;
        out.texture = ctx.texture("particular", w, h);
        if (ctx.fullscreen_pass("particular-composicao", PassStage::Effects, out.texture,
                                ShaderId::effects_particular_compose_frag,
                                {PassTexture{sprites, {}, CommonSampler::LinearClamp},
                                 PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &cu, sizeof(cu)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// --- Presets de Partículas 3D --------------------------------------------------
// Valores nas unidades guardadas (%, ms, graus, px). Cores em sRGB, como o
// usuário escolhe no seletor.
struct PresetValue { u32 param = kParamCount; f32 v = 0.0f; };   // kParamCount = fim da lista
struct PresetColor { f32 r, g, b; };
struct Preset {
    PresetValue values[32];
    PresetColor color, colorEnd;
};

const Preset& preset_table(u32 i) noexcept {
    static const Preset kPresets[kPresetCount] = {
        // 0 Padrão: os padrões da declaração.
        {{}, {1, 1, 1}, {1, 1, 1}},
        // 1 Chuva
        {{{kRate, 500}, {kPreRoll, 3000}, {kPositionY, -10}, {kEmitterW, 160}, {kEmitterH, 240}, {kEmitterD, 160},
          {kVelocity, 1800}, {kVelocityRandom, 25}, {kDirectionTilt, 180}, {kSpread, 4}, {kGravity, 0},
          {kWindX, 160}, {kAirDrag, 0}, {kTurbulence, 0}, {kLife, 2200}, {kLifeRandom, 25}, {kSize, 7},
          {kSizeRandom, 50}, {kSizeEnd, 100}, {kOpacity, 42}, {kFadeIn, 3}, {kFadeOut, 8}, {kFeather, 100},
          {kStretch, 320}, {kAddMode, 0}},
         {0.78f, 0.86f, 1.0f}, {0.78f, 0.86f, 1.0f}},
        // 2 Neve
        {{{kRate, 180}, {kPreRoll, 9000}, {kEmitterW, 160}, {kEmitterH, 240}, {kEmitterD, 200}, {kVelocity, 70},
          {kVelocityRandom, 60}, {kDirectionTilt, 180}, {kSpread, 30}, {kGravity, 35}, {kWindX, 45}, {kAirDrag, 60},
          {kTurbulence, 140}, {kTurbulenceSpeed, 35}, {kLife, 9000}, {kLifeRandom, 45}, {kSize, 15}, {kSizeRandom, 70},
          {kSizeEnd, 100}, {kOpacity, 90}, {kFadeIn, 6}, {kFadeOut, 12}, {kFeather, 100}, {kStretch, 0}, {kAddMode, 0}},
         {1, 1, 1}, {1, 1, 1}},
        // 3 Fogo
        {{{kRate, 300}, {kPreRoll, 1500}, {kEmitterW, 18}, {kEmitterH, 3}, {kEmitterD, 10}, {kVelocity, 420},
          {kVelocityRandom, 50}, {kDirectionTilt, 0}, {kSpread, 22}, {kGravity, -260}, {kAirDrag, 120},
          {kTurbulence, 90}, {kTurbulenceSpeed, 160}, {kLife, 1400}, {kLifeRandom, 40}, {kSize, 46}, {kSizeRandom, 50},
          {kSizeEnd, 15}, {kOpacity, 85}, {kFadeIn, 8}, {kFadeOut, 55}, {kFeather, 100}, {kStretch, 60}, {kAddMode, 1}},
         {1.0f, 0.85f, 0.35f}, {0.95f, 0.15f, 0.03f}},
        // 4 Faíscas
        {{{kRate, 220}, {kPreRoll, 800}, {kEmitterW, 2}, {kEmitterH, 2}, {kEmitterD, 2}, {kVelocity, 1100},
          {kVelocityRandom, 70}, {kDirectionTilt, 35}, {kSpread, 45}, {kGravity, 900}, {kAirDrag, 100},
          {kTurbulence, 40}, {kTurbulenceSpeed, 200}, {kLife, 1200}, {kLifeRandom, 50}, {kSize, 8}, {kSizeRandom, 60},
          {kSizeEnd, 5}, {kOpacity, 100}, {kFadeIn, 2}, {kFadeOut, 50}, {kFeather, 85}, {kStretch, 260},
          {kColorRandom, 15}, {kAddMode, 1}},
         {1.0f, 0.9f, 0.55f}, {1.0f, 0.25f, 0.05f}},
        // 5 Fogos de artifício
        {{{kRate, 60}, {kPreRoll, 0}, {kEmitterW, 0}, {kEmitterH, 0}, {kEmitterD, 0}, {kVelocity, 900},
          {kVelocityRandom, 35}, {kSpread, 180}, {kGravity, 380}, {kAirDrag, 160}, {kTurbulence, 0}, {kLife, 2200},
          {kLifeRandom, 30}, {kSize, 12}, {kSizeRandom, 60}, {kSizeEnd, 10}, {kOpacity, 100}, {kFadeIn, 2},
          {kFadeOut, 60}, {kFeather, 100}, {kStretch, 140}, {kColorRandom, 60}, {kAddMode, 1}},
         {1.0f, 0.95f, 0.7f}, {1.0f, 0.3f, 0.1f}},
        // 6 Poeira
        {{{kRate, 120}, {kPreRoll, 8000}, {kEmitterW, 140}, {kEmitterH, 140}, {kEmitterD, 160}, {kVelocity, 30},
          {kVelocityRandom, 80}, {kSpread, 180}, {kGravity, 0}, {kWindX, 25}, {kAirDrag, 40}, {kTurbulence, 70},
          {kTurbulenceSpeed, 25}, {kLife, 10000}, {kLifeRandom, 50}, {kSize, 8}, {kSizeRandom, 80}, {kSizeEnd, 100},
          {kOpacity, 50}, {kFadeIn, 15}, {kFadeOut, 30}, {kFeather, 100}, {kStretch, 0}, {kAddMode, 1}},
         {1.0f, 0.97f, 0.9f}, {1.0f, 0.97f, 0.9f}},
        // 8 Rede (plexus): pontos poucos e lentos, ligados quando perto.
        // (entra pelo fim da tabela; ver abaixo)
        // 7 Bokeh
        {{{kRate, 40}, {kPreRoll, 12000}, {kEmitterW, 160}, {kEmitterH, 160}, {kEmitterD, 240}, {kVelocity, 12},
          {kVelocityRandom, 100}, {kSpread, 180}, {kGravity, 0}, {kAirDrag, 0}, {kTurbulence, 25},
          {kTurbulenceSpeed, 15}, {kLife, 14000}, {kLifeRandom, 50}, {kSize, 10}, {kSizeRandom, 90}, {kSizeEnd, 120},
          {kOpacity, 90}, {kFadeIn, 20}, {kFadeOut, 30}, {kFeather, 100}, {kStretch, 0}, {kColorRandom, 25},
          {kAddMode, 1}},
         {0.85f, 0.92f, 1.0f}, {1.0f, 0.95f, 0.85f}},
        // 8 Rede (plexus)
        {{{kRate, 12}, {kPreRoll, 20000}, {kEmitterW, 150}, {kEmitterH, 130}, {kEmitterD, 90}, {kVelocity, 22},
          {kVelocityRandom, 80}, {kSpread, 180}, {kGravity, 0}, {kAirDrag, 0}, {kTurbulence, 45},
          {kTurbulenceSpeed, 18}, {kLife, 14000}, {kLifeRandom, 10}, {kSize, 9}, {kSizeRandom, 45}, {kSizeEnd, 100},
          {kOpacity, 100}, {kFadeIn, 8}, {kFadeOut, 8}, {kFeather, 70}, {kStretch, 0}, {kAddMode, 1},
          {kPlexus, 1}, {kPlexusDistance, 230}, {kPlexusWidth, 1.3f}, {kPlexusOpacity, 65}, {kPlexusPoints, 200}},
         {1, 1, 1}, {0.85f, 0.92f, 1.0f}},
        // 9 Borboletas: poucas, lentas, vagando e batendo as asas.
        {{{kRate, 5}, {kPreRoll, 16000}, {kEmitterW, 140}, {kEmitterH, 130}, {kEmitterD, 120}, {kVelocity, 45},
          {kVelocityRandom, 60}, {kSpread, 180}, {kGravity, -4}, {kAirDrag, 30}, {kTurbulence, 150},
          {kTurbulenceSpeed, 28}, {kLife, 14000}, {kLifeRandom, 30}, {kSize, 64}, {kSizeRandom, 40},
          {kSizeEnd, 100}, {kOpacity, 100}, {kFadeIn, 8}, {kFadeOut, 12}, {kFeather, 4}, {kStretch, 0},
          {kColorRandom, 55}, {kAddMode, 0}, {kShape, kShapeButterfly}, {kFlutter, 100}},
         {1.0f, 0.62f, 0.12f}, {1.0f, 0.62f, 0.12f}},
        // 10 Folhas caindo: tombando e girando com o vento, outono no fim da vida.
        {{{kRate, 24}, {kPreRoll, 12000}, {kEmitterW, 170}, {kEmitterH, 240}, {kEmitterD, 160}, {kVelocity, 70},
          {kVelocityRandom, 50}, {kDirectionTilt, 180}, {kSpread, 25}, {kGravity, 28}, {kWindX, 70}, {kAirDrag, 60},
          {kTurbulence, 120}, {kTurbulenceSpeed, 40}, {kLife, 11000}, {kLifeRandom, 35}, {kSize, 46},
          {kSizeRandom, 45}, {kSizeEnd, 100}, {kOpacity, 100}, {kFadeIn, 5}, {kFadeOut, 10}, {kFeather, 4},
          {kAddMode, 0}, {kShape, kShapeLeaf}, {kSpin, 80}, {kFlutter, 50}},
         {0.55f, 0.72f, 0.16f}, {0.92f, 0.42f, 0.08f}},
        // 11 Pétalas (cerejeira)
        {{{kRate, 45}, {kPreRoll, 10000}, {kEmitterW, 170}, {kEmitterH, 240}, {kEmitterD, 180}, {kVelocity, 60},
          {kVelocityRandom, 60}, {kDirectionTilt, 180}, {kSpread, 30}, {kGravity, 22}, {kWindX, 90}, {kAirDrag, 60},
          {kTurbulence, 110}, {kTurbulenceSpeed, 45}, {kLife, 9000}, {kLifeRandom, 40}, {kSize, 26},
          {kSizeRandom, 45}, {kSizeEnd, 100}, {kOpacity, 95}, {kFadeIn, 5}, {kFadeOut, 12}, {kFeather, 6},
          {kAddMode, 0}, {kShape, kShapePetal}, {kSpin, 120}, {kFlutter, 70}},
         {1.0f, 0.80f, 0.88f}, {1.0f, 0.66f, 0.78f}},
        // 12 Corações: sobem de baixo, balançando de leve.
        {{{kRate, 14}, {kPreRoll, 4000}, {kPositionY, 55}, {kEmitterW, 120}, {kEmitterH, 4}, {kEmitterD, 60},
          {kVelocity, 170}, {kVelocityRandom, 40}, {kDirectionTilt, 0}, {kSpread, 18}, {kGravity, -25},
          {kAirDrag, 20}, {kTurbulence, 70}, {kTurbulenceSpeed, 40}, {kLife, 5500}, {kLifeRandom, 30}, {kSize, 54},
          {kSizeRandom, 45}, {kSizeEnd, 70}, {kOpacity, 100}, {kFadeIn, 10}, {kFadeOut, 40}, {kFeather, 6},
          {kAddMode, 0}, {kShape, kShapeHeart}, {kFlutter, 25}},
         {1.0f, 0.22f, 0.42f}, {1.0f, 0.52f, 0.70f}},
        // 13 Estrelas cintilantes
        {{{kRate, 36}, {kPreRoll, 6000}, {kEmitterW, 160}, {kEmitterH, 160}, {kEmitterD, 100}, {kVelocity, 6},
          {kVelocityRandom, 100}, {kSpread, 180}, {kGravity, 0}, {kAirDrag, 0}, {kTurbulence, 0}, {kLife, 5000},
          {kLifeRandom, 50}, {kSize, 28}, {kSizeRandom, 70}, {kSizeEnd, 100}, {kOpacity, 100}, {kFadeIn, 25},
          {kFadeOut, 35}, {kFeather, 20}, {kAddMode, 1}, {kShape, kShapeSparkle}, {kSpin, 25}, {kTwinkle, 85}},
         {1.0f, 0.95f, 0.72f}, {0.82f, 0.90f, 1.0f}},
        // 14 Vaga-lumes: pontos quentes vagando, acendendo e apagando.
        {{{kRate, 10}, {kPreRoll, 15000}, {kEmitterW, 150}, {kEmitterH, 150}, {kEmitterD, 120}, {kVelocity, 20},
          {kVelocityRandom, 80}, {kSpread, 180}, {kGravity, 0}, {kAirDrag, 20}, {kTurbulence, 95},
          {kTurbulenceSpeed, 25}, {kLife, 9000}, {kLifeRandom, 40}, {kSize, 16}, {kSizeRandom, 50}, {kSizeEnd, 100},
          {kOpacity, 100}, {kFadeIn, 15}, {kFadeOut, 20}, {kFeather, 100}, {kAddMode, 1}, {kTwinkle, 100}},
         {0.85f, 1.0f, 0.35f}, {0.60f, 1.0f, 0.30f}},
        // 15 Confetes: caem de cima girando, todas as cores.
        {{{kRate, 110}, {kPreRoll, 3000}, {kPositionY, -58}, {kEmitterW, 170}, {kEmitterH, 4}, {kEmitterD, 120},
          {kVelocity, 260}, {kVelocityRandom, 50}, {kDirectionTilt, 180}, {kSpread, 25}, {kGravity, 180},
          {kWindX, 30}, {kAirDrag, 150}, {kTurbulence, 60}, {kTurbulenceSpeed, 60}, {kLife, 5500}, {kLifeRandom, 25},
          {kSize, 20}, {kSizeRandom, 40}, {kSizeEnd, 100}, {kOpacity, 100}, {kFadeIn, 2}, {kFadeOut, 10},
          {kFeather, 2}, {kColorRandom, 100}, {kAddMode, 0}, {kShape, kShapeConfetti}, {kSpin, 360}, {kFlutter, 100}},
         {1.0f, 0.35f, 0.45f}, {1.0f, 0.35f, 0.45f}},
        // 16 Flocos de neve: a Neve com flocos de seis pontas.
        {{{kRate, 70}, {kPreRoll, 9000}, {kEmitterW, 160}, {kEmitterH, 240}, {kEmitterD, 200}, {kVelocity, 60},
          {kVelocityRandom, 60}, {kDirectionTilt, 180}, {kSpread, 30}, {kGravity, 30}, {kWindX, 40}, {kAirDrag, 60},
          {kTurbulence, 120}, {kTurbulenceSpeed, 30}, {kLife, 10000}, {kLifeRandom, 45}, {kSize, 30}, {kSizeRandom, 60},
          {kSizeEnd, 100}, {kOpacity, 92}, {kFadeIn, 6}, {kFadeOut, 12}, {kFeather, 10}, {kAddMode, 0},
          {kShape, kShapeSnowflake}, {kSpin, 30}},
         {1, 1, 1}, {0.88f, 0.94f, 1.0f}},
    };
    return kPresets[std::min(i, kPresetCount - 1)];
}

} // namespace

void register_particular_effect(EffectRegistry& r) {
    (void)r.add(std::make_unique<Particular>());
}

} // namespace aurea::builtin

namespace aurea::particular {

bool apply_preset(EffectInstance& instance, const ParameterRegistry& params, u32 preset) noexcept {
    if (preset >= kPresetCount || params.count() < kParamCount) return false;
    const ParamValue seed = instance.params.size() > kSeed ? instance.params[kSeed].constant : ParamValue::scalar(0.0f);
    initialize_instance(instance, params);
    instance.params[kSeed].constant = seed;
    const builtin::Preset& p = builtin::preset_table(preset);
    for (const builtin::PresetValue& v : p.values) {
        if (v.param >= kParamCount) break;
        if (v.param < instance.params.size()) instance.params[v.param].constant = ParamValue::scalar(v.v);
    }
    instance.params[kColor].constant = ParamValue::color(p.color.r, p.color.g, p.color.b, 1.0f);
    instance.params[kColorEnd].constant = ParamValue::color(p.colorEnd.r, p.colorEnd.g, p.colorEnd.b, 1.0f);
    return true;
}

} // namespace aurea::particular
