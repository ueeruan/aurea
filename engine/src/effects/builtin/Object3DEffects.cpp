// =============================================================================
//  Efeitos 3D do pedido de beta de 12/10/2026.
//
//  Partes dos objetos 3D (como o Text 3D Layout: o efeito não desenha nada no
//  2D, o renderer 3D lê os parâmetros — scene3d/PartEffects.cpp):
//    * Animar parte 3D   — posição/rotação/escala de UMA parte, com keyframes;
//    * Esconder partes 3D — some (ou esmaece) um intervalo de partes;
//    * Trocar textura 3D — uma imagem da galeria cobrindo uma parte (ou todas).
//
//  Geometria na GPU (passe de vértices gerados, como o Desintegrar):
//    * 3D Shatter — a camada quebra em peças com espessura que explodem em 3D;
//    * 3D Stroke  — traço ao longo das máscaras da camada (ou de uma forma
//      pronta), girado em 3D, com início/fim, afinamento e repetições.
//
//  Implementação própria; as referências de comportamento são os efeitos de
//  mesmo nome dos editores de movimento. Os índices de parâmetro ficam fixos
//  nos projetos salvos (acrescentar só no fim).
// =============================================================================
#include "BuiltinEffects.hpp"

#include "aurea/effects/Shatter3D.hpp"
#include "aurea/render/MaskRaster.hpp"
#include "aurea/scene3d/PartEffects.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea::builtin {
namespace {

constexpr u16 kAnim = kParamAnimatable;
constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
constexpr f32 kTau = 6.28318530718f;
constexpr f32 kDeg = 0.01745329252f;

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

/// Tamanho da CAMADA (px): o do posicionamento quando há, senão o da entrada.
Vec2 layer_extent(const EffectEval& e, const LayerImage& input) noexcept {
    if (e.placement && e.placement->layerWidth && e.placement->layerHeight)
        return Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
    return Vec2{input.region.w, input.region.h};
}

Vec4 premultiplied_linear(Vec4 c) noexcept {
    const f32 a = std::clamp(finite_or(c.w, 1.0f), 0.0f, 1.0f);
    return Vec4{Color::srgb_to_linear(std::clamp(finite_or(c.x, 1.0f), 0.0f, 1.0f)) * a,
                Color::srgb_to_linear(std::clamp(finite_or(c.y, 1.0f), 0.0f, 1.0f)) * a,
                Color::srgb_to_linear(std::clamp(finite_or(c.z, 1.0f), 0.0f, 1.0f)) * a, a};
}

/// A região de saída na MESMA grade de texels da entrada (a parte da camada
/// que não muda não é reamostrada — o mesmo cuidado da Malha de deformação).
Rect snap_to_input(const Rect& region, const LayerImage& in) noexcept {
    const f32 tx = in.texel_scale_x(), ty = in.texel_scale_y();
    if (!(tx > 0.0f) || !(ty > 0.0f)) return region;
    const f32 x0 = in.region.x + std::floor((region.x - in.region.x) * tx + 1e-3f) / tx;
    const f32 y0 = in.region.y + std::floor((region.y - in.region.y) * ty + 1e-3f) / ty;
    const f32 x1 = in.region.x + std::ceil((region.x + region.w - in.region.x) * tx - 1e-3f) / tx;
    const f32 y1 = in.region.y + std::ceil((region.y + region.h - in.region.y) * ty - 1e-3f) / ty;
    return Rect{x0, y0, std::max(x1 - x0, 1.0f / tx), std::max(y1 - y0, 1.0f / ty)};
}

// =============================================================================
// Partes dos objetos 3D (lidas pelo renderer 3D)
// =============================================================================
class Part3DTransform final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kPart3DTransform, "Animar parte 3D", "3D", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_int("part", "Parte", 1, 1, static_cast<i32>(scene3d::kPartEffectMaxParts));
        p.add_float("position_x", "Posição X", 0.0f, -300.0f, 300.0f, kAnimPct, "%");
        p.typed_range(-10000.0f, 10000.0f);
        p.add_float("position_y", "Posição Y", 0.0f, -300.0f, 300.0f, kAnimPct, "%");
        p.typed_range(-10000.0f, 10000.0f);
        p.add_float("position_z", "Posição Z", 0.0f, -300.0f, 300.0f, kAnimPct, "%");
        p.typed_range(-10000.0f, 10000.0f);
        p.add_angle("rotation_x", "Rotação X", 0.0f);
        p.add_angle("rotation_y", "Rotação Y", 0.0f);
        p.add_angle("rotation_z", "Rotação Z", 0.0f);
        p.add_float("scale", "Escala", 100.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.0f, 10000.0f);
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

class Part3DHide final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kPart3DHide, "Esconder partes 3D", "3D", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_int("first", "Primeira parte", 1, 1, static_cast<i32>(scene3d::kPartEffectMaxParts));
        p.add_int("last", "Última parte", 1, 1, static_cast<i32>(scene3d::kPartEffectMaxParts));
        p.add_float("hide", "Esconder", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("invert", "Esconder as outras", false);
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

class Part3DTexture final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kPart3DTexture, "Trocar textura 3D", "3D", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_int("part", "Parte", 1, 1, static_cast<i32>(scene3d::kPartEffectMaxParts));
        p.add_texture_ref("image", "Imagem");
        p.add_bool("all_parts", "Todas as partes", false);
        p.add_float("repeat", "Repetir", 100.0f, 10.0f, 1000.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

// =============================================================================
// 3D Shatter
// =============================================================================
class Shatter3D final : public Effect {
public:
    enum : u32 { kProgress = 0, kShape, kPieces, kThickness, kCenter, kForce, kTowardCamera, kSpin, kGravity,
                 kRandomness, kWave, kPerspective, kLight, kFade, kSeed };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kShatter3D, "3D Shatter", "3D", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const shapes[] = {"Quadrados", "Triângulos", "Vidro"};
        p.add_float("progress", "Explosão", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("shape", "Peças", shapes, 3, 2);
        p.add_int("pieces", "Quantidade de peças", 12, 2, 48);
        p.add_float("thickness", "Espessura", 12.0f, 0.0f, 200.0f, kAnim | kParamPixels, "px");
        p.typed_range(0.0f, 2000.0f);
        p.add_point2("center", "Centro da explosão", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnim | kParamRelative);
        p.add_float("force", "Força", 900.0f, 0.0f, 4000.0f, kAnim | kParamPixels, "px");
        p.typed_range(0.0f, 40000.0f);
        p.add_float("toward_camera", "Para a câmera", 40.0f, -100.0f, 100.0f, kAnimPct, "%");
        p.add_angle("spin", "Giro", 540.0f, 0.0f, 3600.0f);
        p.add_float("gravity", "Gravidade", 600.0f, -3000.0f, 3000.0f, kAnim | kParamPixels, "px");
        p.typed_range(-30000.0f, 30000.0f);
        p.add_float("randomness", "Aleatoriedade", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("wave", "Onda", 60.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("perspective", "Perspectiva", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("light", "Luz nas faces", 60.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("fade", "Esmaecer", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_int("seed", "Semente", 0, 0, 9999, kAnim);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(finite_or(e.f(kProgress), 0.0f) > 0.0f);
    }
    // Peças de fora do quadro visível podem voar para dentro dele.
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        PipelineKey k = PipelineKey::graphics(ShaderId::effects_shatter3d_vert, ShaderId::effects_shatter3d_frag, work);
        k.hasDepth = true;
        k.depthTest = true;
        k.depthWrite = true;
        k.depthCompare = CompareOp::GreaterOrEqual;
        k.depthFormat = SurfaceFormat::Depth32F;
        out.push_back(k);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kProgress] = ParamValue::scalar(45.0f);
        v[kForce] = ParamValue::scalar(260.0f);
        return true;
    }

    /// O bloco comum ao desenho 2D e ao da cena (sem `dst` e sem a câmera
    /// própria, view.z). Devolve o número de peças; 0 = camada intacta.
    static u32 fill_uniforms(const EffectEval& e, const LayerImage& input, shatter3d::Uniforms& u) noexcept {
        u = {};
        const Rect src = input.region;
        if (!(src.w > 0.0f) || !(src.h > 0.0f)) return 0;
        const f32 progress = std::clamp(finite_or(e.f(kProgress), 0.0f) / 100.0f, 0.0f, 1.0f);
        if (!(progress > 0.0f)) return 0;
        const bool triangles = e.e(kShape) != 0;
        const f32 cols = static_cast<f32>(std::clamp(e.value(kPieces).as_int(), 2, 48));
        const f32 rows = std::clamp(std::round(cols * src.h / src.w), 1.0f, 96.0f);
        const Vec2 size = layer_extent(e, input);
        const Vec2 rel = e.p2(kCenter);
        const Vec2 center{finite_or(rel.x, 0.5f) * size.x, finite_or(rel.y, 0.5f) * size.y};
        f32 far = 1.0f;
        for (const Vec2 c : {Vec2{src.x, src.y}, Vec2{src.x + src.w, src.y}, Vec2{src.x, src.y + src.h},
                             Vec2{src.x + src.w, src.y + src.h}})
            far = std::max(far, std::hypot(c.x - center.x, c.y - center.y));
        const f32 force = std::clamp(finite_or(e.f(kForce), 900.0f), 0.0f, 40000.0f);
        const f32 toward = std::clamp(finite_or(e.f(kTowardCamera), 40.0f) / 100.0f, -1.0f, 1.0f);
        const f32 gravity = std::clamp(finite_or(e.f(kGravity), 600.0f), -30000.0f, 30000.0f);
        const f32 thickness = std::clamp(finite_or(e.f(kThickness), 12.0f), 0.0f, 2000.0f);
        u.src = Vec4{src.x, src.y, src.w, src.h};
        u.grid = Vec4{cols, rows, static_cast<f32>(e.e(kShape)), static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 0, 9999))};
        u.motion = Vec4{progress, force, std::clamp(finite_or(e.f(kSpin), 540.0f), 0.0f, 3600.0f) * kDeg, gravity};
        u.shape = Vec4{thickness, toward, std::clamp(finite_or(e.f(kRandomness), 50.0f) / 100.0f, 0.0f, 1.0f),
                       std::clamp(finite_or(e.f(kWave), 60.0f) / 100.0f, 0.0f, 1.0f)};
        u.view = Vec4{center.x, center.y, 0.0f, std::clamp(finite_or(e.f(kLight), 60.0f) / 100.0f, 0.0f, 1.0f)};
        u.misc = Vec4{std::clamp(finite_or(e.f(kFade), 0.0f) / 100.0f, 0.0f, 1.0f), src.w / cols, src.h / rows, far};
        return static_cast<u32>(cols * rows) * (triangles ? 2u : 1u);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        // Plano da cena 3D: o grupo desenha as peças como geometria de verdade
        // (scene3d/shatter.vert) a partir da imagem final da camada.
        if (e.placement && e.placement->scenePlane) { out = input; return OkStatus; }
        shatter3d::Uniforms u{};
        const u32 pieces = fill_uniforms(e, input, u);
        if (!pieces) { out = input; return OkStatus; }
        const Rect src = input.region;
        const f32 cols = u.grid.x, rows = u.grid.y;
        const f32 force = u.motion.y, toward = u.shape.y, gravity = u.motion.w, thickness = u.shape.x;
        const f32 persp = std::clamp(finite_or(e.f(kPerspective), 50.0f) / 100.0f, 0.0f, 1.0f);
        const f32 camera = (8.0f + (1.5f - 8.0f) * persp) * std::max(src.w, src.h);

        // Alcance do voo: força (o maior sorteio), queda, espessura e o
        // aumento da perspectiva quando a peça vem para a câmera.
        const f32 cell = std::max(src.w / cols, src.h / rows);
        const f32 reach = (force * 1.7f + std::fabs(gravity) + thickness + cell) * (1.0f + std::max(toward, 0.0f) * 2.0f);
        const bool clipped = e.placement && !e.placement->inScene3d && !e.placement->preserveFullExtent
                          && e.placement->compWidth && e.placement->compHeight;
        const f32 grow = clipped ? reach : std::min(reach, std::max(0.25f * std::max(src.w, src.h), 512.0f));
        const Rect region = spread_region(src, grow, grow, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        if (!w || !h) { out = input; return OkStatus; }

        u.dst = Vec4{region.x, region.y, region.w, region.h};
        u.view.z = camera;

        out = LayerImage{ctx.texture("3d-shatter", w, h), region, w, h};
        if (ctx.geometry_pass("3d-shatter", PassStage::Effects, out.texture, ShaderId::effects_shatter3d_vert,
                              ShaderId::effects_shatter3d_frag,
                              {PassTexture{input.texture, {}, CommonSampler::LinearBorder}}, &u, sizeof(u),
                              pieces * 36u, true) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// =============================================================================
// 3D Stroke
// =============================================================================
/// Teto de pontos do caminho (textura de dado N×1; o vértice lê dois por trecho).
constexpr u32 kStrokeMaxPoints = 1024;
constexpr u32 kStrokeMaxCopies = 12;

struct StrokeUniforms {
    Vec4 dst, path, window, taper, rot, center, copies, color0, color1, compose;
};
static_assert(sizeof(StrokeUniforms) == 160);

class Stroke3D final : public Effect {
public:
    enum : u32 { kPath = 0, kColor, kEndColor, kThickness, kStart, kEnd, kOffset, kTaperStart, kTaperEnd,
                 kRotationX, kRotationY, kRotationZ, kSize, kCenter, kPerspective, kCopies, kCopyDistance,
                 kCopyRotation, kCopyScale, kCopyFade, kComposite, kOpacity };
    enum : u32 { kCircle = 0, kSpiral, kWave, kStar, kHeart, kLine, kMasks };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kStroke3D, "3D Stroke", "3D", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const paths[] = {"Círculo", "Espiral", "Onda", "Estrela", "Coração", "Linha", "Máscaras da camada"};
        static const char* const modes[] = {"Sobre a camada", "Só o traço", "Atrás da camada"};
        p.add_enum("path", "Caminho", paths, 7, kCircle);
        p.add_color("color", "Cor", Vec4{1.0f, 0.82f, 0.3f, 1.0f});
        p.add_color("end_color", "Cor do fim", Vec4{1.0f, 0.35f, 0.55f, 1.0f});
        p.add_float("thickness", "Espessura", 12.0f, 0.0f, 200.0f, kAnim | kParamPixels, "px");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("start", "Início", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("end", "Fim", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("offset", "Deslocamento", 0.0f, -100.0f, 100.0f, kAnimPct, "%");
        p.typed_range(-100000.0f, 100000.0f);
        p.add_float("taper_start", "Afinar no início", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("taper_end", "Afinar no fim", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_angle("rotation_x", "Rotação X", 0.0f);
        p.add_angle("rotation_y", "Rotação Y", 0.0f);
        p.add_angle("rotation_z", "Rotação Z", 0.0f);
        p.add_float("size", "Tamanho", 100.0f, 1.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.1f, 4000.0f);
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnim | kParamRelative);
        p.add_float("perspective", "Perspectiva", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_int("copies", "Repetições", 1, 1, static_cast<i32>(kStrokeMaxCopies));
        p.add_float("copy_distance", "Distância entre repetições", 60.0f, -1000.0f, 1000.0f, kAnim | kParamPixels, "px");
        p.typed_range(-20000.0f, 20000.0f);
        p.add_angle("copy_rotation", "Giro por repetição", 0.0f, -360.0f, 360.0f);
        p.add_float("copy_scale", "Escala por repetição", 100.0f, 10.0f, 200.0f, kAnimPct, "%");
        p.add_float("copy_fade", "Esmaecer repetições", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("composite", "Mostrar", modes, 3, 0);
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.e(kComposite) == 1) return false;   // "só o traço": a camada some mesmo com o traço invisível
        return !(finite_or(e.f(kOpacity), 100.0f) > 0.0f) || !(finite_or(e.f(kThickness), 12.0f) > 0.0f)
            || !(finite_or(e.f(kEnd), 100.0f) > finite_or(e.f(kStart), 0.0f));
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        // Mistura "sobre" + profundidade: as junções dos trechos se sobrepõem
        // (a borda suave de um não apaga o miolo do outro) e o perto cobre o longe.
        PipelineKey k = PipelineKey::graphics(ShaderId::effects_stroke3d_vert, ShaderId::effects_stroke3d_frag, work,
                                              true, BlendMode::Normal);
        k.hasDepth = true;
        k.depthTest = true;
        k.depthWrite = true;
        k.depthCompare = CompareOp::GreaterOrEqual;
        k.depthFormat = SurfaceFormat::Depth32F;
        out.push_back(k);
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_stroke3d_compose_frag, work));
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kThickness] = ParamValue::scalar(24.0f);
        v[kRotationX] = ParamValue::scalar(35.0f);
        v[kCopies] = ParamValue::scalar(3.0f);
        return true;
    }

    /// O caminho no instante (px da camada, relativo ao centro), com o
    /// comprimento acumulado: [0] = (nº de pontos, comprimento, fechado, 0).
    void resolve_resources(EffectEval& e) const noexcept override {
        e.pathSamples.reset();
        e.aux = TextureHandle{};
        if (!e.placement || !e.placement->layerWidth || !e.placement->layerHeight) return;
        const Vec2 size{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
        const Vec2 rel = e.p2(kCenter);
        const Vec2 center{finite_or(rel.x, 0.5f) * size.x, finite_or(rel.y, 0.5f) * size.y};
        const f32 scale = std::clamp(finite_or(e.f(kSize), 100.0f), 0.1f, 4000.0f) / 100.0f;
        const f32 s = std::min(size.x, size.y) * 0.7f * scale;

        // (x, y, -, continua): continua = 0 começa outro caminho.
        std::vector<Vec4> pts;
        bool closed = true;
        auto add = [&](Vec2 q, bool continues) { pts.push_back(Vec4{q.x, q.y, 0.0f, continues ? 1.0f : 0.0f}); };
        const u32 kind = std::min<u32>(e.e(kPath), kMasks);
        switch (kind) {
            case kCircle:
                for (u32 i = 0; i <= 128; ++i) {
                    const f32 a = kTau * static_cast<f32>(i) / 128.0f - kTau * 0.25f;
                    add(Vec2{std::cos(a), std::sin(a)} * (0.5f * s), i > 0);
                }
                break;
            case kSpiral:
                closed = false;
                for (u32 i = 0; i <= 384; ++i) {
                    const f32 t = static_cast<f32>(i) / 384.0f;
                    const f32 a = t * 3.0f * kTau;
                    add(Vec2{std::cos(a), std::sin(a)} * (s * (0.04f + 0.46f * t)), i > 0);
                }
                break;
            case kWave:
                closed = false;
                for (u32 i = 0; i <= 192; ++i) {
                    const f32 t = static_cast<f32>(i) / 192.0f;
                    add(Vec2{(t - 0.5f) * s * 1.4f, std::sin(t * 3.0f * kTau) * 0.15f * s}, i > 0);
                }
                break;
            case kStar:
                for (u32 i = 0; i <= 10; ++i) {
                    const f32 a = kTau * static_cast<f32>(i % 10) / 10.0f - kTau * 0.25f;
                    const f32 r = (i % 2 == 0 ? 0.5f : 0.2f) * s;
                    add(Vec2{std::cos(a), std::sin(a)} * r, i > 0);
                }
                break;
            case kHeart:
                for (u32 i = 0; i <= 160; ++i) {
                    const f32 t = kTau * static_cast<f32>(i) / 160.0f;
                    const f32 x = 16.0f * std::pow(std::sin(t), 3.0f);
                    const f32 y = 13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t);
                    add(Vec2{x, -y} * (s / 34.0f), i > 0);
                }
                break;
            case kLine:
                closed = false;
                add(Vec2{-0.5f * s, 0.0f}, false);
                add(Vec2{0.5f * s, 0.0f}, true);
                break;
            default: {
                // As máscaras da camada, na ordem do painel (use o modo
                // "Desligada" para a máscara só guiar o traço sem recortar).
                if (!e.layer) return;
                std::vector<MaskPoint> points;
                std::vector<Vec4> edges;
                u32 paths = 0;
                closed = false;
                for (const Mask& m : e.layer->masks) {
                    points.clear();
                    edges.clear();
                    mask::evaluate_path(m, e.time_frames(), points);
                    if (points.size() < 2) continue;
                    mask::flatten(points, m.closed, 0.5f, Vec2{-center.x, -center.y}, edges);
                    if (edges.empty()) continue;
                    add(Vec2{edges.front().x, edges.front().y} * scale, false);
                    for (const Vec4& edge : edges) add(Vec2{edge.z, edge.w} * scale, true);
                    closed = m.closed;
                    ++paths;
                }
                if (paths != 1) closed = false;
                break;
            }
        }
        if (pts.size() < 2) return;
        // Trechos longos divididos: o afinamento e o degradê seguem o
        // comprimento, não os poucos pontos de uma estrela ou linha.
        f32 total = 0.0f;
        for (usize i = 1; i < pts.size(); ++i)
            if (pts[i].w > 0.5f) total += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        if (!(total > 1e-3f)) return;
        const f32 step = total / 256.0f;
        std::vector<Vec4> fine;
        fine.reserve(pts.size() + 300);
        fine.push_back(pts.front());
        for (usize i = 1; i < pts.size(); ++i) {
            if (pts[i].w > 0.5f) {
                const f32 len = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
                const u32 parts = std::clamp(static_cast<u32>(std::ceil(len / step)), 1u, 64u);
                for (u32 k = 1; k < parts; ++k) {
                    const f32 t = static_cast<f32>(k) / static_cast<f32>(parts);
                    fine.push_back(Vec4{pts[i - 1].x + (pts[i].x - pts[i - 1].x) * t,
                                        pts[i - 1].y + (pts[i].y - pts[i - 1].y) * t, 0.0f, 1.0f});
                }
            }
            fine.push_back(pts[i]);
        }
        // Teto de pontos: amostra uniforme, sem perder os começos de caminho.
        if (fine.size() > kStrokeMaxPoints) {
            std::vector<Vec4> kept;
            kept.reserve(kStrokeMaxPoints);
            const f64 stride = static_cast<f64>(fine.size()) / static_cast<f64>(kStrokeMaxPoints - 1);
            f64 next = 0.0;
            for (usize i = 0; i < fine.size() && kept.size() < kStrokeMaxPoints; ++i) {
                const bool last = i + 1 == fine.size();
                if (fine[i].w < 0.5f || last || static_cast<f64>(i) >= next) {
                    kept.push_back(fine[i]);
                    next = static_cast<f64>(i) + stride;
                }
            }
            fine.swap(kept);
        }
        f32 cum = 0.0f;
        for (usize i = 0; i < fine.size(); ++i) {
            if (i > 0 && fine[i].w > 0.5f) cum += std::hypot(fine[i].x - fine[i - 1].x, fine[i].y - fine[i - 1].y);
            fine[i].z = cum;
        }
        auto samples = std::make_shared<std::vector<Vec4>>();
        samples->reserve(fine.size() + 1);
        samples->push_back(Vec4{static_cast<f32>(fine.size()), cum, closed ? 1.0f : 0.0f, 0.0f});
        samples->insert(samples->end(), fine.begin(), fine.end());
        u64 key = 1469598103934665603ull ^ 0x53544b33ull;
        for (const Vec4& v : *samples) {
            u32 bits[4];
            std::memcpy(bits, &v, sizeof bits);
            for (const u32 b : bits) key = (key ^ b) * 1099511628211ull;
        }
        if (e.resources) e.aux = e.resources->data_texture(key, samples->data() + 1, static_cast<u32>(fine.size()), 1);
        e.pathSamples = std::move(samples);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const u32 composite = std::min<u32>(e.e(kComposite), 2u);
        const bool ready = e.pathSamples && e.pathSamples->size() >= 3 && e.aux.valid();
        const f32 opacity = std::clamp(finite_or(e.f(kOpacity), 100.0f) / 100.0f, 0.0f, 1.0f);
        const f32 thickness = std::clamp(finite_or(e.f(kThickness), 12.0f), 0.0f, 2000.0f);
        // Sem caminho (nenhuma máscara, camada sem tamanho): a camada como está.
        if (!ready) { out = input; return OkStatus; }

        const Rect src = input.region;
        const Vec2 size = layer_extent(e, input);
        const Vec2 rel = e.p2(kCenter);
        const Vec2 center{finite_or(rel.x, 0.5f) * size.x, finite_or(rel.y, 0.5f) * size.y};
        const f32 persp = std::clamp(finite_or(e.f(kPerspective), 50.0f) / 100.0f, 0.0f, 1.0f);
        const f32 camera = (8.0f + (1.5f - 8.0f) * persp) * std::max(size.x, size.y);
        const u32 copies = static_cast<u32>(std::clamp(e.value(kCopies).as_int(), 1, static_cast<i32>(kStrokeMaxCopies)));
        const f32 copyDistance = std::clamp(finite_or(e.f(kCopyDistance), 60.0f), -20000.0f, 20000.0f);
        const f32 copyRotation = finite_or(e.f(kCopyRotation), 0.0f) * kDeg;
        const f32 copyScale = std::clamp(finite_or(e.f(kCopyScale), 100.0f), 10.0f, 200.0f) / 100.0f;
        const f32 rx = finite_or(e.f(kRotationX), 0.0f) * kDeg;
        const f32 ry = finite_or(e.f(kRotationY), 0.0f) * kDeg;
        const f32 rz = finite_or(e.f(kRotationZ), 0.0f) * kDeg;

        // Caixa do traço na tela: a MESMA conta do vértice (stroke3d.vert).
        f32 minX = src.x, minY = src.y, maxX = src.x + src.w, maxY = src.y + src.h;
        if (composite == 1) { minX = minY = 1e30f; maxX = maxY = -1e30f; }
        u32 count = 0;
        {
            const auto& s = *e.pathSamples;
            count = static_cast<u32>(std::min<f32>(s[0].x, static_cast<f32>(s.size() - 1)));
            for (u32 c = 0; c < copies; ++c) {
                const f32 ci = static_cast<f32>(c);
                const f32 k0 = std::pow(copyScale, ci);
                const f32 cz = std::cos(rz + ci * copyRotation), sz = std::sin(rz + ci * copyRotation);
                const f32 cy = std::cos(ry), sy = std::sin(ry), cx = std::cos(rx), sx = std::sin(rx);
                for (u32 i = 1; i <= count; ++i) {
                    const f32 x0 = s[i].x * k0, y0 = s[i].y * k0;
                    const f32 x1 = cz * x0 - sz * y0, y1 = sz * x0 + cz * y0;              // Z
                    const f32 x2 = cy * x1, z2 = -sy * x1;                                  // Y
                    const f32 y3 = cx * y1 - sx * z2, z3 = sx * y1 + cx * z2;               // X
                    const f32 zc = std::min(z3 - ci * copyDistance, 0.8f * camera);
                    const f32 k = camera / (camera - zc);
                    const f32 px = center.x + x2 * k, py = center.y + y3 * k;
                    const f32 r = 0.5f * thickness * k + 2.0f;
                    minX = std::min(minX, px - r); maxX = std::max(maxX, px + r);
                    minY = std::min(minY, py - r); maxY = std::max(maxY, py + r);
                }
            }
        }
        if (!(maxX > minX) || !(maxY > minY)) { minX = src.x; minY = src.y; maxX = src.x + 1.0f; maxY = src.y + 1.0f; }
        Rect region = spread_region(Rect{minX, minY, maxX - minX, maxY - minY}, 0.0f, 0.0f, e.placement, margin);
        region = snap_to_input(region, input);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        if (!w || !h) { out = input; return OkStatus; }

        StrokeUniforms u{};
        u.dst = Vec4{region.x, region.y, region.w, region.h};
        u.path = Vec4{static_cast<f32>(count), (*e.pathSamples)[0].y, (*e.pathSamples)[0].z,
                      1.0f / std::max(input.texel_scale_x(), 1e-4f)};
        const f32 start = std::clamp(finite_or(e.f(kStart), 0.0f) / 100.0f, 0.0f, 1.0f);
        const f32 end = std::clamp(finite_or(e.f(kEnd), 100.0f) / 100.0f, 0.0f, 1.0f);
        u.window = Vec4{start, std::max(end, start), finite_or(e.f(kOffset), 0.0f) / 100.0f, thickness};
        u.taper = Vec4{std::clamp(finite_or(e.f(kTaperStart), 0.0f) / 100.0f, 0.0f, 1.0f),
                       std::clamp(finite_or(e.f(kTaperEnd), 0.0f) / 100.0f, 0.0f, 1.0f), opacity, static_cast<f32>(composite)};
        u.rot = Vec4{rx, ry, rz, camera};
        u.center = Vec4{center.x, center.y, static_cast<f32>(copies), std::clamp(finite_or(e.f(kCopyFade), 0.0f) / 100.0f, 0.0f, 1.0f)};
        u.copies = Vec4{copyDistance, copyRotation, copyScale, 0.0f};
        u.color0 = premultiplied_linear(e.color(kColor));
        u.color1 = premultiplied_linear(e.color(kEndColor));
        u.compose = EffectBuildContext::uv_map(region, src);

        const FGTexture stroke = ctx.texture("3d-stroke-traco", w, h);
        const u32 vertices = count >= 2 && opacity > 0.0f && thickness > 0.0f ? (count - 1) * 6u * copies : 0u;
        if (ctx.geometry_pass("3d-stroke-traco", PassStage::Effects, stroke, ShaderId::effects_stroke3d_vert,
                              ShaderId::effects_stroke3d_frag,
                              {PassTexture{input.texture, {}, CommonSampler::LinearBorder},
                               PassTexture{{}, e.aux, CommonSampler::NearestClamp}},
                              &u, sizeof(u), vertices, true, true, BlendMode::Normal) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        out = LayerImage{ctx.texture("3d-stroke", w, h), region, w, h};
        if (ctx.fullscreen_pass("3d-stroke", PassStage::Effects, out.texture, ShaderId::effects_stroke3d_compose_frag,
                                {PassTexture{stroke, {}, CommonSampler::LinearClamp},
                                 PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_object3d_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Part3DTransform>());
    (void)r.add(std::make_unique<Part3DHide>());
    (void)r.add(std::make_unique<Part3DTexture>());
    (void)r.add(std::make_unique<Shatter3D>());
    (void)r.add(std::make_unique<Stroke3D>());
}

} // namespace aurea::builtin

namespace aurea::shatter3d {
bool is_shatter(EffectTypeId type) noexcept { return type == effect_type_id(effect_keys::kShatter3D); }
u32 fill(const EffectEval& e, const LayerImage& input, Uniforms& out) noexcept {
    out = {};
    if (!e.effect || !is_shatter(e.effect->type_id())) return 0;
    return builtin::Shatter3D::fill_uniforms(e, input, out);
}
} // namespace aurea::shatter3d
