// =============================================================================
//  Hollow Box (0.4.5, pedido de usuários: "Hollow box como no AM").
//
//  Referência de COMPORTAMENTO: o efeito Hollow Box do Alight Motion (guia
//  público do AM): a camada vira a textura de uma CAIXA 3D OCA — seis lados com
//  espessura, cada um com estilo próprio (normal, oculto, recorte ou corte
//  circular/quadrado), lados que se afastam abrindo as emendas, cantos
//  arredondados, giro 3D e luz. A saída continua sendo uma imagem 2D da camada.
//  A matemática (campo de distância marchado no shader) e o shader
//  (shaders/effects/hollow_box.frag) são nossos.
//
//  Medidas: largura e altura em % da camada, profundidade, espessura, afastar
//  e arredondar em % do lado menor dela — a prévia e o export desenham o mesmo.
//  O custo é um laço de marcha com teto ("Precisão"), por pixel da região da
//  caixa. Os índices de parâmetro ficam fixos nos projetos salvos.
// =============================================================================
#include "CCCommon.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

using namespace cc;

/// Espelho do bloco de uniforms de shaders/effects/hollow_box.frag (std140,
/// só vec4). O começo é o mesmo de `cc::Uniforms` (dst, src, box, info) e a
/// luz mora nos mesmos p4/p5/color de `cc::light_uniforms`.
struct HollowUniforms {
    Vec4 dst, src, box, info;
    Vec4 p0, p1, p2;    ///< xyz = linhas da rotação inversa (vista → objeto)
    Vec4 p3;            ///< meia largura, meia altura, meia profundidade, espessura (px)
    Vec4 p4, p5;        ///< luz (cc::light_uniforms)
    Vec4 color;         ///< cor da luz, metal
    Vec4 shape;         ///< afastar, arredondar (px), tamanho do recorte (0..1), passos da marcha
    Vec4 view;          ///< centro (px), distância da câmera (0 = sem perspectiva), luz ligada
    Vec4 sides0;        ///< estilos: frente, trás, esquerda, direita
    Vec4 sides1;        ///< estilos: topo, base
};
static_assert(sizeof(HollowUniforms) == 240, "layout std140 de hollow_box.frag");

const char* const kSideStyles[] = {"Normal", "Oculto", "Recorte circular", "Corte circular", "Recorte quadrado",
                                   "Corte quadrado"};
constexpr u32 kSideStyleCount = 6;

class HollowBox final : public Effect {
public:
    enum : u32 { kWidth = 0, kHeight, kDepth, kThickness, kOutset, kSmoothing, kRotX, kRotY, kRotZ, kPosition,
                 kScale, kPerspective, kFront, kBack, kLeft, kRight, kTop, kBottom, kCutout, kShading, kLight,
                 kPrecision = kLight + kLightParams };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.distort.hollow_box", "Hollow Box", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("width", "Largura", 100.0f, 1.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.5f, 2000.0f);
        p.add_float("height", "Altura", 100.0f, 1.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.5f, 2000.0f);
        p.add_float("depth", "Profundidade", 100.0f, 1.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.5f, 2000.0f);
        p.add_float("thickness", "Espessura", 5.0f, 0.5f, 50.0f, kAnimPct, "%");
        p.typed_range(0.1f, 500.0f);
        p.add_float("outset", "Afastar os lados", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("smoothing", "Arredondar", 0.0f, 0.0f, 25.0f, kAnimPct, "%");
        p.typed_range(0.0f, 250.0f);
        p.add_angle("rotation_x", "Rotação X", -25.0f);
        p.add_angle("rotation_y", "Rotação Y", 35.0f);
        p.add_angle("rotation_z", "Rotação Z", 0.0f);
        p.add_point2("position", "Posição", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("scale", "Escala", 55.0f, 1.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.5f, 1000.0f);
        p.add_float("perspective", "Perspectiva", 30.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("front", "Frente", kSideStyles, kSideStyleCount, 0);
        p.add_enum("back", "Trás", kSideStyles, kSideStyleCount, 0);
        p.add_enum("left", "Esquerda", kSideStyles, kSideStyleCount, 0);
        p.add_enum("right", "Direita", kSideStyles, kSideStyleCount, 0);
        p.add_enum("top", "Topo", kSideStyles, kSideStyleCount, 0);
        p.add_enum("bottom", "Base", kSideStyles, kSideStyleCount, 0);
        p.add_float("cutout", "Tamanho do recorte", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("shading", "Luz", true);
        declare_light(p, LightDefaults{100.0f, -45.0f, 60.0f, 35.0f, 75.0f, 35.0f, 30.0f, 0.0f});
        p.add_int("precision", "Precisão", 48, 10, 200, kParamNone);
        p.set_tip(kCutout, "Vale para os lados em recorte ou corte.");
        p.set_tip(kPrecision, "Mais passos: bordas mais limpas e prévia mais lenta.");
        p.add_section("sides", "Lados", {kFront, kBack, kLeft, kRight, kTop, kBottom, kCutout}, kInvalidIndex, true);
        p.add_section_range("light", "Luz", kLight, kLight + kLightParams - 1, kShading, false);
        p.add_section("quality", "Qualidade", {kPrecision}, kInvalidIndex, false);
    }
    bool needs_full_input() const noexcept override { return true; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kFront] = ParamValue::scalar(1.0f);   // oculto: mostra a caixa por dentro
        v[kScale] = ParamValue::scalar(50.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_hollow_box_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin,
                 LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const f32 side = min_side(box);
        const f32 scale = clampf(e.f(kScale), 0.5f, 1000.0f, 55.0f) / 100.0f;
        auto pct = [&](u32 i, f32 lo, f32 hi, f32 def) { return clampf(e.f(i), lo, hi, def) / 100.0f; };
        const Vec3 half{0.5f * pct(kWidth, 0.5f, 2000.0f, 100.0f) * std::max(box.w, 1.0f) * scale,
                        0.5f * pct(kHeight, 0.5f, 2000.0f, 100.0f) * std::max(box.h, 1.0f) * scale,
                        0.5f * pct(kDepth, 0.5f, 2000.0f, 100.0f) * side * scale};
        const f32 minHalf = std::min(half.x, std::min(half.y, half.z));
        // A parede não passa da metade da caixa (seria maciça); o arredondado
        // não passa da metade da parede (o lado viraria um tubo).
        const f32 thickness = std::clamp(pct(kThickness, 0.1f, 500.0f, 5.0f) * side * scale, 0.5f, std::max(minHalf, 0.5f));
        const f32 outset = pct(kOutset, 0.0f, 1000.0f, 0.0f) * side * scale;
        const f32 smoothing = std::min(pct(kSmoothing, 0.0f, 250.0f, 0.0f) * side * scale, 0.5f * thickness);
        const Vec2 c = layer_point(e, in, kPosition);

        HollowUniforms u{};
        Uniforms rot{};
        rotation_rows(e.f(kRotX), e.f(kRotY), e.f(kRotZ), rot);
        u.p0 = rot.p0; u.p1 = rot.p1; u.p2 = rot.p2;
        u.p3 = Vec4{half.x, half.y, half.z, thickness};
        Uniforms light{};
        light_uniforms(e, kLight, light);
        u.p4 = light.p4; u.p5 = light.p5; u.color = light.color;
        // Raio da esfera que contém a caixa (lados afastados inclusive).
        const f32 bound = std::sqrt((half.x + outset) * (half.x + outset) + (half.y + outset) * (half.y + outset)
                                    + (half.z + outset) * (half.z + outset)) + 1.0f;
        // Perspectiva = campo de visão de 0° a 90° na esfera da caixa; a câmera
        // nunca entra nela.
        const f32 persp = pct(kPerspective, 0.0f, 100.0f, 30.0f);
        f32 camera = 0.0f;
        if (persp > 1e-3f) camera = std::max(bound / std::tan(0.5f * persp * 0.5f * kPi), 1.5f * bound);
        const u32 steps = std::clamp<u32>(static_cast<u32>(std::max(e.value(kPrecision).as_int(), 10)), 10u, 200u);
        u.shape = Vec4{outset, smoothing, pct(kCutout, 0.0f, 100.0f, 50.0f), static_cast<f32>(steps)};
        u.view = Vec4{c.x, c.y, camera, e.b(kShading) ? 1.0f : 0.0f};
        auto style = [&](u32 i) { return static_cast<f32>(std::min(e.e(i), kSideStyleCount - 1)); };
        u.sides0 = Vec4{style(kFront), style(kBack), style(kLeft), style(kRight)};
        u.sides1 = Vec4{style(kTop), style(kBottom), 0.0f, 0.0f};

        // A região é a da caixa PROJETADA (os 8 cantos, lados afastados
        // inclusive), não o quadrado da esfera que a contém: menos pixels
        // marchando e textura menor (beta, nov/2026: "a caixa deixa o projeto
        // pesado"). Mesma projeção do shader: d = vista.xy · c / (c − vista.z).
        f32 x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        const Vec3 hull{half.x + outset + 1.0f, half.y + outset + 1.0f, half.z + outset + 1.0f};
        for (u32 k = 0; k < 8; ++k) {
            const f32 ox = (k & 1u) ? hull.x : -hull.x, oy = (k & 2u) ? hull.y : -hull.y, oz = (k & 4u) ? hull.z : -hull.z;
            // Objeto → vista: as linhas p0..p2 são a rotação inversa (vista → objeto).
            const f32 vx = rot.p0.x * ox + rot.p1.x * oy + rot.p2.x * oz;
            const f32 vy = rot.p0.y * ox + rot.p1.y * oy + rot.p2.y * oz;
            const f32 vz = rot.p0.z * ox + rot.p1.z * oy + rot.p2.z * oz;
            const f32 k2 = camera > 0.0f ? camera / std::max(camera - vz, 1e-3f) : 1.0f;
            x0 = std::min(x0, vx * k2); x1 = std::max(x1, vx * k2);
            y0 = std::min(y0, vy * k2); y1 = std::max(y1, vy * k2);
        }
        const Rect region = grown_region(e, in.region, Rect{c.x + x0 - 2.0f, c.y + y0 - 2.0f, x1 - x0 + 4.0f, y1 - y0 + 4.0f},
                                         margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, in.texel_scale_x(), w, h);
        if (!w || !h) { out = in; return OkStatus; }
        u.dst = Vec4{region.x, region.y, region.w, region.h};
        u.src = Vec4{in.region.x, in.region.y, in.region.w, in.region.h};
        u.box = Vec4{box.x, box.y, box.w, box.h};
        const f32 texel = 0.5f * (region.w / static_cast<f32>(w) + region.h / static_cast<f32>(h));
        u.info = Vec4{std::max(texel, 1e-4f), seconds(e), in.texel_scale_x(), 0.0f};
        out = LayerImage{ctx.texture("caixa-oca", w, h), region, w, h};
        return ctx.fullscreen_pass("caixa-oca", PassStage::Effects, out.texture, ShaderId::effects_hollow_box_frag,
                                   {PassTexture{in.texture, {}, CommonSampler::LinearClamp}}, &u, sizeof(u)) == kInvalidIndex
            ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }
};

} // namespace

void register_hollow_box_effect(EffectRegistry& r) {
    (void)r.add(std::make_unique<HollowBox>());
}

} // namespace aurea::builtin
