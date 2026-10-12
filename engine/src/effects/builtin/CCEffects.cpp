// =============================================================================
//  Pacote CC — distorções e ladrilhos.
//
//  Kaleidoscope · Sphere Wrap · Cylinder Wrap · Twister · Flow Pinch ·
//  Griddler · Repeat Tile · Smear · Split · Slant · Tiler · Hex Tile
//
//  Referência de COMPORTAMENTO: os efeitos "CC" (Cycore) dos editores de
//  movimento — Kaleida, Sphere, Cylinder, Twister, Flo Motion, Griddler,
//  RepeTile, Smear, Split 2, Slant, Tiler e HexTile. Os parâmetros seguem o
//  que eles documentam; a matemática e os shaders são nossos (cc_*.frag).
//  Os nomes exibidos são genéricos; o nome "CC" fica só na busca.
//
//  Cada efeito é UM passe de tela cheia com uma amostra por pixel (no máximo
//  9 no antisserrilhado do Flow Pinch) — o custo não cresce com os valores.
//  Os índices de parâmetro ficam fixos nos projetos salvos (acrescentar só no
//  fim).
// =============================================================================
#include "CCCommon.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace cc {

// -----------------------------------------------------------------------------
// Peças comuns (CCCommon.hpp)
// -----------------------------------------------------------------------------
Rect layer_box(const EffectEval& e, const LayerImage& in) noexcept {
    if (e.placement && e.placement->layerWidth && e.placement->layerHeight)
        return Rect{0.0f, 0.0f, static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
    return in.region;
}

Vec2 layer_point(const EffectEval& e, const LayerImage& in, u32 param) noexcept {
    const Rect box = layer_box(e, in);
    const Vec2 rel = e.p2(param);
    return Vec2{box.x + finite_or(rel.x, 0.5f) * box.w, box.y + finite_or(rel.y, 0.5f) * box.h};
}

f32 min_side(const Rect& box) noexcept { return std::max(1.0f, std::min(box.w, box.h)); }

f32 seconds(const EffectEval& e) noexcept {
    const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
    const f64 s = e.time_frames() / fps;
    return std::isfinite(s) ? static_cast<f32>(std::fmod(s, 100000.0)) : 0.0f;
}

Vec4 premultiplied_linear(Vec4 c) noexcept {
    const f32 a = std::clamp(finite_or(c.w, 1.0f), 0.0f, 1.0f);
    return Vec4{Color::srgb_to_linear(std::clamp(finite_or(c.x, 1.0f), 0.0f, 1.0f)) * a,
                Color::srgb_to_linear(std::clamp(finite_or(c.y, 1.0f), 0.0f, 1.0f)) * a,
                Color::srgb_to_linear(std::clamp(finite_or(c.z, 1.0f), 0.0f, 1.0f)) * a, a};
}

Rect grown_region(const EffectEval& e, const Rect& input, const Rect& want, f32 margin) noexcept {
    const bool clipped = e.placement && !e.placement->inScene3d && !e.placement->preserveFullExtent
                      && e.placement->compWidth && e.placement->compHeight;
    Rect r = want;
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.w) || !std::isfinite(r.h) || r.empty()) r = input;
    if (!clipped) {
        const f32 grow = std::max(0.25f * std::max(input.w, input.h), 512.0f);
        const Rect cap{input.x - grow, input.y - grow, input.w + 2.0f * grow, input.h + 2.0f * grow};
        r = Rect::intersect(r, cap);
        if (!(r.w > 0.5f && r.h > 0.5f)) r = input;
    }
    return spread_region(r, 0.0f, 0.0f, e.placement, margin);
}

Status run_pass(EffectBuildContext& ctx, const EffectEval& e, ShaderId frag, const LayerImage& in,
                const Rect& region, Uniforms& u, const char* name, LayerImage& out, CommonSampler sampler,
                const LayerImage* extra) {
    u32 w = 0, h = 0;
    ctx.region_size(region, in.texel_scale_x(), w, h);
    if (!w || !h) { out = in; return OkStatus; }
    u.dst = Vec4{region.x, region.y, region.w, region.h};
    u.src = Vec4{in.region.x, in.region.y, in.region.w, in.region.h};
    const Rect box = layer_box(e, in);
    u.box = Vec4{box.x, box.y, box.w, box.h};
    const f32 texel = 0.5f * (region.w / static_cast<f32>(w) + region.h / static_cast<f32>(h));
    u.info = Vec4{std::max(texel, 1e-4f), seconds(e), in.texel_scale_x(), 0.0f};
    out = LayerImage{ctx.texture(name, w, h), region, w, h};
    const u32 pass = extra
        ? ctx.fullscreen_pass(name, PassStage::Effects, out.texture, frag,
                              {PassTexture{in.texture, {}, sampler},
                               PassTexture{extra->texture, {}, CommonSampler::LinearClamp}}, &u, sizeof(u))
        : ctx.fullscreen_pass(name, PassStage::Effects, out.texture, frag,
                              {PassTexture{in.texture, {}, sampler}}, &u, sizeof(u));
    return pass == kInvalidIndex ? Status{Errc::PipelineCompileFailed} : OkStatus;
}

void declare_light(ParameterRegistry& p, const LightDefaults& d) {
    p.add_float("light_intensity", "Intensidade da luz", d.intensity, 0.0f, 200.0f, kAnimPct, "%");
    p.typed_range(0.0f, 1000.0f);
    p.add_color("light_color", "Cor da luz", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
    p.add_angle("light_direction", "Direção da luz", d.direction);
    p.add_float("light_height", "Altura da luz", d.height, -100.0f, 100.0f, kAnimPct, "%");
    p.add_float("ambient", "Ambiente", d.ambient, 0.0f, 200.0f, kAnimPct, "%");
    p.add_float("diffuse", "Difusa", d.diffuse, 0.0f, 200.0f, kAnimPct, "%");
    p.add_float("specular", "Especular", d.specular, 0.0f, 200.0f, kAnimPct, "%");
    p.add_float("roughness", "Aspereza", d.roughness, 0.0f, 100.0f, kAnimPct, "%");
    p.add_float("metal", "Metal", d.metal, 0.0f, 100.0f, kAnimPct, "%");
}

void light_uniforms(const EffectEval& e, u32 i, Uniforms& u) noexcept {
    // Direção como nos editores: 0° = luz vindo de cima, 90° = da direita
    // (horário); a altura leva de rente ao plano (0%) a de frente (100%).
    const f32 dir = finite_or(e.f(i + 2), -45.0f) * kDeg;
    const f32 elev = clampf(e.f(i + 3), -100.0f, 100.0f, 50.0f) * 0.9f * kDeg;
    u.p4 = Vec4{std::cos(elev) * std::sin(dir), -std::cos(elev) * std::cos(dir), std::sin(elev),
                clampf(e.f(i), 0.0f, 10000.0f, 100.0f) / 100.0f};
    const f32 rough = clampf(e.f(i + 7), 0.0f, 100.0f, 15.0f) / 100.0f;
    u.p5 = Vec4{clampf(e.f(i + 4), 0.0f, 10000.0f, 40.0f) / 100.0f, clampf(e.f(i + 5), 0.0f, 10000.0f, 70.0f) / 100.0f,
                clampf(e.f(i + 6), 0.0f, 10000.0f, 40.0f) / 100.0f, std::exp2(8.0f - 7.0f * std::sqrt(rough))};
    const Vec4 c = e.color(i + 1);
    u.color = Vec4{Color::srgb_to_linear(std::clamp(finite_or(c.x, 1.0f), 0.0f, 1.0f)),
                   Color::srgb_to_linear(std::clamp(finite_or(c.y, 1.0f), 0.0f, 1.0f)),
                   Color::srgb_to_linear(std::clamp(finite_or(c.z, 1.0f), 0.0f, 1.0f)),
                   clampf(e.f(i + 8), 0.0f, 100.0f, 100.0f) / 100.0f};
}

void rotation_rows(f32 xDeg, f32 yDeg, f32 zDeg, Uniforms& u) noexcept {
    const f32 ax = finite_or(xDeg, 0.0f) * kDeg, ay = finite_or(yDeg, 0.0f) * kDeg, az = finite_or(zDeg, 0.0f) * kDeg;
    const f32 cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay);
    const f32 cz = std::cos(az), sz = std::sin(az);
    // Ida (objeto → vista) = Rz · Ry · Rx, na tela com y para baixo.
    const f32 rx[3][3] = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}};
    const f32 ry[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}};
    const f32 rz[3][3] = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
    f32 zy[3][3]{}, m[3][3]{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) zy[r][c] += rz[r][k] * ry[k][c];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) m[r][c] += zy[r][k] * rx[k][c];
    // Linhas da inversa = colunas da ida.
    u.p0 = Vec4{m[0][0], m[1][0], m[2][0], u.p0.w};
    u.p1 = Vec4{m[0][1], m[1][1], m[2][1], u.p1.w};
    u.p2 = Vec4{m[0][2], m[1][2], m[2][2], u.p2.w};
}

} // namespace cc

namespace {
using namespace cc;

/// Região de um efeito que só remapeia dentro da própria entrada.
Status same_region(EffectBuildContext& ctx, const EffectEval& e, ShaderId frag, const LayerImage& in, Uniforms& u,
                   const char* name, LayerImage& out, CommonSampler sampler = CommonSampler::LinearBorder) {
    return run_pass(ctx, e, frag, in, in.region, u, name, out, sampler);
}

// =============================================================================
// Kaleidoscope (CC Kaleida)
// =============================================================================
class Kaleidoscope final : public Effect {
public:
    enum : u32 { kCenter = 0, kSize, kMirroring, kRotation, kSegments };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kKaleidoscope, "Kaleidoscope", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const modes[] = {"Flor", "Roda", "Desdobrar", "Triângulos"};
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        // % do menor lado da camada: o mesmo desenho em qualquer resolução.
        p.add_float("size", "Tamanho", 50.0f, 1.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.5f, 2000.0f);
        p.add_enum("mirroring", "Espelhamento", modes, 4, 0);
        p.add_angle("rotation", "Rotação", 0.0f);
        p.add_int("segments", "Fatias", 6, 2, 32);
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_kaleidoscope_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        Uniforms u{};
        const Vec2 c = layer_point(e, in, kCenter);
        u.p0 = Vec4{c.x, c.y, clampf(e.f(kSize), 0.5f, 2000.0f, 50.0f) / 100.0f * min_side(layer_box(e, in)),
                    finite_or(e.f(kRotation), 0.0f) * kDeg};
        u.p1 = Vec4{static_cast<f32>(std::min(e.e(kMirroring), 3u)),
                    static_cast<f32>(std::clamp(e.value(kSegments).as_int(), 2, 32)), 0.0f, 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_kaleidoscope_frag, in, u, "caleidoscopio", out);
    }
};

// =============================================================================
// Sphere Wrap (CC Sphere) e Cylinder Wrap (CC Cylinder)
// =============================================================================
const char* const kRenderModes[] = {"Completo", "Por fora", "Por dentro"};

class SphereWrap final : public Effect {
public:
    enum : u32 { kRadius = 0, kPosition, kRotX, kRotY, kRotZ, kRender, kLight };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSphereWrap, "Sphere Wrap", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("radius", "Raio", 45.0f, 1.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.5f, 1000.0f);
        p.add_point2("position", "Posição", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_angle("rotation_x", "Rotação X", 0.0f);
        p.add_angle("rotation_y", "Rotação Y", 0.0f);
        p.add_angle("rotation_z", "Rotação Z", 0.0f);
        p.add_enum("render", "Renderizar", kRenderModes, 3, 0);
        declare_light(p, LightDefaults{100.0f, -45.0f, 50.0f, 40.0f, 70.0f, 40.0f, 15.0f, 100.0f});
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_sphere_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const f32 R = clampf(e.f(kRadius), 0.5f, 1000.0f, 45.0f) / 100.0f * min_side(box);
        const Vec2 c = layer_point(e, in, kPosition);
        Uniforms u{};
        u.p0.w = R;
        u.p1.w = static_cast<f32>(std::min(e.e(kRender), 2u));
        rotation_rows(e.f(kRotX), e.f(kRotY), e.f(kRotZ), u);
        u.p3 = Vec4{c.x, c.y, 0.0f, 0.0f};
        light_uniforms(e, kLight, u);
        const Rect region = grown_region(e, in.region, Rect{c.x - R - 1.0f, c.y - R - 1.0f, 2.0f * R + 2.0f, 2.0f * R + 2.0f}, margin);
        return run_pass(ctx, e, ShaderId::effects_cc_sphere_frag, in, region, u, "esfera", out, CommonSampler::LinearClamp);
    }
};

class CylinderWrap final : public Effect {
public:
    enum : u32 { kRadius = 0, kPosition, kRotX, kRotY, kRotZ, kRender, kLight };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kCylinderWrap, "Cylinder Wrap", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // 100% = a largura da camada dá exatamente uma volta (sem esticar).
        p.add_float("radius", "Raio", 100.0f, 5.0f, 400.0f, kAnimPct, "%");
        p.typed_range(1.0f, 2000.0f);
        p.add_point2("position", "Posição", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_angle("rotation_x", "Rotação X", 0.0f);
        p.add_angle("rotation_y", "Rotação Y", 0.0f);
        p.add_angle("rotation_z", "Rotação Z", 0.0f);
        p.add_enum("render", "Renderizar", kRenderModes, 3, 0);
        declare_light(p, LightDefaults{100.0f, -45.0f, 40.0f, 40.0f, 70.0f, 40.0f, 15.0f, 100.0f});
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_cylinder_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const f32 R = clampf(e.f(kRadius), 1.0f, 2000.0f, 100.0f) / 100.0f * std::max(box.w, 1.0f) / kTwoPi;
        const f32 halfH = 0.5f * std::max(box.h, 1.0f);
        const Vec2 c = layer_point(e, in, kPosition);
        Uniforms u{};
        u.p0.w = R;
        u.p1.w = static_cast<f32>(std::min(e.e(kRender), 2u));
        u.p2.w = halfH;
        rotation_rows(e.f(kRotX), e.f(kRotY), e.f(kRotZ), u);
        u.p3 = Vec4{c.x, c.y, 0.0f, 0.0f};
        light_uniforms(e, kLight, u);
        const f32 reach = std::sqrt(R * R + halfH * halfH) + 1.0f;
        const Rect region = grown_region(e, in.region, Rect{c.x - reach, c.y - reach, 2.0f * reach, 2.0f * reach}, margin);
        return run_pass(ctx, e, ShaderId::effects_cc_cylinder_frag, in, region, u, "cilindro", out, CommonSampler::LinearClamp);
    }
};

// =============================================================================
// Twister (CC Twister)
// =============================================================================
class Twister final : public Effect {
public:
    enum : u32 { kTwist = 0, kCenter, kAxis, kSize, kShading };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kTwister, "Twister", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_angle("twist", "Torção", 180.0f);
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_angle("axis", "Eixo", 90.0f);
        p.add_float("size", "Tamanho", 60.0f, 1.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.5f, 2000.0f);
        p.add_float("shading", "Sombreado", 30.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return std::fabs(finite_or(e.f(kTwist), 0.0f)) < 0.01f; }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_twister_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const Vec2 c = layer_point(e, in, kCenter);
        Uniforms u{};
        u.p0 = Vec4{c.x, c.y, finite_or(e.f(kAxis), 90.0f) * kDeg, finite_or(e.f(kTwist), 180.0f) * kDeg};
        u.p1 = Vec4{0.5f * clampf(e.f(kSize), 0.5f, 2000.0f, 60.0f) / 100.0f * std::max(box.h, 1.0f),
                    clampf(e.f(kShading), 0.0f, 100.0f, 30.0f) / 100.0f, 0.0f, 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_twister_frag, in, u, "torcer", out);
    }
};

// =============================================================================
// Flow Pinch (CC Flo Motion)
// =============================================================================
class FlowPinch final : public Effect {
public:
    enum : u32 { kKnot1 = 0, kAmount1, kKnot2, kAmount2, kReach, kTile, kAntialias };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kFlowPinch, "Flow Pinch", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const quality[] = {"Baixo", "Médio", "Alto"};
        p.add_point2("knot1", "Nó 1", Vec2{0.3f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("amount1", "Força 1", 70.0f, -95.0f, 400.0f, kAnimPct, "%");
        p.typed_range(-95.0f, 2000.0f);
        p.add_point2("knot2", "Nó 2", Vec2{0.7f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("amount2", "Força 2", 70.0f, -95.0f, 400.0f, kAnimPct, "%");
        p.typed_range(-95.0f, 2000.0f);
        p.add_float("reach", "Alcance", 40.0f, 1.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.5f, 2000.0f);
        p.add_bool("tile_edges", "Repetir bordas", false);
        p.add_enum("antialiasing", "Antisserrilhado", quality, 3, 0);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(finite_or(e.f(kAmount1), 0.0f)) < 0.01f && std::fabs(finite_or(e.f(kAmount2), 0.0f)) < 0.01f;
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_flow_pinch_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Vec2 k1 = layer_point(e, in, kKnot1), k2 = layer_point(e, in, kKnot2);
        Uniforms u{};
        u.p0 = Vec4{k1.x, k1.y, clampf(e.f(kAmount1), -95.0f, 2000.0f, 70.0f) / 100.0f, 0.0f};
        u.p1 = Vec4{k2.x, k2.y, clampf(e.f(kAmount2), -95.0f, 2000.0f, 70.0f) / 100.0f, 0.0f};
        u.p2 = Vec4{clampf(e.f(kReach), 0.5f, 2000.0f, 40.0f) / 100.0f * min_side(layer_box(e, in)),
                    e.b(kTile) ? 1.0f : 0.0f, static_cast<f32>(std::min(e.e(kAntialias), 2u) + 1u), 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_flow_pinch_frag, in, u, "fluxo", out);
    }
};

// =============================================================================
// Griddler (CC Griddler)
// =============================================================================
class Griddler final : public Effect {
public:
    enum : u32 { kScaleX = 0, kScaleY, kTileSize, kRotation, kCut };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kGriddler, "Griddler", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("scale_x", "Escala horizontal", 85.0f, 1.0f, 400.0f, kAnimPct, "%");
        p.typed_range(1.0f, 2000.0f);
        p.add_float("scale_y", "Escala vertical", 85.0f, 1.0f, 400.0f, kAnimPct, "%");
        p.typed_range(1.0f, 2000.0f);
        p.add_float("tile_size", "Tamanho do ladrilho", 12.0f, 1.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.2f, 1000.0f);
        p.add_angle("rotation", "Rotação", 12.0f);
        p.add_bool("cut_tiles", "Cortar ladrilhos", true);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(finite_or(e.f(kScaleX), 100.0f) - 100.0f) < 0.01f && std::fabs(finite_or(e.f(kScaleY), 100.0f) - 100.0f) < 0.01f
            && std::fabs(std::remainder(finite_or(e.f(kRotation), 0.0f), 360.0f)) < 0.01f && !e.b(kCut);
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_griddler_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        Uniforms u{};
        // Teto de ~4 texels por ladrilho (abaixo disso é ruído, não grade).
        const f32 tile = std::max(clampf(e.f(kTileSize), 0.2f, 1000.0f, 12.0f) / 100.0f * min_side(layer_box(e, in)),
                                  4.0f / std::max(in.texel_scale_x(), 1e-3f));
        u.p0 = Vec4{tile, clampf(e.f(kScaleX), 1.0f, 2000.0f, 85.0f) / 100.0f, clampf(e.f(kScaleY), 1.0f, 2000.0f, 85.0f) / 100.0f,
                    finite_or(e.f(kRotation), 0.0f) * kDeg};
        u.p1 = Vec4{e.b(kCut) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_griddler_frag, in, u, "grade", out);
    }
};

// =============================================================================
// Repeat Tile (CC RepeTile)
// =============================================================================
class RepeatTile final : public Effect {
public:
    enum : u32 { kRight = 0, kLeft, kUp, kDown, kTiling, kBlend, kOffset };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kRepeatTile, "Repeat Tile", "Estilizar", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const modes[] = {"Repetir", "Desdobrar", "Espelhar na horizontal", "Espelhar na vertical",
                                            "Girar 180°", "Tijolos", "Tijolos verticais"};
        // % do tamanho da camada; a região é recortada ao quadro visível.
        p.add_float("expand_right", "Expandir à direita", 100.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("expand_left", "Expandir à esquerda", 100.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("expand_up", "Expandir para cima", 100.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("expand_down", "Expandir para baixo", 100.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_enum("tiling", "Ladrilhos", modes, 7, 1);
        p.add_float("blend_borders", "Misturar bordas", 0.0f, 0.0f, 50.0f, kAnimPct, "%");
        p.add_point2("offset", "Deslocamento", Vec2{0.0f, 0.0f}, -2000.0f, 2000.0f, kAnimPx);
        p.typed_range(-100000.0f, 100000.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        const Vec2 o = e.p2(kOffset);
        return finite_or(e.f(kRight), 0.0f) <= 0.0f && finite_or(e.f(kLeft), 0.0f) <= 0.0f && finite_or(e.f(kUp), 0.0f) <= 0.0f
            && finite_or(e.f(kDown), 0.0f) <= 0.0f && std::fabs(finite_or(o.x, 0.0f)) < 0.01f && std::fabs(finite_or(o.y, 0.0f)) < 0.01f;
    }
    bool needs_full_input() const noexcept override { return true; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // A prévia cobre o quadro inteiro: o deslocamento mostra os ladrilhos.
        v[kOffset] = ParamValue::vec2(27.0f, 19.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_repeat_tile_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const f32 r = clampf(e.f(kRight), 0.0f, 1000.0f, 100.0f) / 100.0f * box.w;
        const f32 l = clampf(e.f(kLeft), 0.0f, 1000.0f, 100.0f) / 100.0f * box.w;
        const f32 up = clampf(e.f(kUp), 0.0f, 1000.0f, 100.0f) / 100.0f * box.h;
        const f32 dn = clampf(e.f(kDown), 0.0f, 1000.0f, 100.0f) / 100.0f * box.h;
        const Rect want{box.x - l, box.y - up, box.w + l + r, box.h + up + dn};
        const Vec2 o = e.p2(kOffset);
        Uniforms u{};
        u.p0 = Vec4{static_cast<f32>(std::min(e.e(kTiling), 6u)), clampf(e.f(kBlend), 0.0f, 50.0f, 0.0f) / 100.0f,
                    clampf(o.x, -100000.0f, 100000.0f, 0.0f), clampf(o.y, -100000.0f, 100000.0f, 0.0f)};
        return run_pass(ctx, e, ShaderId::effects_cc_repeat_tile_frag, in, grown_region(e, in.region, want, margin), u,
                        "ladrilho", out, CommonSampler::LinearClamp);
    }
};

// =============================================================================
// Smear (CC Smear)
// =============================================================================
class Smear final : public Effect {
public:
    enum : u32 { kFrom = 0, kTo, kReach, kRadius };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSmear, "Smear", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("from", "Origem", Vec2{0.35f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_point2("to", "Destino", Vec2{0.65f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("reach", "Alcance", 100.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.add_float("radius", "Raio", 20.0f, 1.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.5f, 2000.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        const Vec2 a = e.p2(kFrom), b = e.p2(kTo);
        return finite_or(e.f(kReach), 100.0f) < 0.01f || (std::fabs(a.x - b.x) < 1e-5f && std::fabs(a.y - b.y) < 1e-5f);
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_smear_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Vec2 a = layer_point(e, in, kFrom), b = layer_point(e, in, kTo);
        Uniforms u{};
        u.p0 = Vec4{a.x, a.y, b.x, b.y};
        u.p1 = Vec4{clampf(e.f(kReach), 0.0f, 200.0f, 100.0f) / 100.0f,
                    clampf(e.f(kRadius), 0.5f, 2000.0f, 20.0f) / 100.0f * min_side(layer_box(e, in)), 0.0f, 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_smear_frag, in, u, "borrar", out);
    }
};

// =============================================================================
// Split (CC Split 2)
// =============================================================================
class Split final : public Effect {
public:
    enum : u32 { kPointA = 0, kPointB, kSplit1, kSplit2 };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSplit, "Split", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("point_a", "Ponto A", Vec2{0.3f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_point2("point_b", "Ponto B", Vec2{0.7f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        // % do comprimento do corte (A→B), de cada lado.
        p.add_float("split1", "Abertura 1", 20.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("split2", "Abertura 2", 20.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return finite_or(e.f(kSplit1), 0.0f) < 0.01f && finite_or(e.f(kSplit2), 0.0f) < 0.01f;
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_split_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Vec2 a = layer_point(e, in, kPointA), b = layer_point(e, in, kPointB);
        const f32 len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        Uniforms u{};
        u.p0 = Vec4{a.x, a.y, b.x, b.y};
        u.p1 = Vec4{clampf(e.f(kSplit1), 0.0f, 1000.0f, 20.0f) / 100.0f * 0.5f * len,
                    clampf(e.f(kSplit2), 0.0f, 1000.0f, 20.0f) / 100.0f * 0.5f * len, 0.0f, 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_split_frag, in, u, "rasgar", out);
    }
};

// =============================================================================
// Slant (CC Slant)
// =============================================================================
class Slant final : public Effect {
public:
    enum : u32 { kSlant = 0, kHeight, kFloor, kSetColor, kColor };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSlant, "Slant", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_angle("slant", "Inclinação", 30.0f, -80.0f, 80.0f);
        p.add_float("height", "Altura", 100.0f, 0.0f, 300.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        // % da altura da camada: 100% = a borda de baixo.
        p.add_float("floor", "Chão", 100.0f, -100.0f, 200.0f, kAnimPct, "%");
        p.typed_range(-1000.0f, 1000.0f);
        p.add_bool("set_color", "Usar cor", false);
        p.add_color("color", "Cor", Vec4{0.0f, 0.0f, 0.0f, 1.0f});
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(finite_or(e.f(kSlant), 0.0f)) < 0.01f && std::fabs(finite_or(e.f(kHeight), 100.0f) - 100.0f) < 0.01f
            && !e.b(kSetColor);
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_slant_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const f32 t = std::tan(clampf(e.f(kSlant), -80.0f, 80.0f, 30.0f) * kDeg);
        const f32 h = clampf(e.f(kHeight), 0.0f, 1000.0f, 100.0f) / 100.0f;
        const f32 F = box.y + clampf(e.f(kFloor), -1000.0f, 1000.0f, 100.0f) / 100.0f * box.h;
        // A caixa da entrada levada pela ida (cisalhamento + escala no chão).
        const Rect src = in.region;
        f32 x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        for (const f32 x : {src.x, src.x + src.w}) {
            for (const f32 y : {src.y, src.y + src.h}) {
                const f32 px = x + (F - y) * t, py = F - (F - y) * h;
                x0 = std::min(x0, px); x1 = std::max(x1, px); y0 = std::min(y0, py); y1 = std::max(y1, py);
            }
        }
        Uniforms u{};
        u.p0 = Vec4{t, h, F, e.b(kSetColor) ? 1.0f : 0.0f};
        u.color = premultiplied_linear(e.color(kColor));
        const Rect want{x0 - 1.0f, y0 - 1.0f, std::max(x1 - x0, 1.0f) + 2.0f, std::max(y1 - y0, 1.0f) + 2.0f};
        return run_pass(ctx, e, ShaderId::effects_cc_slant_frag, in, grown_region(e, in.region, want, margin), u,
                        "inclinar", out);
    }
};

// =============================================================================
// Tiler (CC Tiler) e Hex Tile (CC HexTile)
// =============================================================================
class Tiler final : public Effect {
public:
    enum : u32 { kScale = 0, kCenter, kBlend };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kTiler, "Tiler", "Estilizar", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("scale", "Escala", 33.0f, 1.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.5f, 100.0f);
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("blend", "Mistura com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        const Vec2 c = e.p2(kCenter);
        return finite_or(e.f(kBlend), 0.0f) >= 99.999f
            || (finite_or(e.f(kScale), 33.0f) >= 99.999f && std::fabs(c.x - 0.5f) < 1e-5f && std::fabs(c.y - 0.5f) < 1e-5f);
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_tiler_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Vec2 c = layer_point(e, in, kCenter);
        // Ladrilhos menores que ~2 texels viram ruído: a escala para lá.
        const Rect box = layer_box(e, in);
        const f32 floorScale = 2.0f / std::max(std::min(box.w, box.h) * in.texel_scale_x(), 1.0f);
        Uniforms u{};
        u.p0 = Vec4{c.x, c.y, std::max(clampf(e.f(kScale), 0.5f, 100.0f, 33.0f) / 100.0f, floorScale),
                    clampf(e.f(kBlend), 0.0f, 100.0f, 0.0f) / 100.0f};
        return same_region(ctx, e, ShaderId::effects_cc_tiler_frag, in, u, "mosaico", out, CommonSampler::LinearClamp);
    }
};

class HexTile final : public Effect {
public:
    enum : u32 { kInputCenter = 0, kGridCenter, kRadius, kRotation, kMirror, kBlend };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kHexTile, "Hex Tile", "Estilizar", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("input_center", "Centro de entrada", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_point2("grid_center", "Centro da grade", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("radius", "Raio", 20.0f, 1.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.2f, 1000.0f);
        p.add_angle("rotation", "Rotação", 0.0f);
        p.add_bool("mirror_edges", "Espelhar bordas", false);
        p.add_float("blend", "Mistura com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return finite_or(e.f(kBlend), 0.0f) >= 99.999f; }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_hex_tile_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Vec2 a = layer_point(e, in, kInputCenter), b = layer_point(e, in, kGridCenter);
        const f32 radius = std::max(clampf(e.f(kRadius), 0.2f, 1000.0f, 20.0f) / 100.0f * min_side(layer_box(e, in)),
                                    2.0f / std::max(in.texel_scale_x(), 1e-3f));
        Uniforms u{};
        u.p0 = Vec4{a.x, a.y, b.x, b.y};
        u.p1 = Vec4{radius, finite_or(e.f(kRotation), 0.0f) * kDeg, e.b(kMirror) ? 1.0f : 0.0f,
                    clampf(e.f(kBlend), 0.0f, 100.0f, 0.0f) / 100.0f};
        return same_region(ctx, e, ShaderId::effects_cc_hex_tile_frag, in, u, "colmeia", out);
    }
};

} // namespace

void register_cc_distort_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Kaleidoscope>());
    (void)r.add(std::make_unique<SphereWrap>());
    (void)r.add(std::make_unique<CylinderWrap>());
    (void)r.add(std::make_unique<Twister>());
    (void)r.add(std::make_unique<FlowPinch>());
    (void)r.add(std::make_unique<Griddler>());
    (void)r.add(std::make_unique<RepeatTile>());
    (void)r.add(std::make_unique<Smear>());
    (void)r.add(std::make_unique<Split>());
    (void)r.add(std::make_unique<Slant>());
    (void)r.add(std::make_unique<Tiler>());
    (void)r.add(std::make_unique<HexTile>());
}

} // namespace aurea::builtin
