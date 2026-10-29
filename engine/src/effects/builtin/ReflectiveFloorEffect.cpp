// =============================================================================
//  Aurea / effects / builtin / ReflectiveFloorEffect.cpp
//
//  Reflective Floor (0.4.1, categoria 3D). Pensado para uma CAMADA DE AJUSTE no
//  topo (Engine::add_reflective_floor): a entrada é tudo o que está abaixo —
//  2D, 3D, partículas — e o chão reflete TUDO isso. Abaixo da linha do
//  horizonte: chão opaco em perspectiva (liso, ladrilhos ou gasto) com a imagem
//  espelhada, desfocada pela distância, distorcida pela textura e desbotada.
//  Comportamento especificado pelos plugins de chão reflexivo do After Effects
//  (referência de vídeo); implementação própria, shader reflective_floor.frag.
// =============================================================================
#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

class ReflectiveFloor final : public Effect {
public:
    enum : u32 { Horizon, Reflection, Falloff, Blur, Distortion, Style, Color, Tile, Perspective, Texture, Brightness, Opacity, TextureLayer };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kReflectiveFloor, "Reflective Floor", "3D", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const styles[] = {"Liso", "Ladrilhos", "Ladrilhos gastos"};
        p.add_float("horizon", "Horizonte", 62, 0, 100, kParamAnimatable, "%");
        p.add_float("reflection", "Reflexo", 80, 0, 100, kParamAnimatable, "%");
        p.add_float("falloff", "Distância do reflexo", 60, 1, 100, kParamAnimatable, "%");
        p.add_float("blur", "Desfoque", 3, 0, 40, kParamAnimatable | kParamPixels, "px");
        p.add_float("distortion", "Distorção", 25, 0, 100, kParamAnimatable, "%");
        p.add_enum("floor", "Chão", styles, 3, 2);
        p.add_color("color", "Cor do chão", Vec4{0.03f, 0.03f, 0.034f, 1.0f});
        p.add_float("tile", "Tamanho do ladrilho", 160, 10, 2000, kParamAnimatable | kParamPixels, "px");
        p.add_float("perspective", "Perspectiva", 70, 0, 100, kParamAnimatable, "%");
        p.add_float("texture", "Textura", 70, 0, 100, kParamAnimatable, "%");
        p.add_float("brightness", "Brilho do chão", 30, 0, 100, kParamAnimatable, "%");
        // Numa camada de ajuste a entrada já tem o fundo (opaco): o chão SUBSTITUI o que
        // está abaixo da linha, nesta opacidade.
        p.add_float("opacity", "Opacidade do chão", 100, 0, 100, kParamAnimatable, "%");
        // 0.4.1: a textura do chão pode ser uma camada do projeto (imagem ou vídeo; pode
        // ficar com o olho desligado). Cada ladrilho mostra a imagem inteira.
        p.add_layer_ref("texture_layer", "Textura do chão (camada)");
    }
    i32 input_layer_param() const noexcept override { return TextureLayer; }
    u32 input_layer_count() const noexcept override { return 1; }
    i32 input_layer_param_at(u32 slot) const noexcept override { return slot == 0 ? static_cast<i32>(TextureLayer) : -1; }
    bool needs_full_input() const noexcept override { return true; }
    bool is_identity(const EffectEval& e) const noexcept override {
        // Opacidade 0 com entrada opaca: nada muda. (O horizonte só vale no modo 2D.)
        return e.count > Opacity && finite_or(e.f(Opacity), 100) <= 0.0f && finite_or(e.f(Reflection), 80) <= 0.0f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_reflective_floor_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        const f32 w = e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 h = e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        // 3D (camada 3D): o chão é o PLANO da camada no mundo — segue a câmera 3D e os
        // pais (nulo 3D). O shader acha o ponto do chão de cada pixel pela inversa da
        // homografia px ← chão e o reflexo pelo raio refletido até o plano z = 0 da
        // composição (onde moram as camadas 2D).
        struct Uniforms { EffectUniforms base; Vec4 q0, h0, h1, h2; Mat4 world, proj; } uu{};
        static_assert(sizeof(Uniforms) == 112 + 64 + 128);
        EffectUniforms& u = uu.base;
        u = base_uniforms(input);
        const bool space3d = e.placement && e.placement->layerWorldValid && e.placement->sceneCameraValid;
        if (space3d) {
            const Mat4 m = e.placement->sceneCompFromWorld * e.placement->layerWorld;
            // Homografia px (homogêneo) ← chão (u, v, 1): linhas x, y, w; colunas u, v, 1.
            const f32 a = m.col[0].x, b = m.col[1].x, c = m.col[3].x;
            const f32 d = m.col[0].y, f = m.col[1].y, g = m.col[3].y;
            const f32 k = m.col[0].w, l2 = m.col[1].w, q = m.col[3].w;
            const f32 A = f * q - g * l2, B = -(d * q - g * k), C = d * l2 - f * k;
            const f32 det = a * A + b * B + c * C;
            if (std::isfinite(det) && std::fabs(det) > 1e-12f) {
                const f32 r = 1.0f / det;
                // Linhas da inversa (u, v, w ← px).
                uu.h0 = Vec4{A * r, -(b * q - c * l2) * r, (b * g - c * f) * r, 0};
                uu.h1 = Vec4{B * r, (a * q - c * k) * r, -(a * g - c * d) * r, 0};
                uu.h2 = Vec4{C * r, -(a * l2 - b * k) * r, (a * f - b * d) * r, 0};
                uu.world = e.placement->layerWorld;
                uu.proj = e.placement->sceneCompFromWorld;
                const Vec3 cam = e.placement->sceneCameraPosition;
                uu.q0 = Vec4{cam.x, cam.y, cam.z, 1.0f};
            }
        }
        const u64 texRef = e.value(TextureLayer).ref;
        const LayerImage* texLayer = texRef ? ctx.layer_input(texRef) : nullptr;
        uu.q0.w = space3d && uu.q0.w > 0.5f ? 1.0f : 0.0f;
        u.p0 = Vec4{std::clamp(finite_or(e.f(Horizon), 62), 0.f, 100.f) * .01f,
                    std::clamp(finite_or(e.f(Reflection), 80), 0.f, 100.f) * .01f,
                    std::clamp(finite_or(e.f(Falloff), 60), 1.f, 100.f) * .01f,
                    std::clamp(finite_or(e.f(Blur), 3), 0.f, 40.f)};
        u.p1 = Vec4{std::clamp(finite_or(e.f(Distortion), 25), 0.f, 100.f) * .01f,
                    static_cast<f32>(std::min<u32>(e.e(Style), 2u)),
                    std::clamp(finite_or(e.f(Tile), 160), 10.f, 2000.f),
                    std::clamp(finite_or(e.f(Perspective), 70), 0.f, 100.f) * .01f};
        u.p2 = Vec4{input.region.x, input.region.y, input.region.w, input.region.h};
        u.p3 = Vec4{std::clamp(finite_or(e.f(Texture), 70), 0.f, 100.f) * .01f,
                    std::clamp(finite_or(e.f(Brightness), 30), 0.f, 100.f) * .01f,
                    e.count > Opacity ? std::clamp(finite_or(e.f(Opacity), 100), 0.f, 100.f) * .01f : 1.f, 0.f};
        (void)w; (void)h;
        u.color = e.color(Color);
        u.p3.w = texLayer ? 1.0f : 0.0f;
        out = input;
        out.texture = ctx.texture("reflective-floor", input.width, input.height);
        return ctx.fullscreen_pass("reflective-floor", PassStage::Effects, out.texture, ShaderId::effects_reflective_floor_frag,
            {PassTexture{input.texture, {}, CommonSampler::LinearClamp},
             PassTexture{texLayer ? texLayer->texture : input.texture, {}, CommonSampler::LinearRepeat}}, &uu, sizeof(uu)) == kInvalidIndex
            ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }
};
}
void register_reflective_floor(EffectRegistry& r) { (void)r.add(std::make_unique<ReflectiveFloor>()); }
}
