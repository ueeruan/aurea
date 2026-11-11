// =============================================================================
//  Peças comuns do lote "AE 2026" (AeEffects2026Color/Stylize/Generate.cpp).
//
//  Os efeitos deste lote foram especificados pelo COMPORTAMENTO e pelos
//  parâmetros públicos dos efeitos do After Effects (levantamento em
//  build/reference/ae2026/gap_effects.md). Nenhum código, shader ou asset da
//  Adobe: as contas são do Aurea.
//
//  Todos usam o mesmo bloco de uniforms (`AeUniforms`, espelho de
//  shaders/effects/ae_common.glsl) e o mesmo passe (`ae_pass`), que aceita
//  região de saída diferente da entrada (sombra radial, deformação).
// =============================================================================
#pragma once

#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin::ae26 {

struct AeUniforms {
    Vec4 uvMap{};
    Vec4 texel{};
    Vec4 inRegion{};
    Vec4 outRegion{};
    Vec4 layer{};
    Vec4 p[12]{};
    Vec4 c[3]{};
};
static_assert(sizeof(AeUniforms) == 320, "layout std140 de ae_common.glsl");

inline f32 fin(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }
inline f32 clampf(f32 v, f32 lo, f32 hi, f32 fallback) noexcept { return std::clamp(fin(v, fallback), lo, hi); }
/// Porcentagem do parâmetro → fração, presa em [lo, hi].
inline f32 pct(const EffectEval& e, u32 i, f32 lo = 0.0f, f32 hi = 1.0f) noexcept {
    return std::clamp(fin(e.f(i), 0.0f) * 0.01f, lo, hi);
}
inline f32 en(const EffectEval& e, u32 i, u32 count) noexcept {
    return static_cast<f32>(std::min<u32>(e.e(i), count ? count - 1 : 0));
}
inline f32 flag(bool b) noexcept { return b ? 1.0f : 0.0f; }

/// Tamanho natural da camada (px); sem posicionamento, a região da entrada.
inline Vec2 natural_size(const EffectEval& e, const LayerImage& input) noexcept {
    if (e.placement && e.placement->layerWidth && e.placement->layerHeight) {
        return Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
    }
    return Vec2{std::max(input.region.w, 1.0f), std::max(input.region.h, 1.0f)};
}
/// Depois do Motion Tile (`input.tiled`) a PAREDE é o plano da camada: centro,
/// raio e caixa dos efeitos do lote passam a ser os dela (o efeito chega aos
/// ladrilhos, como os outros efeitos do catálogo). Fora disso, a caixa natural.
inline Vec2 layer_size(const EffectEval& e, const LayerImage& input) noexcept {
    if (input.tiled) return Vec2{std::max(input.region.w, 1.0f), std::max(input.region.h, 1.0f)};
    return natural_size(e, input);
}
/// Origem do plano da camada em px da camada (a da parede, com Motion Tile).
inline Vec2 plane_origin(const LayerImage& input) noexcept {
    return input.tiled ? Vec2{input.region.x, input.region.y} : Vec2{0.0f, 0.0f};
}
/// Ponto relativo (0..1 da camada) → px da camada.
inline Vec2 layer_point(const EffectEval& e, const LayerImage& input, u32 param) noexcept {
    const Vec2 s = layer_size(e, input);
    const Vec2 r = e.p2(param);
    return Vec2{fin(r.x, 0.5f) * s.x, fin(r.y, 0.5f) * s.y};
}
inline f64 seconds(const EffectEval& e) noexcept {
    const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
    const f64 t = e.time_frames() / fps;
    return std::isfinite(t) ? t : 0.0;
}

/// Uniforms com o mapa de uv, a escala, as regiões e o tamanho da camada.
/// `wall` falso: sempre a caixa natural (o mapa de profundidade é da fonte).
inline AeUniforms ae_uniforms(const EffectEval& e, const LayerImage& input, const Rect& outRegion,
                              bool wall = true) noexcept {
    AeUniforms u;
    u.uvMap = EffectBuildContext::uv_map(outRegion, input.region);
    u.texel = Vec4{input.width ? 1.0f / static_cast<f32>(input.width) : 0.0f,
                   input.height ? 1.0f / static_cast<f32>(input.height) : 0.0f,
                   input.texel_scale_x(), input.texel_scale_y()};
    const Vec2 o = wall ? plane_origin(input) : Vec2{0.0f, 0.0f};
    u.inRegion = Vec4{input.region.x - o.x, input.region.y - o.y, std::max(input.region.w, 1e-3f), std::max(input.region.h, 1e-3f)};
    u.outRegion = Vec4{outRegion.x - o.x, outRegion.y - o.y, std::max(outRegion.w, 1e-3f), std::max(outRegion.h, 1e-3f)};
    const Vec2 ls = wall ? layer_size(e, input) : natural_size(e, input);
    const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
    u.layer = Vec4{ls.x, ls.y, static_cast<f32>(std::fmod(seconds(e), 100000.0)), static_cast<f32>(fps)};
    return u;
}
inline AeUniforms ae_uniforms(const EffectEval& e, const LayerImage& input, bool wall = true) noexcept {
    return ae_uniforms(e, input, input.region, wall);
}

/// O passe do lote: lê `input` (+ até 3 texturas extras), escreve `region`.
/// Região igual à da entrada = mesmo tamanho de textura (sem reamostrar).
/// `name` precisa ser literal (as medições de GPU voltam quadros depois).
inline Status ae_pass(EffectBuildContext& ctx, const LayerImage& input, const Rect& region, const AeUniforms& u,
                      ShaderId shader, const char* name, CommonSampler sampler, LayerImage& out,
                      const PassTexture* extra = nullptr, u32 extraCount = 0) {
    u32 w = input.width, h = input.height;
    const bool same = region.x == input.region.x && region.y == input.region.y && region.w == input.region.w
                   && region.h == input.region.h;
    if (!same) ctx.region_size(region, input.texel_scale_x(), w, h);
    if (!w || !h) return Errc::InvalidArgument;
    out = same ? input : LayerImage{};
    out.texture = ctx.texture(name, w, h);
    out.region = region;
    out.width = w;
    out.height = h;
    PassTexture tex[4] = {PassTexture{input.texture, {}, sampler}};
    const u32 n = 1 + std::min<u32>(extraCount, 3u);
    for (u32 i = 1; i < n; ++i) tex[i] = extra[i - 1];
    if (ctx.fullscreen_pass(name, PassStage::Effects, out.texture, shader, tex, n, &u, sizeof(u)) == kInvalidIndex)
        return Errc::PipelineCompileFailed;
    return OkStatus;
}
inline Status ae_pass(EffectBuildContext& ctx, const LayerImage& input, const AeUniforms& u, ShaderId shader,
                      const char* name, CommonSampler sampler, LayerImage& out,
                      const PassTexture* extra = nullptr, u32 extraCount = 0) {
    return ae_pass(ctx, input, input.region, u, shader, name, sampler, out, extra, extraCount);
}

/// Região da entrada crescida `grow` px de cada lado, recortada ao que o quadro mostra.
inline Rect grown_region(const EffectEval& e, const LayerImage& input, f32 grow, f32 margin) noexcept {
    if (!(grow > 0.0f)) return input.region;
    return spread_region(input.region, grow, grow, e.placement, margin);
}

void register_color(EffectRegistry& r);
void register_stylize(EffectRegistry& r);
void register_generate(EffectRegistry& r);
/// Segunda leva (AeEffects2026Extra.cpp), registrada DEPOIS das três acima.
void register_extra(EffectRegistry& r);

} // namespace aurea::builtin::ae26
